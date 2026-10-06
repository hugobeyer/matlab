#import "sdf.h"

#bind layer src float
#bind layer flow float2
#bind layer !&dst float

#bind parm unitdist float val=0.40
#bind parm jag_amount float val=0.35
#bind parm jag_cells int val=6
#bind parm y_bias float val=0.0
#bind parm x_feather float val=1.0
#bind parm flow_strength float val=0.65
#bind parm remap_contrast float val=1.2
#bind parm seed float val=1234

static int wrapi(int x, int n)
{
    int r = x % n;
    return r < 0 ? r + n : r;
}

static float frac1(float x)
{
    return x - floor(x);
}

static float hash11(float x)
{
    x = frac1(x * 0.1031f);
    x *= x + 33.33f;
    x *= x + x;
    return frac1(x);
}

static float hash21i(int2 p, float seed)
{
    return hash11((float)p.x * 127.1f + (float)p.y * 311.7f + seed * 74.17f);
}

static float2 rotate2(float2 p, float a)
{
    float c = cos(a);
    float s = sin(a);
    return (float2)(c * p.x - s * p.y, s * p.x + c * p.y);
}

static float periodic_noise(float2 uv, int cells, float seed)
{
    cells = max(cells, 1);

    float2 g = uv * (float)cells;
    int2 i = convert_int2(floor(g));
    float2 f = g - floor(g);
    f = f * f * (3.0f - 2.0f * f);

    int2 i00 = (int2)(wrapi(i.x, cells), wrapi(i.y, cells));
    int2 i10 = (int2)(wrapi(i.x + 1, cells), wrapi(i.y, cells));
    int2 i01 = (int2)(wrapi(i.x, cells), wrapi(i.y + 1, cells));
    int2 i11 = (int2)(wrapi(i.x + 1, cells), wrapi(i.y + 1, cells));

    float a = hash21i(i00, seed);
    float b = hash21i(i10, seed);
    float c = hash21i(i01, seed);
    float d = hash21i(i11, seed);

    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y) * 2.0f - 1.0f;
}

@KERNEL
{
    int2 res = convert_int2(@dst.res);
    int xres = res.x;
    int yres = res.y;
    int2 p = @ixy;

    float src0 = @src.bufferIndex(p);
    float acc = src0;

    float2 uv = (convert_float2(p) + 0.5f) / convert_float2(res);
    float jag = clamp(@jag_amount, 0.0f, 1.0f);
    float ybias = clamp(@y_bias, -1.0f, 1.0f);
    float xfeather = clamp(@x_feather, 0.0f, 1.0f);
    float flowstrength = clamp(@flow_strength, 0.0f, 1.0f);
    float unitdist = fmax(@unitdist, 1e-6f);

    float pixelstep = fmax(fabs(@dPdx.x), 1e-8f);
    float featherpx = unitdist / pixelstep;
    int maxradius = max(1, min(xres, yres) / 2);

    float2 fv = @flow.bufferIndex(p);
    float fl = length(fv);
    float2 fdir = fl > 1e-6f ? fv / fl : (float2)(1.0f, 0.0f);
    float fw = clamp(fl * flowstrength, 0.0f, 1.0f);
    float2 mainraw = mix((float2)(1.0f, 0.0f), fdir, fw);
    float ml = length(mainraw);
    float2 mainaxis = ml > 1e-6f ? mainraw / ml : fdir;

    float jn = periodic_noise(uv, @jag_cells, @seed);
    mainaxis = rotate2(mainaxis, jn * jag * 0.78539816339f);

    const float angle45 = 0.78539816339f;
    float2 axes[4];
    axes[0] = mainaxis;
    axes[1] = rotate2(mainaxis, angle45);
    axes[2] = rotate2(mainaxis, -angle45);
    axes[3] = (float2)(-mainaxis.y, mainaxis.x);

    for (int it = 0; it < 4; ++it)
    {
        float scale = it == 0 ? 0.125f : (it == 1 ? 0.25f : (it == 2 ? 0.5f : 1.0f));
        int radius = clamp((int)floor(featherpx * scale + 0.5f), 1, maxradius);
        float travel = pixelstep * (float)radius / unitdist;

        int feather_dir = (it & 1) == 0;
        float pass_cand = feather_dir ? -1e20f : 1e20f;

        for (int a = 0; a < 4; ++a)
        {
            for (int side = -1; side <= 1; side += 2)
            {
                float2 dir = axes[a] * (float)side;
                int2 step = (int2)(
                    (int)rint(dir.x * (float)radius),
                    (int)rint(dir.y * (float)radius));

                if (step.x == 0 && step.y == 0)
                    continue;

                int2 q = (int2)(wrapi(p.x - step.x, xres), wrapi(p.y - step.y, yres));
                float v = @src.bufferIndex(q);
                v = feather_dir ? v - travel : v + travel;

                float xw = 1.0f - fabs(dir.x) * (1.0f - xfeather);
                float yw = 1.0f;
                if (dir.y > 0.001f && ybias < 0.0f)
                    yw = 1.0f + ybias;
                else if (dir.y < -0.001f && ybias > 0.0f)
                    yw = 1.0f - ybias;

                v = mix(src0, v, clamp(xw * yw, 0.0f, 1.0f));
                pass_cand = feather_dir ? fmax(pass_cand, v) : fmin(pass_cand, v);
            }
        }

        if ((it % 3) == 2)
            acc = sdfOpDifferenceRound(acc, pass_cand, 0.0f);
        else
            acc = sdfOpIntersectChamfer(acc, pass_cand, 0.0f);
    }

    acc = src0 + (acc - src0) * fmax(@remap_contrast, 0.0f);
    @dst.set(acc);
}
