#runover attribute

#bind point &count name=count int portname=geo
#bind point &sum name=sum float3 portname=geo
#bind point &min name=min float3 portname=geo
#bind point &max name=max float3 portname=geo

@KERNEL
{
    // Only run first...
    if (@elemnum)
        return;
    int count = 0;
    float3 mn = MAXFLOAT;
    float3 mx = -MAXFLOAT;
    float3 sum = 0;
    for (int i = 0; i < @count.len; i++)
    {
        count += @count.getAt(i);
        sum += @sum.getAt(i);
        mn = min(mn, @min.getAt(i));
        mx = max(mx, @max.getAt(i));
    }
    @count.set(count);
    @sum.set(sum);
    @min.set(mn);
    @max.set(mx);
}
