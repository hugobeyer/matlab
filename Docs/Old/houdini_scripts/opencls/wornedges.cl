#runover layer

#bind layer height float border=WRAP
#bind layer ids int border=WRAP

#bind layer !&eroded float
#bind layer !&erosion float

#bind parm radius int val=24
#bind parm slope float val=0.05
#bind parm strength float val=1.0
#bind parm feather float val=0.35

#bind parm directions int val=24
#bind parm angular_aa float val=0.35

#bind parm gravity float val=0.0
#bind parm gravity_angle float val=-90.0

#bind parm seed int val=1

#bind parm macro_scale int val=4
#bind parm macro_amount float val=0.30

#bind parm cell_scale int val=9
#bind parm cell_amount float val=0.25

#bind parm ridge_scale int val=14
#bind parm ridge_amount float val=0.20

#bind parm micro_scale int val=32
#bind parm micro_amount float val=0.12

#bind parm warp_scale int val=3
#bind parm warp_amount float val=0.30

#bind parm noise_contrast float val=1.20

#bind parm id_variation float val=0.35
#bind parm id_radius float val=0.40
#bind parm id_slope float val=0.30
#bind parm id_strength float val=0.30
#bind parm id_noise float val=0.35

uint er_hash(uint x)
{
    x^=x>>16;
    x*=0x7feb352du;
    x^=x>>15;
    x*=0x846ca68bu;
    x^=x>>16;
    return x;
}

float er_u01(uint h)
{
    return
        (float)(h&0x00ffffffu)*
        (1.0f/16777216.0f);
}

uint er_idhash(
    int id,
    int seed,
    uint salt)
{
    return er_hash(
        (uint)id ^
        (uint)seed*0x9e3779b9u ^
        salt);
}

float er_idrand(
    int id,
    int seed,
    uint salt)
{
    return
        er_u01(
            er_idhash(
                id,
                seed,
                salt));
}

int er_wrap(int x,int n)
{
    int r=x%n;
    return r<0?r+n:r;
}

float er_rand(int2 p,int seed)
{
    uint h=
        (uint)p.x*0x8da6b343u ^
        (uint)p.y*0xd8163841u ^
        (uint)seed*0xcb1ab31fu;

    return
        er_u01(
            er_hash(h));
}

float2 er_rand2(int2 p,int seed)
{
    uint h=
        er_hash(
            (uint)p.x*0x9e3779b9u ^
            (uint)p.y*0x85ebca6bu ^
            (uint)seed*0xc2b2ae35u);

    uint h2=
        er_hash(
            h^0x68bc21ebu);

    return (float2)(
        er_u01(h),
        er_u01(h2));
}

float2 er_grad(
    int2 p,
    int cells,
    int seed)
{
    int2 w=(int2)(
        er_wrap(p.x,cells),
        er_wrap(p.y,cells));

    uint h=
        er_hash(
            (uint)w.x*0x9e3779b9u ^
            (uint)w.y*0x85ebca6bu ^
            (uint)seed*0xc2b2ae35u);

    switch(h&7u)
    {
        case 0:return (float2)( 1.0f,0.0f);
        case 1:return (float2)(-1.0f,0.0f);
        case 2:return (float2)(0.0f, 1.0f);
        case 3:return (float2)(0.0f,-1.0f);
        case 4:return (float2)( 0.70710678f, 0.70710678f);
        case 5:return (float2)(-0.70710678f, 0.70710678f);
        case 6:return (float2)( 0.70710678f,-0.70710678f);
        default:return (float2)(-0.70710678f,-0.70710678f);
    }
}

float er_gradnoise(
    float2 uv,
    int cells,
    int seed)
{
    cells=max(cells,1);

    float2 p=
        uv*(float)cells;

    int2 ip=
        convert_int2(
            floor(p));

    float2 f=
        p-
        convert_float2(ip);

    float2 u=
        f*f*f*
        (f*(f*6.0f-15.0f)+10.0f);

    float n00=
        dot(
            er_grad(
                ip+(int2)(0,0),
                cells,
                seed),
            f-(float2)(0.0f,0.0f));

    float n10=
        dot(
            er_grad(
                ip+(int2)(1,0),
                cells,
                seed),
            f-(float2)(1.0f,0.0f));

    float n01=
        dot(
            er_grad(
                ip+(int2)(0,1),
                cells,
                seed),
            f-(float2)(0.0f,1.0f));

    float n11=
        dot(
            er_grad(
                ip+(int2)(1,1),
                cells,
                seed),
            f-(float2)(1.0f,1.0f));

    float nx0=
        n00+
        (n10-n00)*u.x;

    float nx1=
        n01+
        (n11-n01)*u.x;

    return clamp(
        0.5f+
        (nx0+(nx1-nx0)*u.y)*
        0.70710678f,
        0.0f,
        1.0f);
}

float er_fbm(
    float2 uv,
    int cells,
    int seed)
{
    cells=max(cells,1);

    float n=0.0f;
    float w=0.0f;
    float a=0.50f;

    for(int o=0;o<4;o++)
    {
        n+=
            er_gradnoise(
                uv,
                cells,
                seed+o*137)*
            a;

        w+=a;
        cells*=2;
        a*=0.50f;
    }

    return
        n/
        fmax(w,1.0e-6f);
}

float er_ridged(
    float2 uv,
    int cells,
    int seed)
{
    cells=max(cells,1);

    float n=0.0f;
    float w=0.0f;
    float a=0.55f;

    for(int o=0;o<4;o++)
    {
        float q=
            er_gradnoise(
                uv,
                cells,
                seed+o*193);

        float r=
            1.0f-
            fabs(
                q*2.0f-1.0f);

        r*=r;

        n+=r*a;
        w+=a;

        cells*=2;
        a*=0.52f;
    }

    return
        n/
        fmax(w,1.0e-6f);
}

float er_cheby_cellular(
    float2 uv,
    int cells,
    int seed)
{
    cells=max(cells,1);

    float2 p=
        uv*
        (float)cells;

    int2 base=
        convert_int2(
            floor(p));

    float best=
        1.0e20f;

    float second=
        1.0e20f;

    for(int oy=-1;oy<=1;oy++)
    {
        for(int ox=-1;ox<=1;ox++)
        {
            int2 c=
                base+
                (int2)(ox,oy);

            int2 wc=(int2)(
                er_wrap(c.x,cells),
                er_wrap(c.y,cells));

            float2 r=
                0.10f+
                er_rand2(
                    wc,
                    seed)*
                0.80f;

            float2 fp=
                convert_float2(c)+
                r;

            float2 d=
                fabs(
                    p-fp);

            float dist=
                fmax(
                    d.x,
                    d.y);

            if(dist<best)
            {
                second=best;
                best=dist;
            }
            else if(dist<second)
            {
                second=dist;
            }
        }
    }

    float body=
        1.0f-
        smoothstep(
            0.10f,
            0.82f,
            best);

    float interior=
        clamp(
            (second-best)*
            1.55f,
            0.0f,
            1.0f);

    float corner=
        1.0f-
        smoothstep(
            0.05f,
            0.42f,
            second-best);

    return clamp(
        body*0.42f+
        interior*0.43f+
        corner*0.15f,
        0.0f,
        1.0f);
}

float2 er_warp(
    float2 uv,
    int scale,
    int seed,
    float amount)
{
    scale=max(scale,1);

    float2 w=(float2)(
        er_fbm(
            uv,
            scale,
            seed+701)*2.0f-1.0f,

        er_fbm(
            uv,
            scale,
            seed+1301)*2.0f-1.0f);

    float2 q=
        uv+
        w*
        (
            amount/
            (float)scale
        );

    return
        q-
        floor(q);
}

float er_shape_signed(
    float x,
    float contrast)
{
    x=
        clamp(
            x,
            -1.0f,
            1.0f);

    float a=
        pow(
            fabs(x),
            1.0f/
            fmax(
                contrast,
                0.05f));

    return
        x<0.0f
        ?-a
        :a;
}

@KERNEL
{
    int2 px=@ixy;

    float h0=
        @height.bufferIndex(px);

    int rid=
        @ids.bufferIndex(px);

    float2 res=
        convert_float2(@res);

    float2 uv=
        (
            convert_float2(px)+
            (float2)(0.5f,0.5f)
        )/
        res;

    uv=
        uv-
        floor(uv);

    float idAmount=
        rid!=0
        ?clamp(
            @id_variation,
            0.0f,
            1.0f)
        :0.0f;

    float idR0=
        er_idrand(
            rid,
            @seed+17,
            0x51ed270bu)*
        2.0f-1.0f;

    float idR1=
        er_idrand(
            rid,
            @seed+29,
            0xd8163841u)*
        2.0f-1.0f;

    float idR2=
        er_idrand(
            rid,
            @seed+43,
            0xcb1ab31fu)*
        2.0f-1.0f;

    float idR3=
        er_idrand(
            rid,
            @seed+71,
            0x8da6b343u)*
        2.0f-1.0f;

    int baseRadius=
        clamp(
            @radius,
            1,
            64);

    float idRadiusMul=
        fmax(
            1.0f+
            idR0*
            idAmount*
            @id_radius,
            0.15f);

    int radius=
        clamp(
            (int)floor(
                (float)baseRadius*
                idRadiusMul+
                0.5f),
            1,
            64);

    int dcount=
        clamp(
            @directions,
            8,
            32);

    float2 nuv=
        er_warp(
            uv,
            max(@warp_scale,1),
            @seed+41,
            fmax(
                @warp_amount,
                0.0f));

    float macro=
        er_fbm(
            nuv,
            max(@macro_scale,1),
            @seed+11)*
        2.0f-1.0f;

    float cell=
        er_cheby_cellular(
            nuv,
            max(@cell_scale,1),
            @seed+173)*
        2.0f-1.0f;

    float ridge=
        er_ridged(
            nuv,
            max(@ridge_scale,1),
            @seed+349)*
        2.0f-1.0f;

    float micro=
        er_fbm(
            uv,
            max(@micro_scale,1),
            @seed+613)*
        2.0f-1.0f;

    float idNoise=
        idAmount*
        @id_noise;

    float macroWeight=
        @macro_amount*
        fmax(
            1.0f+
            idR1*
            idNoise,
            0.0f);

    float cellWeight=
        @cell_amount*
        fmax(
            1.0f-
            idR1*
            idNoise,
            0.0f);

    float ridgeWeight=
        @ridge_amount*
        fmax(
            1.0f+
            idR3*
            idNoise,
            0.0f);

    float microWeight=
        @micro_amount*
        fmax(
            1.0f-
            idR3*
            idNoise,
            0.0f);

    float noise=
        macro*macroWeight+
        cell*cellWeight+
        ridge*ridgeWeight+
        micro*microWeight;

    noise=
        er_shape_signed(
            noise,
            fmax(
                @noise_contrast,
                0.05f));

    float resistance=
        fmax(
            1.0f+
            noise,
            0.06f);

    float radiusNoise=
        macro*
        macroWeight*
        0.65f+

        cell*
        cellWeight*
        0.85f+

        ridge*
        ridgeWeight*
        0.25f;

    float radiusScale=
        clamp(
            1.0f-
            radiusNoise,
            0.30f,
            1.80f);

    int localRadius=
        clamp(
            (int)floor(
                (float)radius*
                radiusScale+
                0.5f),
            1,
            64);

    float idSlopeMul=
        fmax(
            1.0f+
            idR1*
            idAmount*
            @id_slope,
            0.10f);

    float localSlope=
        fmax(
            @slope,
            0.0f)*
        resistance*
        idSlopeMul;

    float strengthNoise=
        1.0f-
        ridge*
        ridgeWeight*
        0.40f-
        micro*
        microWeight*
        0.35f;

    float idStrengthMul=
        fmax(
            1.0f+
            idR2*
            idAmount*
            @id_strength,
            0.0f);

    float localStrength=
        clamp(
            @strength,
            0.0f,
            1.0f)*
        clamp(
            strengthNoise,
            0.20f,
            1.50f)*
        idStrengthMul;

    float gravity=
        clamp(
            @gravity,
            0.0f,
            1.0f);

    float ga=
        @gravity_angle*
        0.017453292519943295f;

    float2 gdir=
        (float2)(
            cos(ga),
            sin(ga));

    float best0=h0;
    float best1=h0;
    float best2=h0;
    float best3=h0;

    const float TAU=
        6.28318530717958647692f;

    float angleOffset=
        (
            er_u01(
                er_hash(
                    (uint)@seed*
                    0x9e3779b9u
                )
            )-
            0.5f
        )*
        (
            TAU/
            (float)dcount
        );

    for(int d=0;d<dcount;d++)
    {
        float a=
            TAU*
            (
                (float)d/
                (float)dcount
            )+
            angleOffset;

        float2 ndir=
            (float2)(
                cos(a),
                sin(a));

        float align=
            dot(
                ndir,
                gdir);

        float gravitySlope=
            fmax(
                1.0f-
                0.70f*
                gravity*
                align,
                0.10f);

        float directionalSlope=
            localSlope*
            gravitySlope;

        float dirTarget=h0;

        int2 previousOff=
            (int2)(0,0);

        for(int s=1;s<=localRadius;s++)
        {
            int2 off=
                convert_int2_rte(
                    ndir*
                    (float)s);

            if(
                off.x==0 &&
                off.y==0
            )
                continue;

            if(
                off.x==previousOff.x &&
                off.y==previousOff.y
            )
                continue;

            previousOff=off;

            float dist=
                length(
                    convert_float2(off));

            if(
                dist>
                (float)localRadius+
                0.5f
            )
                continue;

            float hn=
                @height.bufferIndex(
                    px+off);

            float dn=
                dist/
                (float)localRadius;

            float allowed=
                hn+
                directionalSlope*
                dn;

            float excess=
                h0-
                allowed;

            if(excess<=0.0f)
                continue;

            float fw=1.0f;

            if(@feather>0.0f)
            {
                float featherWidth=
                    fmax(
                        directionalSlope*
                        @feather,
                        1.0e-6f);

                fw=
                    smoothstep(
                        0.0f,
                        featherWidth,
                        excess);
            }

            float candidate=
                h0-
                excess*
                fw;

            dirTarget=
                fmin(
                    dirTarget,
                    candidate);
        }

        if(dirTarget<best0)
        {
            best3=best2;
            best2=best1;
            best1=best0;
            best0=dirTarget;
        }
        else if(dirTarget<best1)
        {
            best3=best2;
            best2=best1;
            best1=dirTarget;
        }
        else if(dirTarget<best2)
        {
            best3=best2;
            best2=dirTarget;
        }
        else if(dirTarget<best3)
        {
            best3=dirTarget;
        }
    }

    float aa=
        clamp(
            @angular_aa,
            0.0f,
            1.0f);

    float averaged=
        (
            best0+
            best1+
            best2+
            best3
        )*
        0.25f;

    float target=
        best0+
        (averaged-best0)*
        aa;

    float amount=
        fmax(
            h0-target,
            0.0f);

    amount*=
        localStrength;

    @eroded.setIndex(
        px,
        h0-amount);

    @erosion.setIndex(
        px,
        amount);
}