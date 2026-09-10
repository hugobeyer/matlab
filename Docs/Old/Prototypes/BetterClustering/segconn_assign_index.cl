#bind layer src int
#bind layer !&dst int

@KERNEL
{
    int val = @ix + @iy * @xres;
    @dst.set(val);
}
