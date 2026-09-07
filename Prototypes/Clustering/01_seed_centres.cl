// SLIC step 1 of 4 -- plant one cluster centre per grid cell.
//
// Run over the CENTRES layer, not the image. That layer is tiny: its resolution IS the cluster
// grid, so pixel (i,j) of it holds cluster (i,j). Every later pass keeps that convention, which
// is what lets the whole prototype avoid atomics and structured buffers -- a cluster's record is
// just a texel, and "reduce over a cluster" becomes "one texel scans its own neighbourhood".
//
// That matters beyond Houdini: it ports to RDG as plain textures, which is the one thing the
// Unreal compositor already knows how to bind.
//

#runover layer
#bind layer height float
#bind layer centre_pos float3 noread write
#bind layer centre_feat float3 noread write
#bind parm src_res_x int val=1024
#bind parm src_res_y int val=1024
#bind parm jitter float val=1

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
    int2 cell = @ixy;          // which cluster this texel owns
    int2 grid = @res;          // the cluster grid dimensions

    // Cell size in source pixels. Non-integer is fine and expected -- the assign pass works in
    // source pixels throughout, so nothing depends on the grid dividing the image evenly.
    float2 S = (float2)((float)@src_res_x / (float)grid.x,
                        (float)@src_res_y / (float)grid.y);

    // Centre of the cell, plus optional scatter. Jitter breaks the "everything on a lattice"
    // read that uniform SLIC otherwise gives, before a single iteration has run.
    float2 pos = ((float2)((float)cell.x, (float)cell.y) + (float2)(0.5f, 0.5f)) * S;

    uint seed = (uint)(cell.y * grid.x + cell.x);
    float jx = randFromId(seed * 2u + 1u, 0u) - 0.5f;
    float jy = randFromId(seed * 2u + 7919u, 0u) - 0.5f;
    pos += (float2)(jx, jy) * S * @jitter;

    @centre_pos.set((float3)(pos.x, pos.y, 0.0f));

    // Feature seeded from the pixel under the centre. One sample is enough: the update pass
    // replaces it with the true mean after the first iteration.
    int2 p = (int2)(clamp((int)pos.x, 0, @src_res_x - 1),
                    clamp((int)pos.y, 0, @src_res_y - 1));
    float h = @height.bufferIndex(p);
    @centre_feat.set((float3)(h, 0.0f, 0.0f));
}
