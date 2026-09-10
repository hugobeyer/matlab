// Step 4 -- group the micro clusters into macro clusters of deliberately uneven size.
//
// Runs over the CENTRES layer, so this is a few thousand elements, not millions of pixels.
//
// Each micro cluster joins whichever macro seed is nearest *to its centroid*, under a
// multiplicatively weighted distance. That last part is what makes the sizes uneven: a seed with
// a big weight reaches further and swallows more micro cells, a small one keeps only its
// immediate neighbours. Uniform macro regions are the tell that SLIC was involved, so the weights
// are drawn at random per seed.
//
// Weighted Voronoi rather than region-growing because it is O(9) per cell with no iteration and
// no dependency between cells -- the same bounded 3x3 search that makes step 02 linear.
//
// Every macro group is a union of whole micro cells, because the decision is made per micro
// cluster and never per pixel. So a macro boundary can never cut through the middle of a micro
// cluster. Two independent SLIC runs at different grid spacings would not give you that, and the
// mismatched double edge is visible under a hue shift.
//
// Deterministic in @seed: the grouping has to survive a re-cook, or the variation dialled in at
// preview is not the variation that renders.
//
// This replaces an earlier version that walked a serpentine path over the grid and cut it into
// random-length runs. That was wrong twice over: a run along a space-filling walk is a one-cell
// ribbon, so the groups came out as snakes rather than patches; and it keyed off grid indices,
// which stop matching spatial adjacency the moment the centres move during iteration.

#runover layer
#bind layer macro float noread write
#bind layer centre_pos float3
#bind parm src_res_x int val=1024
#bind parm src_res_y int val=1024
#bind parm macro_grid_x int val=6
#bind parm macro_grid_y int val=6
#bind parm size_variation float val=0.6
#bind parm jitter float val=0.8
#bind parm tiling int val=1
#bind parm seed int val=1

#import <random.h>

// Houdini's own RNG rather than a hand-rolled hash: SYSwang_inthash to decorrelate the id,
// SYSfastRandom to draw from it. Same pair the shipped SideFX kernels use.
static float randFromId(uint id, uint salt)
{
    uint seed = SYSwang_inthash(id ^ salt);
    return SYSfastRandom(&seed);
}

static int wrapi(int v, int n) { return ((v % n) + n) % n; }

static float wrapd(float d, float n, int tiling)
{
    if (!tiling) return d;
    if (d >  n * 0.5f) d -= n;
    if (d < -n * 0.5f) d += n;
    return d;
}

@KERNEL
{
    int2 mgrid = (int2)(max(@macro_grid_x, 1), max(@macro_grid_y, 1));
    float2 res = (float2)((float)@src_res_x, (float)@src_res_y);
    uint s = (uint)@seed;

    // This micro cluster's centroid, in source pixels -- written by step 03, so it is where the
    // cluster actually ended up rather than where its cell started.
    float3 c = @centre_pos;
    float2 p = (float2)(c.x, c.y);

    float2 MS = res / (float2)((float)mgrid.x, (float)mgrid.y);
    int2 home = (int2)((int)(p.x / MS.x), (int)(p.y / MS.y));

    float best  = 1e30f;
    int   bestI = 0;

    for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx)
    {
        int sx = home.x + dx;
        int sy = home.y + dy;

        if (@tiling) { sx = wrapi(sx, mgrid.x); sy = wrapi(sy, mgrid.y); }
        else if (sx < 0 || sy < 0 || sx >= mgrid.x || sy >= mgrid.y) continue;

        uint sid = (uint)(sy * mgrid.x + sx);

        // Seed position: centre of its macro cell plus scatter, so the macro lattice does not
        // read as a grid of its own.
        float jx = randFromId(sid * 3u + 1u + s, 0u) - 0.5f;
        float jy = randFromId(sid * 3u + 2u + s, 0u) - 0.5f;
        float2 sp = ((float2)((float)sx, (float)sy) + (float2)(0.5f, 0.5f)) * MS
                  + (float2)(jx, jy) * MS * clamp(@jitter, 0.0f, 1.0f);

        float ddx = wrapd(p.x - sp.x, res.x, @tiling);
        float ddy = wrapd(p.y - sp.y, res.y, @tiling);
        float d = sqrt(ddx * ddx + ddy * ddy);

        // The uneven-size term. Dividing the distance by a per-seed weight is a multiplicatively
        // weighted Voronoi: weight > 1 reaches further and takes more micro cells, weight < 1
        // keeps fewer. At size_variation 0 every weight is 1 and the macro cells come out even.
        //
        // Bounded well away from zero -- a tiny weight makes a seed unreachable, which leaves a
        // macro id that no micro cluster carries and a gap in the hue distribution.
        float v = clamp(@size_variation, 0.0f, 1.0f);
        float w = 1.0f + (randFromId(sid * 3u + 3u + s, 0u) - 0.5f) * 2.0f * v * 0.8f;
        w = max(w, 0.2f);

        float dw = d / w;

        if (dw < best) { best = dw; bestI = (int)sid; }
    }

    @macro.set((float)bestI);
}
