#bind layer src float val=0
#bind layer !&dst int

#bind parm cutoff float val=0
#bind parm above int val=0

@KERNEL
{
    int val = @ix + @iy * @xres;
    
    if (@above == 0 && @src > @cutoff)
        val = -1;
    if (@above == 1 && @src < @cutoff)
        val = -1;
    
    @dst.set(val);
}
