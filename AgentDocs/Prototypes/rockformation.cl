// Houdini COP OpenCL: standalone foundation-first rock formation.
// Continuous solid, shared ordered bedding, differential erosion and faults.
// Geometry is evaluated in XYZ; vertical-ray intersections produce height.
// See rockformation.md. Source-reviewed; not compiled or visually validated.
#runover layer
#bind layer &height float

#bind parm seed int val=17
#bind parm elevation float val=0.0
#bind parm relief float val=1.0
#bind parm formation_cells int val=2
#bind parm formation_height float val=0.20
#bind parm formation_facets float val=0.045
#bind parm layer_count int val=8
#bind parm diagonal_turns int val=-3
#bind parm joint_count int val=3
#bind parm joint_turns int val=1
#bind parm depth_beds float val=14.0
#bind parm fold_strength float val=0.12
#bind parm thickness_variation float val=0.18
#bind parm differential_erosion float val=0.075
#bind parm bedding_recess float val=0.060
#bind parm bedding_width float val=0.12
#bind parm edge_jaggedness float val=0.018
#bind parm edge_cells int val=6
#bind parm joint_depth float val=0.14
#bind parm joint_width float val=0.035
#bind parm joint_jaggedness float val=0.055
#bind parm fault_throw float val=0.035
#bind parm cross_x int val=2
#bind parm cross_y int val=-3
#bind parm cross_depth float val=0.055
#bind parm cross_width float val=0.025
#bind parm cross_coverage float val=0.65
#bind parm enable_chips int val=1
#bind parm chip_cells int val=5
#bind parm chip_density float val=0.40
#bind parm chip_depth float val=0.018
#bind parm chip_width float val=0.10
#bind parm enable_spalls int val=1
#bind parm spall_cells int val=2
#bind parm spall_coverage float val=0.30
#bind parm spall_depth float val=0.010


#define RF_EPS 1.0e-7f


// Hash family shared with layeredformation.cl; widened lattice addressing
// follows layeredrock3d.cl. Kept self-contained for the COP code parameter.
uint rf_hash(uint h)
{
    h ^= h >> 16; h *= 2246822519u;
    h ^= h >> 13; h *= 3266489917u;
    return h ^ (h >> 16);
}

float rf_random(uint h)
{
    return (float)(rf_hash(h) >> 8) * (1.0f / 16777216.0f);
}

long rf_wrap(long value, long period)
{
    return ((value % period) + period) % period;
}

// The columns of B=[a;b] are the UV tile translations in bedding space.
// adj(B)*cell mod |det(B)| is invariant under both translations.
uint rf_key(int2 cell, int2 a, int2 b, uint seed)
{
    long det = (long)a.x * b.y - (long)a.y * b.x;
    long period = det < 0 ? -det : det;
    long x = rf_wrap((long)b.y * cell.x - (long)a.y * cell.y, period);
    long y = rf_wrap((long)a.x * cell.y - (long)b.x * cell.x, period);
    uint hx = (uint)x ^ (uint)((ulong)x >> 32);
    uint hy = (uint)y ^ (uint)((ulong)y >> 32);
    return rf_hash(hx * 1597334677u ^ hy * 3812015801u ^ seed);
}

// A boundary has ONE identity shared by the beds on either side. This is the
// ordered-boundary principle in Geology/StratigraphicCoordinates.clh, with a
// tileable, piecewise-linear along-bed variation rather than sinusoidal folds.
float rf_bed_signal(float s, int bed, int2 a, int2 b, int rate, uint seed)
{
    float q = s * (float)rate;
    int cell = (int)floor(q);
    float t = q - floor(q);
    int2 scaled_a = a * rate;
    float x = rf_random(rf_key((int2)(cell, bed), scaled_a, b, seed));
    float y = rf_random(rf_key((int2)(cell + 1, bed), scaled_a, b, seed));
    return 2.0f * mix(x, y, t) - 1.0f;
}

float rf_noise(float2 p, int2 a, int2 b, int2 rate, uint seed, int angular)
{
    float2 q = p * convert_float2(rate);
    int2 cell = convert_int2(floor(q));
    float2 t = q - floor(q);
    a *= rate.x;
    b *= rate.y;
    float v00 = rf_random(rf_key(cell, a, b, seed));
    float v10 = rf_random(rf_key(cell + (int2)(1, 0), a, b, seed));
    float v01 = rf_random(rf_key(cell + (int2)(0, 1), a, b, seed));
    float v11 = rf_random(rf_key(cell + (int2)(1, 1), a, b, seed));
    float value;
    if (angular)
    {
        // Alternate triangle diagonals without breaking the periodic lattice.
        if (rf_random(rf_key(cell, a, b, seed ^ 0xa511e9b3u)) < 0.5f)
            value = t.x > t.y
                  ? v00 + t.x * (v10 - v00) + t.y * (v11 - v10)
                  : v00 + t.y * (v01 - v00) + t.x * (v11 - v01);
        else
            value = t.x + t.y < 1.0f
                  ? v00 + t.x * (v10 - v00) + t.y * (v01 - v00)
                  : v11 + (1.0f - t.x) * (v01 - v11) + (1.0f - t.y) * (v10 - v11);
    }
    else
    {
        float2 u = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
        value = mix(mix(v00, v10, u.x), mix(v01, v11, u.x), u.y);
    }
    return 2.0f * value - 1.0f;
}

float rf_visibility(float2 footprint, int2 rate)
{
    float frequency = fmax(footprint.x * (float)rate.x, footprint.y * (float)rate.y);
    return 1.0f - smoothstep(0.25f, 0.5f, frequency);
}


float rf_boundary(float s, int bed, int2 a, int2 b, float variation,
                  float jaggedness, int edge_cells, uint seed)
{
    return (float)bed
         + variation * rf_bed_signal(s, bed, a, b, 1, seed ^ 0x9e3779b9u)
         + jaggedness * rf_bed_signal(s, bed, a, b, edge_cells, seed ^ 0x85ebca6bu);
}

float rf_event(float value, float coverage)
{
    if (coverage <= 0.0f) return 0.0f;
    // This bounds an event's support, never the resulting rock depth.
    return clamp((value - (1.0f - coverage)) / coverage, 0.0f, 1.0f);
}

// az*z <= rhs. A negative az gives a LOWER bound, not an upper cap.
int rf_intersect(float az, float rhs, float2 *span)
{
    if (fabs(az) < RF_EPS) return rhs >= 0.0f;
    float z = rhs / az;
    if (az > 0.0f) span->y = fmin(span->y, z);
    else span->x = fmax(span->x, z);
    return span->x <= span->y;
}

@KERNEL
{
    float2 uv = (convert_float2(@ixy) + 0.5f) / convert_float2(@height.res);
    float2 inv_res = 1.0f / convert_float2(@height.res);
    int2 a = (int2)(@joint_count, @joint_turns);
    int2 b = (int2)(@diagonal_turns, @layer_count);
    long det = (long)a.x * b.y - (long)a.y * b.x;
    uint seed = (uint)@seed;
    float dip = @depth_beds;
    float boundary_extent = @thickness_variation + @edge_jaggedness;

    // Reject invalid geometry instead of silently clamping bed spacing/depth.
    // Domain limits bound integer addressing, not output values.
    int valid = det != 0 && dip > RF_EPS && isfinite(dip)
        && @formation_cells >= 1 && @formation_cells <= 64
        && all(abs(a) <= (uint2)(64u)) && all(abs(b) <= (uint2)(64u))
        && @edge_cells >= 1 && @edge_cells <= 64
        && @chip_cells >= 1 && @chip_cells <= 64
        && @spall_cells >= 1 && @spall_cells <= 64

        && @thickness_variation >= 0.0f && @edge_jaggedness >= 0.0f
        && boundary_extent < 0.45f
        && @bedding_width > RF_EPS && @bedding_recess >= 0.0f
        && @differential_erosion >= 0.0f
        && @joint_width > RF_EPS && @cross_width > RF_EPS
        && @joint_depth >= 0.0f && @cross_depth >= 0.0f
        && @cross_coverage >= 0.0f && @cross_coverage <= 1.0f
        && @chip_density >= 0.0f && @chip_density <= 1.0f
        && @spall_coverage >= 0.0f && @spall_coverage <= 1.0f
        && @chip_depth >= 0.0f && @chip_width > RF_EPS
        && @spall_depth >= 0.0f;
    if (!valid)
    {
        @height.set(as_float(0x7fc00000u));
        return;
    }

    float2 p = (float2)(dot(uv, convert_float2(a)), dot(uv, convert_float2(b)));
    float2 footprint = (float2)(length(convert_float2(a) * inv_res),
                                length(convert_float2(b) * inv_res));

    // Broad planar mass BEFORE bedding and fracture. Integer reciprocal modes
    // approximate its orientation while keeping it independent of bed frequency.
    int2 macro_b = convert_int2_rte(normalize(convert_float2(b)) * (float)@formation_cells);
    int2 macro_a = (int2)(macro_b.y, -macro_b.x);
    float2 mp = (float2)(dot(uv, convert_float2(macro_a)), dot(uv, convert_float2(macro_b)));
    float macro_shape = rf_noise(mp, macro_a, macro_b, (int2)(1, 1), seed ^ 0x2545f491u, 1);
    float macro_facets = rf_noise(mp, macro_a, macro_b, (int2)(2, 2), seed ^ 0x68bc21ebu, 1);
    float foundation = @elevation + @formation_height * macro_shape + @formation_facets * macro_facets;
    float fold = @fold_strength
               * rf_noise(mp, macro_a, macro_b, (int2)(1, 1), seed ^ 0x27d4eb2du, 1);
    float base_w = p.y + fold;


    float joint_phase = p.x + @joint_jaggedness
        * rf_noise(p, a, b, (int2)(1, 2), seed ^ 0x2c1b3c6du, 1);
    float jf = joint_phase - floor(joint_phase);
    float jd = fmin(jf, 1.0f - jf);
    float cross_phase = dot(uv, convert_float2((int2)(@cross_x, @cross_y)))
        + @joint_jaggedness * rf_noise(p, a, b, (int2)(2, 1), seed ^ 0x297a2d39u, 1);
    float cf = cross_phase - floor(cross_phase);
    float cd = fmin(cf, 1.0f - cf);
    float cross_activity = rf_event(0.5f + 0.5f * rf_noise(mp, macro_a, macro_b,
                                      (int2)(2, 2), seed ^ 0x165667b1u, 1), @cross_coverage);
    // A periodic piecewise-affine fault displacement moves the WHOLE mass and
    // its beds. All intersection algebra below uses material z; add slip once.
    float slip = @fault_throw * (jf - 0.5f);
    // Joint cuts are relative to each face, not an output height clamp.
    float joint_cut = @joint_depth * fmax(1.0f - jd / @joint_width, 0.0f);
    float cross_cut = @cross_depth * fmax(cross_activity - cd / @cross_width, 0.0f);

    float jag_visible = 1.0f - smoothstep(0.25f, 0.5f, footprint.x * (float)@edge_cells);
    float jaggedness = @edge_jaggedness * jag_visible;
    float chip_visible = 1.0f - smoothstep(0.25f, 0.5f, footprint.x * (float)@chip_cells);
    float spall_visible = rf_visibility(footprint, (int2)(@spall_cells, @spall_cells));

    // Every remaining operation removes a bounded amount from foundation.
    // The search bound locates intact rock; it is never an output depth floor.
    float chip_bound = @enable_chips ? @chip_depth : 0.0f;
    float spall_bound = @enable_spalls ? @spall_depth : 0.0f;
    float removed_bound = @differential_erosion + spall_bound
                        + fmax(@joint_depth, @cross_depth) + @bedding_recess + chip_bound;
    float back = foundation - removed_bound - (1.0f + 2.0f * boundary_extent) / dip;
    int first_bed = (int)floor(base_w + dip * foundation + boundary_extent);
    int last_bed = (int)ceil(base_w + dip * back - 1.0f - boundary_extent);
    float height = as_float(0x7fc00000u);

    for (int bed = first_bed; bed >= last_bed; --bed)
    {
        float lower = rf_boundary(p.x, bed, a, b, @thickness_variation,
                                  jaggedness, @edge_cells, seed);
        float upper = rf_boundary(p.x, bed + 1, a, b, @thickness_variation,
                                  jaggedness, @edge_cells, seed);
        // These are actual 3D planes W(X,Y,Z)=boundary, not a 2D edge mask.
        float2 span = (float2)((lower - base_w) / dip, (upper - base_w) / dip);
        float hardness = 0.5f + 0.5f * rf_bed_signal(p.x, bed, a, b, 1, seed ^ 0x51ed270bu);
        float face = foundation - @differential_erosion * (1.0f - hardness);


        if (@enable_spalls && @spall_depth > 0.0f && @spall_coverage > 0.0f)
        {
            uint bed_key = rf_key((int2)((int)floor(p.x), bed), a, b, seed);
            float patch = 0.5f + 0.5f * rf_noise(p, a, b, (int2)(@spall_cells, @spall_cells),
                                                bed_key ^ 0xb5297a4du, 1);
            face -= @spall_depth * spall_visible * rf_event(patch, @spall_coverage);
        }
        face -= fmax(joint_cut, cross_cut);
        if (!rf_intersect(1.0f, face, &span)) continue;

        float lower_chip = 0.0f;
        float upper_chip = 0.0f;
        if (@enable_chips && @chip_depth > 0.0f && @chip_density > 0.0f)
        {
            lower_chip = chip_visible * rf_event(0.5f + 0.5f * rf_bed_signal(p.x, bed,
                            a, b, @chip_cells, seed ^ 0x7feb352du), @chip_density);
            upper_chip = chip_visible * rf_event(0.5f + 0.5f * rf_bed_signal(p.x, bed + 1,
                            a, b, @chip_cells, seed ^ 0x7feb352du), @chip_density);
        }
        float lower_cut = @bedding_recess + @chip_depth * lower_chip;
        float upper_cut = @bedding_recess + @chip_depth * upper_chip;
        float lower_width = @bedding_width + @chip_width * lower_chip;
        float upper_width = @bedding_width + @chip_width * upper_chip;
        float lower_slope = lower_cut / lower_width;
        float upper_slope = upper_cut / upper_width;

        // Intersection with two finite weathering wedges. Inside this bed the
        // distances to its boundaries are W-lower and upper-W, both affine in z.
        // At depth these cuts vanish; the foundation is never replaced by a bed.
        int solid = rf_intersect(1.0f - lower_slope * dip,
                                  face - lower_cut + lower_slope * (base_w - lower), &span);
        if (solid)
            solid = rf_intersect(1.0f + upper_slope * dip,
                                  face - upper_cut + upper_slope * (upper - base_w), &span);
        if (solid)
        {
            // Ordered, disjoint bed intervals make this the frontmost valid hit.
            height = span.y + slip;
            break;
        }
    }
    @height.set(@relief * height);
}
