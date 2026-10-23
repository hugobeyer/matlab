// CHAMFER RESOLVE — final COP of the OLDER Eikonal branch only.
// Inputs: height = original Mono height (same source as Scatter Carve);
//         carved_height = Scatter Carve height_out (Mono);
//         state = Eikonal Step state_out (RGBA);
//         mask = optional Mono mask; resolve_noise = optional Mono noise.
// Outputs: height_out = final Mono height; cut_depth = Mono removed height.
// Do not feed Envelope Step envelope_out into this distance-based resolver.
#runover layer
#bind layer height float border=WRAP
#bind layer carved_height float border=WRAP
#bind layer state float4 border=WRAP
#bind layer ?mask float border=WRAP
#bind layer ?resolve_noise float border=WRAP
#bind layer !&height_out float
#bind layer !&cut_depth float

#bind parm edge int val=0

#bind parm noise_strength float val=0.001
#bind parm noise_center float val=0.5

// edge: 0 = flat chamfer, 1 = quarter-circle profile, 2 = smooth cubic.
// Negative perturbed distance is inside the eroded region: use its floor.
// Only the transition interval evaluates the profile; no parameter/output clamps.
float carve_bevel_profile(float t, int edge)
{
    if (t <= 0.0f)
        return 0.0f;
    if (edge == 1)
        return 1.0f - sqrt(1.0f - t * t);
    if (edge == 2)
        return t * t * (3.0f - 2.0f * t);
    return t;
}

@KERNEL
{
    float original = @height;
    float result = @carved_height;
    float4 sample = @state;
    float weight = @mask.bound ? @mask : 1.0f;
    // Connect native Fractal Noise (Torus, Perlin, etc.) as a Mono field.
    // Perturb distance before profiling, never add noise directly to height.
    float noise_offset = @resolve_noise.bound
        ? @noise_strength * (@resolve_noise - @noise_center) : 0.0f;
    float bevel_distance = sample.x + noise_offset;

    if (isfinite(bevel_distance) && sample.w > 0.0f
        && sample.x >= 0.0f && bevel_distance < sample.w)
    {
        // Anchor the profile to the transported cavity floor and LOCAL surface.
        // Transported source-surface height can leave a lip on varying relief.
        float t = bevel_distance / sample.w;
        float bevel_height = sample.y + (original - sample.y)
            * carve_bevel_profile(t, @edge);
        float removal = original - bevel_height;
        if (removal > 0.0f)
            result = fmin(result, original - removal * weight);
    }


    @height_out.set(result);
    @cut_depth.set(original - result);
}
