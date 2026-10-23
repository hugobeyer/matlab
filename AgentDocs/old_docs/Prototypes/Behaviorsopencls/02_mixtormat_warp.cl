#runover layer
#bind layer height float border=WRAP
#bind layer flow float2 border=WRAP
#bind layer ?size_ref float
#bind layer ?mask float border=WRAP
#bind layer ?color float4 border=WRAP
#bind layer ?coverage float border=WRAP
#bind layer ?region_id int border=WRAP
#bind layer !&height_out float
#bind layer !&color_out float4
#bind layer !&coverage_out float
#bind layer !&region_id_out int
#bind layer !&uv float2

#bind parm mode int val=1
#bind parm strength float val=1
#bind parm trace_length float val=0.1
#bind parm trace_steps int val=16

float2 warp_safe_flow(float2 f)
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
            p -= distance * warp_safe_flow(@flow.textureSample(start));
        }
        else
        {
            int count = clamp(@trace_steps, 1, 64);
            float ds = distance / (float)count;
            for (int i = 0; i < count; ++i)
            {
                float2 k1 = warp_safe_flow(@flow.textureSample(p - floor(p)));
                float2 mid = p - 0.5f * ds * k1;
                float2 k2 = warp_safe_flow(@flow.textureSample(mid - floor(mid)));
                p -= ds * k2;
            }
        }
    }

    if (!all(isfinite(p))) p = start;
    float2 q = p - floor(p);
    @height_out.set(@height.textureSample(q));
    @color_out.set(@color.bound ? @color.textureSample(q) : (float4)(0.0f));
    @coverage_out.set(@coverage.bound ? @coverage.textureSample(q) : 1.0f);
    int2 index = convert_int2(floor(q * convert_float2(@res)));
    @region_id_out.set(@region_id.bound ? @region_id.bufferIndex(index) : 0);
    @uv.set(p);
}
