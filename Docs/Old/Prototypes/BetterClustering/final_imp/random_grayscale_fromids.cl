#bind layer !size_ref? val=0
#bind layer min_layer? float
#bind layer max_layer? float
#bind layer ramp? float 
#bind layer seed_layer? int val=0
#bind layer centerx? int
#bind layer centery? int
#bind point geoN? name=n int port=points
#bind point geoWS? name=weightsum float port=points
#bind point geoCDF? name=cdf float[] port=points
#bind point geoCD? name=values float[] port=points
#bind layer !&dst float3

#bind parm rangemode int val=0
#bind parm minval float val=0
#bind parm maxval float val=1
#bind ramp floatramp float

#bind parm seed int val=0
#bind parm perpixel int val=0
#bind parm timeoff float val=0
#bind parm zeroinvalid int

#import "random.h"

int sample_discrete(global float* cdf, int n, float u)
{
    for (int i = 0; i < n; i++)
    {
        if (u <= cdf[i])
            return i;
    }
    return n - 1;
}

@KERNEL
{
    float val = 0.0;
    
    // Hash current seed:
    uint    seed = @seed;
    seed = SYSwang_inthash(seed);
    
    if (@perpixel)
    {
        seed ^= (int)floor(@P.pixel.x);
        seed = SYSwang_inthash(seed);
        seed ^= (int)floor(@P.pixel.y);       
    }
    
    if (@seed_layer.bound) {
        seed += @seed_layer;   
        seed = SYSwang_inthash(seed);
        if (@zeroinvalid && @seed_layer < 0)
        {
            @dst.set(0);            
            return;
        }    
    }
        
    seed += as_int(@timeoff);    
    seed = SYSwang_inthash(seed);  

    if(@rangemode == 0)
    {       
        val = SYSfastRandom(&seed);   
                       
        float minrange = @minval;
        float maxrange = @maxval;
            
        if (@min_layer.bound)           
            minrange = @min_layer;
        if (@max_layer.bound)           
            maxrange = @max_layer;
         
        val = minrange + (maxrange - minrange) * val;
    }    
    else if(@rangemode == 1) {
        // Apply random distribution
        val = SYSfastRandom(&seed);
        if (@ramp.bound)
        {
            val = @ramp.imageSample( (float2)(val*2.0f-1.0f, 0) );
        }
        else {
            val = @floatramp(val);
        }
    }
    else if(@rangemode == 2) {
        
        val = SYSfastRandom(&seed);
        int s = sample_discrete(@geoCDF.tupleAt(0), @geoN(0), val * @geoWS(0));    
        //float* t = @geoCD.tupleAt(0);
        val = @geoCD.tupleAt(0)[s];
    }    
    @dst.set(val);
}
