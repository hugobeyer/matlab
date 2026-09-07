// SLIC step 3 of 4 -- recompute each centre as the mean of the pixels that chose it.
//
// Run over the CENTRES layer again. Each cluster scans only its own 2S x 2S window, so the total
// work is O(pixels) even though it reads like a per-cluster loop -- and no atomics, no scatter,
// no structured buffer. That is the trade the layout at step 1 bought.
//
// Run 02 and 03 alternately. Five to ten rounds is plenty; SLIC converges fast and the last
// rounds move almost nothing.
//
// The centroid falls out here for free, which is worth noticing: it is the per-region frame you
// would otherwise need a whole extra reduction to get, and it is what a per-cluster UV or a
// random rotation would be built on later.
//

#runover layer
#bind layer centre_pos float3 read write
#bind layer centre_feat float3 read write
#bind layer height float
#bind layer label float
#bind parm src_res_x int val=1024
#bind parm src_res_y int val=1024
#bind parm grid_x int val=32
#bind parm grid_y int val=32
#bind parm tiling int val=1

static int wrapi(int v, int n) { return ((v % n) + n) % n; }

@KERNEL
{
    int2 cell = @ixy;
    int2 grid = (int2)(@grid_x, @grid_y);
    int2 res  = (int2)(@src_res_x, @src_res_y);

    float2 S = (float2)((float)res.x / (float)grid.x,
                        (float)res.y / (float)grid.y);

    int myIndex = cell.y * grid.x + cell.x;

    float3 old = @centre_pos;
    int2 c0 = (int2)((int)old.x, (int)old.y);

    int rx = (int)(S.x) + 1;
    int ry = (int)(S.y) + 1;

    float2 sumPos = (float2)(0.0f, 0.0f);
    float  sumH   = 0.0f;
    float  minH   =  1e30f;
    float  maxH   = -1e30f;
    float  count  = 0.0f;

    for (int dy = -ry; dy <= ry; ++dy)
    for (int dx = -rx; dx <= rx; ++dx)
    {
        int px = c0.x + dx;
        int py = c0.y + dy;

        if (@tiling) { px = wrapi(px, res.x); py = wrapi(py, res.y); }
        else if (px < 0 || py < 0 || px >= res.x || py >= res.y) continue;

        int2 q = (int2)(px, py);
        if ((int)(@label.bufferIndex(q) + 0.5f) != myIndex) continue;

        // Accumulate relative to the old centre, then add it back. Summing absolute wrapped
        // coordinates would average across the seam and drag the centre to the middle of the
        // image; the offsets are already the short way round.
        float hq = @height.bufferIndex(q);
        sumPos += (float2)((float)dx, (float)dy);
        sumH   += hq;
        minH    = fmin(minH, hq);
        maxH    = fmax(maxH, hq);
        count  += 1.0f;
    }

    // A cluster can lose every pixel to its neighbours. Leaving it where it is keeps the record
    // valid and lets it recover on a later iteration.
    if (count < 0.5f) return;

    float2 mean = (float2)(old.x, old.y) + sumPos / count;
    if (@tiling)
    {
        mean.x = fmod(fmod(mean.x, (float)res.x) + (float)res.x, (float)res.x);
        mean.y = fmod(fmod(mean.y, (float)res.y) + (float)res.y, (float)res.y);
    }

    @centre_pos.set((float3)(mean.x, mean.y, count));

    // Statistics by id, which is what the centre record is for. The two channels beyond the mean
    // were already sitting unused, so the range costs nothing to carry.
    //
    // This is the thing that lets variation follow the material instead of being pure noise: a
    // cluster's mean says how high it sits, and max-minus-min says how busy it is. Tinting the
    // tall ones warm or the flat ones lighter reads as the surface varying, where a hash reads as
    // noise laid over it.
    @centre_feat.set((float3)(sumH / count, minH, maxH));
}
