// ENVELOPE STEP — new branch; use Kernel Iterations + Use Write Back Kernel.
// Inputs: envelope = Envelope Init envelope (RGBA);
//         speed_field = optional Mono positive speed multiplier.
// Outputs: envelope_out = propagated RGBA height/validity state;
//          envelope = writable iteration state (do not wire as final height).
// Next: envelope_out -> chamfer_envelope_resolve.envelope.
#runover layer
#bind layer &envelope float4 border=WRAP
#bind layer ?speed_field float border=WRAP
#bind layer &envelope_out float4

#bind parm rise_per_uv float val=1

// Shared Chamfer slope in RAW height units per UV unit, not a pixel width.
// Higher slope makes narrower rims. No per-source width or curved profiles.
// Enable Use Write Back Kernel and set Kernel Iterations on this COP.
// Each iteration advances at most one eight-connected grid step.
@KERNEL
{
    // RGBA state: x = raw height, y = valid, z/w = unused.
    float4 current = @envelope;
    current = current.y == 1.0f && isfinite(current.x)
        ? (float4)(current.x, 1.0f, 0.0f, 0.0f) : (float4)(0.0f);
    float field = @speed_field.bound ? @speed_field : 1.0f;
    if (!isfinite(@rise_per_uv) || @rise_per_uv < 0.0f
        || !isfinite(field) || field <= 0.0f)
    {
        @envelope_out.set(current);
        return;
    }

    float slope = @rise_per_uv / field;
    if (!isfinite(slope))
    {
        @envelope_out.set(current);
        return;
    }
    int2 xy = @ixy;
    int2 res = @res;
    float2 texel = 1.0f / convert_float2(res);
    float best = current.x;
    int valid = current.y == 1.0f;

    // Fixed eight-tap stencil, independent of rim radius. This solves a graph
    // height envelope, not an exact Euclidean SDF; diagonal bias remains.
    // A shared additive rise lets a lower candidate safely replace a higher
    // one: future extensions add the same cost, regardless of source identity.
    for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx)
    {
        if (dx == 0 && dy == 0)
            continue;
        int2 neighbor = (int2)((xy.x + dx + res.x) % res.x,
                              (xy.y + dy + res.y) % res.y);
        float4 source = @envelope.bufferIndex(neighbor);
        if (source.y != 1.0f || !isfinite(source.x))
            continue;
        float distance = length(convert_float2((int2)(dx, dy)) * texel);
        float candidate = source.x + slope * distance;
        if (isfinite(candidate) && (!valid || candidate < best))
        {
            best = candidate;
            valid = 1;
        }
    }

    // Cavity sources are upper bounds, not frozen pixels: deeper neighbors
    // may lower them too. Iteration extends coverage, not cumulative erosion.
    @envelope_out.set((float4)(best, (float)valid, 0.0f, 0.0f));
}

@WRITEBACK
{
    @envelope.set(@envelope_out.bufferIndex(@ixy));
}
