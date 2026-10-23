/*
 * CRACKING — PASS 2: originals + junction seeds compete for integer IDs.
 *
 * Connect the seeds output of cracking.cl to this node's seeds input.
 * seeds must remain 32-bit RGBA DATA at its original resolution, unfiltered.
 * Configure dst as an integer ID output. No parameters need copying between
 * nodes: positions, depth offsets, radii, speeds and aspect are in the table.
 *
 * Score = (periodic_XY_distance_squared + signed_offset) / speed_squared.
 * Originals: offset = Z*Z, speed = 1.
 * Junctions: offset = -initial_radius*initial_radius, speed randomized.
 * Lower score owns the pixel. Speed changes relative growth influence in a
 * static partition; initial radius gives independent size variation.
 *
 * IDs 1..N are the original seeds. Later IDs identify junction seeds.
 * Every periodic image retains its seed ID. Period is one output rectangle.
 * ID 0 indicates invalid table/capacity, not a legitimate fracture region.
 */

#bind layer &size? ref
#bind layer seeds float4
#bind layer !&dst int

@KERNEL
{
    int2 size = convert_int2(@dst.res);
    int2 pixel = (int2)((int)get_global_id(0), (int)get_global_id(1));
    if (size.x <= 0 || size.y <= 0 || pixel.x >= size.x || pixel.y >= size.y)
        return;

    int2 table_size = convert_int2(@seeds.res);
    if (table_size.x <= 0 || table_size.y <= 0)
    {
        @dst.set(0);
        return;
    }
    float4 header = @seeds.bufferIndex((int2)(0, 0));
    if (header.w != -1.0f || header.x < 2.0f || header.z <= 0.0f ||
        header.x > 135169.0f)
    {
        @dst.set(0);
        return;
    }
    int records = (int)header.x;
    if ((ulong)records > (ulong)table_size.x * (ulong)table_size.y)
    {
        @dst.set(0);
        return;
    }

    float2 uv = (convert_float2(pixel) + 0.5f) / convert_float2(size);
    float aspect = header.z;
    float nearest = INFINITY;
    int nearest_id = 0;
    for (int record = 1; record < records; ++record)
    {
        int2 address = (int2)(record % table_size.x, record / table_size.x);
        float4 point = @seeds.bufferIndex(address);
        if (point.w < 0.0f)
        {
            @dst.set(0);
            return;
        }
        if (point.w == 0.0f)
            continue;

        float2 delta = uv - point.xy;
        delta -= floor(delta + 0.5f);
        delta.x *= aspect;
        float score = (dot(delta, delta) + point.z) / (point.w * point.w);
        if (score < nearest)
        {
            nearest = score;
            nearest_id = record;
        }
    }
    @dst.set(nearest_id);
}
