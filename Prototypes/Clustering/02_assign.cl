// SLIC step 2 of 4 -- every pixel picks its cluster. This is the pass that does the work.
//
// The bounded search is the whole reason SLIC is linear rather than k-means-quadratic: a pixel
// only ever tests the 3x3 block of grid cells around it, because no centre further away can win
// once the spatial term is in the metric. Nine candidates, not K.
//
// D = sqrt( (dFeature / m)^2 + (dSpatial / S)^2 )
//
// m is compactness. High m makes space dominate and you get uniform blobs; low m lets the
// feature dominate and clusters hug the texture's own edges. For subtle per-cluster hue jitter
// you want edge adherence -- a cluster straddling a mortar line tints half a brick, which is the
// one failure people actually notice.
//
// Feature weights are the interesting control and are NOT in the paper. Achanta clusters photos
// in CIELAB; here the vector is the surface's own maps, so weighting picks what "similar" means:
// normal-heavy gives facets, height-heavy gives tiles, albedo-heavy gives material zones.
//

#runover layer
#bind layer height float
#bind layer normal float3
#bind layer albedo float3
#bind layer centre_pos float3
#bind layer centre_feat float3
#bind layer label float noread write
#bind parm grid_x int val=32
#bind parm grid_y int val=32
#bind parm compactness float val=10
#bind parm w_height float val=1
#bind parm w_normal float val=1
#bind parm w_albedo float val=0.5
#bind parm tiling int val=1

static int wrapi(int v, int n)  { return ((v % n) + n) % n; }

// Shortest separation on a wrapping axis. Textures here tile, and photographs do not -- get this
// wrong and the clusters mismatch across the seam, which is exactly where a hue shift shows.
static float wrapd(float d, float n, int tiling)
{
    if (!tiling) return d;
    if (d >  n * 0.5f) d -= n;
    if (d < -n * 0.5f) d += n;
    return d;
}

@KERNEL
{
    int2 p    = @ixy;
    int2 res  = @res;
    int2 grid = (int2)(@grid_x, @grid_y);

    float2 S = (float2)((float)res.x / (float)grid.x,
                        (float)res.y / (float)grid.y);

    float  h = @height;
    float3 n = @normal;
    float3 a = @albedo;

    // Which grid cell this pixel falls in, and therefore which nine centres can reach it.
    int2 home = (int2)((int)((float)p.x / S.x), (int)((float)p.y / S.y));

    float best  = 1e30f;
    int   bestI = 0;

    for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx)
    {
        int cx = home.x + dx;
        int cy = home.y + dy;

        if (@tiling) { cx = wrapi(cx, grid.x); cy = wrapi(cy, grid.y); }
        else if (cx < 0 || cy < 0 || cx >= grid.x || cy >= grid.y) continue;

        int2 c = (int2)(cx, cy);
        float3 cpos  = @centre_pos.bufferIndex(c);
        float3 cfeat = @centre_feat.bufferIndex(c);

        float ddx = wrapd((float)p.x - cpos.x, (float)res.x, @tiling);
        float ddy = wrapd((float)p.y - cpos.y, (float)res.y, @tiling);
        float ds2 = (ddx * ddx) / (S.x * S.x) + (ddy * ddy) / (S.y * S.y);

        // Feature distance. centre_feat.x carries height; the normal and albedo terms are read
        // from the image at the centre so the prototype stays on three float3 layers rather than
        // packing a wider record. Swap to a packed record when this ports.
        int2 cp = (int2)(clamp((int)cpos.x, 0, res.x - 1),
                         clamp((int)cpos.y, 0, res.y - 1));
        float3 cn = @normal.bufferIndex(cp);
        float3 ca = @albedo.bufferIndex(cp);

        float dh = (h - cfeat.x) * @w_height;
        float3 dn = (n - cn) * @w_normal;
        float3 da = (a - ca) * @w_albedo;

        float df2 = dh * dh + dot(dn, dn) + dot(da, da);

        float m = max(@compactness, 1e-4f);
        float D = df2 / (m * m) + ds2;

        if (D < best) { best = D; bestI = cy * grid.x + cx; }
    }

    @label.set((float)bestI);
}
