#bind layer src float
#bind layer !&dst float

@KERNEL
{
    int2 p = @ixy;

    // Configure dst as 2x1. One work item scans the full image and writes
    // the global minimum and maximum to the two output pixels.
    if (p.x == 0 && p.y == 0)
    {
        float min_value = 1e30f;
        float max_value = -1e30f;

        for (int y = 0; y < @src.yres; ++y)
        {
            for (int x = 0; x < @src.xres; ++x)
            {
                float value = @src.bufferIndex((int2)(x, y));
                min_value = fmin(min_value, value);
                max_value = fmax(max_value, value);
            }
        }

        @dst.setIndex((int2)(0, 0), min_value);
        @dst.setIndex((int2)(1, 0), max_value);
    }
}
