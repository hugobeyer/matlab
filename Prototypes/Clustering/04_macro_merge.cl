// Step 4 -- group the micro clusters into macro clusters of deliberately uneven size.
//
// Runs over the CENTRES layer, so this is a few thousand elements, not millions of pixels. The
// expensive pass already happened; this one is free.
//
// Uniform macro regions are the tell that SLIC was involved, so the sizes are drawn at random
// between a minimum and a maximum instead. Some macro groups end up two micro cells, some
// fifteen.
//
// Every macro group is a union of whole micro cells, so a macro boundary can never cut through
// the middle of a micro cluster. Two independent SLIC runs at different grid spacings would not
// give you that, and the mismatched double edge is visible under a hue shift.
//
// Deterministic in @seed: the grouping has to survive a re-cook, or the variation you dialled in
// at preview is not the variation that renders.
//

#runover layer
#bind layer macro noread write
#bind layer centre_pos float3
#bind parm grid int2
#bind parm min_cells int val=2
#bind parm max_cells int val=12
#bind parm seed int val=1

static float hash11(uint n)
{
    n = (n ^ 61u) ^ (n >> 16u);
    n *= 9u; n = n ^ (n >> 4u); n *= 0x27d4eb2du; n = n ^ (n >> 15u);
    return (float)(n & 0x00FFFFFFu) / (float)0x01000000;
}

@KERNEL
{
    int2 cell = @ixy;
    int2 grid = @grid;
    uint s = (uint)@seed;

    // A macro group is a run of micro cells on a space-filling walk of the grid, with a random
    // length per group. Boustrophedon order -- serpentine, reversing every row -- so a run that
    // crosses a row boundary stays adjacent on the image instead of teleporting across it.
    //
    // A proper adjacency-graph flood would be better and is what the Unreal port should do. This
    // is the version that fits in one dependency-free kernel and is enough to judge the look.
    int row = cell.y;
    int col = (row & 1) ? (grid.x - 1 - cell.x) : cell.x;
    int walk = row * grid.x + col;

    int lo = max(@min_cells, 1);
    int hi = max(@max_cells, lo);

    // March group boundaries from the start of the walk. Cheap because the walk index is small,
    // and it keeps every cell's answer independent of every other cell's.
    int at = 0;
    int group = 0;
    while (at <= walk)
    {
        float r = hash11((uint)group * 2654435761u + s);
        int len = lo + (int)(r * (float)(hi - lo + 1));
        len = clamp(len, lo, hi);
        at += len;
        if (at <= walk) group++;
    }

    @macro.set((float)group);
}
