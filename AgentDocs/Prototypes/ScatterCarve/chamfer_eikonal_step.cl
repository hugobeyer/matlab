// EIKONAL STEP — OLDER branch, not part of the envelope chain.
// Inputs: state = Scatter Carve chamfer_state (RGBA);
//         speed_field = optional Mono speed multiplier.
// Outputs: state_out = propagated RGBA distance/source state;
//          state = writable iteration state (Use Write Back Kernel).
// Next: state_out -> chamfer_resolve.state.
#runover layer
#bind layer &state float4 border=WRAP
#bind layer ?speed_field float border=WRAP
#bind layer &state_out float4

#bind parm speed float val=1
#bind parm speed_variation float val=0.5
#bind parm noise_frequency float val=32
#bind parm noise_seed int val=1

// Enable Use Write Back Kernel and set Kernel Iterations on this COP.
// Main kernel reads state and writes state_out; writeback copies the completed
// output into state between iterations, without parallel neighbor-write races.
uint bevel_hash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    return x ^ (x >> 16);
}

float bevel_noise_corner(int2 p, uint seed)
{
    uint h = bevel_hash((uint)p.x * 0x9e3779b9u
        ^ (uint)p.y * 0x85ebca6bu ^ seed);
    return (float)(h >> 8) * (1.0f / 16777216.0f);
}

float bevel_noise(float2 p, uint seed)
{
    int2 cell = convert_int2(floor(p));
    float2 f = p - floor(p);
    f = f * f * (3.0f - 2.0f * f);
    float a = bevel_noise_corner(cell, seed);
    float b = bevel_noise_corner(cell + (int2)(1, 0), seed);
    float c = bevel_noise_corner(cell + (int2)(0, 1), seed);
    float d = bevel_noise_corner(cell + (int2)(1, 1), seed);
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

@KERNEL
{
    float4 current = @state;
    float2 uv = (convert_float2(@ixy) + 0.5f) / convert_float2(@res);
    float frequency = @noise_frequency;
    // Embed UV on a periodic noise domain using mirrored coordinates.
    float2 noise_uv = (float2)(cos(6.28318530718f * uv.x),
                               cos(6.28318530718f * uv.y));
    float noise = bevel_noise(noise_uv * frequency, (uint)@noise_seed);
    float field = @speed_field.bound ? @speed_field : 1.0f;
    float velocity = @speed * field * (1.0f + @speed_variation * (2.0f * noise - 1.0f));

    if (current.x == 0.0f || !isfinite(velocity) || velocity <= 0.0f)
    {
        @state_out.set(current);
        return;
    }

    int2 res = @res;
    int2 xy = @ixy;
    float4 left = @state.bufferIndex((int2)((xy.x + res.x - 1) % res.x, xy.y));
    float4 right = @state.bufferIndex((int2)((xy.x + 1) % res.x, xy.y));
    float4 down = @state.bufferIndex((int2)(xy.x, (xy.y + res.y - 1) % res.y));
    float4 up = @state.bufferIndex((int2)(xy.x, (xy.y + 1) % res.y));
    float4 ax = left.x <= right.x ? left : right;
    float4 ay = down.x <= up.x ? down : up;
    float hx = 1.0f / (float)res.x;
    float hy = 1.0f / (float)res.y;
    float invspeed = 1.0f / velocity;
    float tx = ax.x + hx * invspeed;
    float ty = ay.x + hy * invspeed;
    float arrival = fmin(tx, ty);
    float4 source = tx <= ty ? ax : ay;

    // Anisotropic-grid Godunov solve: ((T-a)/hx)^2 + ((T-b)/hy)^2 = 1/F^2.
    if (isfinite(ax.x) && isfinite(ay.x))
    {
        float origin = fmin(ax.x, ay.x);
        float a = ax.x - origin;
        float b = ay.x - origin;
        float wx = 1.0f / (hx * hx);
        float wy = 1.0f / (hy * hy);
        float A = wx + wy;
        float B = -2.0f * (a * wx + b * wy);
        float C = a * a * wx + b * b * wy - invspeed * invspeed;
        float discriminant = B * B - 4.0f * A * C;
        if (discriminant >= 0.0f)
        {
            float both = origin + (-B + sqrt(discriminant)) / (2.0f * A);
            if (both >= fmax(ax.x, ay.x) && both < arrival)
            {
                arrival = both;
                // Depth/width payload follows the dominant upwind contribution.
                float gx = (both - ax.x) * wx;
                float gy = (both - ay.x) * wy;
                source = gx >= gy ? ax : ay;
            }
        }
    }

    // Travel time is weighted distance when velocity differs from one.
    // Payload transport is a nearest-source approximation, not a volume SDF.
    if (arrival < current.x)
        current = (float4)(arrival, source.y, source.z, source.w);
    @state_out.set(current);
}

@WRITEBACK
{
    @state.set(@state_out.bufferIndex(@ixy));
}
