// ENVELOPE INIT — new branch, immediately after Scatter Carve.
// Inputs: height = original Mono height (same source as Scatter Carve);
//         carved_height = Scatter Carve height_out (Mono).
// Output: envelope = RGBA height/validity state.
// Next: envelope -> chamfer_envelope_step.envelope. Do not connect chamfer_state.
#runover layer
#bind layer height float border=WRAP
#bind layer carved_height float border=WRAP
#bind layer !&envelope float4

@KERNEL
{
    float original = @height;
    float carved = @carved_height;
    // RGBA state: x = raw height, y = valid, z/w = unused.
    // Only the outer boundary of the merged cut launches a rim.
    // Interior cut pixels remain in carved_height but are not rim sources.
    int cut = isfinite(original) && isfinite(carved) && carved < original;
    int boundary = 0;
    if (cut)
    {
        int2 xy = @ixy;
        int2 res = @res;
        int2 neighbors[4] = {
            (int2)((xy.x + res.x - 1) % res.x, xy.y),
            (int2)((xy.x + 1) % res.x, xy.y),
            (int2)(xy.x, (xy.y + res.y - 1) % res.y),
            (int2)(xy.x, (xy.y + 1) % res.y)
        };
        for (int i = 0; i < 4; ++i)
        {
            float neighbor_original = @height.bufferIndex(neighbors[i]);
            float neighbor_carved = @carved_height.bufferIndex(neighbors[i]);
            if (isfinite(neighbor_original) && isfinite(neighbor_carved)
                && neighbor_carved >= neighbor_original)
            {
                boundary = 1;
                break;
            }
        }
    }
    @envelope.set(boundary ? (float4)(carved, 1.0f, 0.0f, 0.0f)
                           : (float4)(0.0f));
}
