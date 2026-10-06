#bind layer size_ref? float port=size_ref nouipromote
#bind layer !&dst float
#bind layer !&flow float2
#bind layer !&row_id int
#bind layer !&block_id int

#bind parm count_x int val=8
#bind parm count_y int val=6
#bind parm density float val=0.88
#bind parm size_min float val=0.75
#bind parm size_max float val=1.35
#bind parm size_aspect float val=0.78
#bind parm jitter float val=0.18
#bind parm flow_variation float val=0.35
#bind parm height_min float val=0.18
#bind parm height_max float val=0.90
#bind parm steps int val=0
#bind parm rotation float val=0
#bind parm lean_x float val=0
#bind parm lean_y float val=0
#bind parm formation_cells int val=3
#bind parm formation_amount float val=0.30
#bind parm quarter_copies int val=1
#bind parm quarter_y_count int val=1
#bind parm quarter_fill float val=0.55
#bind parm quarter_size float val=0.82
#bind parm quarter_height float val=0.35
#bind parm quarter_jitter_x float val=0.10
#bind parm quarter_jitter_y float val=0.10
#bind parm sides int val=4
#bind parm shape_random int val=0
#bind parm camera_yaw float val=0.5
#bind parm camera_pitch float val=0.784
#bind parm view_scale float val=1.55
#bind parm depth_min float val=-0.5
#bind parm depth_max float val=1.0
#bind parm seed float val=1234

static float frac1(float x){ return x-floor(x); }

static float hash11(float x)
{
    x=frac1(x*0.1031f);
    x*=x+33.33f;
    x*=x+x;
    return frac1(x);
}

static float hash21(int2 p,float seed){ return hash11((float)p.x*127.1f+(float)p.y*311.7f+seed*74.17f); }
static float rand_cell(int2 p,float seed,float salt){ return hash21(p,seed+salt); }
static float rand_signed(int2 p,float seed,float salt){ return rand_cell(p,seed,salt)*2.0f-1.0f; }

static float2 rotate2(float2 p,float a)
{
    float c=cos(a),s=sin(a);
    return (float2)(c*p.x-s*p.y,s*p.x+c*p.y);
}

static float quantized01(float x,int steps)
{
    x=clamp(x,0.0f,1.0f);
    if(steps<=1) return x;
    float n=(float)(steps-1);
    return floor(x*n+0.5f)/n;
}

static float ray_prism(
    float3 ro,float3 rd,float2 extent,int sides,float2 step_rot,
    float zmin,float zmax,float jag,float seed)
{
    float ex=fmax(extent.x,1e-8f),ey=fmax(extent.y,1e-8f);
    float2 nro=(float2)(ro.x/ex,ro.y/ey);
    float2 nrd=(float2)(rd.x/ex,rd.y/ey);
    float2 n=(float2)(1.0f,0.0f),csum=(float2)(0.0f);

    for(int k=0;k<sides;++k)
    {
        float j=1.0f+(hash11(seed+(float)k*12.9898f)*2.0f-1.0f)*jag;
        csum+=(j-1.0f)*n;
        n=(float2)(n.x*step_rot.x-n.y*step_rot.y,n.x*step_rot.y+n.y*step_rot.x);
    }

    nro+=csum*(2.0f/(float)sides);

    float te=-1e20f,tx=1e20f;

    {
        float d=rd.z,num=zmax-ro.z;
        if(fabs(d)<1e-8f){ if(num<0.0f) return -1.0f; }
        else
        {
            float t=num/d;
            if(d>0.0f) tx=fmin(tx,t);
            else te=fmax(te,t);
        }
    }

    {
        float d=rd.z,num=zmin-ro.z;
        if(fabs(d)<1e-8f){ if(num>0.0f) return -1.0f; }
        else
        {
            float t=num/d;
            if(d>0.0f) tx=fmin(tx,t);
            else te=fmax(te,t);
        }
    }

    n=(float2)(1.0f,0.0f);

    for(int k=0;k<sides;++k)
    {
        float j=1.0f+(hash11(seed+(float)k*12.9898f)*2.0f-1.0f)*jag;
        float d=dot(n,nrd),num=j-dot(n,nro);

        if(fabs(d)<1e-8f){ if(num<0.0f) return -1.0f; }
        else
        {
            float t=num/d;
            if(d>0.0f) tx=fmin(tx,t);
            else te=fmax(te,t);
        }

        n=(float2)(n.x*step_rot.x-n.y*step_rot.y,n.x*step_rot.y+n.y*step_rot.x);
    }

    if(te>tx||tx<0.0f) return -1.0f;
    return te>=0.0f?te:tx;
}

@KERNEL
{
    int2 res=convert_int2(@dst.res);
    int nx=max(@count_x,1),ny=max(@count_y,1);
    int qycount=max(@quarter_y_count,1);
    float fq=(float)qycount;

    float2 uv=(convert_float2(@ixy)+0.5f)/convert_float2(res)-0.5f;
    float aspect=(float)res.x/fmax((float)res.y,1.0f);
    uv.x*=aspect;
    uv*=@view_scale;

    float yaw=@camera_yaw*1.57079632679f;
    float pitch=@camera_pitch*0.78539816339f;
    float cp=cos(pitch),sp=sin(pitch),cy=cos(yaw),sy=sin(yaw);

    float3 rd=normalize((float3)(cp*cy,cp*sy,-sp));
    float3 up=(float3)(0.0f,0.0f,1.0f);
    float3 right=normalize(cross(rd,up));
    float3 cup=normalize(cross(right,rd));

    float hmin=fmin(@height_min,@height_max);
    float hmax=fmax(@height_min,@height_max);
    float formation=clamp(@formation_amount,0.0f,1.0f);
    float qheight=fmax(@quarter_height,0.0f);

    float quarter_possible_max=(hmax/fq)*(1.0f+qheight);
    float global_hmax=fmax(fmax(hmax,quarter_possible_max),1e-4f)*1.05f;

    float cam_dist=2.0f+global_hmax;
    float3 target=(float3)(0.5f,0.5f,0.0f);
    float3 ro=target-rd*cam_dist+right*uv.x+cup*uv.y;

    float3 tile_x=right*(aspect*@view_scale);
    float3 tile_y=cup*@view_scale;

    float density=clamp(@density,0.0f,1.0f);
    float qfill=clamp(@quarter_fill,0.0f,1.0f);
    float qsize=fmax(@quarter_size,0.01f);
    float qjx=clamp(@quarter_jitter_x,0.0f,0.48f);
    float qjy=clamp(@quarter_jitter_y,0.0f,0.48f);

    float jit=fmax(@jitter,0.0f);
    float posjx=clamp(jit,0.0f,0.48f),posjy=clamp(jit,0.0f,0.48f);
    float sidejag=clamp(jit*1.1f,0.0f,0.80f);
    float rotjit=jit*39.0f,leanjit=jit*0.55f;

    float sxmin=fmax(fmin(@size_min,@size_max),0.02f);
    float sxmax=fmax(fmax(@size_min,@size_max),sxmin);
    float saspect=fmax(@size_aspect,0.02f);
    float symin=fmax(sxmin*saspect,0.02f);
    float symax=fmax(sxmax*saspect,symin);

    int height_steps=max(@steps,0),angle_steps=max(@steps,0);
    float out_near=fmax(@depth_min,@depth_max),out_far=fmin(@depth_min,@depth_max);
    float dnear=cam_dist*0.5f,dfar=cam_dist*1.5f;

    int base_sides=clamp(@sides,3,12);
    float ba=6.28318530718f/(float)base_sides;
    float2 rot_base=(float2)(cos(ba),sin(ba));
    float2 rot4=(float2)(0.0f,1.0f);
    float2 rot6=(float2)(0.5f,0.86602540378f);
    float2 rot8=(float2)(0.70710678118f,0.70710678118f);

    int cluster_cells=max(@formation_cells,1);
    float2 cell_size=(float2)(1.0f/(float)nx,1.0f/(float)ny);

    float max_phase_size=fmax(1.0f,qsize);
    float max_half_x=0.5f*cell_size.x*sxmax*max_phase_size;
    float max_half_y=0.5f*cell_size.y*symax*max_phase_size;
    float max_xy_radius=fmax(max_half_x,max_half_y)*(1.0f+sidejag);

    float max_lean_x=fabs(@lean_x)+leanjit;
    float max_lean_y=fabs(@lean_y)+leanjit;
    float lean_radius_x=max_lean_x*global_hmax*0.5f;
    float lean_radius_y=max_lean_y*global_hmax*0.5f;

    int pad_x=max(1,(int)ceil((max_xy_radius+lean_radius_x)/fmax(cell_size.x,1e-8f)+posjx+1.0f));
    int pad_y=max(1,(int)ceil((max_xy_radius+lean_radius_y)/fmax(cell_size.y,1e-8f)+posjy+1.0f));

    float best_t=1e20f;
    int hit=0,best_row=-1,best_block=-1;
    float2 best_flow=(float2)(0.0f);

    int y_sub=4*qycount;
    int phase_count=@quarter_copies?4*y_sub:1;

    for(int phase=0;phase<phase_count;++phase)
    {
        int xi=phase&3;
        int yi=phase>>2;

        float phase_x=(float)xi*0.25f;
        float phase_y=(float)yi/(float)y_sub;
        int secondary=phase!=0;

        if(secondary)
        {
            phase_x+=(hash11((float)phase*5.31f+@seed*0.611f)*2.0f-1.0f)*qjx;
            phase_y+=(hash11((float)phase*9.73f+@seed*0.917f)*2.0f-1.0f)*qjy;
        }

        float phase_density=secondary?density*qfill:density;
        float phase_size=secondary?qsize:1.0f;
        float phase_seed=@seed+(float)phase*971.371f;

        for(int copy_y=-1;copy_y<=1;++copy_y)
        for(int copy_x=-1;copy_x<=1;++copy_x)
        {
            float fx=(float)copy_x+phase_x;
            float fy=(float)copy_y+phase_y;
            float3 copy_shift=tile_x*fx+tile_y*fy;
            float3 cro=ro-copy_shift;

            float rz=fabs(rd.z)>1e-6f?rd.z:-1e-6f;
            float zlo=-global_hmax*0.5f,zhi=global_hmax*0.5f;
            float t0=(zlo-cro.z)/rz,t1=(zhi-cro.z)/rz;

            float2 rp0=cro.xy+rd.xy*t0;
            float2 rp1=cro.xy+rd.xy*t1;
            float2 rg0=fmin(rp0,rp1),rg1=fmax(rp0,rp1);

            int mincx=(int)floor(rg0.x*(float)nx)-pad_x;
            int maxcx=(int)floor(rg1.x*(float)nx)+pad_x;
            int mincy=(int)floor(rg0.y*(float)ny)-pad_y;
            int maxcy=(int)floor(rg1.y*(float)ny)+pad_y;

            mincx=max(mincx,0);
            maxcx=min(maxcx,nx-1);
            mincy=max(mincy,0);
            maxcy=min(maxcy,ny-1);

            if(mincx>maxcx||mincy>maxcy) continue;

            for(int cyi=mincy;cyi<=maxcy;++cyi)
            for(int cxi=mincx;cxi<=maxcx;++cxi)
            {
                int2 cell=(int2)(cxi,cyi);
                int2 cluster=(int2)(cxi/cluster_cells,cyi/cluster_cells);

                float cr=rand_signed(cluster,phase_seed,701.37f);
                float local_density=clamp(phase_density+cr*formation*0.35f,0.0f,1.0f);
                if(rand_cell(cell,phase_seed,11.31f)>local_density) continue;

                float2 pj=(float2)(
                    rand_signed(cell,phase_seed,17.13f)*posjx,
                    rand_signed(cell,phase_seed,31.71f)*posjy);

                float2 centre=(convert_float2(cell)+0.5f+pj)*cell_size;
                float cluster_size=1.0f+cr*formation*0.30f;

                float sx=mix(sxmin,sxmax,rand_cell(cell,phase_seed,47.93f))*cluster_size*phase_size;
                float syv=mix(symin,symax,rand_cell(cell,phase_seed,63.47f))*cluster_size*phase_size;

                float hx=0.5f*cell_size.x*sx;
                float hy=0.5f*cell_size.y*syv;

                float hr=rand_cell(cell,phase_seed,149.71f);
                hr=clamp(hr+cr*formation*0.30f,0.0f,1.0f);
                hr=quantized01(hr,height_steps);

                float height=mix(hmin,hmax,hr);

                if(secondary)
                {
                    float qr=rand_signed(cell,phase_seed,331.17f);
                    height/=fq;
                    height*=1.0f+qr*qheight;
                    height=fmax(height,1e-4f);
                }

                float angle=(@rotation*180.0f+rand_signed(cell,phase_seed,173.27f)*rotjit)*0.0174532925199433f;

                if(angle_steps>0)
                {
                    float astep=6.28318530718f/(float)angle_steps;
                    angle=floor(angle/astep+0.5f)*astep;
                }

                float2 lean=(float2)(@lean_x,@lean_y);
                lean+=(float2)(
                    rand_signed(cell,phase_seed,197.51f),
                    rand_signed(cell,phase_seed,223.73f))*leanjit;

                float3 rel=cro-(float3)(centre.x,centre.y,0.0f);
                float2 lroxy=rel.xy-lean*rel.z;
                float2 lrdxy=rd.xy-lean*rd.z;

                lroxy=rotate2(lroxy,-angle);
                lrdxy=rotate2(lrdxy,-angle);

                float3 lro=(float3)(lroxy.x,lroxy.y,rel.z);
                float3 lrd=(float3)(lrdxy.x,lrdxy.y,rd.z);

                int cell_sides=base_sides;
                float2 step_rot=rot_base;

                if(@shape_random)
                {
                    float sr=rand_cell(cell,phase_seed,251.17f);
                    cell_sides=4+2*(int)(sr*3.0f);
                    step_rot=cell_sides==4?rot4:(cell_sides==6?rot6:rot8);
                }

                float side_seed=rand_cell(cell,phase_seed,269.83f)*1000.0f;

                float t=ray_prism(
                    lro,lrd,(float2)(hx,hy),cell_sides,step_rot,
                    -height*0.5f,height*0.5f,sidejag,side_seed);

                if(t>=0.0f&&t<best_t)
                {
                    best_t=t;
                    hit=1;
                    best_row=yi;
                    best_block=phase*(nx*ny)+cyi*nx+cxi;

                    float fa=angle+rand_signed(cell,phase_seed,307.11f)*clamp(@flow_variation,0.0f,3.14159265f);
                    float2 axis=rotate2((float2)(1.0f,0.0f),fa);
                    float3 fw=(float3)(axis.x,axis.y,0.0f)+(float3)(lean.x,lean.y,0.0f)*0.35f;
                    float2 fp=(float2)(dot(fw,right),dot(fw,cup));

                    best_flow=fp/fmax(length(fp),1e-6f);
                }
            }
        }
    }

    float outv=0.0f;

    if(hit)
    {
        float depth01=clamp((best_t-dnear)/fmax(dfar-dnear,1e-6f),0.0f,1.0f);
        outv=mix(out_near,out_far,depth01);
    }

    @dst.set(outv);
    @flow.set(hit?best_flow:(float2)(0.0f));
    @row_id.set(best_row);
    @block_id.set(best_block);
}