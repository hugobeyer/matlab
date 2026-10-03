#bind layer &mask float

#bind parm normalize_input int val=0
#bind parm measured_min float val=0
#bind parm measured_max float val=1

#bind parm input_min float val=0
#bind parm input_max float val=1
#bind ramp balance float
#bind parm contrast float val=1
#bind parm offset float val=0
#bind parm invert int val=0

float remap_levels(float value, float lo, float hi)
{
    float range = hi - lo;

    // Equal endpoints form a threshold; reversed endpoints invert.
    if (fabs(range) <= 1.0e-6f)
        return value >= lo ? 1.0f : 0.0f;

    return clamp((value - lo) / range, 0.0f, 1.0f);
}

@KERNEL
{
    float value = @mask;

    // Optional normalization. Constant fields retain their value.
    float measured_range = @measured_max - @measured_min;
    if (@normalize_input && measured_range > 1.0e-6f)
    {
        value = clamp(
            (value - @measured_min) / measured_range,
            0.0f, 1.0f);
    }

    // Manual input levels ALWAYS run, independently of normalization.
    value = remap_levels(value, @input_min, @input_max);

    value = clamp(
        (value - 0.5f) * @contrast + 0.5f + @offset,
        0.0f, 1.0f);

    // Curve maps the shaped mask value to its output; a diagonal ramp is neutral.
    value = clamp(@balance.getAt(value), 0.0f, 1.0f);

    if (@invert)
        value = 1.0f - value;

    @mask.set(value);
}
