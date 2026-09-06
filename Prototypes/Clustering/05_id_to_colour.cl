// Debug view -- hash a cluster index into a flat colour so the segmentation can be looked at.
//
// Look at this before wiring anything to hue. What you are judging is whether the clusters
// follow the structure in your scans: do brick faces come out whole, do mortar lines fall on
// cluster boundaries, is the size right. If they do not, the feature weights in 02 are the dial,
// not the variation amounts.
//
// Bind label for micro, or macro-through-label for macro (see the README).
//

#runover layer
#bind layer colour float3 noread write
#bind layer id
#bind parm mode int val=0

static float hash11(uint n)
{
    n = (n ^ 61u) ^ (n >> 16u);
    n *= 9u; n = n ^ (n >> 4u); n *= 0x27d4eb2du; n = n ^ (n >> 15u);
    return (float)(n & 0x00FFFFFFu) / (float)0x01000000;
}

@KERNEL
{
    uint id = (uint)(@id + 0.5f);

    if (@mode == 1)
    {
        float g = hash11(id);
        @colour.set((float3)(g, g, g));
        return;
    }

    @colour.set((float3)(hash11(id * 3u + 1u),
                         hash11(id * 3u + 2u),
                         hash11(id * 3u + 3u)));
}
