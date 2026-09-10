// The thing all of the above exists for -- a small random HSV shift per cluster, at two scales.
//
// Micro and macro offsets are summed. Macro groups are unions of whole micro clusters, so the
// two never disagree at a boundary: a macro group carries a batch tone and the micro clusters
// deviate around it. Bricks fired together share a cast; individual bricks vary within it.
//
// Keep the amounts small. 0.02 hue is already visible on a flat surface.
//
// This mirrors AdjustHSV in MixtormatComposite.usf, which is where it lands on the Unreal side:
// same three operations, same order, so the port is adding a hashed offset to HueShift,
// Saturation and Value rather than writing anything new.
//

#runover layer
#bind layer out float3 noread write
#bind layer albedo float3
#bind layer label float
#bind layer macro_id float
#bind layer centre_feat float3
#bind parm grid_x int val=32
#bind parm stat_mix float val=0
#bind parm hue_var float val=0.02
#bind parm sat_var float val=0.05
#bind parm val_var float val=0.05
#bind parm macro_mix float val=0.5

#import <random.h>

// Houdini's own RNG rather than a hand-rolled hash: SYSwang_inthash to decorrelate the id,
// SYSfastRandom to draw from it. Same pair the shipped SideFX kernels use.
static float randFromId(uint id, uint salt)
{
    uint seed = SYSwang_inthash(id ^ salt);
    return SYSfastRandom(&seed);
}

static float3 rgb2hsv(float3 c)
{
    float mx = fmax(c.x, fmax(c.y, c.z));
    float mn = fmin(c.x, fmin(c.y, c.z));
    float d  = mx - mn;
    float h = 0.0f;
    if (d > 1e-6f)
    {
        if      (mx == c.x) h = fmod((c.y - c.z) / d, 6.0f);
        else if (mx == c.y) h = (c.z - c.x) / d + 2.0f;
        else                h = (c.x - c.y) / d + 4.0f;
        h /= 6.0f;
        if (h < 0.0f) h += 1.0f;
    }
    return (float3)(h, mx > 1e-6f ? d / mx : 0.0f, mx);
}

static float3 hsv2rgb(float3 c)
{
    float3 k = (float3)(fabs(fmod(c.x * 6.0f + 0.0f, 6.0f) - 3.0f) - 1.0f,
                        fabs(fmod(c.x * 6.0f + 4.0f, 6.0f) - 3.0f) - 1.0f,
                        fabs(fmod(c.x * 6.0f + 2.0f, 6.0f) - 3.0f) - 1.0f);
    k = clamp(k, 0.0f, 1.0f);
    return c.z * mix((float3)(1.0f, 1.0f, 1.0f), k, c.y);
}

// Signed, zero-mean. A one-sided offset would tint the whole surface rather than vary it.
static float signedHash(uint id, uint salt)
{
    return randFromId(id * 7919u + salt, 0u) * 2.0f - 1.0f;
}

@KERNEL
{
    uint mi = (uint)(@label + 0.5f);
    uint ma = (uint)(@macro_id + 0.5f);

    float mix_macro = clamp(@macro_mix, 0.0f, 1.0f);
    float wMicro = 1.0f - mix_macro;
    float wMacro = mix_macro;

    float dh = (signedHash(mi, 11u) * wMicro + signedHash(ma, 101u) * wMacro) * @hue_var;
    float ds = (signedHash(mi, 23u) * wMicro + signedHash(ma, 211u) * wMacro) * @sat_var;
    float dv = (signedHash(mi, 37u) * wMicro + signedHash(ma, 307u) * wMacro) * @val_var;

    // Statistics by id, blended in against the pure hash. centre_feat carries (mean, min, max)
    // of the cluster's height, written by step 03.
    //
    // At stat_mix 0 the variation is pure noise. Turned up, it follows the surface: value tracks
    // how high the cluster sits, saturation tracks how busy it is. That is the difference between
    // variation that reads as material and variation that reads as dirt on the lens.
    float sm = clamp(@stat_mix, 0.0f, 1.0f);
    if (sm > 0.0f)
    {
        int gx = max(@grid_x, 1);
        int2 mc = (int2)((int)(mi % (uint)gx), (int)(mi / (uint)gx));
        float3 st = @centre_feat.bufferIndex(mc);

        // Recentred on 0.5 so the stats push both ways rather than only brightening.
        float lift  = (st.x - 0.5f) * 2.0f;
        float range = clamp((st.z - st.y) * 2.0f, 0.0f, 1.0f);

        dv = mix(dv, lift  * @val_var, sm);
        ds = mix(ds, range * @sat_var, sm);
    }

    float3 hsv = rgb2hsv(@albedo);
    hsv.x = fmod(hsv.x + dh + 1.0f, 1.0f);
    hsv.y = clamp(hsv.y * (1.0f + ds), 0.0f, 1.0f);
    hsv.z = fmax(hsv.z * (1.0f + dv), 0.0f);

    @out.set(hsv2rgb(hsv));
}
