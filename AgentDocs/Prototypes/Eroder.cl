#bind layer size_ref? float port=size_ref nouipromote

// INPUT + FINAL HEIGHT
#bind layer &height float port=height border=WRAP

// PERSISTENT STATE / OUTPUTS
#bind layer &flow float2 port=flow border=WRAP
#bind layer &water float port=water border=WRAP
#bind layer &sediment float port=sediment border=WRAP
#bind layer &wear float port=wear border=WRAP
#bind layer &deposit float port=deposit border=WRAP
#bind layer &erosion float port=erosion border=WRAP

// SCRATCH PING-PONG
#bind layer &height_tmp float port=height_tmp border=WRAP
#bind layer &flow_tmp float2 port=flow_tmp border=WRAP
#bind layer &water_tmp float port=water_tmp border=WRAP
#bind layer &sediment_tmp float port=sediment_tmp border=WRAP
#bind layer &wear_tmp float port=wear_tmp border=WRAP
#bind layer &deposit_tmp float port=deposit_tmp border=WRAP
#bind layer &erosion_tmp float port=erosion_tmp border=WRAP

// GRAVITY / SURFACE
#bind parm gravity float3 val={0,-1,-0.15}
#bind parm height_scale float val=1
#bind parm gravity_force float val=0.12
#bind parm slope_follow float val=0.15

// FLOW
#bind parm flow_memory float val=0.94
#bind parm flow_diffusion float val=0.08
#bind parm advection float val=1
#bind parm damping float val=0.99
#bind parm max_speed float val=3
#bind parm ridge_block float val=0.65
#bind parm trace_steps int val=2

// WATER / ACCUMULATION
#bind parm source float val=0.025
#bind parm evaporation float val=0.025
#bind parm convergence_gain float val=1.5
#bind parm water_diffusion float val=0.05
#bind parm max_water float val=4

// EROSION / SEDIMENT
#bind parm capacity float val=1
#bind parm base_capacity float val=0.05
#bind parm slope_capacity float val=1
#bind parm speed_capacity float val=1
#bind parm erosion_rate float val=0.08
#bind parm deposition_rate float val=0.12
#bind parm erodability float val=1
#bind parm max_erosion_step float val=0.008
#bind parm max_deposit_step float val=0.008

// ROCK / BRICK EDGE WEAR
// Now a SUBTLE directional modifier only (0.15 x edge_wear), not a peak
// multiplier. Set edge_wear = 0 to disable convexity entirely.
#bind parm edge_wear float val=2
#bind parm edge_threshold float val=0.005
#bind parm edge_softness float val=0.03

// WEAR / EROSION FIELD DIFFUSION
// Smooths the erosion field before it is removed from the height, so cuts
// are rounded chamfers instead of razor incisions. The height is never blurred.
#bind parm wear_diffusion float val=0.25

// THERMAL / TALUS
#bind parm thermal float val=0.08
#bind parm talus float val=0.04
#bind parm max_thermal_step float val=0.004

// RESOLUTION REFERENCE
#bind parm reference_res float val=2048


// One thermal pair transfer, in RAW height units.
// Symmetric in the height difference, so the outflow from one pixel equals
// the inflow to its neighbour: material is moved, never created or deleted.
static float thermal_pair(float d, float talus, float k, float cap)
{
    return d > talus ? fmin((d - talus) * k, cap) : 0.0f;
}


@KERNEL
{
    int2 xy = (int2)(@ix, @iy);
    float2 p = convert_float2(xy);

    float resscale =
        (float)max(@xres, @yres) /
        fmax(@reference_res, 1.0f);

    resscale = fmax(resscale, 0.125f);

    //----------------------------------------------------------------------
    // CURRENT STATE
    //----------------------------------------------------------------------

    float hC = @height.bufferIndex(xy);

    float2 oldflow = (float2)(0.0f);
    float oldwater = 0.0f;
    float oldsed = 0.0f;
    float oldwear = 0.0f;
    float olddeposit = 0.0f;

    if (@Iteration > 0)
    {
        oldflow = @flow.bufferIndex(xy);
        oldwater = @water.bufferIndex(xy);
        oldsed = @sediment.bufferIndex(xy);
        oldwear = @wear.bufferIndex(xy);
        olddeposit = @deposit.bufferIndex(xy);
    }

    //----------------------------------------------------------------------
    // HEIGHT NEIGHBORHOOD
    //----------------------------------------------------------------------

    float hL  = @height.bufferIndex(xy + (int2)(-1, 0));
    float hR  = @height.bufferIndex(xy + (int2)( 1, 0));
    float hD  = @height.bufferIndex(xy + (int2)( 0,-1));
    float hU  = @height.bufferIndex(xy + (int2)( 0, 1));

    float hDL = @height.bufferIndex(xy + (int2)(-1,-1));
    float hDR = @height.bufferIndex(xy + (int2)( 1,-1));
    float hUL = @height.bufferIndex(xy + (int2)(-1, 1));
    float hUR = @height.bufferIndex(xy + (int2)( 1, 1));

    //----------------------------------------------------------------------
    // SMOOTHER SOBEL GRADIENT
    //----------------------------------------------------------------------

    float gx =
        (
            hUR + 2.0f*hR + hDR
          - hUL - 2.0f*hL - hDL
        ) * 0.125f;

    float gy =
        (
            hUL + 2.0f*hU + hUR
          - hDL - 2.0f*hD - hDR
        ) * 0.125f;

    float2 grad =
        (float2)(gx, gy) *
        @height_scale *
        resscale;

    float gradlen = length(grad);

    //----------------------------------------------------------------------
    // SURFACE NORMAL + TRUE TANGENT GRAVITY
    //----------------------------------------------------------------------

    float3 N =
        normalize(
            (float3)(
                -grad.x,
                -grad.y,
                1.0f
            )
        );

    float3 G = @gravity;

    float gl = length(G);

    if (gl > 1e-8f)
        G /= gl;
    else
        G = (float3)(0.0f, -1.0f, -0.15f);

    float3 GT =
        G - N * dot(G, N);

    float2 gravity2 = GT.xy;

    //----------------------------------------------------------------------
    // RK2 SELF-ADVECTION OF FLOW
    //----------------------------------------------------------------------

    float2 vadv = oldflow;

    if (@Iteration > 0)
    {
        int steps = max(@trace_steps, 1);

        float trace =
            @advection *
            resscale;

        float subdt =
            trace / (float)steps;

        float2 back = p;

        for (int i = 0; i < steps; i++)
        {
            float2 v0 =
                @flow.bufferSample(back);

            float2 mid =
                back
                - v0 * subdt * 0.5f;

            float2 vm =
                @flow.bufferSample(mid);

            back -= vm * subdt;
        }

        vadv =
            @flow.bufferSample(back);
    }

    //----------------------------------------------------------------------
    // VELOCITY DIFFUSION
    //----------------------------------------------------------------------

    float2 vavg = vadv;

    if (@Iteration > 0)
    {
        float2 vL =
            @flow.bufferIndex(xy + (int2)(-1,0));

        float2 vR =
            @flow.bufferIndex(xy + (int2)( 1,0));

        float2 vD =
            @flow.bufferIndex(xy + (int2)(0,-1));

        float2 vU =
            @flow.bufferIndex(xy + (int2)(0, 1));

        vavg =
            (vL + vR + vD + vU) * 0.25f;
    }

    float2 v =
        mix(
            vadv,
            vavg,
            clamp(@flow_diffusion, 0.0f, 1.0f)
        );

    v *= clamp(@flow_memory, 0.0f, 1.0f);

    //----------------------------------------------------------------------
    // ADD TANGENT GRAVITY
    //----------------------------------------------------------------------

    v += gravity2 * @gravity_force;

    if (gradlen > 1e-8f)
    {
        float2 downhill =
            -grad / gradlen;

        v +=
            downhill *
            @slope_follow *
            fmin(gradlen, 1.0f);
    }

    //----------------------------------------------------------------------
    // RIDGE COLLISION / SLIDING
    //
    // Remove only motion trying to penetrate strongly uphill.
    // Tangential component survives.
    //----------------------------------------------------------------------

    if (gradlen > 1e-8f)
    {
        float2 uphill =
            grad / gradlen;

        float into =
            fmax(
                dot(v, uphill),
                0.0f
            );

        v -=
            uphill *
            into *
            clamp(@ridge_block, 0.0f, 1.0f);
    }

    v *= clamp(@damping, 0.0f, 1.0f);

    float speed = length(v);

    if (@max_speed > 0.0f &&
        speed > @max_speed)
    {
        v *=
            @max_speed / speed;

        speed = @max_speed;
    }

    float2 dir =
        speed > 1e-8f
        ? v / speed
        : (float2)(0.0f);

    //----------------------------------------------------------------------
    // FLOW DIVERGENCE / CONVERGENCE
    //----------------------------------------------------------------------

    float convergence = 0.0f;

    if (@Iteration > 0)
    {
        float2 vL =
            @flow.bufferIndex(xy + (int2)(-1,0));

        float2 vR =
            @flow.bufferIndex(xy + (int2)( 1,0));

        float2 vD =
            @flow.bufferIndex(xy + (int2)(0,-1));

        float2 vU =
            @flow.bufferIndex(xy + (int2)(0, 1));

        float div =
            (
                (vR.x - vL.x) +
                (vU.y - vD.y)
            ) * 0.5f;

        convergence =
            fmax(-div, 0.0f);
    }

    //----------------------------------------------------------------------
    // BACKTRACE WATER + SEDIMENT
    //----------------------------------------------------------------------

    float transported_water = 0.0f;
    float transported_sed = 0.0f;

    if (@Iteration > 0 &&
        speed > 1e-8f)
    {
        float2 back =
            p
            - dir
            * @advection
            * resscale;

        transported_water =
            @water.bufferSample(back);

        transported_sed =
            @sediment.bufferSample(back);

        //------------------------------------------------------------------
        // SMALL SIDEWAYS WATER DIFFUSION
        //------------------------------------------------------------------

        float2 side =
            (float2)(-dir.y, dir.x);

        float sideoff =
            resscale;

        float wl =
            @water.bufferSample(
                back - side * sideoff
            );

        float wr =
            @water.bufferSample(
                back + side * sideoff
            );

        float wav =
            (wl + transported_water + wr)
            / 3.0f;

        transported_water =
            mix(
                transported_water,
                wav,
                clamp(
                    @water_diffusion,
                    0.0f,
                    1.0f
                )
            );
    }

    //----------------------------------------------------------------------
    // WATER / FLOW MASS
    //----------------------------------------------------------------------

    float water =
        transported_water;

    water *=
        1.0f
        + convergence
        * fmax(@convergence_gain, 0.0f);

    water +=
        fmax(@source, 0.0f);

    water *=
        1.0f
        - clamp(@evaporation, 0.0f, 1.0f);

    water =
        clamp(
            water,
            0.0f,
            fmax(@max_water, 0.0f)
        );

    //----------------------------------------------------------------------
    // FLOW-DIRECTIONAL HEIGHT SAMPLES
    //
    // upstream (-flowDir), current, downstream (+flowDir)
    //----------------------------------------------------------------------

    float sampledist =
        fmax(resscale, 1.0f);

    float hBack = hC;
    float hFront = hC;

    if (speed > 1e-8f)
    {
        hBack =
            @height.bufferSample(
                p - dir * sampledist
            );

        hFront =
            @height.bufferSample(
                p + dir * sampledist
            );
    }

    //----------------------------------------------------------------------
    // FLOW-FACING SHOULDER / DIRECTIONAL RELIEF
    //
    // incoming : upstream stands above us  (we sit on a descending face)
    // outgoing : we stand above downstream  (flow leaves us downhill)
    // shoulder : both true -> a flow-facing slope. It is ZERO at a crest
    //            (incoming = 0) and at a valley (outgoing = 0), so the
    //            exact local maximum is never the erosion target.
    // relief   : total drop across the sample span.
    //----------------------------------------------------------------------

    float incoming = fmax(hBack - hC, 0.0f);
    float outgoing = fmax(hC - hFront, 0.0f);
    float shoulder = fmin(incoming, outgoing);
    float relief   = fmax(hBack - hFront, 0.0f);

    //----------------------------------------------------------------------
    // SUBTLE DIRECTIONAL CONVEX-BREAK MODIFIER
    //
    // Second derivative ALONG the flow, not the isotropic Laplacian that
    // used to attack every convex maximum. Kept deliberately weak.
    //----------------------------------------------------------------------

    float d2 = hBack - 2.0f*hC + hFront;

    float convex_along =
        fmax(
            -d2
            * @height_scale
            * resscale,
            0.0f
        );

    float edgefactor =
        smoothstep(
            @edge_threshold,
            @edge_threshold
                + fmax(@edge_softness, 1e-6f),
            convex_along
        );

    //----------------------------------------------------------------------
    // SEDIMENT CAPACITY
    //
    // Driven by the flow-facing shoulder, not by convexity.
    //----------------------------------------------------------------------

    float slope_term =
        (shoulder + relief * 0.25f)
        * @height_scale
        * resscale;

    float capacity_value =
        water
        * fmax(@capacity, 0.0f)
        * (
            fmax(@base_capacity, 0.0f)
            + slope_term
                * fmax(@slope_capacity, 0.0f)
        )
        * (
            1.0f
            + speed
                * fmax(@speed_capacity, 0.0f)
        );

    // Convexity is now only a small optional nudge (0.15 x edge_wear).
    capacity_value *=
        1.0f
        + edgefactor
        * fmax(@edge_wear, 0.0f)
        * 0.15f;

    //----------------------------------------------------------------------
    // ERODE / DEPOSIT (raw, before wear diffusion)
    //----------------------------------------------------------------------

    float sediment =
        fmax(transported_sed, 0.0f);

    float raw_eroded = 0.0f;
    float deposited = 0.0f;

    float delta =
        capacity_value - sediment;

    if (delta > 0.0f)
    {
        raw_eroded =
            delta
            * fmax(@erosion_rate, 0.0f)
            * fmax(@erodability, 0.0f);

        if (@max_erosion_step > 0.0f)
            raw_eroded =
                fmin(
                    raw_eroded,
                    @max_erosion_step
                );
    }
    else
    {
        deposited =
            (-delta)
            * fmax(@deposition_rate, 0.0f);

        deposited =
            fmin(
                deposited,
                sediment
            );

        if (@max_deposit_step > 0.0f)
            deposited =
                fmin(
                    deposited,
                    @max_deposit_step
                );
    }

    //----------------------------------------------------------------------
    // WEAR / EROSION FIELD DIFFUSION
    //
    // Smooth the erosion field across the 4-neighbourhood BEFORE it is
    // removed from the height. The height itself is never blurred. The
    // field is read from the previous iteration (lagged), which is stable
    // and cheap; over iterations it turns razor incisions into chamfers.
    //----------------------------------------------------------------------

    float eroded = raw_eroded;

    if (@Iteration > 0 &&
        @wear_diffusion > 0.0f)
    {
        float eL =
            @erosion.bufferIndex(xy + (int2)(-1, 0));

        float eR =
            @erosion.bufferIndex(xy + (int2)( 1, 0));

        float eD =
            @erosion.bufferIndex(xy + (int2)( 0,-1));

        float eU =
            @erosion.bufferIndex(xy + (int2)( 0, 1));

        float eavg =
            (eL + eR + eD + eU) * 0.25f;

        eroded =
            mix(
                raw_eroded,
                eavg,
                clamp(@wear_diffusion, 0.0f, 1.0f)
            );
    }

    //----------------------------------------------------------------------
    // THERMAL / TALUS — LOCAL REDISTRIBUTION
    //
    // Material slides to lower neighbours and arrives from higher ones.
    // Each pair transfer is counted once as outflow and once as inflow, so
    // nothing disappears and the total is conserved. No convexity boost.
    //----------------------------------------------------------------------

    float hs =
        fmax(@height_scale * resscale, 1e-6f);

    float talus =
        fmax(@talus, 0.0f) / hs;

    float thermal_k =
        clamp(@thermal, 0.0f, 1.0f) * 0.2f;

    float thermal_cap =
        @max_thermal_step > 0.0f
        ? @max_thermal_step
        : 1e9f;

    float thermal_out =
        thermal_pair(hC - hL, talus, thermal_k, thermal_cap) +
        thermal_pair(hC - hR, talus, thermal_k, thermal_cap) +
        thermal_pair(hC - hD, talus, thermal_k, thermal_cap) +
        thermal_pair(hC - hU, talus, thermal_k, thermal_cap);

    float thermal_in =
        thermal_pair(hL - hC, talus, thermal_k, thermal_cap) +
        thermal_pair(hR - hC, talus, thermal_k, thermal_cap) +
        thermal_pair(hD - hC, talus, thermal_k, thermal_cap) +
        thermal_pair(hU - hC, talus, thermal_k, thermal_cap);

    //----------------------------------------------------------------------
    // SIGNED HEIGHT UPDATE
    //----------------------------------------------------------------------

    float hnew =
        hC
        - eroded
        - thermal_out
        + thermal_in
        + deposited;

    //----------------------------------------------------------------------
    // SEDIMENT BALANCE
    // Hydraulic erosion adds, deposition removes. Thermal moves material
    // directly in the height field, so it does not touch sediment.
    //----------------------------------------------------------------------

    sediment += eroded - deposited;

    //----------------------------------------------------------------------
    // CUMULATIVE DEBUG / OUTPUT FIELDS
    //----------------------------------------------------------------------

    float wear =
        oldwear
        + eroded
        + thermal_out;

    float dep =
        olddeposit
        + deposited;

    //----------------------------------------------------------------------
    // WRITE SCRATCH
    //----------------------------------------------------------------------

    @height_tmp.set(hnew);
    @flow_tmp.set(v);
    @water_tmp.set(water);
    @sediment_tmp.set(sediment);
    @wear_tmp.set(wear);
    @deposit_tmp.set(dep);
    @erosion_tmp.set(eroded);
}


@WRITEBACK
{
    int2 xy = (int2)(@ix, @iy);

    @height.set(
        @height_tmp.bufferIndex(xy)
    );

    @flow.set(
        @flow_tmp.bufferIndex(xy)
    );

    @water.set(
        @water_tmp.bufferIndex(xy)
    );

    @sediment.set(
        @sediment_tmp.bufferIndex(xy)
    );

    @wear.set(
        @wear_tmp.bufferIndex(xy)
    );

    @deposit.set(
        @deposit_tmp.bufferIndex(xy)
    );

    @erosion.set(
        @erosion_tmp.bufferIndex(xy)
    );
}
