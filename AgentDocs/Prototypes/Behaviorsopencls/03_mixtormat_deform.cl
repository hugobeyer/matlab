#runover layer
#bind layer height float border=WRAP
#bind layer flow float2 border=WRAP
#bind layer ?size_ref float
#bind layer ?mask float border=WRAP
#bind layer !&height_out float
#bind layer !&uv float2

#bind parm mode int val=0
#bind parm strength float val=1
#bind parm trace_length float val=0.1
#bind parm trace_steps int val=16

float2 deform_safe_flow(float2 f)
{
    return all(isfinite(f)) ? f : (float2)(0.0f);
}

@KERNEL
{
    float2 start = (convert_float2(@ixy) + 0.5f) / convert_float2(@res);
    float2 p = start;
    float mask = @mask.bound ? @mask.textureSample(start) : 1.0f;
    mask = isfinite(mask) ? clamp(mask, 0.0f, 1.0f) : 0.0f;
    float distance = fmax(@trace_length, 0.0f) * @strength * mask;

    if (distance != 0.0f)
    {
        if (@mode == 0)
        {
            // Standard: one Flow-driven source coordinate.
            p -= distance * deform_safe_flow(@flow.textureSample(start));
        }
        else
        {
            // Traced: midpoint RK2 through the same incoming Flow field.
            int count = clamp(@trace_steps, 1, 64);
            float ds = distance / (float)count;
            for (int i = 0; i < count; ++i)
            {
                float2 k1 = deform_safe_flow(@flow.textureSample(p - floor(p)));
                float2 mid = p - 0.5f * ds * k1;
                float2 k2 = deform_safe_flow(@flow.textureSample(mid - floor(mid)));
                p -= ds * k2;
            }
        }
    }

    if (!all(isfinite(p))) p = start;
    @height_out.set(@height.textureSample(p - floor(p)));
    @uv.set(p);
}
