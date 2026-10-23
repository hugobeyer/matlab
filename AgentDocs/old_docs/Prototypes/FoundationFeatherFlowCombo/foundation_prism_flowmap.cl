#bind layer size_ref? float port=size_ref nouipromote
#bind layer !&dst float
#bind layer !&flow float2
// Y copy index of the hit rock (0..y_sub-1); -1 = no hit.
#bind layer !&row_id int
// Unique id per block: phase * (count_x * count_y) + row * count_x + col; -1 = no hit.
#bind layer !&block_id int

#bind parm count_x int val=8
#bind parm count_y int val=6
#bind parm density float val=0.88

// Size is one range plus an aspect (y/x); the x range times the aspect gives y.
#bind parm size_min float val=0.75
#bind parm size_max float val=1.35
#bind parm size_aspect float val=0.78

// One jitter drives position, side, rotation and lean offsets (see kernel).
#bind parm jitter float val=0.18
#bind parm flow_variation float val=0.35

#bind parm height_min float val=0.18
#bind parm height_max float val=0.90

// Shared quantisation for both height and angle; 0 = off.
#bind parm steps int val=0

// Rotation normalised: -1..1 maps to -180..180 degrees.
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
// Per-copy offset jitter (tile fraction), X and Y separate; stays seamless because it is constant per phase.
#bind parm quarter_jitter_x float val=0.10
#bind parm quarter_jitter_y float val=0.10

// Cross-section of each rock: 4 = box, 6 = hexagon, 8 = octagon, ...
#bind parm sides int val=4
// When on, each cell picks its own side count from {4, 6, 8}.
#bind parm shape_random int val=0

// Yaw normalised: -1..1 maps to -90..90 degrees. Pitch: -1..1 maps to -45..45.
#bind parm camera_yaw float val=0.5
#bind parm camera_pitch float val=0.784
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

/*
    Convex N-gon prism, extruded in Z from zmin to zmax.

    The cross-section is a regular polygon of apothem 1 in a
    normalised XY space (the ray is divided by the half-extents,
    which preserves the ray parameter t). Each side plane gets
    its own jitter, so outlines stay irregular like the box did.

    step_rot is (cos, sin) of one side's angle, precomputed once
    per side count; the normals are walked with a rotation
    recurrence instead of calling cos/sin per side.

    sides = 4 reproduces the axis-aligned box exactly.
*/
static float ray_prism(
    float3 ro, float3 rd,
    float2 extent, int sides, float2 step_rot,
    float zmin, float zmax,
    float jag, float seed)
{
    // Normalise XY so the polygon is regular regardless of stretch.
    float2 nro = (float2)(ro.x / extent.x, ro.y / extent.y);
    float2 nrd = (float2)(rd.x / extent.x, rd.y / extent.y);

    /*
        First pass: the jittered polygon's centroid, so the block is
        centred on it rather than on the cell origin. To first order
        (exact for the box) the centroid is
        (2 / sides) * sum((j_k - 1) * n_k).
    */
    float2 n = (float2)(1.0f, 0.0f);
    float2 csum = (float2)(0.0f, 0.0f);

    for (int k = 0; k < sides; ++k)
    {
        float j = 1.0f + (hash11(seed + (float)k * 12.9898f) * 2.0f - 1.0f) * jag;
        csum += (j - 1.0f) * n;
        n = (float2)(n.x * step_rot.x - n.y * step_rot.y,
                     n.x * step_rot.y + n.y * step_rot.x);
    }

    nro += csum * (2.0f / (float)sides);

    float t_enter = -1e20f;
    float t_exit = 1e20f;

    // Z caps.
    {
        float denom = rd.z;
        float num = zmax - ro.z;
        if (fabs(denom) < 1e-8f)
        {
            if (num < 0.0f)
                return -1.0f;
        }
        else
        {
            float t = num / denom;
            if (denom > 0.0f)
                t_exit = fmin(t_exit, t);
            else
                t_enter = fmax(t_enter, t);
        }
    }
    {
        float denom = rd.z;
        float num = zmin - ro.z;
        if (fabs(denom) < 1e-8f)
        {
            if (num > 0.0f)
                return -1.0f;
        }
        else
        {
            float t = num / denom;
            if (denom > 0.0f)
                t_exit = fmin(t_exit, t);
            else
                t_enter = fmax(t_enter, t);
        }
    }

    n = (float2)(1.0f, 0.0f);

    for (int k = 0; k < sides; ++k)
    {
        // Per-side irregularity.
        float j = 1.0f + (hash11(seed + (float)k * 12.9898f) * 2.0f - 1.0f) * jag;

        float denom = n.x * nrd.x + n.y * nrd.y;
        float num = j - (n.x * nro.x + n.y * nro.y);

        if (fabs(denom) < 1e-8f)
        {
            if (num < 0.0f)
                return -1.0f;
        }
        else
        {
            float t = num / denom;
            if (denom > 0.0f)
                t_exit = fmin(t_exit, t);
            else
                t_enter = fmax(t_enter, t);
        }

        // Rotate the normal by one side angle.
        n = (float2)(n.x * step_rot.x - n.y * step_rot.y,
                     n.x * step_rot.y + n.y * step_rot.x);
    }

    if (t_enter > t_exit || t_exit < 0.0f)
        return -1.0f;

    return t_enter >= 0.0f ? t_enter : t_exit;
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

    float yaw = @camera_yaw * 1.57079632679f;
    float pitch = @camera_pitch * 0.78539816339f;

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
    float quarter_jitter_x = clamp(@quarter_jitter_x, 0.0f, 0.48f);
    float quarter_jitter_y = clamp(@quarter_jitter_y, 0.0f, 0.48f);

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

    int base_sides = clamp(@sides, 3, 12);

    // Polygon step rotations, precomputed once per side count.
    float2 rot_base = (float2)(cos(6.28318530718f / (float)base_sides),
                               sin(6.28318530718f / (float)base_sides));
    float2 rot4 = (float2)(0.0f, 1.0f);
    float2 rot6 = (float2)(0.5f, 0.86602540378f);
    float2 rot8 = (float2)(0.70710678118f, 0.70710678118f);

    int cluster_cells = max(@formation_cells, 1);
    int pad = 1;
    int cr = 1;
    // --------------------------------------------------------------------

    float2 cell_size = (float2)(1.0f / (float)nx, 1.0f / (float)ny);

    float best_t = 1e20f;
    int hit = 0;
    int best_row = -1;
    int best_block = -1;
    float2 best_flow = (float2)(0.0f, 0.0f);

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

        /*
            Jitter each staggered copy's offset. The offset is
            constant per phase, so copies one tile apart stay
            identical and the pattern remains seamless.
        */
        if (secondary)
        {
            phase_x += (hash11((float)phase * 5.31f + @seed * 0.611f) * 2.0f - 1.0f) * quarter_jitter_x;
            phase_y += (hash11((float)phase * 9.73f + @seed * 0.917f) * 2.0f - 1.0f) * quarter_jitter_y;
        }

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

                        float angle = (@rotation * 180.0f + rand_signed(cell, phase_seed, 173.27f) * rotjit) * 0.0174532925199433f;

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

                        // Per-cell shape, optionally randomised.
                        int cell_sides = base_sides;
                        float2 step_rot = rot_base;
                        if (@shape_random)
                        {
                            float sr = rand_cell(cell, phase_seed, 251.17f);
                            cell_sides = 4 + 2 * (int)(sr * 3.0f);
                            step_rot = (cell_sides == 4) ? rot4 : ((cell_sides == 6) ? rot6 : rot8);
                        }

                        float side_seed = rand_cell(cell, phase_seed, 269.83f) * 1000.0f;

                        float t = ray_prism(
                            lro, lrd,
                            (float2)(hx, hy), cell_sides, step_rot,
                            -height * 0.5f, height * 0.5f,
                            sidejag, side_seed);

                        if (t >= 0.0f && t < best_t)
                        {
                            best_t = t;
                            hit = 1;
                            best_row = yi;
                            best_block = phase * (nx * ny) + cyi * nx + cxi;

                            float flow_angle = angle + rand_signed(cell, phase_seed, 307.11f)
                                * clamp(@flow_variation, 0.0f, 3.14159265f);
                            float2 flow_axis = rotate2((float2)(1.0f, 0.0f), flow_angle);
                            float3 flow_world = (float3)(flow_axis.x, flow_axis.y, 0.0f)
                                + (float3)(lean.x, lean.y, 0.0f) * 0.35f;
                            float2 flow_projected = (float2)(
                                dot(flow_world, cam_right),
                                dot(flow_world, cam_up));
                            best_flow = flow_projected / fmax(length(flow_projected), 1e-6f);
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

        outv = 1.0f - 1.5f * d;
    }

    @dst.set(outv);
    @flow.set(hit ? best_flow : (float2)(0.0f, 0.0f));
    @row_id.set(best_row);
    @block_id.set(best_block);
}
