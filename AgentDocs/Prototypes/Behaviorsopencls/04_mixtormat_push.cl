#runover layer
#bind layer height float border=WRAP
#bind layer push_height float border=WRAP
#bind layer ?size_ref float
#bind layer ?mask float border=WRAP
#bind layer !&height_out float
#bind layer !&delta_out float

#bind parm strength float val=1
#bind parm reverse int val=0
#bind parm blend int val=0

@KERNEL
{
    float base = @height;
    float source = @push_height;
    float mask = @mask.bound ? @mask : 1.0f;
    mask = isfinite(mask) ? clamp(mask, 0.0f, 1.0f) : 0.0f;
    float weight = @strength * (@reverse != 0 ? -1.0f : 1.0f) * mask;
    float delta = isfinite(source) && isfinite(weight) ? source * weight : 0.0f;
    float output = base;

    if (@blend == 1)
        output = base * (1.0f + delta);
    else if (@blend == 2)
        output = fmin(base, base + delta);
    else if (@blend == 3)
        output = fmax(base, base + delta);
    else
        output = base + delta;

    if (!isfinite(output)) output = base;
    @height_out.set(output);
    @delta_out.set(output - base);
}
