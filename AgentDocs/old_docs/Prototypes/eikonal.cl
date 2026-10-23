#bind layer src
#bind layer id
#bind layer &!dst

#bind parm unitdist float
#bind parm dir int
#bind parm amount_y float val=1
#bind parm amount_x float val=1
#bind parm random_amount float val=0
#bind parm contrast float val=1
#bind parm speed float val=1

static float frac1(float x) { return x - floor(x); }

static float hash11(float x)
{
    x = frac1(x * 0.1031f);
    x *= x + 33.33f;
    x *= x + x;
    return frac1(x);
}

@KERNEL
{
    // --- Vertical sweep down column @ix, capture its value at row @iy ---
    int2 xy = (int2)(@ix, @yres-1);

    float falloff = (@unitdist / max(@dPdx.x, 0.0001f)) * @amount_y;

    float4 val = @src.bufferIndex(xy + (int2)(0, +1));
    float4 vert = val;

    for (int i = 0; i < @yres; i++)
    {
        float4 s = @src.bufferIndex(xy);

        // Randomise the input by the incoming id.
        float idv = @id.bufferIndex(xy).x;
        float rnd = hash11(idv * 12.9898f);
        s.x += (rnd * 2.0f - 1.0f) * @random_amount;

        // Step speed follows the input value, scaled by a lerped multiplier.
        // Apply contrast only to the falloff/speed calculation.
        float contrast_s = mix(0.5f, s.x, @contrast);
        float f = falloff * (1.0f / max(contrast_s, 0.0001f)) * mix(1.0f, 1.0f / max(@speed, 0.0001f), rnd);

        // Randomise the direction too, via a lerp between the two.
        float base = @dir ? -1.0f : 1.0f;
        float d = mix(base, -base, rnd);
        val += f * d;
        val = (d < 0.0f) ? max(val, s) : min(val, s);

        if (xy.y == @iy) vert = val;
        xy.y--;
    }

    // --- Horizontal sweep left along row @iy, blended into dst at @ix ---
    int2 xy2 = (int2)(@xres-1, @iy);

    float hfalloff = (@unitdist / max(@dPdy.y, 0.0001f)) * @amount_x;

    float4 valx = @src.bufferIndex(xy2 + (int2)(+1, 0));

    for (int i = 0; i < @xres; i++)
    {
        float4 s = @src.bufferIndex(xy2);

        float idv = @id.bufferIndex(xy2).x;
        float rnd = hash11(idv * 12.9898f);
        s.x += (rnd * 2.0f - 1.0f) * @random_amount;

        float contrast_s = mix(0.5f, s.x, @contrast);
        float f = hfalloff * (1.0f / max(contrast_s, 0.0001f)) * mix(1.0f, 1.0f / max(@speed, 0.0001f), rnd);

        float base = @dir ? -1.0f : 1.0f;
        float d = mix(base, -base, rnd);
        valx += f * d;
        valx = (d < 0.0f) ? max(valx, s) : min(valx, s);

        // Blend the horizontal sweep with the vertical one: max when pushing down, min when pushing up.
        if (xy2.x == @ix)
        {
            float4 blended = (d < 0.0f) ? max(valx, vert) : min(valx, vert);
            @dst.setIndex(xy2, blended);
        }
        xy2.x--;
    }
}
