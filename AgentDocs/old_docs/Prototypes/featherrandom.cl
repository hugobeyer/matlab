#import "sdf.h"

#bind layer src float
#bind layer !&dst float

#bind parm unitdist float val=0.40

#bind parm jag_amount float val=0.35
#bind parm jag_length int val=3

#bind parm edge_width float val=0.06

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


/*
    0  -X
    1  +X
    2  -Y
    3  +Y
    4  -X-Y
    5  -X+Y
    6  +X-Y
    7  +X+Y
*/
static int2 dir_step(int d)
{
    if (d == 0) return (int2)(-1,  0);
    if (d == 1) return (int2)( 1,  0);
    if (d == 2) return (int2)( 0, -1);
    if (d == 3) return (int2)( 0,  1);
    if (d == 4) return (int2)(-1, -1);
    if (d == 5) return (int2)(-1,  1);
    if (d == 6) return (int2)( 1, -1);

    return (int2)(1, 1);
}


static int jag_neighbor(int d, int side)
{
    if (side == 0) return d;

    if (d == 0) return side < 0 ? 4 : 5;
    if (d == 1) return side < 0 ? 6 : 7;
    if (d == 2) return side < 0 ? 4 : 6;
    if (d == 3) return side < 0 ? 5 : 7;
    if (d == 4) return side < 0 ? 0 : 2;
    if (d == 5) return side < 0 ? 0 : 3;
    if (d == 6) return side < 0 ? 2 : 1;

    return side < 0 ? 3 : 1;
}


static int base_enabled(int pass, int d)
{
    if (pass == 0) return d == 0 || d == 2 || d == 7;
    if (pass == 1) return d == 0 || d == 3 || d == 7;
    if (pass == 2) return d == 1 || d == 3 || d == 6;

    return d == 0 || d == 3 || d == 4 || d == 5 || d == 6 || d == 7;
}


static float normalize_pass(float v, float floorv, float ceilv)
{
    floorv = clamp(floorv, 0.001f, 0.49f);
    ceilv  = clamp(ceilv, floorv + 0.001f, 1.0f);

    if (v < 0.0f)
    {
        float n = -v;
        return floorv / (1.0f + n);
    }

    if (v <= 1.0f)
        return mix(floorv, ceilv, v);

    float o = v - 1.0f;
    return ceilv + (1.0f - ceilv) * (o / (1.0f + o));
}


@KERNEL
{
    int2 res = convert_int2(@dst.res);
    int xres = res.x;
    int yres = res.y;

    int2 p = @ixy;

    float src0 = @src.bufferIndex(p);
    float acc  = src0;

    int iter_count = 4;
    int maxsteps   = max(xres, yres);
    int jlen       = max(@jag_length, 1);

    float jag        = clamp(@jag_amount, 0.0f, 1.0f);
    float unitdist   = fmax(@unitdist, 1e-6f);
    float edge_width = fmax(@edge_width, 0.0f);


    for (int it = 0; it < iter_count; ++it)
    {
        float fi = (float)it;
        int pass  = it & 3;
        int cycle = it >> 2;

        float falloff = fabs(@dPdx.x) / unitdist;

        int feather_dir = pass == 0;
        if (cycle & 1)
            feather_dir = !feather_dir;

        float pass_cand = feather_dir ? -1e20f : 1e20f;
        int active = 0;


        for (int original_dir = 0; original_dir < 8; ++original_dir)
        {
            if (!base_enabled(pass, original_dir))
                continue;

            int dir = original_dir;

            float dir_cand = feather_dir ? -1e20f : 1e20f;

            int2 q = p;
            float travel = 0.0f;


            for (int s = 0; s < maxsteps; ++s)
            {
                if (travel > 1.0f)
                    break;

                q.x = wrapi(q.x, xres);
                q.y = wrapi(q.y, yres);

                float sv = @src.bufferIndex(q);
                float v  = feather_dir ? sv - travel : sv + travel;

                dir_cand = feather_dir ? fmax(dir_cand, v) : fmin(dir_cand, v);

                int seg = s / jlen;

                float hj = hash11(
                    @seed +
                    fi * 97.31f +
                    (float)dir * 23.71f +
                    (float)seg * 41.73f
                );

                int walk_dir = dir;

                if (hj < jag)
                {
                    int dl = jag_neighbor(dir, -1);
                    int dr = jag_neighbor(dir,  1);

                    float hside = hash11(
                        @seed +
                        fi * 53.91f +
                        (float)dir * 79.13f +
                        (float)seg * 157.31f
                    );

                    walk_dir = hside < 0.5f ? dl : dr;
                }

                int2 step = dir_step(walk_dir);
                q -= step;

                float step_length = walk_dir >= 4 ? 1.41421356237f : 1.0f;

                travel += falloff * step_length;
            }


            pass_cand = feather_dir ? fmax(pass_cand, dir_cand) : fmin(pass_cand, dir_cand);
            ++active;
        }


        if (active == 0)
            pass_cand = src0;


        if ((it % 3) == 2)
            acc = sdfOpDifferenceRound(acc, pass_cand, edge_width);
        else
            acc = sdfOpIntersectChamfer(acc, pass_cand, edge_width);


        /*
            Normalize/floor after EVERY SDF pass.

            Prevents negative values from accumulating
            into one flat zero plateau.
        */
        acc = normalize_pass(acc, 0.0f, 0.5f);
    }


    @dst.set(acc);
}
