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
#bind layer height
#bind layer centre_pos float3 noread write
#bind layer centre_feat float3 noread write
#bind parm src_res int2
#bind parm jitter float val=1

static float hash11(uint n)
{
    n = (n ^ 61u) ^ (n >> 16u);
    n *= 9u;
    n = n ^ (n >> 4u);
    n *= 0x27d4eb2du;
    n = n ^ (n >> 15u);
    return (float)(n & 0x00FFFFFFu) / (float)0x01000000;
}

@KERNEL
{
    int2 cell = @ixy;          // which cluster this texel owns
    int2 grid = @res;          // the cluster grid dimensions

    // Cell size in source pixels. Non-integer is fine and expected -- the assign pass works in
    // source pixels throughout, so nothing depends on the grid dividing the image evenly.
    float2 S = (float2)((float)@src_res.x / (float)grid.x,
                        (float)@src_res.y / (float)grid.y);

    // Centre of the cell, plus optional scatter. Jitter breaks the "everything on a lattice"
    // read that uniform SLIC otherwise gives, before a single iteration has run.
    float2 pos = ((float2)((float)cell.x, (float)cell.y) + (float2)(0.5f, 0.5f)) * S;

    uint seed = (uint)(cell.y * grid.x + cell.x);
    float jx = hash11(seed * 2u + 1u) - 0.5f;
    float jy = hash11(seed * 2u + 7919u) - 0.5f;
    pos += (float2)(jx, jy) * S * @jitter;

    @centre_pos.set((float3)(pos.x, pos.y, 0.0f));

    // Feature seeded from the pixel under the centre. One sample is enough: the update pass
    // replaces it with the true mean after the first iteration.
    int2 p = (int2)(clamp((int)pos.x, 0, @src_res.x - 1),
                    clamp((int)pos.y, 0, @src_res.y - 1));
    float h = @height.bufferIndex(p);
    @centre_feat.set((float3)(h, 0.0f, 0.0f));
}
