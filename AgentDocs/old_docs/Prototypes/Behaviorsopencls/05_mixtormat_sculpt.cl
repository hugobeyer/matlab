#runover layer
#bind layer height float border=WRAP
#bind layer flow float2 border=WRAP
#bind layer ?size_ref float
#bind layer ?mask float border=WRAP
#bind layer !&height_out float
#bind layer !&sculpt_mask float
#bind layer !&delta_out float

#bind parm depth float val=1
#bind parm strength float val=1
#bind parm width float val=0.01
#bind parm trace_length float val=0.1
#bind parm trace_steps int val=16
#bind parm falloff float val=1

@KERNEL
{
    float2 start = (convert_float2(@ixy) + 0.5f) / convert_float2(@res);
    float original = @height;
    float mask = @mask.bound ? @mask : 1.0f;
    mask = isfinite(mask) ? clamp(mask, 0.0f, 1.0f) : 0.0f;
    float signed_depth = @depth * @strength;
    float distance = fmax(@trace_length, 0.0f);
    int count = clamp(@trace_steps, 1, 64);
    float best = 0.0f;

    if (isfinite(signed_depth) && signed_depth != 0.0f && distance > 0.0f && mask > 0.0f)
    {
        // Positive Depth removes relief downstream. Negative Depth raises relief upstream.
        float sign_dir = signed_depth > 0.0f ? 1.0f : -1.0f;
        float ds = distance / (float)count;
        float2 p = start;
        for (int i = 1; i <= count; ++i)
        {
            float2 k1 = @flow.textureSample(p - floor(p));
            if (!all(isfinite(k1)) || dot(k1, k1) < 1.0e-12f) break;
            float2 middle = p + 0.5f * sign_dir * ds * k1;
            float2 k2 = @flow.textureSample(middle - floor(middle));
            if (!all(isfinite(k2)) || dot(k2, k2) < 1.0e-12f) break;
            p += sign_dir * ds * k2;
            if (!all(isfinite(p))) break;

            float2 q = p - floor(p);
            float h = @height.textureSample(q);
            float width = fmax(@width, 0.0f);
            if (width > 0.0f)
            {
                float2 across = (float2)(-k2.y, k2.x);
                float speed = length(across);
                if (speed > 1.0e-8f) across *= width / speed;
                float a = @height.textureSample(q + across);
                float b = @height.textureSample(q - across);
                h = signed_depth > 0.0f ? fmin(h, fmin(a, b)) : fmax(h, fmax(a, b));
            }
            if (!isfinite(h)) continue;

            float travel = (float)i * ds;
            float decay = pow(fmax(0.0f, 1.0f - travel / (distance + ds)),
                fmax(@falloff, 1.0e-4f));
            float relief = signed_depth > 0.0f ? original - h : h - original;
            best = fmax(best, fmax(relief, 0.0f) * decay);
        }
    }

    float delta = isfinite(best) ? -signed_depth * best * mask : 0.0f;
    float result = original + delta;
    if (!isfinite(result)) { result = original; delta = 0.0f; }
    @height_out.set(result);
    @delta_out.set(delta);
    @sculpt_mask.set(best * mask);
}
