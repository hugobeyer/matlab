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
#bind layer label
#bind layer macro_id
#bind parm hue_var float val=0.02
#bind parm sat_var float val=0.05
#bind parm val_var float val=0.05
#bind parm macro_mix float val=0.5

static float hash11(uint n)
{
    n = (n ^ 61u) ^ (n >> 16u);
    n *= 9u; n = n ^ (n >> 4u); n *= 0x27d4eb2du; n = n ^ (n >> 15u);
    return (float)(n & 0x00FFFFFFu) / (float)0x01000000;
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
    return hash11(id * 7919u + salt) * 2.0f - 1.0f;
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

    float3 hsv = rgb2hsv(@albedo);
    hsv.x = fmod(hsv.x + dh + 1.0f, 1.0f);
    hsv.y = clamp(hsv.y * (1.0f + ds), 0.0f, 1.0f);
    hsv.z = fmax(hsv.z * (1.0f + dv), 0.0f);

    @out.set(hsv2rgb(hsv));
}
