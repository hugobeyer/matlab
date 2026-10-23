#runover layer
#bind layer &height float

#bind parm seed int val=17
#bind parm layer_count int val=7
#bind parm diagonal_turns int val=-3
#bind parm joint_count int val=3
#bind parm joint_turns int val=1
#bind parm depth_layers int val=3
#bind parm surface_height float val=0.62
#bind parm depth_spacing float val=0.10
#bind parm slab_thickness float val=0.24
#bind parm height_variation float val=0.08
#bind parm face_tilt float val=0.35
#bind parm tilt_variation float val=0.25
#bind parm along_tilt float val=0.12
#bind parm block_jitter float val=0.85
#bind parm layer_jitter float val=0.55
#bind parm missing_blocks float val=0.04
#bind parm fold_strength float val=0.06
#bind parm fold_secondary float val=0.25
#bind parm bed_height float val=0.06
#bind parm bed_roughness float val=0.018
#bind parm gap_width float val=0.002
#bind parm edge_bevel float val=0.003
#bind parm edge_jaggedness float val=0.0035
#bind parm edge_jag_cells int val=5
#bind parm facet_strength float val=0.08
#bind parm joint_width float val=0.010
#bind parm joint_depth float val=0.18
#bind parm joint_wander float val=0.12
#bind parm joint_persistence float val=0.80
#bind parm chip_density float val=0.55
#bind parm chip_width float val=0.022
#bind parm chip_depth float val=0.045
#bind parm chip_cells int val=4
#bind parm spall_coverage float val=0.55
#bind parm spall_depth float val=0.025
#bind parm spall_layers int val=3
#bind parm spall_cells int val=2
#bind parm spall_bevel float val=0.02
#bind parm grain_height float val=0.003
#bind parm grain_cells int val=8
#bind parm grain_octaves int val=3
#bind parm grain_gain float val=0.45
#bind parm relief float val=1.0

// Standalone COP prototype. Geometry is in tile-width units; p is in bedding cells.
// Integer lattice rows keep arbitrary layer tilt periodic without rotating a
// square-periodic noise texture. See layeredrock3d.md for ranges and presets.
#define LR_TAU 6.283185307179586f
#define LR_EPS 1.0e-7f
#define LR_INFINITY 1.0e20f
#define LR_MIN_CELL_SEARCH 2
#define LR_MAX_DEPTH_LAYERS 8
#define LR_MAX_NOISE_OCTAVES 6
#define LR_MAX_SPALL_LAYERS 8
#define LR_FACET_COUNT 3

uint lr_hash(uint h)
{
    h ^= h >> 16; h *= 2246822519u;
    h ^= h >> 13; h *= 3266489917u;
    return h ^ (h >> 16);
}

float lr_random(uint seed, uint salt)
{
    return (float)(lr_hash(seed ^ lr_hash(salt + 1u)) >> 8)
         * (1.0f / 16777216.0f);
}

float lr_signed(uint seed, uint salt)
{
    return 2.0f * lr_random(seed, salt) - 1.0f;
}

long lr_wrap(long value, long period)
{
    return ((value % period) + period) % period;
}

// B = [a; b]. adj(B)*cell modulo |det(B)| identifies the same cell
// under either original UV tile translation. Wrapping cell.x/cell.y
// independently would introduce seams for a tilted lattice.
uint lr_cell_key(int2 cell, int2 a, int2 b, uint seed)
{
    // Octave-scaled lattice products can exceed 32 bits, even when the
    // base rows are bounded. Widen BEFORE multiplication, including adj(B).
    long det = (long)a.x * b.y - (long)a.y * b.x;
    long period = det < 0 ? -det : det;
    long x = lr_wrap((long)b.y * cell.x - (long)a.y * cell.y, period);
    long y = lr_wrap((long)a.x * cell.y - (long)b.x * cell.x, period);
    uint hx = (uint)x ^ (uint)((ulong)x >> 32);
    uint hy = (uint)y ^ (uint)((ulong)y >> 32);
    return lr_hash(hx * 1597334677u ^ hy * 3812015801u ^ seed);
}

float2 lr_cell_center(int2 cell, int2 a, int2 b, float2 jitter, uint seed,
                      float2 offset, float2 fold_settings, uint fold_seed)
{
    uint key = lr_cell_key(cell, a, b, seed);
    float2 point = convert_float2(cell) + 0.5f
                 + 0.5f * jitter * (float2)(lr_signed(key, 0u), lr_signed(key, 1u));
    float2 p = point - offset;
    float det = (float)(a.x * b.y - a.y * b.x);
    float2 tile_pos = (float2)((float)b.y * p.x - (float)a.y * p.y,
                              (float)a.x * p.y - (float)b.x * p.x) / det;
    // Closest integer low-frequency modes preserve tiling. Folding moves
    // seed positions only: all resulting fracture planes remain straight.
    float2 mode_a = convert_float2(convert_int2_rte(normalize(convert_float2(a))));
    float2 mode_b = convert_float2(convert_int2_rte(normalize(convert_float2(b))));
    float phase_a = LR_TAU * (dot(tile_pos, mode_a) + lr_random(fold_seed, 70u));
    float phase_b = LR_TAU * (dot(tile_pos, mode_a + mode_b) + lr_random(fold_seed, 71u));
    point.y += fold_settings.x * (sin(phase_a) + fold_settings.y * sin(phase_b))
             / (1.0f + fold_settings.y);
    return point;
}

// Smooth noise for rock faces; triangular interpolation for angular fracture cuts.
float lr_noise(float2 p, int2 a, int2 b, int2 rate, uint seed, int angular)
{
    float2 q = p * convert_float2(rate);
    int2 cell = convert_int2(floor(q));
    float2 f = q - floor(q);
    a *= rate.x;
    b *= rate.y;
    float v00 = lr_signed(lr_cell_key(cell, a, b, seed), 0u);
    float v10 = lr_signed(lr_cell_key(cell + (int2)(1, 0), a, b, seed), 0u);
    float v01 = lr_signed(lr_cell_key(cell + (int2)(0, 1), a, b, seed), 0u);
    float v11 = lr_signed(lr_cell_key(cell + (int2)(1, 1), a, b, seed), 0u);
    if (angular)
        return f.x > f.y
             ? v00 + f.x * (v10 - v00) + f.y * (v11 - v10)
             : v00 + f.y * (v01 - v00) + f.x * (v11 - v01);
    float2 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);
    return mix(mix(v00, v10, u.x), mix(v01, v11, u.x), u.y);
}

float2 lr_to_uv(float2 p, int2 a, int2 b)
{
    float det = (float)(a.x * b.y - a.y * b.x);
    return (float2)((float)b.y * p.x - (float)a.y * p.y,
                    (float)a.x * p.y - (float)b.x * p.x) / det;
}

float2 lr_local(float2 delta, float2 along, float2 across)
{
    return (float2)(dot(delta, along), dot(delta, across));
}

// Clip a vertical ray against az*z <= rhs. The largest surviving z is
// the first hit from +Z. This is an exact half-space intersection, not
// sphere tracing with a warped field incorrectly assumed to be an SDF.
int lr_clip_z(float az, float rhs, float2 *interval)
{
    if (fabs(az) < LR_EPS)
        return rhs >= 0.0f;
    float z = rhs / az;
    if (az > 0.0f) interval->y = fmin(interval->y, z);
    else interval->x = fmax(interval->x, z);
    return interval->x <= interval->y;
}

float lr_detail_visibility(float2 footprint, int2 frequency)
{
    float pixel_frequency = fmax(footprint.x * (float)frequency.x,
                                 footprint.y * (float)frequency.y);
    return 1.0f - smoothstep(0.25f, 0.5f, pixel_frequency);
}

float lr_grain(float2 p, int2 a, int2 b, float2 footprint,
               int cells, int octaves, float gain, uint seed)
{
    float value = 0.0f;
    float weight = 1.0f;
    float norm = 0.0f;
    int rate = cells;
    for (int octave = 0; octave < octaves; ++octave)
    {
        // More samples across bedding than along it: cleavage, not white noise.
        int2 frequency = (int2)(rate, 2 * rate);
        float visible = lr_detail_visibility(footprint, frequency);
        if (visible > 0.0f)
            value += weight * visible * lr_noise(p, a, b, frequency,
                         lr_hash(seed + (uint)octave), octave & 1);
        // Keep unresolved octaves in the normalization; filtering must not
        // boost the surviving octaves when the output resolution changes.
        norm += weight;
        weight *= gain;
        rate *= 2;
    }
    return value / fmax(norm, LR_EPS);
}

@KERNEL
{
    float2 uv = (convert_float2(@ixy) + 0.5f) / convert_float2(@height.res);
    uint root = (uint)@seed;

    // Base lattice bounds; octave-scaled hash arithmetic uses 64-bit integers.
    int2 a = (int2)(clamp(@joint_count, 0, 32), clamp(@joint_turns, -32, 32));
    int2 b = (int2)(clamp(@diagonal_turns, -32, 32), clamp(@layer_count, 0, 32));
    int det = a.x * b.y - a.y * b.x;
    if (det == 0)
    {
        // Parallel/zero rows do not describe a 2D rock lattice. Do not silently
        // substitute a different formation; the documented invalid result is 0.
        @height.set(0.0f);
        return;
    }

    float2 across = normalize(convert_float2(b));
    float2 along = (float2)(across.y, -across.x);
    float2 material_p = (float2)(dot(uv, convert_float2(a)), dot(uv, convert_float2(b)));
    float2 inv_res = 1.0f / convert_float2(@height.res);
    float2 footprint = (float2)(length(convert_float2(a) * inv_res),
                                length(convert_float2(b) * inv_res));

    // Sample all surfaces in the undeformed bedding frame. Continuously
    // warping this ray position also bends every face into a rubbery sheet.
    float2 p = material_p;
    float2 fold_settings = (float2)(@fold_strength, clamp(@fold_secondary, 0.0f, 1.0f));

    float grain = lr_grain(p, a, b, footprint, clamp(@grain_cells, 1, 64),
                          clamp(@grain_octaves, 1, LR_MAX_NOISE_OCTAVES),
                          clamp(@grain_gain, 0.0f, 1.0f), root ^ 0x3c6ef372u);
    float bed = @bed_height + fmax(@bed_roughness, 0.0f)
              * lr_noise(p, a, b, (int2)(2, 3), root ^ 0x85ebca6bu, 1);
    float height = 0.0f;
    int has_hit = 0;
    float2 jitter = clamp((float2)(@block_jitter, @layer_jitter), 0.0f, 0.90f);
    // There is one seed per cell, displaced by at most max_shift per axis.
    // This bounds both nearest-owner search and possible Voronoi neighbors.
    float2 max_shift = 0.5f * jitter + (float2)(0.0f, fabs(fold_settings.x));
    float cover_radius = length(0.5f + max_shift);
    float max_axis_shift = fmax(max_shift.x, max_shift.y);
    int owner_search = max(LR_MIN_CELL_SEARCH, (int)ceil(cover_radius + 0.5f + max_axis_shift));
    int plane_search = max(LR_MIN_CELL_SEARCH, (int)ceil(2.0f * (cover_radius + max_axis_shift)));
    float gap = fmax(@gap_width, 0.0f);
    float bevel = fmax(@edge_bevel, 0.0f);
    float thickness = fmax(@slab_thickness, 0.0f);
    int tiers = clamp(@depth_layers, 1, LR_MAX_DEPTH_LAYERS);

    // Cross-cutting joints pass through several beds. Their depth is independent
    // of slab presence: real termination exposes the next solid, not a faded edge.
    float joint_distance = LR_INFINITY;
    float joint_strength = 0.0f;
    if (@joint_width > 0.0f && @joint_depth > 0.0f && @joint_persistence > 0.0f)
    {
        float joint_phase = p.x + @joint_wander
            * lr_noise(p, a, b, (int2)(1, 2), root ^ 0x68bc21ebu, 1);
        float f = joint_phase - floor(joint_phase);
        joint_distance = fabs(f - 0.5f) / length(convert_float2(a));
        float activity = 0.5f + 0.5f
            * lr_noise(p, a, b, (int2)(1, 1), root ^ 0x2545f491u, 0);
        float persistence = clamp(@joint_persistence, 0.0f, 1.0f);
        joint_strength = smoothstep(1.0f - persistence, 1.0f,
                                    activity + persistence);
    }

    for (int tier = 0; tier < tiers && thickness > 0.0f; ++tier)
    {
        uint seed = lr_hash(root ^ ((uint)tier + 1u) * 747796405u);
        // Offset buried layers so a missing face reveals different rock,
        // rather than a shallower copy of the same outline.
        float2 offset = (float2)(lr_random(seed, 80u), lr_random(seed, 81u));
        float2 q = p + offset;
        int2 base = convert_int2(floor(q));
        int2 owner = base;
        float2 center = (float2)(0.0f);
        float nearest = LR_INFINITY;
        for (int y = -owner_search; y <= owner_search; ++y)
        for (int x = -owner_search; x <= owner_search; ++x)
        {
            int2 cell = base + (int2)(x, y);
            float2 point = lr_cell_center(cell, a, b, jitter, seed, offset, fold_settings, root);
            float distance2 = dot(q - point, q - point);
            if (distance2 < nearest)
            {
                nearest = distance2;
                owner = cell;
                center = point;
            }
        }

        uint key = lr_cell_key(owner, a, b, seed);
        if (lr_random(key, 2u) < clamp(@missing_blocks, 0.0f, 1.0f)) continue;

        float2 local_pos = lr_local(lr_to_uv(q - center, a, b), along, across);
        float hardness = lr_random(key, 3u);
        float top_center = @surface_height - (float)tier * fmax(@depth_spacing, 0.0f)
                         + @height_variation * lr_signed(key, 4u);
        float2 tilt = (float2)(@along_tilt * lr_signed(key, 5u),
                              -@face_tilt + @tilt_variation * lr_signed(key, 6u));
        float top = top_center + dot(tilt, local_pos);
        float bottom = top - thickness;
        float top_norm = sqrt(1.0f + dot(tilt, tilt));

        // A few intersecting cleavage planes create large quiet facets.
        // Small-scale relief is not used to define the rock's overall mass.
        float face = top;
        for (int facet = 0; facet < LR_FACET_COUNT; ++facet)
        {
            uint salt = 100u + (uint)facet * 3u;
            float angle = LR_TAU * lr_random(key, salt);
            float2 direction = (float2)(cos(angle), sin(angle));
            float plane = top + fmax(@facet_strength, 0.0f)
                        * (0.5f * lr_signed(key, salt + 1u) + dot(direction, q - center));
            face = fmin(face, plane);
        }

        // Nested subtractive laminae, not bright additive flakes hovering over gaps.
        // Angular contours keep spalls planar; narrow transitions form their lips.
        if (@spall_coverage > 0.0f && @spall_depth > 0.0f)
        {
            int rate = clamp(@spall_cells, 1, 64);
            float field = 0.5f + 0.5f * lr_noise(q, a, b, (int2)(rate, 2 * rate),
                                                key ^ 0xa511e9b3u, 1);
            float coverage = clamp(@spall_coverage, 0.0f, 1.0f);
            int count = clamp(@spall_layers, 1, LR_MAX_SPALL_LAYERS);
            float field_footprint = fmax(footprint.x * (float)rate, footprint.y * (float)(2 * rate));
            float lip = fmax(@spall_bevel, fmax(field_footprint, LR_EPS));
            float visible = lr_detail_visibility(footprint, (int2)(rate, 2 * rate));
            for (int layer = 0; layer < count; ++layer)
            {
                float threshold = 1.0f - coverage + coverage * (float)layer / (float)count;
                face -= visible * @spall_depth / (float)count
                      * smoothstep(threshold, threshold + lip, field);
            }
        }
        face += fmax(@grain_height, 0.0f) * grain;

        float chip_event = 0.0f;
        if (@chip_density > 0.0f && @chip_width > 0.0f && @chip_depth > 0.0f)
        {
            int rate = clamp(@chip_cells, 1, 64);
            float event = 0.5f + 0.5f * lr_noise(q, a, b, (int2)(rate, rate),
                                                key ^ 0x2c1b3c6du, 1);
            float density = clamp(@chip_density, 0.0f, 1.0f);
            chip_event = lr_detail_visibility(footprint, (int2)(rate, rate))
                       * clamp((event - (1.0f - density)) / fmax(density, LR_EPS), 0.0f, 1.0f);
        }
        int jag_rate = clamp(@edge_jag_cells, 1, 64);
        int2 jag_frequency = (int2)(jag_rate, 2 * jag_rate);
        float jag = fmax(@edge_jaggedness, 0.0f) * lr_detail_visibility(footprint, jag_frequency)
                  * (0.5f + 0.5f * lr_noise(q, a, b, jag_frequency, key ^ 0x9e3779b9u, 1));

        float2 interval = (float2)(-LR_INFINITY, LR_INFINITY);
        int solid = lr_clip_z(-1.0f, -bottom, &interval);
        solid = solid && lr_clip_z(1.0f, face, &interval);
        float edge_distance = LR_INFINITY;

        // The nearest cell owns the entire uncut prism footprint. Every operation
        // below only removes material, so no other cell in this tier can hide it.
        for (int y = -plane_search; y <= plane_search && solid; ++y)
        for (int x = -plane_search; x <= plane_search && solid; ++x)
        {
            if (x == 0 && y == 0) continue;
            float2 neighbor = lr_cell_center(owner + (int2)(x, y), a, b, jitter, seed,
                                             offset, fold_settings, root);
            float2 delta = neighbor - center;
            float2 gradient = convert_float2(a) * delta.x + convert_float2(b) * delta.y;
            float metric = length(gradient);
            if (metric <= LR_EPS) continue;
            float2 normal = lr_local(gradient / metric, along, across);
            float limit = 0.5f * dot(delta, delta) / metric;
            float inset = limit - dot(normal, local_pos) - gap - jag;
            edge_distance = fmin(edge_distance, inset);
            solid = lr_clip_z(0.0f, inset, &interval);
            // Intersect the top/side bevel plane; do not multiply face detail by
            // an edge mask. A missing slab reveals the next actual depth interval.
            if (solid)
                solid = lr_clip_z(1.0f / top_norm,
                                  face / top_norm + inset - bevel, &interval);
        }
        if (!solid) continue;

        // Local wedge removals near exposed edges, stronger in softer rock.
        // This caps depth instead of globally attenuating chips, grain and spalls.
        if (chip_event > 0.0f)
        {
            float cut = @chip_depth * chip_event * (1.0f - 0.5f * hardness);
            float chip_cap = face - cut + fmax(edge_distance, 0.0f)
                           * @chip_depth / fmax(@chip_width, LR_EPS);
            solid = lr_clip_z(1.0f, chip_cap, &interval);
        }
        if (solid && joint_strength > 0.0f)
        {
            float joint_cap = top - @joint_depth * joint_strength
                            + joint_distance * @joint_depth / @joint_width;
            solid = lr_clip_z(1.0f, joint_cap, &interval);
        }
        if (solid)
        {
            // Choose the frontmost actual hit, even when every hit is below bed.
            height = has_hit ? fmax(height, interval.y) : interval.y;
            has_hit = 1;
        }
    }
    // Bed fills uncovered pixels only; it must not clamp a valid rock depth.
    @height.set(@relief * (has_hit ? height : bed));
}
