#runover layer
#bind layer height float border=WRAP
#bind layer ?size_ref float
#bind layer ?surface_height float border=WRAP
#bind layer ?incoming_flow float2 border=WRAP
#bind layer ?mask float border=WRAP
#bind layer !&generated float2
#bind layer !&flow float2
#bind layer !&magnitude float

#bind parm height_weight float val=0
#bind parm slope_weight float val=0
#bind parm curl_weight float val=1
#bind parm force_weight float val=0
#bind parm angle float val=0
#bind parm gravity float val=0
#bind parm strength float val=1
#bind parm add float val=1
#bind parm mix_amount float val=0
#bind parm gradient_radius int val=1
#bind parm curl_period int val=4
#bind parm curl_octaves int val=3
#bind parm curl_roughness float val=0.5
#bind parm curl_lacunarity float val=2
#bind parm seed int val=1

int flow_wrapi(int x, int period)
{
    int m = max(period, 1);
    int v = x % m;
    return v < 0 ? v + m : v;
}

uint flow_hash(uint v)
{
    v ^= v >> 16;
    v *= 0x7feb352du;
    v ^= v >> 15;
    v *= 0x846ca68bu;
    v ^= v >> 16;
    return v;
}

float2 flow_gradient(int2 cell, int period, uint seed)
{
    uint x = (uint)flow_wrapi(cell.x, period);
    uint y = (uint)flow_wrapi(cell.y, period);
    uint h = flow_hash(x * 0x27d4eb2du ^ y * 0x165667b1u ^ seed * 0x9e3779b9u);
    float a = (float)(h & 0x00FFFFFFu) * (6.28318530718f / 16777216.0f);
    return (float2)(cos(a), sin(a));
}

float3 flow_periodic_derivative(float2 p, int period, uint seed)
{
    int2 i = convert_int2(floor(p));
    float2 f = p - convert_float2(i);
    float2 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    float2 du = 30.0f * f * f * (f * (f - 2.0f) + 1.0f);

    float2 ga = flow_gradient(i, period, seed);
    float2 gb = flow_gradient(i + (int2)(1, 0), period, seed);
    float2 gc = flow_gradient(i + (int2)(0, 1), period, seed);
    float2 gd = flow_gradient(i + (int2)(1, 1), period, seed);

    float va = dot(ga, f);
    float vb = dot(gb, f - (float2)(1.0f, 0.0f));
    float vc = dot(gc, f - (float2)(0.0f, 1.0f));
    float vd = dot(gd, f - (float2)(1.0f, 1.0f));
    float k = va - vb - vc + vd;
    float value = va + u.x * (vb - va) + u.y * (vc - va) + u.x * u.y * k;
    float2 d = ga + u.x * (gb - ga) + u.y * (gc - ga)
        + u.x * u.y * (ga - gb - gc + gd)
        + du * ((float2)(u.y, u.x) * k + (float2)(vb, vc) - va);
    return (float3)(value, d.x, d.y);
}

float2 flow_curl(float2 uv, int period, int octaves, float roughness, float lacunarity, uint seed)
{
    float2 sum = (float2)(0.0f);
    float weight = 1.0f;
    float weights = 0.0f;
    float p = (float)clamp(period, 1, 4096);
    float persistence = clamp(roughness, 0.0f, 1.0f);
    float step = fmax(lacunarity, 1.0f);
    int count = clamp(octaves, 1, 8);
    for (int i = 0; i < count; ++i)
    {
        int n = clamp((int)rint(p), 1, 4096);
        float3 derivative = flow_periodic_derivative(uv * (float)n, n,
            seed + (uint)i * 0x2545F491u);
        sum += weight * (float2)(derivative.z, -derivative.y);
        weights += weight;
        weight *= persistence;
        p = fmin(4096.0f, p * step);
    }
    return sum / fmax(weights, 1.0e-6f);
}

float2 flow_downhill(float xp, float xm, float yp, float ym, int radius)
{
    float2 v = (float2)(xp - xm, yp - ym);
    if (!all(isfinite(v))) return (float2)(0.0f);
    return v * (-0.5f / (float)max(radius, 1));
}

@KERNEL
{
    float2 uv = (convert_float2(@ixy) + 0.5f) / convert_float2(@res);
    float2 pixel = 1.0f / convert_float2(@res);
    int radius = clamp(@gradient_radius, 1, 16);
    float2 dx = (float2)(pixel.x * (float)radius, 0.0f);
    float2 dy = (float2)(0.0f, pixel.y * (float)radius);

    float2 own = (float2)(0.0f);
    if (@height_weight != 0.0f)
    {
        own = flow_downhill(
            @height.textureSample(uv + dx), @height.textureSample(uv - dx),
            @height.textureSample(uv + dy), @height.textureSample(uv - dy), radius);
    }

    float2 surface = (float2)(0.0f);
    if (@slope_weight != 0.0f || (@force_weight != 0.0f && @gravity != 0.0f))
    {
        if (@surface_height.bound)
        {
            surface = flow_downhill(
                @surface_height.textureSample(uv + dx), @surface_height.textureSample(uv - dx),
                @surface_height.textureSample(uv + dy), @surface_height.textureSample(uv - dy), radius);
        }
        else
        {
            surface = flow_downhill(
                @height.textureSample(uv + dx), @height.textureSample(uv - dx),
                @height.textureSample(uv + dy), @height.textureSample(uv - dy), radius);
        }
    }

    float2 curl = @curl_weight != 0.0f
        ? flow_curl(uv, @curl_period, @curl_octaves, @curl_roughness,
            @curl_lacunarity, (uint)@seed) : (float2)(0.0f);

    float a = radians(@angle);
    float2 force = (float2)(cos(a), -sin(a));
    // Gravity steers Force downhill, based only on surface Height.
    // Normalize only the slope steering basis, never the final combined Flow.
    float slope_length = length(surface);
    if (@gravity != 0.0f && isfinite(slope_length) && slope_length > 0.0f)
        force += @gravity * surface / fmax(1.0f, slope_length);

    float2 made = @strength * (
        @height_weight * own + @slope_weight * surface
        + @curl_weight * curl + @force_weight * force);
    if (!all(isfinite(made))) made = (float2)(0.0f);

    float m = @mask.bound ? @mask.textureSample(uv) : 1.0f;
    m = isfinite(m) ? clamp(m, 0.0f, 1.0f) : 0.0f;
    float2 incoming = @incoming_flow.bound
        ? @incoming_flow.textureSample(uv) : (float2)(0.0f);
    if (!all(isfinite(incoming))) incoming = (float2)(0.0f);

    float mix_weight = clamp(@mix_amount * m, 0.0f, 1.0f);
    float2 combined = mix(incoming, made, mix_weight) + made * (@add * m);
    if (!all(isfinite(combined))) combined = (float2)(0.0f);

    @generated.set(made);
    @flow.set(combined);
    @magnitude.set(length(combined));
}
