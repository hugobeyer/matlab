#bind layer src
#bind layer !&dst
#bind layer count int val=0

@KERNEL
{
    int count = @count.bufferIndex((int2)(@count.xres-1, @count.yres-1));
    if (!count)
        @dst.set(@src);
    else
    {
        float invcount = 1.0f / count;
        @dst.set(@src * invcount);
    }
}
