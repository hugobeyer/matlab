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
#bind layer id float
#bind parm mode int val=0

#import <random.h>

// Houdini's own RNG rather than a hand-rolled hash: SYSwang_inthash to decorrelate the id,
// SYSfastRandom to draw from it. Same pair the shipped SideFX kernels use.
static float randFromId(uint id, uint salt)
{
    uint seed = SYSwang_inthash(id ^ salt);
    return SYSfastRandom(&seed);
}

@KERNEL
{
    uint id = (uint)(@id + 0.5f);

    if (@mode == 1)
    {
        float g = randFromId(id, 0u);
        @colour.set((float3)(g, g, g));
        return;
    }

    @colour.set((float3)(randFromId(id * 3u + 1u, 0u),
                         randFromId(id * 3u + 2u, 0u),
                         randFromId(id * 3u + 3u, 0u)));
}
