#bind layer src
#bind layer id
#bind layer &!dst
#bind layer &!dst_x

#bind parm unitdist float
#bind parm dir int
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
    float falloff = @dPdx.x / @unitdist;

    // --- Vertical feather (per column) ---
    int2 xy = (int2)(@ix, @yres-1);

    float4 val = @src.bufferIndex(xy + (int2)(0, +1));

    for (int i = 0; i < @yres; i++)
    {
        float4 s = @src.bufferIndex(xy);

        // Randomise the input by the incoming id, then remap with a lerp to add contrast.
        float idv = @id.bufferIndex(xy).x;
        float rnd = hash11(idv * 12.9898f);
        s.x += (rnd * 2.0f - 1.0f) * @random_amount;
        s.x = mix(0.5f, s.x, @contrast);

        // Step speed follows the input value, scaled by a lerped multiplier.
        float f = falloff * s.x * mix(1.0f, @speed, rnd);

        // Randomise the direction too, via a lerp between the two.
        float base = @dir ? -1.0f : 1.0f;
        float d = mix(base, -base, rnd);
        val += f * d;
        val = (d < 0.0f) ? max(val, s) : min(val, s);

        @dst.setIndex(xy, val);
        xy.y--;
    }

    // --- Horizontal feather (per row) ---
    int2 xy2 = (int2)(@xres-1, @iy);

    float4 valx = @src.bufferIndex(xy2 + (int2)(+1, 0));

    for (int i = 0; i < @xres; i++)
    {
        float4 s = @src.bufferIndex(xy2);

        float idv = @id.bufferIndex(xy2).x;
        float rnd = hash11(idv * 12.9898f);
        s.x += (rnd * 2.0f - 1.0f) * @random_amount;
        s.x = mix(0.5f, s.x, @contrast);

        float f = falloff * s.x * mix(1.0f, @speed, rnd);

        float base = @dir ? -1.0f : 1.0f;
        float d = mix(base, -base, rnd);
        valx += f * d;
        valx = (d < 0.0f) ? max(valx, s) : min(valx, s);

        @dst_x.setIndex(xy2, valx);
        xy2.x--;
    }
}
