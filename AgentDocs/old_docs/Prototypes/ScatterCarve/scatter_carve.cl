// SCATTER CARVE — finite geometric cutters with a signed-field Chamfer Boolean.
// Inputs: height = original Mono height; size_ref = optional Mono size reference;
//         mask = optional Mono carve mask.
// Outputs: height_out = FINAL Mono height; cut_depth = signed Mono removal;
//          geometry_front/back/mask = original, unbeveled Mono cutter previews;
//          chamfer_state = older RGBA diagnostic output; leave disconnected.
// Connect height_out directly to normals/preview. No envelope/Eikonal COPs.
// center_depth < 0 selects additive union; otherwise operation selects subtract/union.
// chamfer_fraction controls geometric blend width relative to each cutter radius.
// height_to_uv converts one raw height unit to UV length; height is NOT normalized.
// Use one Kernel Iteration and disable Use Write Back Kernel.
#runover layer
#bind layer height float border=WRAP
#bind layer ?size_ref float
#bind layer ?mask float border=WRAP
#bind layer !&height_out float
#bind layer !&cut_depth float
#bind layer !&geometry_front float
#bind layer !&geometry_back float
#bind layer !&geometry_mask float
#bind layer !&chamfer_state float4

#bind parm seed int val=1
#bind parm count int val=1024
#bind parm radius float val=0.0125
#bind parm radius_variation float val=0.5
#bind parm center_depth float val=0.04
#bind parm depth_variation float val=0.08
#bind parm reference_height float val=0
#bind parm depth_scale float val=1

#bind parm operation int val=0
#bind parm shape int val=1
#bind parm sides int val=7
#bind parm irregularity float val=0.25
#bind parm rotation float val=0
#bind parm rotation_variation float val=180
#bind parm chamfer_fraction float val=0.2
#bind parm chamfer_variation float val=0.5
#bind parm height_to_uv float val=1

// Prototype capacity, not a plugin parameter contract.
#define SCATTER_CARVE_MAX_SEEDS 4096
// Float-precision root refinement, not a spatial width/radius limit.
#define SCATTER_CARVE_ROOT_STEPS 24

uint carve_hash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    return x ^ (x >> 16);
}

float carve_random(uint key, uint channel)
{
    return (float)(carve_hash(key + channel * 0x9e3779b9u) >> 8)
        * (1.0f / 16777216.0f);
}

// Unit side normal and support distance of an irregular convex polygon.
float3 carve_side(uint key, int side, int sides, float radius,
                  float angle, float irregularity)
{
    float theta = 6.28318530718f * (float)side / (float)sides + angle;
    float2 n = (float2)(cos(theta), sin(theta));
    float support = radius * (1.0f + irregularity
        * (2.0f * carve_random(key, 16u + (uint)side) - 1.0f));
    return (float3)(n.x, n.y, support);
}

// Signed distance to the convex half-plane polygon. Outside distance uses
// clipped face segments, including redundant planes and non-adjacent corners.
float carve_polygon_distance(uint key, int sides, float radius, float angle,
                             float irregularity, float2 p, float bound)
{
    float3 first = carve_side(key, 0, sides, radius, angle, irregularity);
    float plane_distance = dot(first.xy, p) - first.z;
    for (int i = 1; i < sides; ++i)
    {
        float3 side = carve_side(key, i, sides, radius, angle, irregularity);
        plane_distance = fmax(plane_distance, dot(side.xy, p) - side.z);
    }
    if (plane_distance <= 0.0f)
        return plane_distance;

    float best = 0.0f;
    int found = 0;
    for (int i = 0; i < sides; ++i)
    {
        float3 side = carve_side(key, i, sides, radius, angle, irregularity);
        float2 origin = side.xy * side.z;
        float2 tangent = (float2)(-side.y, side.x);
        float lo = -bound;
        float hi = bound;
        for (int j = 0; j < sides; ++j)
        {
            if (j == i)
                continue;
            float3 clip = carve_side(key, j, sides, radius, angle, irregularity);
            float along = dot(clip.xy, tangent);
            float remaining = clip.z - dot(clip.xy, origin);
            if (along > 0.0f)
                hi = fmin(hi, remaining / along);
            else if (along < 0.0f)
                lo = fmax(lo, remaining / along);
            else if (remaining < 0.0f)
                lo = bound + 1.0f;
            if (lo > hi)
                break;
        }
        if (lo > hi)
            continue;
        float t = clamp(dot(p - origin, tangent), lo, hi);
        float distance = length(p - (origin + t * tangent));
        if (!found || distance < best)
        {
            best = distance;
            found = 1;
        }
    }
    // Negative/degenerate supports may define an empty polygon.
    return found ? best : bound + length(p) + radius;
}

// Outside distance is Euclidean. Inside, only the sign is needed by the
// bevel root solve; the analytic ray interval preserves the exact solid.
float carve_volume_distance(int shape, float lateral, float radius,
                            float half_height, float z_offset)
{
    if (shape == 1)
    {
        float2 q = (float2)(lateral, fabs(z_offset) - half_height);
        return length(fmax(q, (float2)(0.0f)))
            + fmin(fmax(q.x, q.y), 0.0f);
    }

    float2 p = (float2)(lateral, fabs(z_offset));
    float2 axes = (float2)(radius, half_height);
    if (radius == half_height)
        return length(p) - radius;
    float norm = length(p / axes);
    if (norm <= 1.0f)
        return (norm - 1.0f) * fmin(radius, half_height);

    // Closest point on the meridian ellipse. Normalizing the calculation is
    // numerical conditioning only; it never changes the stored raw heights.
    float scale = fmax(radius, half_height);
    p /= scale;
    axes /= scale;
    float2 axes2 = axes * axes;
    float lo = 0.0f;
    float hi = length(axes * p);
    for (int i = 0; i < SCATTER_CARVE_ROOT_STEPS; ++i)
    {
        float mid = lo + (hi - lo) * 0.5f;
        if (mid == lo || mid == hi)
            break;
        float2 q = axes * p / (axes2 + mid);
        if (dot(q, q) > 1.0f)
            lo = mid;
        else
            hi = mid;
    }
    float2 closest = axes2 * p / (axes2 + hi);
    return length(p - closest) * scale;
}

// A = vertical signed height field; D = signed cutter distance.
// Subtract: max(A, -D, (A-D+width)/sqrt(2)).
// Union:    min(A,  D, (A+D-width)/sqrt(2)).
// With A = direction*t, the extra bevel volume ends at D(t)+t-width = 0.
// Its exterior crossing is monotone because Euclidean distance is 1-Lipschitz.
float carve_chamfer_travel(int shape, float lateral, float radius,
                           float half_height, float z_offset,
                           float width, float direction)
{
    float lo = 0.0f;
    float hi = width;
    float end_distance = carve_volume_distance(shape, lateral, radius,
        half_height, z_offset + direction * hi);
    if (end_distance <= 0.0f)
        return hi; // The sharp cutter interval continues beyond the bevel band.
    for (int i = 0; i < SCATTER_CARVE_ROOT_STEPS; ++i)
    {
        float mid = lo + (hi - lo) * 0.5f;
        if (mid == lo || mid == hi)
            break;
        float distance = carve_volume_distance(shape, lateral, radius,
            half_height, z_offset + direction * mid);
        if (distance + mid >= width)
            hi = mid;
        else
            lo = mid;
    }
    return hi;
}

@KERNEL
{
    float base = @height;
    float weight = @mask.bound ? @mask : 1.0f;
    weight = isfinite(weight) ? clamp(weight, 0.0f, 1.0f) : 0.0f;
    float result = base;
    int operation = @center_depth < 0.0f ? 1 : @operation;
    float height_scale = @height_to_uv;
    int bevel_enabled = isfinite(height_scale) && height_scale > 0.0f
        && (operation == 0 || operation == 1);
    float cut_width = 0.0f;
    int seed_count = clamp(@count, 0, SCATTER_CARVE_MAX_SEEDS);
    float front = 0.0f;
    float back = 0.0f;
    int hits = 0;

    // Preview shows surface-anchored geometry without target clipping or masking.
    if (seed_count > 0 && @radius > 0.0f && @depth_scale > 0.0f)
    {
        float2 uv = (convert_float2(@ixy) + 0.5f) / convert_float2(@res);
        float lower[SCATTER_CARVE_MAX_SEEDS];
        float upper[SCATTER_CARVE_MAX_SEEDS];
        float widths[SCATTER_CARVE_MAX_SEEDS];
        float variation = clamp(@radius_variation, 0.0f, 1.0f);

        for (int i = 0; i < seed_count; ++i)
        {
            uint key = carve_hash((uint)@seed + (uint)i * 0x9e3779b9u);
            float2 center = (float2)(carve_random(key, 1u), carve_random(key, 2u));
            float r = @radius * mix(1.0f - variation, 1.0f, carve_random(key, 3u));
            if (!isfinite(r) || r <= 0.0f)
                continue;
            float width = r * @chamfer_fraction * (1.0f
                + @chamfer_variation * (2.0f * carve_random(key, 4096u) - 1.0f));
            float reach = bevel_enabled && isfinite(width) && width > 0.0f
                ? width : 0.0f;
            float hz = r * @depth_scale;
            if (!isfinite(hz) || hz <= 0.0f)
                continue;
            float2 d = uv - center;
            d -= floor(d + 0.5f);
            float half_span = 0.0f;
            float lateral = 0.0f;

            if (@shape == 0)
            {
                lateral = length(d);
                if (lateral > r + reach)
                    continue;
                float2 q = d / r;
                float discriminant = 1.0f - dot(q, q);
                if (discriminant > 0.0f)
                    half_span = hz * sqrt(discriminant);
            }
            else if (@shape == 1 && @sides >= 3)
            {
                float angle = (@rotation + @rotation_variation
                    * (2.0f * carve_random(key, 5u) - 1.0f)) * 0.01745329252f;
                // Bound both the finite polygon and its external bevel support.
                float polygon_bound = r * (1.0f + fabs(@irregularity))
                    / cos(3.14159265359f / (float)@sides);
                float bound = polygon_bound + reach;
                if (!isfinite(bound))
                    continue;
                int xmin = (int)ceil(-bound - d.x);
                int xmax = (int)floor(bound - d.x);
                int ymin = (int)ceil(-bound - d.y);
                int ymax = (int)floor(bound - d.y);
                int found = 0;

                for (int ty = ymin; ty <= ymax; ++ty)
                for (int tx = xmin; tx <= xmax; ++tx)
                {
                    float2 p = d + (float2)((float)tx, (float)ty);
                    if (dot(p, p) > bound * bound)
                        continue;
                    float distance = carve_polygon_distance(key, @sides, r,
                        angle, @irregularity, p, polygon_bound);
                    if (!found || distance < lateral)
                    {
                        lateral = distance;
                        found = 1;
                    }
                }
                if (!found || lateral > reach)
                    continue;
                if (lateral <= 0.0f)
                    half_span = hz;
            }
            else
                continue;

            // Keep a rigid finite cutter: sample only at its seed, not per pixel.
            float surface = @height.textureSample(center);
            float z = surface + @reference_height - @center_depth
                + (2.0f * carve_random(key, 4u) - 1.0f) * fabs(@depth_variation);
            if (!isfinite(z))
                continue;

            if (half_span > 0.0f)
            {
                lower[hits] = z - half_span;
                upper[hits] = z + half_span;
                widths[hits] = width;
                front = hits == 0 ? upper[hits] : fmax(front, upper[hits]);
                back = hits == 0 ? lower[hits] : fmin(back, lower[hits]);
                ++hits;
            }

            float scaled_hz = hz * height_scale;
            float z_offset = (base - z) * height_scale;
            if (reach > 0.0f && isfinite(scaled_hz) && scaled_hz > 0.0f
                && isfinite(z_offset))
            {
                float distance = carve_volume_distance(@shape, lateral, r,
                    scaled_hz, z_offset);
                if (isfinite(distance) && distance < reach)
                {
                    float direction = operation == 0 ? -1.0f : 1.0f;
                    float travel = carve_chamfer_travel(@shape, lateral, r,
                        scaled_hz, z_offset, reach, direction);
                    float beveled = base + direction * (travel / height_scale);
                    if (isfinite(beveled))
                    {
                        if (operation == 0 && beveled < result)
                        {
                            result = beveled;
                            cut_width = width;
                        }
                        else if (operation == 1)
                            result = fmax(result, beveled);
                    }
                }
            }
        }

        // All bevel contributions reach the original surface. Do not bridge
        // a gap to a separate buried sharp interval by taking an interval hull.
        // Subtract the union of geometry intervals from the solid z <= base.
        // Revisit intervals so overlapping cuts are independent of seed order.
        // Buried intervals participate only when the current cut reaches them.
        for (int pass = 0; operation == 0 && pass < hits; ++pass)
        {
            float previous = result;
            for (int i = 0; i < hits; ++i)
            {
                if (lower[i] < result && upper[i] >= result)
                {
                    result = lower[i];
                    cut_width = widths[i];
                }
            }
            if (result == previous)
                break;
        }

        // Union: frontmost hit of the target solid and scattered geometry.
        if (operation == 1 && hits > 0)
            result = fmax(result, front);
    }

    // Mask blends resolved relief; fractional masks are not volumetric CSG.
    result = mix(base, result, weight);
    @height_out.set(result);
    @cut_depth.set(base - result);
    @geometry_front.set(front);
    @geometry_back.set(back);
    @geometry_mask.set(hits > 0 ? 1.0f : 0.0f);
    // State: travel distance, source floor, source surface, radius-derived width.
    // Actual cut samples are fixed zero-distance sources; uncut pixels are unreached.
    @chamfer_state.set(result < base && cut_width > 0.0f
        ? (float4)(0.0f, result, base, cut_width)
        : (float4)(INFINITY, 0.0f, 0.0f, 0.0f));
}
