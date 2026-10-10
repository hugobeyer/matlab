#runover attribute

#bind point P float3
#bind point N float3
#bind point neighs int[] name=topo:neighbours

#bind point &_curv float name=_curv
#bind point &_shadow_mask float name=_shadow_mask
#bind point &_shadow_tmp float name=_shadow_tmp

#bind detail _avg_edge_length float name=_avg_edge_length

#bind parm height_axis int val=1
#bind parm curv_gain float val=8.0
#bind parm curv_power float val=1.0

#bind parm lap_gain float val=1.0
#bind parm lap_bias float val=0.0
#bind parm lap_abs int val=0
#bind parm mask_min float val=-1.0
#bind parm mask_max float val=1.0

#bind parm diffusion_rate float val=0.25
#bind parm blur_radius_edges float val=2.0
#bind parm base_blur_speed float val=0.05
#bind parm curv_blur_speed float val=1.0

@KERNEL
{
    float avg = @_avg_edge_length;
    if (avg <= 1e-8f)
        avg = 1.0f;

    int n = @neighs.entries;

    if (@Iteration == 0)
    {
        float3 P0 = @P;

        float3 N0 = @N;
        float nl = length(N0);
        if (nl > 1e-8f)
            N0 /= nl;
        else
            N0 = (float3)(0.0f, 1.0f, 0.0f);

        float p0_axis = P0.y;
        if (@height_axis == 0)
            p0_axis = P0.x;
        else if (@height_axis == 2)
            p0_axis = P0.z;

        float lap = 0.0f;
        float lapw = 0.0f;
        float k = 0.0f;
        float kw = 0.0f;

        for (int i = 0; i < n; i++)
        {
            int pt = @neighs.comp(i);

            float3 Pn = @P.getAt(pt);
            float3 Nn = @N.getAt(pt);
            float3 dp = Pn - P0;
            float edge_len = length(dp);

            if (edge_len <= 1e-8f)
                continue;

            float pn_axis = Pn.y;
            if (@height_axis == 0)
                pn_axis = Pn.x;
            else if (@height_axis == 2)
                pn_axis = Pn.z;

            float w = avg / edge_len;
            lap += w * (pn_axis - p0_axis);
            lapw += w;

            float nln = length(Nn);
            if (nln > 1e-8f)
                Nn /= nln;
            else
                Nn = N0;

            k += w * length(Nn - N0) * (avg / edge_len);
            kw += w;
        }

        if (lapw > 1e-8f)
            lap /= lapw;

        if (kw > 1e-8f)
            k /= kw;

        float curv = 1.0f - exp(-fmax(@curv_gain, 0.0f) * k);
        curv = fmax(0.0f, fmin(1.0f, curv));
        curv = pow(curv, fmax(@curv_power, 1e-4f));

        float seed = lap / avg;
        if (@lap_abs)
            seed = fabs(seed);

        seed = @lap_bias + @lap_gain * seed;
        seed = fmax(@mask_min, fmin(@mask_max, seed));

        @_curv.set(curv);
        @_shadow_tmp.set(seed);
        return;
    }

    float center = @_shadow_mask;
    float ci = fmax(0.0f, @base_blur_speed + @_curv * @curv_blur_speed);

    float accum = 0.0f;
    float wsum = 0.0f;
    float radius = fmax(@blur_radius_edges, 1e-4f);

    for (int i = 0; i < n; i++)
    {
        int pt = @neighs.comp(i);

        float3 Pn = @P.getAt(pt);
        float edge_len = length(Pn - @P);
        if (edge_len <= 1e-8f)
            continue;

        float edge_units = edge_len / avg;
        float w = exp(-(edge_units * edge_units) / (2.0f * radius * radius));

        float nbv = @_shadow_mask.getAt(pt);
        float cj = fmax(0.0f, @base_blur_speed + @_curv.getAt(pt) * @curv_blur_speed);
        float speed = 0.5f * (ci + cj);

        accum += speed * w * (nbv - center);
        wsum += w;
    }

    float out = center;
    if (wsum > 1e-8f)
        out = center + fmax(@diffusion_rate, 0.0f) * accum / wsum;

    out = fmax(@mask_min, fmin(@mask_max, out));
    @_shadow_tmp.set(out);
}

@WRITEBACK
{
    @_shadow_mask.set(@_shadow_tmp);
}
