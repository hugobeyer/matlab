    /*
     * ONE COP: periodic power cells and adjacent-ID merging.
     * enable_junctions defaults to 0: junction discovery/growth is bypassed.
     * Set it to 1 to restore junction fragments without changing their controls.
     * junction_jitter is shared relative variation for base speed and size:
     * 0 = constant, 0.5 = +/-50%, 1 = zero through twice the base value.
     * dst is an integer ID layer; size is an optional metadata reference.
     *
     * Clustering changes ONLY the final labels, never positions or growth weights.
     * Merged cells share one ID: their shared border disappears while their union's
     * outer outline stays unchanged. Adjacency includes opposite tile boundaries.
     *
     * clustering: probability of accepting an eligible shared edge; 0 disables.
     * cluster_count: random ID-affinity groups; fewer groups allow more merges.
     *                This is not a requested final number of connected regions.
     * cluster_radius: maximum periodic seed separation for an accepted merge.
     *
     * size_aware_clustering: preserve tiny cells and merge other cells toward
     * their largest eligible edge-neighbor. small_cell_fraction is the protected
     * area threshold relative to the nominal original-cell area.
     * This favors small/large neighboring regions; it does not relocate cells
     * or guarantee that the global smallest and largest clusters share an edge.
     *
     * Only positive-length edges of the FINAL diagram qualify. Size-aware parent
     * links increase area (ties decrease ID); legacy links decrease ID. Both
     * produce connected groups without cycles or disconnected hash labels.
     *
     * Growth uses additive power offsets: boundaries are straight, not rounded.
     * One normalized period spans the output bounds. Still ONE OpenCL COP.
     *
     * REQUIRED NODE SETTINGS
     * - Enable Use Write Back Kernel. Kernel Iterations = 1; pixel tiles = 1x1.
     * - Regenerate bindings to add cr_cache: RGBA, 32-bit, same resolution as dst.
     * - dst remains the final integer-ID output. cr_cache is scratch data only.
     *
     * @KERNEL: one work-item per original seed builds its junction descriptors
     * and computes one immediate merge parent per valid seed, once per cook.
     * @WRITEBACK: pixels compare cached seed scores and follow cached parents.
     * No polygon clipping, junction discovery, or adjacency reconstruction is
     * performed per output pixel. Kernel completion separates writes and reads.
     *
     * Cache requires point_count pixels with junctions off, 33*point_count on.
     * Each original's header stores a junction bitmask, error status, and parent ID.
     * With junctions off, only original-cell adjacency is computed. Junction records
     * store XY, power offset, and parent ID. Record index equals seed ID minus 1.
     * Polygon capacity is 32 vertices. ID 0 reports capacity/geometry failure.
     * API checked against SideFX docs; not compiled or benchmarked in Houdini.
     */
    
    #bind layer &size? ref
    #bind layer !&dst int
    #bind layer !&cr_cache float4
    #bind parm seed int val=17
    #bind parm point_count int val=48
    #bind parm depth_jitter float val=0.08
    #bind parm enable_junctions int val=0
    #bind parm junction_speed float val=0.22
    #bind parm junction_size float val=0.011
    #bind parm junction_jitter float val=0.5
    #bind parm junction_growth float val=0.03
    #bind parm clustering float val=1
    #bind parm cluster_count int val=5
    #bind parm cluster_radius float val=0.22
    #bind parm size_aware_clustering int val=1
    #bind parm small_cell_fraction float val=0.35
    
    #define CR_VERTICES 32
    
    typedef struct
    {
        int count;
        int junctions;
        int size_aware;
        float small_fraction;
        uint seed;
        float aspect;
        float depth;
        float2 speed;
        float2 radius;
        float growth;
        float spacing;
    } CrSettings;
    
    uint cr_hash(uint value)
    {
        value ^= value >> 16;
        value *= 0x7feb352du;
        value ^= value >> 15;
        value *= 0x846ca68bu;
        value ^= value >> 16;
        return value;
    }
    
    float cr_random(uint value)
    {
        return (float)(cr_hash(value) >> 8) * 5.960464477539063e-8f;
    }
    
    /* xy: normalized position; z: additive power offset, NOT position/depth. */
    float3 cr_original(int index, CrSettings settings)
    {
        uint key = cr_hash(settings.seed ^ ((uint)index * 0x9e3779b9u));
        float z = (2.0f * cr_random(key ^ 0x529a4d31u) - 1.0f) * settings.depth;
        return (float3)(cr_random(key ^ 0x49d8a731u),
                        cr_random(key ^ 0xb932e167u), z * z);
    }
    
    float2 cr_delta(float2 a, float2 b, float aspect)
    {
        float2 delta = a - b;
        delta -= floor(delta + 0.5f);
        delta.x *= aspect;
        return delta;
    }
    
    int cr_initialize(int id, float aspect,
                       __private float2 *polygon, __private int *incoming)
    {
        polygon[0] = (float2)(-0.5f * aspect, -0.5f);
        polygon[1] = (float2)( 0.5f * aspect, -0.5f);
        polygon[2] = (float2)( 0.5f * aspect,  0.5f);
        polygon[3] = (float2)(-0.5f * aspect,  0.5f);
        for (int i = 0; i < 4; ++i)
            incoming[i] = id;
        return 4;
    }
    
    /* incoming[k] labels the neighboring cell across the edge ENDING at k.
     * Self-image cuts retain the center ID and are never merge candidates.
     */
    int cr_clip(float3 center, float3 neighbor, int neighbor_id, float aspect,
                 int vertices, __private float2 *polygon, __private int *incoming)
    {
        float2 nearest_image = cr_delta(neighbor.xy, center.xy, aspect);
        float2 clipped[CR_VERTICES];
        int clipped_incoming[CR_VERTICES];
        for (int iy = -1; iy <= 1; ++iy)
        {
            for (int ix = -1; ix <= 1; ++ix)
            {
                float2 offset = nearest_image + (float2)((float)ix * aspect, (float)iy);
                float2 normal = 2.0f * offset;
                float limit = dot(offset, offset) + neighbor.z - center.z;
                float support = 0.5f * (fabs(normal.x) * aspect + fabs(normal.y));
                if (limit >= support)
                    continue;
                if (limit < -support)
                    return 0;
    
                int needs_clip = 0;
                for (int k = 0; k < vertices; ++k)
                {
                    if (dot(polygon[k], normal) > limit)
                    {
                        needs_clip = 1;
                        break;
                    }
                }
                if (!needs_clip)
                    continue;
    
                int kept = 0;
                for (int k = 0; k < vertices; ++k)
                {
                    int next = (k + 1) % vertices;
                    float2 a = polygon[k];
                    float2 b = polygon[next];
                    float da = dot(a, normal) - limit;
                    float db = dot(b, normal) - limit;
                    int inside_a = da <= 0.0f;
                    int inside_b = db <= 0.0f;
                    if (inside_a != inside_b)
                    {
                        if (kept == CR_VERTICES)
                            return -1;
                        clipped[kept] = mix(a, b, da / (da - db));
                        clipped_incoming[kept] = inside_a ? incoming[next] : neighbor_id;
                        ++kept;
                    }
                    if (inside_b)
                    {
                        if (kept == CR_VERTICES)
                            return -1;
                        clipped[kept] = b;
                        clipped_incoming[kept] = incoming[next];
                        ++kept;
                    }
                }
                if (kept < 3)
                    return 0;
                vertices = kept;
                for (int k = 0; k < vertices; ++k)
                {
                    polygon[k] = clipped[k];
                    incoming[k] = clipped_incoming[k];
                }
            }
        }
        return vertices;
    }
    
    int cr_original_cell(int owner, CrSettings settings,
                          __private float2 *polygon, __private int *incoming)
    {
        float3 center = cr_original(owner, settings);
        int vertices = cr_initialize(owner, settings.aspect, polygon, incoming);
        for (int other = 0; other < settings.count; ++other)
        {
            if (other == owner)
                continue;
            vertices = cr_clip(center, cr_original(other, settings), other,
                                settings.aspect, vertices, polygon, incoming);
            if (vertices <= 0)
                return vertices;
        }
        return vertices;
    }
    
    int cr_is_junction(int owner, int slot, int vertices, __private const int *incoming)
    {
        int a = incoming[slot];
        int b = incoming[(slot + 1) % vertices];
        return a != b && owner < a && owner < b;
    }
    
    /* xyz: seed descriptor; w: positive growth bias, or zero when disabled. */
    float4 cr_junction(int owner, int slot, float2 vertex, CrSettings settings)
    {
        int id = 1 + settings.count + owner * CR_VERTICES + slot;
        uint key = cr_hash(settings.seed ^ ((uint)id * 0x27d4eb2fu));
        float speed = mix(settings.speed.x, settings.speed.y, cr_random(key ^ 0x68d24f19u));
        float radius = mix(settings.radius.x, settings.radius.y, cr_random(key ^ 0x3a58c1d7u))
                     + speed * settings.growth;
        float bias = 2.0f * settings.spacing * radius + radius * radius;
        float3 center = cr_original(owner, settings);
        float2 uv = center.xy + vertex / (float2)(settings.aspect, 1.0f);
        uv -= floor(uv);
        return (float4)(uv, dot(vertex, vertex) + center.z - bias, bias);
    }
    
    int cr_get_seed(int id, CrSettings settings, __private float3 *point)
    {
        if (id >= 1 && id <= settings.count)
        {
            *point = cr_original(id - 1, settings);
            return 1;
        }
        int index = id - 1 - settings.count;
        if (!settings.junctions || index < 0 || index >= settings.count * CR_VERTICES)
            return 0;
        int owner = index / CR_VERTICES;
        int slot = index % CR_VERTICES;
        float2 polygon[CR_VERTICES];
        int incoming[CR_VERTICES];
        int vertices = cr_original_cell(owner, settings, polygon, incoming);
        if (vertices < 0)
            return -1;
        if (slot >= vertices || !cr_is_junction(owner, slot, vertices, incoming))
            return 0;
        float4 junction = cr_junction(owner, slot, polygon[slot], settings);
        if (junction.w <= 0.0f)
            return 0;
        *point = junction.xyz;
        return 1;
    }
    
    /* Clip against BOTH original and junction seeds. This is final adjacency,
     * not assumed adjacency from the first diagram or nearest seed positions.
     */
    int cr_final_cell(int id, float3 center, CrSettings settings,
                       __private float2 *polygon, __private int *incoming)
    {
        int vertices = cr_initialize(id, settings.aspect, polygon, incoming);
        for (int other = 0; other < settings.count; ++other)
        {
            if (other + 1 == id)
                continue;
            vertices = cr_clip(center, cr_original(other, settings), other + 1,
                                settings.aspect, vertices, polygon, incoming);
            if (vertices <= 0)
                return vertices;
        }
    
        if (!settings.junctions || settings.radius.y + settings.speed.y * settings.growth == 0.0f)
            return vertices;
    
        float2 original_polygon[CR_VERTICES];
        int original_incoming[CR_VERTICES];
        for (int owner = 0; owner < settings.count; ++owner)
        {
            int original_vertices = cr_original_cell(owner, settings,
                                                     original_polygon, original_incoming);
            if (original_vertices < 0)
                return -1;
            for (int slot = 0; slot < original_vertices; ++slot)
            {
                if (!cr_is_junction(owner, slot, original_vertices, original_incoming))
                    continue;
                int other_id = 1 + settings.count + owner * CR_VERTICES + slot;
                if (other_id == id)
                    continue;
                float4 junction = cr_junction(owner, slot, original_polygon[slot], settings);
                if (junction.w <= 0.0f)
                    continue;
                vertices = cr_clip(center, junction.xyz, other_id, settings.aspect,
                                    vertices, polygon, incoming);
                if (vertices <= 0)
                    return vertices;
            }
        }
        return vertices;
    }
    
    float cr_cell_area(__private const float2 *polygon, int vertices)
    {
        float twice_area = 0.0f;
        for (int k = 0; k < vertices; ++k)
        {
            float2 a = polygon[k];
            float2 b = polygon[(k + 1) % vertices];
            twice_area += a.x * b.y - a.y * b.x;
        }
        return 0.5f * fabs(twice_area);
    }

    /* One graph edge per seed. Parent chains are resolved using the cache,
     * not by repeatedly reconstructing neighboring cells.
     * Returns -1 for failure, 0 for an empty cell, or a positive parent ID.
     */
    int cr_cluster_parent(int id, float3 center, CrSettings settings, float strength,
                           int affinity_groups, float max_separation)
    {
        if (strength == 0.0f || max_separation == 0.0f)
            return id;
        float2 polygon[CR_VERTICES];
        int incoming[CR_VERTICES];
        int vertices = cr_final_cell(id, center, settings, polygon, incoming);
        if (vertices <= 0)
            return vertices;

        float protected_area = settings.small_fraction * settings.aspect
                             / (float)settings.count;
        float best_area = settings.size_aware ? cr_cell_area(polygon, vertices) : 0.0f;
        if (settings.size_aware && best_area <= protected_area)
            return id;

        uint group = cr_hash((uint)id ^ settings.seed ^ 0x63d83595u)
                   % (uint)affinity_groups;
        int parent = id;
        for (int k = 0; k < vertices; ++k)
        {
            int other = incoming[k];
            if (other < 1 || other == id || (!settings.size_aware && other >= parent))
                continue;
            int previous = (k + vertices - 1) % vertices;
            float2 edge = polygon[k] - polygon[previous];
            if (dot(edge, edge) <= 1e-14f)
                continue;
            uint other_group = cr_hash((uint)other ^ settings.seed ^ 0x63d83595u)
                             % (uint)affinity_groups;
            if (other_group != group)
                continue;
            uint pair = (uint)min(other, id) * 0x9e3779b9u
                      ^ (uint)max(other, id) * 0x85ebca6bu
                      ^ settings.seed ^ 0x94d049bbu;
            if (cr_random(pair) >= strength)
                continue;
            float3 neighbor;
            if (cr_get_seed(other, settings, &neighbor) != 1)
                return -1;
            float2 separation = cr_delta(center.xy, neighbor.xy, settings.aspect);
            if (dot(separation, separation) > max_separation * max_separation)
                continue;
            if (settings.size_aware)
            {
                /* Area comparisons run during seed preparation, never per pixel.
                 * Tiny cells cannot absorb others or be absorbed themselves.
                 */
                float2 neighbor_polygon[CR_VERTICES];
                int neighbor_incoming[CR_VERTICES];
                int neighbor_vertices = cr_final_cell(other, neighbor, settings,
                                                      neighbor_polygon, neighbor_incoming);
                if (neighbor_vertices < 0)
                    return -1;
                if (neighbor_vertices == 0)
                    continue;
                float area = cr_cell_area(neighbor_polygon, neighbor_vertices);
                if (area <= protected_area || area < best_area ||
                    (area == best_area && other >= parent))
                    continue;
                best_area = area;
            }
            parent = other;
        }
        return parent;
    }

    int2 cr_address(int record, int width)
    {
        return (int2)(record % width, record / width);
    }
    
    @KERNEL
    {
        int2 size = convert_int2(@dst.res);
        int2 pixel = @ixy;
        if (size.x <= 0 || size.y <= 0 || pixel.x >= size.x || pixel.y >= size.y)
            return;
        @dst.set(0);

        int count = clamp(@point_count, 1, 4096);
        int records = count * (@enable_junctions != 0 ? 1 + CR_VERTICES : 1);
        int2 cache_size = convert_int2(@cr_cache.res);
        if (any(cache_size != size) || @cr_cache.storage != FLOAT32 ||
            @cr_cache.channels != 4 || (ulong)size.x * (ulong)size.y < (ulong)records)
            return;
        int owner = pixel.y * size.x + pixel.x;
        /* !& creates output-only scratch, so readable layer methods are absent.
         * Both kernels use our packed float4 layout after the FLOAT32/4-channel
         * checks above; no input layer or initialized host data is required.
         */
        __global float *cache_data = (__global float *)@cr_cache.data;
        /* The first count work-items own disjoint record ranges. Other work-
         * items must not clear those records while their owners populate them.
         */
        if (owner >= records)
        {
            vstore4((float4)(0.0f), (size_t)owner, cache_data);
            return;
        }
        if (owner >= count)
            return;

        CrSettings settings;
        settings.count = count;
        settings.junctions = @enable_junctions != 0;
        settings.size_aware = @size_aware_clustering != 0;
        settings.small_fraction = clamp(@small_cell_fraction, 0.0f, 1.0f);
        settings.seed = (uint)@seed;
        settings.aspect = (float)size.x / (float)size.y;
        settings.depth = fmax(@depth_jitter, 0.0f);
        float jitter = clamp(@junction_jitter, 0.0f, 1.0f);
        float2 variation = (float2)(1.0f - jitter, 1.0f + jitter);
        settings.speed = fmax(@junction_speed, 0.0f) * variation;
        settings.radius = fmax(@junction_size, 0.0f) * variation;
        settings.growth = fmax(@junction_growth, 0.0f);
        settings.spacing = sqrt(settings.aspect / (float)count);
        float strength = clamp(@clustering, 0.0f, 1.0f);
        int groups = clamp(@cluster_count, 1, 64);
        float separation = fmax(@cluster_radius, 0.0f);

        float3 original = cr_original(owner, settings);
        int parent = cr_cluster_parent(owner + 1, original, settings,
                                        strength, groups, separation);
        int failed = parent < 0;
        float4 header = (float4)(0.0f, 0.0f, 0.0f, (float)max(parent, 0));
        uint mask = 0u;
        float2 polygon[CR_VERTICES];
        int incoming[CR_VERTICES];
        int vertices = 0;
        if (settings.junctions && settings.radius.y + settings.speed.y * settings.growth > 0.0f)
            vertices = cr_original_cell(owner, settings, polygon, incoming);
        if (vertices < 0)
            failed = 1;

        for (int slot = 0; settings.junctions && slot < CR_VERTICES; ++slot)
        {
            int record = count + owner * CR_VERTICES + slot;
            float4 data = (float4)(0.0f);
            if (slot < vertices && cr_is_junction(owner, slot, vertices, incoming))
            {
                float4 junction = cr_junction(owner, slot, polygon[slot], settings);
                if (junction.w > 0.0f)
                {
                    int junction_parent = cr_cluster_parent(record + 1, junction.xyz,
                                              settings, strength, groups, separation);
                    if (junction_parent < 0)
                        failed = 1;
                    else if (junction_parent > 0)
                    {
                        data = (float4)(junction.xyz, (float)junction_parent);
                        mask |= 1u << slot;
                    }
                }
            }
            vstore4(data, (size_t)record, cache_data);
        }
        header.x = (float)(mask & 65535u);
        header.y = (float)(mask >> 16);
        header.z = failed ? -1.0f : 0.0f;
        vstore4(header, (size_t)owner, cache_data);
    }

    @WRITEBACK
    {
        int2 size = convert_int2(@dst.res);
        int2 pixel = @ixy;
        if (size.x <= 0 || size.y <= 0 || pixel.x >= size.x || pixel.y >= size.y)
            return;
        int count = clamp(@point_count, 1, 4096);
        int records = count * (@enable_junctions != 0 ? 1 + CR_VERTICES : 1);
        int2 cache_size = convert_int2(@cr_cache.res);
        if (any(cache_size != size) || @cr_cache.storage != FLOAT32 ||
            @cr_cache.channels != 4 || (ulong)size.x * (ulong)size.y < (ulong)records)
        {
            @dst.set(0);
            return;
        }

        __global const float *cache_data = (__global const float *)@cr_cache.data;
        CrSettings settings = {0};
        settings.count = count;
        settings.seed = (uint)@seed;
        settings.aspect = (float)size.x / (float)size.y;
        settings.depth = fmax(@depth_jitter, 0.0f);
        float2 uv = (convert_float2(pixel) + 0.5f) / convert_float2(size);
        float nearest = INFINITY;
        int nearest_id = 0;

        for (int owner = 0; owner < count; ++owner)
        {
            float4 header = vload4((size_t)owner, cache_data);
            if (header.z < 0.0f)
            {
                @dst.set(0);
                return;
            }
            if (header.w > 0.0f)
            {
                float3 point = cr_original(owner, settings);
                float2 delta = cr_delta(uv, point.xy, settings.aspect);
                float score = dot(delta, delta) + point.z;
                int id = owner + 1;
                if (score < nearest || (score == nearest && id < nearest_id))
                {
                    nearest = score;
                    nearest_id = id;
                }
            }
            uint mask = (uint)header.x | ((uint)header.y << 16);
            while (mask != 0u)
            {
                int slot = 31 - (int)clz(mask);
                mask &= ~(1u << slot);
                int record = count + owner * CR_VERTICES + slot;
                float4 point = vload4((size_t)record, cache_data);
                float2 delta = cr_delta(uv, point.xy, settings.aspect);
                float score = dot(delta, delta) + point.z;
                int id = record + 1;
                if (score < nearest || (score == nearest && id < nearest_id))
                {
                    nearest = score;
                    nearest_id = id;
                }
            }
        }

        int id = nearest_id;
        for (int hop = 0; hop < records && id > 0 && id <= records; ++hop)
        {
            float4 record = vload4((size_t)(id - 1), cache_data);
            int parent = (int)record.w;
            if (parent == id)
            {
                @dst.set(id);
                return;
            }
            if (parent <= 0 || parent > records)
                break;
            id = parent;
        }
        @dst.set(0);
    }
