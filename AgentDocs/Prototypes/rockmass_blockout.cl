
#runover layer
#bind layer &height float
#bind layer &normalization_range float3
#bind parm seed int val=17
#bind parm variation float val=0.25
#bind parm rib_layers int val=2
#bind parm stack_layers int val=2
#bind parm periodic int val=1
#bind parm elevation float val=0.0
#bind parm relief float val=0.25
#bind parm tilt_degrees float val=-12.0
#bind parm yaw_degrees float val=0.0
#bind parm yaw_variation float val=6.0
#bind parm shoulder_width float val=0.05
#bind parm break_depth float val=0.25
#bind parm break_position float val=0.05
#bind parm carve_depth float val=0.18
#bind parm carve_height_threshold float val=0.0
#bind parm carve_blend_width float val=0.25

// (center across ribs, half-width, front depth, face tilt).
#define RM_LEFT_RIB   ((float4)(-0.32f, 0.30f, 0.72f, -0.08f))
#define RM_CENTER_RIB ((float4)( 0.04f, 0.18f, 1.00f,  0.12f))
#define RM_RIGHT_RIB  ((float4)( 0.38f, 0.28f, 0.60f, -0.06f))
#define RM_RIB_VARIATION ((float4)(0.08f, 0.25f, 0.20f, 0.06f))
#define RM_BREAK_TILT 0.35f
#define RM_BREAK_POSITION_VARIATION 0.16f
#define RM_BREAK_TILT_VARIATION 0.15f
#define RM_HALF_LENGTH 0.65f
#define RM_TILE_ROUND_RADIUS 0.7071067811865476f
#define RM_ANCHOR_DEPTH_MARGIN 1.0f

// Ordinary OpenCL functions own geometry. Only the dispatch entry points use
// layer accessors, keeping geometry independent of scratch and iteration state.
typedef struct
{
    uint seed;
    int layers;
    int periodic;
    float variation;
    float depth_scale;
    float shoulder;
    float break_depth;
    float break_position;
    float tilt_degrees;
    float yaw_degrees;
    float yaw_variation;
} RMShapeSettings;

typedef struct
{
    float4 ribs[3];
    float2 yaws[3];
    float2 cut;
} RMRibGroup;

typedef struct
{
    float4 shape;
    float2 yaw;
    float2 cut;
    // Top-face fractions on the left/right, followed by their bevel depth drops.
    float4 crown;
    float half_length;
    float shoulder;
    float break_depth;
    int stepped;
} RMProfiledRib;

float rm_invalid()
{
    return as_float(0x7fc00000u);
}

uint rm_hash(uint h)
{
    h ^= h >> 16; h *= 2246822519u;
    h ^= h >> 13; h *= 3266489917u;
    return h ^ (h >> 16);
}

float rm_signed(uint seed, uint channel)
{
    uint h = rm_hash(seed ^ rm_hash(channel + 1u));
    return 2.0f * (float)(h >> 8) * (1.0f / 16777216.0f) - 1.0f;
}

float2 rm_axis(float degrees)
{
    float angle = radians(degrees);
    return (float2)(cos(angle), sin(angle));
}

float2 rm_rotate(float2 p, float2 axis)
{
    return (float2)(axis.x * p.x + axis.y * p.y, -axis.y * p.x + axis.x * p.y);
}

float4 rm_seed_rib(float4 rib, uint seed, uint index, float variation)
{
    uint key = rm_hash(seed ^ index * 747796405u);
    float4 r = variation * RM_RIB_VARIATION
             * (float4)(rm_signed(key, 0u), rm_signed(key, 1u),
                         rm_signed(key, 2u), rm_signed(key, 3u));
    rib.x += r.x;
    rib.y *= 1.0f + r.y;
    rib.z *= 1.0f + r.z;
    rib.w += r.w;
    return rib;
}

float2 rm_seed_break(uint seed, const RMShapeSettings *s)
{
    return (float2)(
        s->break_position + s->variation * RM_BREAK_POSITION_VARIATION * rm_signed(seed, 10u),
        RM_BREAK_TILT + s->variation * RM_BREAK_TILT_VARIATION * rm_signed(seed, 11u));
}

// Clip the ray interval against the entire yawed half-space, including sidewalls
// and break planes. Negative nz clips the back rather than the front of the ray.
int rm_clip_plane(float3 normal, float bound, float2 pos, float2 yaw, float2 *span)
{
    float nx = yaw.x * normal.x + yaw.y * normal.z;
    float nz = -yaw.y * normal.x + yaw.x * normal.z;
    float rhs = bound - nx * pos.x - normal.y * pos.y;
    if (nz == 0.0f) return rhs >= 0.0f;
    float depth = rhs / nz;
    if (nz > 0.0f) span->y = fmin(span->y, depth);
    else span->x = fmax(span->x, depth);
    return span->x <= span->y;
}

int rm_rib_interval(float2 p, float4 rib, float2 yaw, float depth_scale,
                     float shoulder, float setback, int step_side,
                     float2 cut, float half_length, float2 *span)
{
    float2 pos = (float2)(p.x - rib.x, p.y);
    *span = (float2)(-INFINITY, INFINITY);
    float slope = rib.w / rib.y;
    if (!rm_clip_plane((float3)(-depth_scale * slope, 0.0f, 1.0f),
                       -depth_scale * setback, pos, yaw, span)) return 0;
    float side_bound = depth_scale * (rib.y - shoulder * setback);
    if (!rm_clip_plane((float3)(depth_scale, 0.0f, shoulder), side_bound,
                        pos, yaw, span)) return 0;
    if (!rm_clip_plane((float3)(-depth_scale, 0.0f, shoulder), side_bound,
                        pos, yaw, span)) return 0;
    if (step_side != 0)
    {
        float side = (float)step_side;
        float3 normal = (float3)(-depth_scale * (shoulder * slope + side * cut.y),
                                 -side * depth_scale, shoulder);
        float bound = -depth_scale * (shoulder * setback + side * cut.x);
        if (!rm_clip_plane(normal, bound, pos, yaw, span)) return 0;
    }
    if (half_length > 0.0f)
    {
        float bound = depth_scale * half_length;
        if (!rm_clip_plane((float3)(0.0f, depth_scale, shoulder), bound,
                            pos, yaw, span)) return 0;
        if (!rm_clip_plane((float3)(0.0f, -depth_scale, shoulder), bound,
                            pos, yaw, span)) return 0;
    }
    return 1;
}

float rm_rib_piece(float2 p, float4 rib, float2 yaw, float depth_scale,
                   float shoulder, float setback, int step_side,
                   float2 cut, int finite_ends)
{
    float2 span;
    if (!rm_rib_interval(p, rib, yaw, depth_scale, shoulder, setback, step_side,
                         cut, finite_ends ? RM_HALF_LENGTH : 0.0f, &span)) return -INFINITY;
    return rib.z + span.y / depth_scale;
}

int rm_seed_group(int index, const RMShapeSettings *s, RMRibGroup *group)
{
    // The first group's seed remains unchanged for the original three-rib mode.
    uint key = index == 0 ? s->seed : rm_hash(s->seed ^ (uint)index * 1597334677u);
    group->cut = rm_seed_break(key, s);
    for (int slot = 0; slot < 3; ++slot)
    {
        float4 base = slot == 0 ? RM_LEFT_RIB : (slot == 1 ? RM_CENTER_RIB : RM_RIGHT_RIB);
        float4 rib = rm_seed_rib(base, key, (uint)slot, s->variation);
        if (s->layers > 1)
        {
            rib.x = (rib.x + (float)index + 0.5f) / (float)s->layers - 0.5f;
            rib.y /= (float)s->layers;
        }
        float angle = s->yaw_degrees + s->yaw_variation * rm_signed(key, 20u + (uint)slot);
        float2 yaw = rm_axis(angle);
        // The repeat bounds rely on these open-backed cones receding in depth.
        if (!(fabs(angle) < 90.0f)
            || !(yaw.x > fabs(yaw.y) * (s->shoulder / s->depth_scale))) return 0;
        group->ribs[slot] = rib;
        group->yaws[slot] = yaw;
    }
    return 1;
}

float rm_group_surface(float2 p, const RMRibGroup *group, const RMShapeSettings *s)
{
    float surface = -INFINITY;
    for (int slot = 0; slot < 3; ++slot)
    {
        int stepped = slot == 1;
        surface = fmax(surface, rm_rib_piece(p, group->ribs[slot], group->yaws[slot],
            s->depth_scale, s->shoulder, 0.0f, stepped, group->cut, s->periodic));
        if (stepped)
            surface = fmax(surface, rm_rib_piece(p, group->ribs[slot], group->yaws[slot],
                s->depth_scale, s->shoulder, s->break_depth, -1, group->cut, s->periodic));
    }
    return surface;
}

float2 rm_repeat_extent(float4 rib, float2 yaw, float best, float2 axis,
                        const RMShapeSettings *s)
{
    // Bound the yawed solid above the current hit. Break cuts only remove material.
    float sy = fabs(yaw.y);
    float denominator = yaw.x - sy * s->shoulder / s->depth_scale;
    float bottom = (best - rib.z - sy * rib.y / s->depth_scale) / denominator;
    float slope = fabs(rib.w / rib.y);
    float top = fmin(slope * rib.y / (1.0f + slope * s->shoulder),
                      RM_HALF_LENGTH / s->shoulder);
    if (bottom > top) return (float2)(0.0f);
    float across = rib.y - s->shoulder * bottom;
    float along = RM_HALF_LENGTH - s->shoulder * bottom;
    float depth_reach = s->depth_scale * fmax(fabs(bottom), fabs(top));
    float x_reach = fabs(rib.x) + yaw.x * across + sy * depth_reach;
    float2 a = fabs(axis);
    return (float2)(a.x * x_reach + a.y * along, a.y * x_reach + a.x * along);
}

float2 rm_anchor_copy(float2 uv, float4 rib, float2 yaw, float2 axis,
                      const RMShapeSettings *s)
{
    // Rounding a copy position changes UV by at most sqrt(1/2). At sufficient
    // depth this entire disk lies inside the rib, guaranteeing a finite first hit.
    float radius = RM_TILE_ROUND_RADIUS;
    float lateral = fabs(yaw.y) / s->depth_scale;
    float depth = fabs(yaw.y / s->depth_scale - rib.w / rib.y * yaw.x) * radius;
    depth = fmax(depth, ((s->shoulder * lateral + yaw.x) * radius - rib.y) / s->shoulder);
    depth = fmax(depth, ((s->shoulder * lateral + 1.0f) * radius - RM_HALF_LENGTH) / s->shoulder);
    depth += RM_ANCHOR_DEPTH_MARGIN;
    float center_x = rib.x - yaw.y * s->depth_scale * depth;
    return rint(uv - (float2)(axis.x * center_x, axis.y * center_x));
}

float rm_group_repeat(float2 uv, float2 axis, const RMRibGroup *group,
                       const RMShapeSettings *s, float best)
{
    best = fmax(best, rm_group_surface(rm_rotate(uv, axis), group, s));
    if (!s->periodic) return best;

    float2 anchor = rm_anchor_copy(uv, group->ribs[0], group->yaws[0], axis, s);
    best = fmax(best, rm_rib_piece(rm_rotate(uv - anchor, axis), group->ribs[0],
        group->yaws[0], s->depth_scale, s->shoulder, 0.0f, 0, group->cut, 1));
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
    {
        if (x == 0 && y == 0) continue;
        float2 p = rm_rotate(uv - (float2)((float)x, (float)y), axis);
        best = fmax(best, rm_group_surface(p, group, s));
    }

    // Include all farther copies that could improve the hit; 3x3 alone is not enough.
    float2 extent = (float2)(0.0f);
    for (int slot = 0; slot < 3; ++slot)
        extent = fmax(extent, rm_repeat_extent(group->ribs[slot], group->yaws[slot], best, axis, s));
    int2 first = convert_int2(ceil(uv - extent));
    int2 last = convert_int2(floor(uv + extent));
    for (int y = first.y; y <= last.y; ++y)
    for (int x = first.x; x <= last.x; ++x)
    {
        if (x >= -1 && x <= 1 && y >= -1 && y <= 1) continue;
        float2 p = rm_rotate(uv - (float2)((float)x, (float)y), axis);
        best = fmax(best, rm_group_surface(p, group, s));
    }
    return best;
}

float rm_formation(float2 uv, const RMShapeSettings *s, float2 *front_range)
{
    float2 axis = rm_axis(s->tilt_degrees);
    float best = -INFINITY;
    *front_range = (float2)(INFINITY, -INFINITY);
    // Stream three ribs at a time: no layer-count ceiling or variable-length arrays.
    for (int index = 0; index < s->layers; ++index)
    {
        RMRibGroup group;
        if (!rm_seed_group(index, s, &group)) return rm_invalid();
        for (int slot = 0; slot < 3; ++slot)
        {
            float4 rib = group.ribs[slot];
            front_range->x = fmin(front_range->x, rib.z - fabs(rib.w));
            front_range->y = fmax(front_range->y, rib.z + fabs(rib.w));
        }
        best = rm_group_repeat(uv, axis, &group, s, best);
    }
    return best;
}

float rm_profiled_piece(float2 p, const RMProfiledRib *rib, float depth_scale,
                         float setback, int step_side)
{
    float2 span;
    if (!rm_rib_interval(p, rib->shape, rib->yaw, depth_scale, rib->shoulder,
                         setback, step_side, rib->cut, rib->half_length, &span)) return -INFINITY;

    float2 pos = (float2)(p.x - rib->shape.x, p.y);
    float slope = rib->shape.w / rib->shape.y;
    float left_top = rib->shape.y * rib->crown.x;
    float right_top = rib->shape.y * rib->crown.y;
    float left_slope = rib->crown.z / (rib->shape.y - left_top);
    float right_slope = rib->crown.w / (rib->shape.y - right_top);
    // Two asymmetric bevel planes meet the sloping front face. Narrow top
    // fractions form a sharp ridge; broad fractions leave a flatter slab face.
    if (!rm_clip_plane((float3)(-depth_scale * (slope + left_slope), 0.0f, 1.0f),
        depth_scale * (left_slope * left_top - setback), pos, rib->yaw, &span)) return -INFINITY;
    if (!rm_clip_plane((float3)(-depth_scale * (slope - right_slope), 0.0f, 1.0f),
        depth_scale * (right_slope * right_top - setback), pos, rib->yaw, &span)) return -INFINITY;

    // Beveled ends give each rib its own finite, faceted silhouette. These planes
    // turn with yaw as well; this is solid clipping, not a height/noise overlay.
    float end_top = 0.65f * rib->half_length;
    float end_slope = 0.5f * (rib->crown.z + rib->crown.w) / (rib->half_length - end_top);
    float end_bound = depth_scale * (end_slope * end_top - setback);
    if (!rm_clip_plane((float3)(-depth_scale * slope, depth_scale * end_slope, 1.0f),
                       end_bound, pos, rib->yaw, &span)) return -INFINITY;
    if (!rm_clip_plane((float3)(-depth_scale * slope, -depth_scale * end_slope, 1.0f),
                       end_bound, pos, rib->yaw, &span)) return -INFINITY;
    return rib->shape.z + span.y / depth_scale;
}

float rm_profiled_surface(float2 p, const RMProfiledRib *rib, float depth_scale)
{
    float surface = rm_profiled_piece(p, rib, depth_scale, 0.0f, rib->stepped);
    if (rib->stepped)
        surface = fmax(surface, rm_profiled_piece(p, rib, depth_scale, rib->break_depth, -1));
    return surface;
}

float rm_profiled_repeat(float2 uv, float2 axis, const RMProfiledRib *rib,
                          const RMShapeSettings *s, float best)
{
    if (!s->periodic)
        return fmax(best, rm_profiled_surface(rm_rotate(uv, axis), rib, s->depth_scale));

    RMShapeSettings bounds = *s;
    bounds.shoulder = rib->shoulder;
    // Every profile is shorter than RM_HALF_LENGTH. Its added planes only remove
    // material, so the original rib's bounds conservatively include all copies.
    // best already comes from the base formation; no unprofiled anchor is added.
    float2 extent = rm_repeat_extent(rib->shape, rib->yaw, best, axis, &bounds);
    int2 first = convert_int2(ceil(uv - extent));
    int2 last = convert_int2(floor(uv + extent));
    for (int y = first.y; y <= last.y; ++y)
    for (int x = first.x; x <= last.x; ++x)
    {
        float2 p = rm_rotate(uv - (float2)((float)x, (float)y), axis);
        best = fmax(best, rm_profiled_surface(p, rib, s->depth_scale));
    }
    return best;
}

float rm_stacked_ribs(float2 uv, const RMShapeSettings *s, int count, float best)
{
    float2 axis = rm_axis(s->tilt_degrees);
    for (int tier = 0; tier < count; ++tier)
    {
        RMShapeSettings layer = *s;
        layer.seed = rm_hash(s->seed ^ ((uint)tier + 1u) * 2891336453u);
        for (int index = 0; index < s->layers; ++index)
        {
            RMRibGroup group;
            if (!rm_seed_group(index, &layer, &group)) return rm_invalid();
            for (int slot = 0; slot < 3; ++slot)
            {
                int kind = (tier % 3 + index % 3 + slot) % 3;
                uint key = rm_hash(layer.seed ^ (uint)index * 1597334677u ^ (uint)slot * 747796405u);
                // Stagger every rib, not just the whole group. Deterministic
                // spacing and archetypes remain distinct when variation is zero.
                float phase = ((float)tier + 1.0f) * 0.6180339887f
                            + (float)slot * 0.37f + (float)index * 0.19f;
                float along_phase = ((float)tier + 1.0f) * 0.4142135624f
                                  + (float)slot * 0.31f + (float)index * 0.23f;
                phase -= floor(phase);
                along_phase -= floor(along_phase);
                float direction = (tier % 2 + slot) % 2 == 0 ? 1.0f : -1.0f;

                RMProfiledRib rib;
                rib.shape = group.ribs[slot];
                rib.yaw = group.yaws[slot];
                // Broad/flat, substantial/deep, narrow/sharp. Width and face rise
                // are independent, unlike the previous uniformly scaled copies.
                float width = kind == 0 ? 1.12f : (kind == 1 ? 0.76f : 0.44f);
                float lift = kind == 0 ? 0.08f : (kind == 1 ? 0.26f : 0.44f);
                float face_scale = kind == 0 ? 0.35f : (kind == 1 ? 1.1f : 1.4f);
                float face_rise = kind == 0 ? 0.02f : (kind == 1 ? 0.12f : 0.20f);
                rib.shape.x += (0.20f * (2.0f * phase - 1.0f)
                              + 0.06f * s->variation * rm_signed(key, 40u)) / (float)s->layers;
                rib.shape.y *= width * (0.85f + 0.30f * phase);
                rib.shape.z += lift * (0.75f + 0.50f * along_phase);
                rib.shape.w = rib.shape.w * face_scale + direction * face_rise;
                rib.half_length = (kind == 0 ? 0.48f : (kind == 1 ? 0.36f : 0.24f))
                                * (0.80f + 0.25f * along_phase)
                                * (1.0f + 0.10f * s->variation * rm_signed(key, 41u));
                rib.shoulder = s->shoulder * (kind == 0 ? 0.65f : (kind == 1 ? 0.30f : 0.12f));
                rib.crown = kind == 0 ? (float4)(0.72f, 0.58f, 0.16f, 0.12f)
                          : (kind == 1 ? (float4)(0.42f, 0.56f, 0.40f, 0.70f)
                                       : (float4)(0.08f, 0.14f, 0.90f, 0.65f));
                rib.crown.z *= 1.0f + 0.15f * s->variation * rm_signed(key, 42u);
                rib.crown.w *= 1.0f + 0.15f * s->variation * rm_signed(key, 43u);
                rib.stepped = kind == 1 || (kind == 2 && tier % 2 == 1);
                rib.break_depth = s->break_depth * (kind == 1 ? 0.45f : 0.25f);
                rib.cut = group.cut + (float2)(0.18f * (phase - 0.5f),
                                               0.12f * direction);

                float along = 0.90f * (along_phase - 0.5f)
                            + 0.12f * s->variation * rm_signed(key, 44u);
                float2 offset = (float2)(-axis.y * along, axis.x * along);
                // More tiers add placements, not ever-higher/even-thinner sheets.
                // The hard union retains each surviving face and its sharp edges.
                best = rm_profiled_repeat(uv - offset, axis, &rib, s, best);
            }
        }
    }
    return best;
}

// Cutters reuse the same plane solver. Their positive portion is finite support,
// not a clamp on bind parameters or on the resulting formation height.
float rm_carve_sample(float2 p, float4 rib, int kind, float stretch,
                       float2 cut, const RMShapeSettings *s)
{
    p.y *= stretch;
    float2 yaw = (float2)(1.0f, 0.0f);
    int stepped = kind == 1;
    float surface = rm_rib_piece(p, rib, yaw, 1.0f, s->shoulder, 0.0f, stepped, cut, 1);
    if (stepped)
        surface = fmax(surface, rm_rib_piece(p, rib, yaw, 1.0f, s->shoulder,
                                            s->break_depth, -1, cut, 1));
    return fmax(0.0f, surface);
}

float rm_carve_repeat(float2 uv, float4 rib, int kind, float stretch,
                       float2 axis, float2 cut, const RMShapeSettings *s)
{
    if (!s->periodic)
        return rm_carve_sample(rm_rotate(uv, axis), rib, kind, stretch, cut, s);

    uv -= floor(uv + 0.5f);
    float across = fabs(rib.x) + rib.y + s->shoulder * rib.z;
    float along = (RM_HALF_LENGTH + s->shoulder * rib.z) / stretch;
    float2 a = fabs(axis);
    float2 extent = (float2)(a.x * across + a.y * along, a.y * across + a.x * along);
    int2 first = convert_int2(ceil(uv - extent));
    int2 last = convert_int2(floor(uv + extent));
    float surface = 0.0f;
    for (int y = first.y; y <= last.y; ++y)
    for (int x = first.x; x <= last.x; ++x)
    {
        float2 p = rm_rotate(uv - (float2)((float)x, (float)y), axis);
        surface = fmax(surface, rm_carve_sample(p, rib, kind, stretch, cut, s));
    }
    return surface;
}

float rm_carving(float2 uv, const RMShapeSettings *s)
{
    float carving = 0.0f;
    for (int kind = 0; kind < 3; ++kind)
    {
        // Long wedge, short broken slab, oblique wedge. Finer cuts contribute less.
        float stretch = kind == 0 ? 1.4f : (kind == 1 ? 3.2f : 2.4f);
        float width = kind == 0 ? 0.10f : (kind == 1 ? 0.19f : 0.08f);
        float slope = kind == 0 ? -0.18f : (kind == 1 ? 0.08f : 0.22f);
        float angle_offset = kind == 0 ? -8.0f : (kind == 1 ? 12.0f : 38.0f);
        float weight = kind == 0 ? 1.0f : (kind == 1 ? 0.65f : 0.40f);
        float layer = 0.0f;
        for (int index = 0; index < s->layers; ++index)
        {
            uint key = rm_hash(s->seed ^ ((uint)index * 1597334677u)
                                      ^ ((uint)(kind + 1) * 3812015801u));
            float4 rib = rm_seed_rib((float4)(0.0f, width, 0.75f, slope),
                                     key, (uint)kind, s->variation);
            rib.x /= (float)s->layers;
            rib.y /= (float)s->layers;
            float2 offset = (float2)(
                ((float)index + 0.5f + 0.18f * (float)(kind - 1)) / (float)s->layers - 0.5f,
                0.24f * (float)(kind - 1) + 0.30f * s->variation * rm_signed(key, 30u));
            float2 axis = rm_axis(s->tilt_degrees + angle_offset
                                  + 18.0f * s->variation * rm_signed(key, 31u));
            float2 cut = (float2)(
                s->break_position + s->variation * RM_BREAK_POSITION_VARIATION * rm_signed(key, 32u),
                RM_BREAK_TILT + s->variation * RM_BREAK_TILT_VARIATION * rm_signed(key, 33u));
            layer = fmax(layer, rm_carve_repeat(uv - offset, rib, kind, stretch, axis, cut, s));
        }
        carving += layer * weight;
    }
    return carving;
}

float rm_self_blend(float base, float candidate, float2 front_range,
                     float threshold, float width)
{
    // Structural reference, not a guess at the image's min/max. Values outside
    // the front span remain outside [-1,1]; only the blend weight is bounded.
    float span = front_range.y - front_range.x;
    float self_height = span > 0.0f ? 2.0f * (base - front_range.x) / span - 1.0f : 0.0f;
    float t = width != 0.0f ? 0.5f + 0.5f * tanh((self_height - threshold) / width)
                           : (self_height >= threshold ? 1.0f : 0.0f);
    return mix(fmin(base, candidate), fmax(base, candidate), t);
}

float rm_normalize(float value, float2 range)
{
    if (!isfinite(value)) return value;
    if (range.x == range.y) return 0.0f;
    if (value == range.x) return -1.0f;
    if (value == range.y) return 1.0f;
    // Avoid overflow when finite extrema have large opposite signs.
    float scale = fmax(fabs(range.x), fabs(range.y));
    float low = range.x / scale;
    float high = range.y / scale;
    return 2.0f * ((value / scale - low) / (high - low)) - 1.0f;
}

@KERNEL
{
    if (@Iteration == 1)
    {
        if (@iy != 0) return;
        float2 range = (float2)(INFINITY, -INFINITY);
        for (int x = 0; x < @height.xres; ++x)
        {
            float3 column = @normalization_range.bufferIndex((int2)(x, 0));
            range.x = fmin(range.x, column.x);
            range.y = fmax(range.y, column.y);
        }
        if (!all(isfinite(range))) return;
        // One writer per column; scratch is read-only throughout this dispatch.
        for (int y = 0; y < @height.yres; ++y)
        {
            int2 pixel = (int2)(@ix, y);
            float h = @height.bufferIndex(pixel);
            @height.setIndex(pixel, rm_normalize(h, range));
        }
        return;
    }
    if (@Iteration != 0) return;

    int valid = all(isfinite((float4)(@elevation, @relief, @tilt_degrees, @shoulder_width)))
             && all(isfinite((float2)(@break_depth, @break_position)))
             && all(isfinite((float2)(@yaw_degrees, @yaw_variation)))
             && @stack_layers >= 0
             && (@stack_layers > 0
                 || all(isfinite((float3)(@carve_depth, @carve_height_threshold, @carve_blend_width))))
             && @rib_layers >= 1 && @yaw_variation >= 0.0f
             && @shoulder_width > 0.0f && @break_depth >= 0.0f
             && @variation >= 0.0f && @variation <= 1.0f;
    if (!valid)
    {
        @height.set(rm_invalid());
        return;
    }
    if (@relief == 0.0f)
    {
        @height.set(@elevation);
        return;
    }

    RMShapeSettings s;
    s.seed = (uint)@seed;
    s.layers = @rib_layers;
    s.periodic = @periodic;
    s.variation = @variation;
    s.depth_scale = fabs(@relief);
    s.shoulder = @shoulder_width;
    s.break_depth = @break_depth;
    s.break_position = @break_position;
    s.tilt_degrees = @tilt_degrees;
    s.yaw_degrees = @yaw_degrees;
    s.yaw_variation = @yaw_variation;

    float2 uv = (convert_float2(@ixy) + 0.5f) / convert_float2(@height.res);
    if (s.periodic) uv -= floor(uv);
    uv -= 0.5f;
    float2 front_range;
    float mass = rm_formation(uv, &s, &front_range);
    if (!isfinite(mass))
    {
        @height.set(rm_invalid());
        return;
    }
    if (@stack_layers > 0)
    {
        mass = rm_stacked_ribs(uv, &s, @stack_layers, mass);
    }
    else if (@carve_depth != 0.0f)
    {
        float candidate = mass - @carve_depth * rm_carving(uv, &s);
        mass = rm_self_blend(mass, candidate, front_range,
                             @carve_height_threshold, @carve_blend_width);
    }
    // All shape operations are complete; later dispatches only normalize.
    @height.set(isfinite(mass) ? @elevation + @relief * mass : rm_invalid());
}

@WRITEBACK
{
    if (@Iteration != 0) return;
    @normalization_range.set((float3)(0.0f));
    if (@iy != 0) return;

    // The geometry dispatch has completed globally. Each work item reduces one
    // column without modifying height; iteration 1 reads only completed ranges.
    float2 range = (float2)(INFINITY, -INFINITY);
    for (int y = 0; y < @height.yres; ++y)
    {
        float h = @height.bufferIndex((int2)(@ix, y));
        if (isfinite(h))
        {
            range.x = fmin(range.x, h);
            range.y = fmax(range.y, h);
        }
    }
    @normalization_range.set((float3)(range.x, range.y, 0.0f));
}
