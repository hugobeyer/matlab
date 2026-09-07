#bind layer src? val=0
#bind layer !&dst float

#bind parm angle float
#bind parm width float
#bind parm smooth int
#bind parm mirror int

#include "complex.h"

@KERNEL
{
    float deg_to_rad = M_PI_F / 180.0f;

    float2 pos = convert_float2(@ixy) - convert_float2(@res) / 2.0f;
    if (pos.x != 0.0f || pos.y != 0.0f)
        pos = normalize(pos);

    float2 rotation = cexpimag(-@angle * deg_to_rad);

    float2 unrotated_pos = cmult(pos, rotation);

    float value = unrotated_pos.x;

    if(@mirror == 1)
    {
        value = fabs(value);
    }

    float width = cos(@width * deg_to_rad / 2.0f);

    // Can happen quite easily if the user puts in 0 width
    if(width == 1.0)
    {
        @dst.set(value == 1.0f);
        return;
    }

    float result = max(value - width + 1e-3f, 0.0f) / (1.0f - width);

    if(@smooth == 1)
    {
        @dst.set(result);
    }
    else
    {
        @dst.set(result != 0.0f);
    }
}
