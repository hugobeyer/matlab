// ENVELOPE RESOLVE — final COP of the new Chamfer-only branch.
// Inputs: height = original Mono height (same source as Scatter Carve);
//         carved_height = Scatter Carve height_out (Mono);
//         envelope = Envelope Step envelope_out (RGBA);
//         mask = optional Mono mask for additional rim removal.
// Outputs: height_out = final Mono height (connect to normals/preview);
//          cut_depth = Mono difference from original height.
#runover layer
#bind layer height float
#bind layer carved_height float
#bind layer envelope float4
#bind layer ?mask float
#bind layer !&height_out float
#bind layer !&cut_depth float

@KERNEL
{
    float original = @height;
    float carved = @carved_height;
    float4 state = @envelope;
    float candidate = state.x;
    float weight = @mask.bound ? @mask : 1.0f;
    weight = isfinite(weight) ? clamp(weight, 0.0f, 1.0f) : 0.0f;
    float result = carved;

    // Mask only the additional rim removal; preserve the existing carve.
    // Raw height is never normalized or clamped to a display range.
    if (state.y == 1.0f && isfinite(candidate) && candidate < carved && weight > 0.0f)
        result = mix(carved, candidate, weight);

    @height_out.set(result);
    @cut_depth.set(original - result);
}
