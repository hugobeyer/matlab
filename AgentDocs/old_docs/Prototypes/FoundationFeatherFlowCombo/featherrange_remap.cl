#bind layer processed float
#bind layer processed_range float
#bind layer source_range float
#bind layer !&dst float

@KERNEL
{
    int2 p = @ixy;

    float processed_min = @processed_range.bufferIndex((int2)(0, 0));
    float processed_max = @processed_range.bufferIndex((int2)(1, 0));
    float source_min = @source_range.bufferIndex((int2)(0, 0));
    float source_max = @source_range.bufferIndex((int2)(1, 0));

    float processed_span = processed_max - processed_min;
    float normalized = processed_span > 1e-6f
        ? clamp((@processed.bufferIndex(p) - processed_min) / processed_span, 0.0f, 1.0f)
        : 0.0f;

    @dst.set(mix(source_min, source_max, normalized));
}
