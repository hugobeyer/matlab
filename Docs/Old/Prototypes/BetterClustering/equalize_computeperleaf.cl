#runover vdb

#bind vdb &src float?

#bind point &count name=count int portname=geo
#bind point &sum name=sum float3 portname=geo
#bind point &min name=min float3 portname=geo
#bind point &max name=max float3 portname=geo

#bind vdb active? float val=1

@KERNEL
{
    float3 mn = MAXFLOAT;
    float3 mx = -MAXFLOAT;
    int count = 0;
    float3 sum = 0;
    
    for (int dx = 0; dx < 8; dx ++)
    {
        for (int dy = 0; dy < 8; dy++)
        {
            for (int dz = 0; dz < 8; dz++)
            {
                // Check normal activity.
                if (!@src.activeAt(@ix+dx, @iy+dy, @iz+dz))
                    continue;
                // Check active mask.
                // NOTE: THis is assuming an aligned active mask...
                if (@active.getAt(@ix+dx, @iy+dy, @iz+dz) > 0.5)
                {
                    float3 val = @src.getAt(@ix+dx,@iy+dy,@iz+dz);
                    mn = min(val, mn);
                    mx = max(val, mx);
                    sum += val;
                    count++;
                }
            }
        }
    }
    @sum.setAt(@elemnum/512, sum);
    @min.setAt(@elemnum/512, mn);
    @max.setAt(@elemnum/512, mx);
    @count.setAt(@elemnum/512, count);
}
