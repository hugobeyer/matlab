#runover attribute

#bind point &P float3
#bind point N float3

#bind point &_grav float3 name=_grav
#bind point _curv float name=_curv
#bind point _shadow_mask float name=_shadow_mask
#bind point _ao_mask float name=_ao_mask
#bind point _thickness_mask float name=_thickness_mask

#bind point &_fold_mask float name=_fold_mask
#bind point &_fold_disp float name=_fold_disp

#bind detail _avg_edge_length float name=_avg_edge_length

#bind parm fold_side int val=0
#bind parm mask_mode int val=0
#bind parm contrast float val=0.75
#bind parm mask_gain float val=1.0

#bind parm ao_mask_weight float val=0.0
#bind parm thickness_mask_weight float val=0.0

#bind parm amount_edges float val=0.2
#bind parm signed_low float val=0.0
#bind parm signed_high float val=1.0
#bind parm gate_signed int val=1
#bind parm max_disp_edges float val=0.35

#bind parm normal_weight float val=1.0
#bind parm gravity_weight float val=0.25
#bind parm normalize_direction int val=1
#bind parm mask_normal int val=1
#bind parm write_grav int val=1

@KERNEL
{
    float avg = @_avg_edge_length;
    if (avg <= 1e-8f)
        avg = 1.0f;

    float curv = fmax(0.0f, fmin(1.0f, @_curv));
    float raw_shadow = @_shadow_mask;
    float shadow = raw_shadow;

    // fold_side:
    // 0 = Both Sides: all ridges and valleys
    // 1 = Upper Folds: positive Laplacian side only
    // 2 = Lower Folds: negative Laplacian side only
    // 3 = Soft Signed: broad signed gradient remapped to 0..1
    if (@fold_side == 0)
        shadow = fabs(raw_shadow);
    else if (@fold_side == 1)
        shadow = fmax(raw_shadow, 0.0f);
    else if (@fold_side == 2)
        shadow = fmax(-raw_shadow, 0.0f);
    else if (@fold_side == 3)
        shadow = 0.5f + 0.5f * raw_shadow;

    shadow = fmax(0.0f, fmin(1.0f, shadow));

    float mask = shadow * curv;

    // mask_mode:
    // 0 = shadow * curv
    // 1 = shadow only
    // 2 = curv only
    // 3 = max(shadow, curv)
    // 4 = add, clamped
    if (@mask_mode == 1)
        mask = shadow;
    else if (@mask_mode == 2)
        mask = curv;
    else if (@mask_mode == 3)
        mask = fmax(shadow, curv);
    else if (@mask_mode == 4)
        mask = fmax(0.0f, fmin(1.0f, shadow + curv));

    mask = fmax(0.0f, fmin(1.0f, mask));
    mask = pow(mask, fmax(@contrast, 1e-4f));
    mask *= fmax(@mask_gain, 0.0f);

    float ao = fmax(0.0f, fmin(1.0f, @_ao_mask));
    float thickness = fmax(0.0f, fmin(1.0f, @_thickness_mask));
    mask *= 1.0f + (ao - 1.0f) * fmax(0.0f, fmin(1.0f, @ao_mask_weight));
    mask *= 1.0f + (thickness - 1.0f) * fmax(0.0f, fmin(1.0f, @thickness_mask_weight));

    mask = fmax(0.0f, fmin(1.0f, mask));
    @_fold_mask.set(mask);

    float signed_mask = @signed_low + mask * (@signed_high - @signed_low);
    if (@gate_signed)
        signed_mask *= mask;

    float disp = @amount_edges * avg * signed_mask;
    float max_disp = fmax(@max_disp_edges, 0.0f) * avg;
    disp = fmax(-max_disp, fmin(max_disp, disp));

    @_fold_disp.set(disp);

    float3 Nn = @N;
    float nl = length(Nn);
    if (nl > 1e-8f)
        Nn /= nl;
    else
        Nn = (float3)(0.0f, 1.0f, 0.0f);

    float3 G = @_grav;
    float gl = length(G);
    if (gl > 1e-8f)
        G /= gl;
    else
        G = (float3)(0.0f, -1.0f, 0.0f);

    float normal_mask = 1.0f;
    if (@mask_normal)
        normal_mask = fabs(signed_mask);

    float3 dir = G * @gravity_weight + Nn * @normal_weight * normal_mask;

    if (@normalize_direction)
    {
        float dl = length(dir);
        if (dl > 1e-8f)
            dir /= dl;
        else
            dir = G;
    }

    if (@write_grav)
        @_grav.set(dir);

    @P.set(@P + dir * disp);
}
