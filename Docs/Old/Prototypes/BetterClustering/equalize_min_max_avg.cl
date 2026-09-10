#runover vdb

#bind vdb src float?
#bind vdb &!average float?
#bind vdb &!minimum float?
#bind vdb &!maximum float?

#bind point count name=count int portname=geo
#bind point sum name=sum float3 portname=geo
#bind point min name=min float3 portname=geo
#bind point max name=max float3 portname=geo

@KERNEL
{
    if (!@sum.len)
    {
        // No leaves, but probably shouldn't have even run here as there
        // are no leaves :>
        return;
    }

    float3 average = @sum.getAt(0) / @count.getAt(0);
    @average.set(average);
    @minimum.set(@min.getAt(0));
    @maximum.set(@max.getAt(0));
}
