// Houdini Copernicus OpenCL prototype; not a standalone OpenCL kernel.
// Connect a Mono layer named height; click Create Inputs and Spare Parameters.
// Replaces height. Not compiled or cooked.
// Three independently seeded directional formations, composed with min/max and carves.
// Each formation retains broken sheets, partial flakes, chips and cross-fractures.
// formation_count=1 and formation_carve_depth=0 recover the single-formation path.
// Cost scales roughly with formation_count; no runtime/visual validation yet.
// Height only: no true undercuts, detached geometry or debris.
// Recreate spare parameters after updating this snippet.
// Widths/depths below are normalized profile units, not world-space distances.
// All noise repeats in UV. Integer layer_count / diagonal_turns preserve X/Y tiling.
// diagonal_turns changes direction in discrete steps; do not rotate UV arbitrarily.
#runover layer
#bind layer &height float
#bind parm seed int val=17
#bind parm layer_count int val=9
#bind parm diagonal_turns int val=-3
#bind parm kink_cells int val=4
#bind parm kink_strength float val=0.85
#bind parm jagged_cells int val=28
#bind parm edge_jaggedness float val=0.14
#bind parm ledge_width float val=0.045
// Seeded width variation in stops: 0 restores the uniform ledge-width path.
// The same multiplier scales the chamfer floor so it cannot hide this variation.
#bind parm ledge_width_variation float val=1.25
#bind parm ledge_width_cells int val=6
#bind parm face_tilt float val=0.65
#bind parm height_variation float val=0.30
#bind parm relief float val=1.0
// Signed blend: +1 = max buildup, -1 = min intersection, 0 = leave stack unchanged.
#bind parm formation_count int val=3
// Automatic mode overrides the manual height/offset/phase/frequency controls below.
#bind parm automatic_formations int val=1
#bind parm formation_variation float val=0.55
#bind parm formation2_blend float val=0.85
#bind parm formation3_blend float val=-0.55
#bind parm formation2_height float val=0.85
#bind parm formation3_height float val=1.15
#bind parm formation2_offset float val=0.20
#bind parm formation3_offset float val=0.10
#bind parm formation2_phase float val=0.37
#bind parm formation3_phase float val=0.73
// Integer frequency multipliers preserve direction and periodic winding exactly.
#bind parm formation2_frequency int val=2
#bind parm formation3_frequency int val=1
// 1 shares the broad folds; 0 gives each formation independent folds.
#bind parm formation_fold_coherence float val=0.65
// Broad directional cuts applied BETWEEN composition stages, separate from cracks.
#bind parm formation_carve_cells int val=3
#bind parm formation_carve_coverage float val=0.40
#bind parm formation_carve_depth float val=0.25
#bind parm formation_carve_chamfer float val=0.12
// Additional phase distortion changes local layer thickness and spacing.
#bind parm thickness_variation float val=0.45
#bind parm tilt_variation float val=0.35
#bind parm face_unevenness float val=0.08
// Breakup removes portions of sheets, exposing a lower rock bed.
#bind parm breakup_cells int val=5
#bind parm layer_breakup float val=0.55
// More length cells produce shorter runs; variation stretches/breaks them unevenly.
// length_cells=1 and length_variation=0 restore the original termination field.
#bind parm layer_length_cells int val=1
#bind parm layer_length_variation float val=0.85
#bind parm exposed_height float val=0.16
#bind parm chamfer_width float val=0.07
#bind parm chamfer_variation float val=0.65
// Finite polygon flakes: count is candidates per patch cell, not stripe frequency.
#bind parm flake_length float val=0.80
#bind parm flake_width float val=0.24
#bind parm flake_tilt float val=0.90
#bind parm flake_angle_variation float val=0.30
#bind parm flake_chamfer float val=0.10
#bind parm corner_chipping float val=0.45
#bind parm flake_count int val=3
#bind parm flake_height float val=0.20
#bind parm flake_coverage float val=0.55
// Sparse chip events widen selected rim bevels; density is independent of depth.
// Follows RockRimChips in Shaders/Private/MixtormatRockFormation.usf.
#bind parm chip_density float val=0.18
#bind parm chip_cells int val=45
#bind parm chip_depth float val=0.10
// Cross-fracture widths are fractions of the crack repetition period.
#bind parm crack_count int val=5
#bind parm crack_width float val=0.025
#bind parm crack_depth float val=0.35
#bind parm crack_jaggedness float val=0.35
#bind parm crack_coverage float val=0.65

uint kr_hash(uint h)
{
    h ^= h >> 16; h *= 2246822519u;
    h ^= h >> 13; h *= 3266489917u;
    return h ^ (h >> 16);
}

int kr_wrap(int x, int period)
{
    return ((x % period) + period) % period;
}

float kr_random(int2 cell, int period, uint seed)
{
    uint x = (uint)kr_wrap(cell.x, period);
    uint y = (uint)kr_wrap(cell.y, period);
    uint key = kr_hash(x * 1597334677u ^ y * 3812015801u ^ seed);
    return (float)(key >> 8) * (1.0f / 16777216.0f);
}

float kr_sample(uint key, uint channel)
{
    return (float)(kr_hash(key ^ kr_hash(channel + 1u)) >> 8)
         * (1.0f / 16777216.0f);
}

// Compact angular chip events. Like RockRimChips, hash selection leaves most
// candidates intact; straight-sided bites may have a flat fracture floor.
// UV patches replace edge coordinates here because the bands have no stored edges.
float kr_chip_events(float2 uv, int cells, uint seed, float density)
{
    if (density <= 0.0f) return 0.0f;
    float2 p = uv * (float)cells;
    int2 base = convert_int2(floor(p));
    float result = 0.0f;
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
    {
        int2 cell = base + (int2)(x, y);
        uint key = kr_hash((uint)kr_wrap(cell.x, cells) * 1597334677u
                 ^ (uint)kr_wrap(cell.y, cells) * 3812015801u ^ seed);
        if (kr_sample(key, 0u) >= density) continue;
        float2 center = convert_float2(cell)
                      + (float2)(kr_sample(key, 1u), kr_sample(key, 2u));
        float2 radius = (float2)(mix(0.15f, 0.45f, kr_sample(key, 3u)),
                                mix(0.15f, 0.45f, kr_sample(key, 4u)));
        float2 d = fabs((p - center) / radius);
        float triangle = fmax(1.0f - d.x - d.y, 0.0f);
        float floor_at = kr_sample(key, 5u) < 0.5f ? 0.67f : 1.0f;
        float bite = fmin(triangle, floor_at) / floor_at;
        result = fmax(result, bite * mix(0.5f, 1.0f, kr_sample(key, 6u)));
    }
    return result;
}

// Finite clipped plates in a periodic UV lattice, not repeated phase stripes.
// Search support grows with flake dimensions; large flakes cost more to evaluate.
// Nonpositive dimensions produce no flakes rather than silently resizing them.
float kr_flakes(float2 uv, float2 axis, int cells, int count, uint seed,
    float coverage, float length_control, float width_control, float angle_variation,
    float tilt, float chamfer, float corner_chipping, float chip_depth, float chip_density)
{
    if (length_control <= 0.0f || width_control <= 0.0f) return 0.0f;
    float2 p = uv * (float)cells;
    int2 base = convert_int2(floor(p));
    float result = 0.0f;
    int search = (int)ceil(0.5f * length((float2)(length_control, width_control)));
    for (int y = -search; y <= search; ++y)
    for (int x = -search; x <= search; ++x)
    {
        int2 cell = base + (int2)(x, y);
        uint cell_key = kr_hash((uint)kr_wrap(cell.x, cells) * 1597334677u
                     ^ (uint)kr_wrap(cell.y, cells) * 3812015801u ^ seed);
        for (int i = 0; i < count; ++i)
        {
            uint key = kr_hash(cell_key ^ ((uint)i + 1u) * 747796405u);
            if (kr_sample(key, 0u) >= coverage) continue;
            float2 center = convert_float2(cell)
                          + (float2)(kr_sample(key, 1u), kr_sample(key, 2u));
            float angle = (2.0f * kr_sample(key, 3u) - 1.0f) * angle_variation;
            float2 along = (float2)(axis.x * cos(angle) - axis.y * sin(angle),
                                   axis.x * sin(angle) + axis.y * cos(angle));
            float2 across = (float2)(-along.y, along.x);
            float2 delta = p - center;
            float2 q = (float2)(dot(delta, along), dot(delta, across));
            float hx = 0.5f * length_control
                     * mix(0.35f, 1.0f, kr_sample(key, 4u));
            float hy = 0.5f * width_control
                     * mix(0.35f, 1.0f, kr_sample(key, 5u));
            float inset = fmin(hx - fabs(q.x), hy - fabs(q.y));
            // Independently selected corners; unselected corners stay intact.
            // Plane normals are normalized so the bevel follows every cut face.
            for (int corner = 0; corner < 4; ++corner)
            {
                if (kr_sample(key, 30u + (uint)corner) >= chip_density) continue;
                float sx = (corner & 1) ? 1.0f : -1.0f;
                float sy = (corner & 2) ? 1.0f : -1.0f;
                float cut = corner_chipping
                          * mix(0.2f, 1.0f, kr_sample(key, 10u + (uint)corner));
                float plane = (2.0f - cut - sx * q.x / hx - sy * q.y / hy)
                            / sqrt(1.0f / (hx * hx) + 1.0f / (hy * hy));
                inset = fmin(inset, plane);
            }
            // An asymmetric triangular bite in one long edge, with its own bevel.
            float notch_center = mix(-hx, hx, kr_sample(key, 20u));
            float notch_depth = kr_sample(key, 34u) < chip_density
                              ? chip_depth * hy * kr_sample(key, 21u) : 0.0f;
            float notch_side = kr_sample(key, 22u) < 0.5f ? -1.0f : 1.0f;
            float notch = (hy - notch_side * q.y - notch_depth
                         + fabs(q.x - notch_center)) * 0.70710678f;
            inset = fmin(inset, notch);
            float bevel = clamp(inset / fmax(chamfer * fmin(hx, hy), 0.0001f),
                                0.0f, 1.0f);
            float ramp_t = clamp(0.5f + 0.5f * q.y / hy, 0.0f, 1.0f);
            float ramp = 1.0f - tilt * ramp_t;
            result = fmax(result, bevel * ramp * mix(0.45f, 1.0f, kr_sample(key, 6u)));
        }
    }
    return result;
}

// Piecewise planar interpolation: continuous values, deliberately sharp bends.
// Unlike smooth noise, triangle boundaries retain changes in slope.
float kr_angular_noise(float2 uv, int cells, uint seed)
{
    float2 p = uv * (float)cells;
    int2 cell = convert_int2(floor(p));
    float2 f = p - floor(p);
    float a = kr_random(cell, cells, seed);
    float b = kr_random(cell + (int2)(1, 0), cells, seed);
    float c = kr_random(cell + (int2)(0, 1), cells, seed);
    float d = kr_random(cell + (int2)(1, 1), cells, seed);
    float value;
    if (f.x + f.y <= 1.0f)
        value = a + f.x * (b - a) + f.y * (c - a);
    else
        value = d + (1.0f - f.y) * (b - d) + (1.0f - f.x) * (c - d);
    return 2.0f * value - 1.0f;
}

// Signed contour inset controls both coverage and a straight chamfer.
float kr_bevel(float inset, float width)
{
    return clamp(inset / fmax(width, 0.0001f), 0.0f, 1.0f);
}

float kr_fraction(float value)
{
    return value - floor(value);
}

@KERNEL
{
    float2 uv = (convert_float2(@ixy) + 0.5f) / convert_float2(@height.res);
    int layers = max(@layer_count, 1);
    uint root_seed = (uint)@seed;
    float shared_kink = kr_angular_noise(uv, max(@kink_cells, 1), root_seed);
    float stack = 0.0f;
    // This prototype has three composition slots; the count guard is structural.
    int formations = clamp(@formation_count, 1, 3);

    // Each iteration evaluates a complete formation, not another flake of one phase.
    for (int formation = 0; formation < formations; ++formation)
    {
    uint seed = formation == 0 ? root_seed
              : kr_hash(root_seed ^ (uint)formation * 747796405u);
    int frequency = formation == 0 ? 1 : max(formation == 1
                  ? @formation2_frequency : @formation3_frequency, 1);
    float phase_offset = formation == 0 ? 0.0f : (formation == 1
                       ? @formation2_phase : @formation3_phase);
    float formation_height = formation == 0 ? 1.0f : (formation == 1
                           ? @formation2_height : @formation3_height);
    float elevation = formation == 0 ? 0.0f : (formation == 1
                    ? @formation2_offset : @formation3_offset);
    if (@automatic_formations && formation > 0)
    {
        // Stable by formation index: increasing count does not relocate earlier layers.
        float diversity = @formation_variation;
        phase_offset = kr_sample(seed, 100u);
        frequency = kr_sample(seed, 101u) < diversity * 0.5f ? 2 : 1;
        formation_height = 1.0f + diversity * (kr_sample(seed, 102u) - 0.5f);
        elevation = diversity * 0.35f * kr_sample(seed, 103u);
    }
    float blend = formation == 0 ? 1.0f : (formation == 1
                ? @formation2_blend : @formation3_blend);
    if (formation > 0 && blend == 0.0f) continue;

    // Related broad folds, but independent spacing, damage, coverage and fractures.
    float kink = mix(kr_angular_noise(uv, max(@kink_cells, 1), seed),
                     shared_kink, @formation_fold_coherence);
    float jag = kr_angular_noise(uv, max(@jagged_cells, 1), seed ^ 2891336453u);
    int patches = max(@breakup_cells, 1);
    float thickness = kr_angular_noise(uv, patches, seed ^ 747796405u);
    float phase = (uv.y * (float)layers + uv.x * (float)@diagonal_turns)
                * (float)frequency + phase_offset
                + @kink_strength * kink
                + @edge_jaggedness * jag
                + @thickness_variation * thickness;
    float t = kr_fraction(phase);

    float variation = 0.5f + 0.5f * kr_angular_noise(
        uv, max(@kink_cells, 1), seed ^ 1013904223u);
    float amplitude = mix(1.0f, variation, @height_variation);
    float tilt_noise = kr_angular_noise(uv, patches, seed ^ 277803737u);
    float tilt = @face_tilt + @tilt_variation * tilt_noise;
    // Independent from height and chip randomness; periodic and seeded per formation.
    // Broad patches vary along/across layers, rather than changing width per pixel.
    float width_field = kr_angular_noise(uv, max(@ledge_width_cells, 1),
                                         seed ^ 0x68bc21ebu);
    float width_scale = exp2(@ledge_width_variation * width_field);
    float width = @ledge_width * width_scale;
    // Guard only the singular denominator; do not cap the authored ledge width.
    float face_span = 1.0f - 2.0f * width;
    face_span = copysign(fmax(fabs(face_span), 0.0001f), face_span);
    float face = clamp((t - width) / face_span, 0.0f, 1.0f);
    float ramp = 1.0f - tilt * face;
    float roughness = @face_unevenness * jag;

    float chip_event = kr_chip_events(uv, max(@chip_cells, 1),
                                     seed ^ 2246822519u, @chip_density);
    float chip = @chip_depth * chip_event;
    float chamfer = @chamfer_width * mix(1.0f, variation, @chamfer_variation);
    float bevel_width = fmax(width, chamfer * width_scale);

    // Uneven along-layer termination spacing, independently seeded per formation.
    // A periodic displacement changes local run lengths without scaling UV by noise,
    // which would break the tile boundary. Integer repeats preserve periodicity.
    float length_noise = kr_angular_noise(uv, patches, seed ^ 0x9e3779b9u);
    float size_noise = kr_angular_noise(uv, patches, seed ^ 0x85ebca6bu);
    float2 length_uv = (float2)(uv.x * (float)max(@layer_length_cells, 1), uv.y);
    float2 layer_axis = normalize((float2)((float)layers, -(float)@diagonal_turns));
    length_uv += layer_axis * (@layer_length_variation * length_noise / (float)patches);
    float patch = kr_angular_noise(length_uv, patches, seed ^ 3266489917u);
    // Local threshold variation leaves long surviving sheets beside short remnants.
    // Zero breakup still retains full bands; exposed portions reveal the lower bed.
    float breakup = @layer_breakup;
    float termination = mix(1.0f, patch + @layer_length_variation * size_noise, breakup);
    float band_inset = fmin(t, 1.0f - t);
    // Only selected events clip edge/termination intersections.
    float corner_cut = @corner_chipping * bevel_width * chip_event;
    float corner_inset = (band_inset + termination - corner_cut) * 0.70710678f;
    float inset = fmin(fmin(band_inset, termination), corner_inset);
    // Rim chips widen the chamfer locally; they do not erode every outline or top.
    float edge = kr_bevel(inset, bevel_width + chip);
    float bed = @exposed_height * (0.65f + 0.35f * variation);
    float sheet = amplitude * fmax(ramp + roughness, 0.0f);
    float surface = mix(bed, fmax(bed, sheet), edge);

    // Warp finite plates with the broad folds, without multiplying the band phase.
    // Integer UV translations still map to integer lattice translations.
    float2 flake_uv = uv + (float2)(0.0f, @kink_strength * kink / (float)layers);
    float2 flake_axis = normalize((float2)((float)layers, -(float)@diagonal_turns));
    float flakes = kr_flakes(flake_uv, flake_axis, patches, @flake_count,
        seed ^ 3812015801u, @flake_coverage,
        @flake_length, @flake_width, @flake_angle_variation,
        @flake_tilt, @flake_chamfer, @corner_chipping, @chip_depth, @chip_density);
    surface += edge * @flake_height * amplitude * flakes;

    // Transverse wandering cracks with local terminations, not an unbroken grid.
    // The integer UV winding and periodic warp keep this field tileable as well.
    float crack_phase = uv.x * (float)max(@crack_count, 1)
                      + @crack_jaggedness * (kink + jag);
    float ct = kr_fraction(crack_phase);
    float crack_distance = fmin(ct, 1.0f - ct);
    float crack_patch = kr_angular_noise(uv, patches, seed ^ 1597334677u);
    float crack_end = crack_patch + 2.0f * @crack_coverage - 1.0f;
    float crack = 0.0f;
    if (@crack_count > 0 && @crack_width > 0.0f)
    {
        float crack_inset = fmin(@crack_width - crack_distance, crack_end);
        crack = kr_bevel(crack_inset, fmax(chamfer, @crack_width * 0.25f));
    }
    surface = fmax(surface - @crack_depth * crack, 0.0f);
    surface = fmax(formation_height * surface + elevation, 0.0f);
    if (formation == 0)
        stack = surface;
    else
    {
        float combined = blend >= 0.0f ? fmax(stack, surface) : fmin(stack, surface);
        stack = mix(stack, combined, fabs(blend));
    }

    // A separate low-frequency cutter intersects the current stack with a lower
    // ceiling. Later max formations may bridge earlier cuts; later min passes
    // expose them again. Integer directional coordinates keep the cutter periodic.
    float2 carve_uv = (float2)(uv.x, uv.y * (float)layers
                            + uv.x * (float)@diagonal_turns
                            + @kink_strength * shared_kink);
    float carve_field = kr_angular_noise(carve_uv, max(@formation_carve_cells, 1),
                                         seed ^ 277803737u);
    float carve_inset = carve_field
                      + 2.0f * @formation_carve_coverage - 1.0f;
    float carve_mask = kr_bevel(carve_inset, fmax(@formation_carve_chamfer, 0.0001f));
    if (@formation_carve_depth > 0.0f)
    {
        float ceiling = fmax(formation_height + elevation - @formation_carve_depth, 0.0f);
        stack = mix(stack, fmin(stack, ceiling), carve_mask);
    }
    }
    @height.set(@relief * stack);
}
