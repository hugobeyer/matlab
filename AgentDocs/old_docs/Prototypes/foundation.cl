#bind layer size_ref? float port=size_ref nouipromote
#bind layer !&dst float

#bind parm count_x int val=8
#bind parm count_y int val=6
#bind parm density float val=0.88

// Size is one range plus an aspect (y/x); the x range times the aspect gives y.
#bind parm size_min float val=0.75
#bind parm size_max float val=1.35
#bind parm size_aspect float val=0.78

// One jitter drives position, side, rotation and lean offsets (see kernel).
#bind parm jitter float val=0.18

#bind parm height_min float val=0.18
#bind parm height_max float val=0.90

// Shared quantisation for both height and angle; 0 = off.
#bind parm steps int val=0

#bind parm rotation float val=0

#bind parm lean_x float val=0
#bind parm lean_y float val=0

#bind parm formation_cells int val=3
#bind parm formation_amount float val=0.30

#bind parm quarter_copies int val=1
// Multiplies the quarter-tile subdivisions on Y only (1 = 4 rows, 2 = 8 rows, ...).
#bind parm quarter_y_count int val=1
#bind parm quarter_fill float val=0.55
#bind parm quarter_size float val=0.82
#bind parm quarter_height float val=0.72

#bind parm camera_yaw float val=45
#bind parm camera_pitch float val=35.264
#bind parm view_scale float val=1.55

#bind parm seed float val=1234


static float frac1(float x) { return x - floor(x); }

static float hash11(float x)
{
    x = frac1(x * 0.1031f);
    x *= x + 33.33f;
    x *= x + x;
    return frac1(x);
}

static float hash21(int2 p, float seed)
{
    return hash11((float)p.x * 127.1f + (float)p.y * 311.7f + seed * 74.17f);
}

static float rand_cell(int2 p, float seed, float salt)
{
    return hash21(p, seed + salt);
}

static float rand_signed(int2 p, float seed, float salt)
{
    return rand_cell(p, seed, salt) * 2.0f - 1.0f;
}

static float2 rotate2(float2 p, float a)
{
    float c = cos(a);
    float s = sin(a);
    return (float2)(c * p.x - s * p.y, s * p.x + c * p.y);
}

static float safe_inv(float x)
{
    if (fabs(x) < 1e-8f)
        return x < 0.0f ? -1e8f : 1e8f;
    return 1.0f / x;
}

static float ray_box(float3 ro, float3 rd, float3 bmin, float3 bmax)
{
    float3 inv = (float3)(safe_inv(rd.x), safe_inv(rd.y), safe_inv(rd.z));
    float3 ta = (bmin - ro) * inv;
    float3 tb = (bmax - ro) * inv;
    float3 tmn = fmin(ta, tb);
    float3 tmx = fmax(ta, tb);
    float tn = fmax(tmn.x, fmax(tmn.y, tmn.z));
    float tf = fmin(tmx.x, fmin(tmx.y, tmx.z));
    if (tf < 0.0f || tn > tf)
        return -1.0f;
    return tn >= 0.0f ? tn : tf;
}

static float quantized01(float x, int steps)
{
    x = clamp(x, 0.0f, 1.0f);
    if (steps <= 1)
        return x;
    float n = (float)(steps - 1);
    return floor(x * n + 0.5f) / n;
}


@KERNEL
{
    int2 res = convert_int2(@dst.res);
    int nx = max(@count_x, 1);
    int ny = max(@count_y, 1);

    float2 uv = (convert_float2(@ixy) + 0.5f) / convert_float2(res);
    uv -= 0.5f;

    float aspect = (float)res.x / fmax((float)res.y, 1.0f);
    uv.x *= aspect;
    uv *= @view_scale;

    float yaw = @camera_yaw * 0.0174532925199433f;
    float pitch = @camera_pitch * 0.0174532925199433f;

    float cp = cos(pitch);
    float sp = sin(pitch);
    float cy = cos(yaw);
    float sy = sin(yaw);

    float3 rd = normalize((float3)(cp * cy, cp * sy, -sp));

    float3 world_up = (float3)(0.0f, 0.0f, 1.0f);
    float3 cam_right = normalize(cross(rd, world_up));
    float3 cam_up = normalize(cross(cam_right, rd));

    float hmin = fmin(@height_min, @height_max);
    float hmax = fmax(@height_min, @height_max);

    float formation = clamp(@formation_amount, 0.0f, 1.0f);

    float global_hmax = hmax * (1.0f + formation * 0.5f);

    // Camera distance is derived from the formation height.
    float cam_dist = 2.0f + global_hmax;

    float3 target = (float3)(0.5f, 0.5f, 0.0f);

    float3 ro = target - rd * cam_dist + cam_right * uv.x + cam_up * uv.y;

    /*
        One complete texture repeat in screen X/Y,
        expressed as world-space camera-plane vectors.
    */
    float3 tile_x = cam_right * (aspect * @view_scale);
    float3 tile_y = cam_up * @view_scale;

    float density = clamp(@density, 0.0f, 1.0f);
    float quarter_fill = clamp(@quarter_fill, 0.0f, 1.0f);
    float quarter_size = fmax(@quarter_size, 0.01f);
    float quarter_height = fmax(@quarter_height, 0.01f);

    // --- Derived from the compact parameter set -------------------------
    float jit = fmax(@jitter, 0.0f);

    float posjx = clamp(jit, 0.0f, 0.48f);
    float posjy = clamp(jit, 0.0f, 0.48f);
    float sidejag = clamp(jit * 1.1f, 0.0f, 0.80f);
    float rotjit = jit * 39.0f;
    float leanjit = jit * 0.55f;

    float sxmin = fmax(fmin(@size_min, @size_max), 0.02f);
    float sxmax = fmax(fmax(@size_min, @size_max), sxmin);
    float saspect = fmax(@size_aspect, 0.02f);
    float symin = fmax(sxmin * saspect, 0.02f);
    float symax = fmax(sxmax * saspect, symin);

    int height_steps = max(@steps, 0);
    int angle_steps = max(@steps, 0);

    // Depth ramp rides on the camera distance: near = 0.5x, far = 1.5x.
    float dnear = cam_dist * 0.5f;
    float dfar = cam_dist * 1.5f;

    int cluster_cells = max(@formation_cells, 1);
    int pad = 2;
    int cr = 1;
    // --------------------------------------------------------------------

    float2 cell_size = (float2)(1.0f / (float)nx, 1.0f / (float)ny);

    float best_t = 1e20f;
    int hit = 0;

    /*
        Staggered copies on a quarter-tile grid: 4 columns
        on X and 4 * quarter_y_count rows on Y, so Y can be
        subdivided further than X.

        The pattern itself repeats every complete image
        tile.
    */
    int y_sub = 4 * max(@quarter_y_count, 1);
    int phase_count = @quarter_copies ? 4 * y_sub : 1;

    for (int phase = 0; phase < phase_count; ++phase)
    {
        int xi = phase & 3;
        int yi = phase >> 2;

        float phase_x = (float)xi * 0.25f;
        float phase_y = (float)yi / (float)y_sub;
        int secondary = phase != 0;

        float phase_density = secondary ? density * quarter_fill : density;
        float phase_size = secondary ? quarter_size : 1.0f;

        // Each quarter-Y row gets its own height scale.
        float phase_height = 1.0f;
        if (secondary)
        {
            float hrand = hash11((float)yi * 37.719f + @seed * 0.137f);
            phase_height = quarter_height * mix(0.65f, 1.35f, hrand);
        }

        /*
            Each staggered phase owns a different but
            deterministic rock population.
        */
        float phase_seed = @seed + (float)phase * 971.371f;

        for (int copy_y = -3; copy_y <= 3; ++copy_y)
        {
            if (abs(copy_y) > cr)
                continue;

            for (int copy_x = -3; copy_x <= 3; ++copy_x)
            {
                if (abs(copy_x) > cr)
                    continue;

                /*
                    Full-tile copy plus optional
                    half-tile stagger.
                */
                float fx = (float)copy_x + phase_x;
                float fy = (float)copy_y + phase_y;

                float3 copy_shift = tile_x * fx + tile_y * fy;
                float3 cro = ro - copy_shift;

                /*
                    Ray footprint between ground and
                    maximum formation height.
                */
                float rz = fabs(rd.z) > 1e-6f ? rd.z : -1e-6f;

                float zlo = -global_hmax * 0.5f;
                float zhi = global_hmax * 0.5f;

                float t0 = (zlo - cro.z) / rz;
                float t1 = (zhi - cro.z) / rz;

                float2 rp0 = cro.xy + rd.xy * t0;
                float2 rp1 = cro.xy + rd.xy * t1;

                float2 rg0 = fmin(rp0, rp1);
                float2 rg1 = fmax(rp0, rp1);

                int mincx = (int)floor(rg0.x * (float)nx) - pad;
                int maxcx = (int)floor(rg1.x * (float)nx) + pad;
                int mincy = (int)floor(rg0.y * (float)ny) - pad;
                int maxcy = (int)floor(rg1.y * (float)ny) + pad;

                /*
                    Each phase is one canonical
                    0..1 formation.
                */
                mincx = max(mincx, 0);
                maxcx = min(maxcx, nx - 1);
                mincy = max(mincy, 0);
                maxcy = min(maxcy, ny - 1);

                if (mincx > maxcx || mincy > maxcy)
                    continue;

                for (int cyi = mincy; cyi <= maxcy; ++cyi)
                {
                    for (int cxi = mincx; cxi <= maxcx; ++cxi)
                    {
                        int2 cell = (int2)(cxi, cyi);
                        int2 cluster = (int2)(cxi / cluster_cells, cyi / cluster_cells);

                        float cluster_rand = rand_signed(cluster, phase_seed, 701.37f);

                        float local_density = clamp(
                            phase_density + cluster_rand * formation * 0.35f,
                            0.0f, 1.0f);

                        float occupied = rand_cell(cell, phase_seed, 11.31f);
                        if (occupied > local_density)
                            continue;

                        float2 jitter = (float2)(
                            rand_signed(cell, phase_seed, 17.13f) * posjx,
                            rand_signed(cell, phase_seed, 31.71f) * posjy);

                        float2 centre = (convert_float2(cell) + 0.5f + jitter) * cell_size;

                        float cluster_size = 1.0f + cluster_rand * formation * 0.30f;

                        float sx = mix(sxmin, sxmax, rand_cell(cell, phase_seed, 47.93f)) * cluster_size * phase_size;
                        float syv = mix(symin, symax, rand_cell(cell, phase_seed, 63.47f)) * cluster_size * phase_size;

                        float hx = 0.5f * cell_size.x * sx;
                        float hy = 0.5f * cell_size.y * syv;

                        /*
                            Each side varies independently,
                            making slab/block outlines
                            irregular while retaining
                            planar geometry.
                        */
                        float ml = 1.0f + rand_signed(cell, phase_seed, 83.17f) * sidejag;
                        float mr = 1.0f + rand_signed(cell, phase_seed, 97.13f) * sidejag;
                        float mb = 1.0f + rand_signed(cell, phase_seed, 109.91f) * sidejag;
                        float mt = 1.0f + rand_signed(cell, phase_seed, 127.37f) * sidejag;

                        float xmin = -hx * ml;
                        float xmax = hx * mr;
                        float ymin = -hy * mb;
                        float ymax = hy * mt;

                        /*
                            Formation-level height
                            clustering.
                        */
                        float hr = rand_cell(cell, phase_seed, 149.71f);
                        hr = clamp(hr + cluster_rand * formation * 0.30f, 0.0f, 1.0f);
                        hr = quantized01(hr, height_steps);

                        float height = mix(hmin, hmax, hr) * phase_height;

                        /*
                            Slightly lower secondary
                            foundation pieces should never
                            completely disappear.
                        */
                        height = fmax(height, hmin * 0.35f);

                        float angle = @rotation + rand_signed(cell, phase_seed, 173.27f) * rotjit;
                        angle *= 0.0174532925199433f;

                        if (angle_steps > 0)
                        {
                            float astep = 6.28318530718f / (float)angle_steps;
                            angle = floor(angle / astep + 0.5f) * astep;
                        }

                        float2 lean = (float2)(@lean_x, @lean_y);
                        lean += (float2)(
                            rand_signed(cell, phase_seed, 197.51f),
                            rand_signed(cell, phase_seed, 223.73f)) * leanjit;

                        float3 rel = cro - (float3)(centre.x, centre.y, 0.0f);

                        /*
                            Undo block lean.
                        */
                        float2 lroxy = rel.xy - lean * rel.z;
                        float2 lrdxy = rd.xy - lean * rd.z;

                        /*
                            Undo local block rotation.
                        */
                        lroxy = rotate2(lroxy, -angle);
                        lrdxy = rotate2(lrdxy, -angle);

                        float3 lro = (float3)(lroxy.x, lroxy.y, rel.z);
                        float3 lrd = (float3)(lrdxy.x, lrdxy.y, rd.z);

                        float t = ray_box(
                            lro, lrd,
                            (float3)(xmin, ymin, -height * 0.5f),
                            (float3)(xmax, ymax, height * 0.5f));

                        if (t >= 0.0f && t < best_t)
                        {
                            best_t = t;
                            hit = 1;
                        }
                    }
                }
            }
        }
    }

    float outv = 0.0f;
    if (hit)
    {
        float d = (best_t - dnear) / fmax(dfar - dnear, 1e-6f);
        d = clamp(d, 0.0f, 1.0f);

        outv = 1.0f - d;
    }

    @dst.set(outv);
}
