#runover layer

#bind layer height float border=WRAP
#bind layer roughness float border=WRAP

#bind layer !&guide float border=WRAP
#bind layer !&signal float border=WRAP
#bind layer !&id int
#bind layer !&preview float3
#bind layer !&parent int

#bind parm threshold float val=0.33
#bind parm offset float val=0
#bind parm height_influence float val=1
#bind parm make_preview int val=1

#define UNION_START 4
#define UNION_PASSES 12
#define ROOT_STAGE (UNION_START+UNION_PASSES)

static uint f2ord(float v)
{
    uint u=as_uint(v);
    return (u&0x80000000u)?~u:(u^0x80000000u);
}

static float ord2f(uint o)
{
    uint u=(o&0x80000000u)?(o^0x80000000u):~o;
    return as_float(u);
}

static int qlevel(float v,float t,float o)
{
    return (int)floor((v-o)/fmax(t,1e-7f));
}

static void hook(global volatile int *p,int a,int b)
{
    int pa=p[a],pb=p[b];
    pa=p[pa]; pb=p[pb];

    int hi=max(pa,pb),lo=min(pa,pb);
    if(hi!=lo) atomic_min(&p[hi],lo);
}

static uint hash32(uint x)
{
    x^=x>>16;x*=0x7feb352du;
    x^=x>>15;x*=0x846ca68bu;
    x^=x>>16;
    return x;
}

static float hash01(uint x)
{
    return (float)(hash32(x)&0x00ffffffu)*(1.0f/16777215.0f);
}

static float3 idcolor(int n)
{
    uint x=(uint)n;
    return (float3)(
        hash01(x*3u+1u),
        hash01(x*3u+2u),
        hash01(x*3u+3u)
    );
}

@KERNEL
{
    int idx=@ix+@iy*@xres;

    global float *h01=(global float *)@guide.data;
    global float *r01=(global float *)@signal.data;
    global int *ids=(global int *)@id.data;
    global volatile int *par=(global volatile int *)@parent.data;
    global volatile uint *stats=(global volatile uint *)@parent.data;

    if(@Iteration==0)
    {
        if(idx==0) stats[0]=0xffffffffu;
        if(idx==1) stats[1]=0u;
        if(idx==2) stats[2]=0xffffffffu;
        if(idx==3) stats[3]=0u;
        return;
    }

    if(@Iteration==1)
    {
        float h=@height.bufferIndex(@ixy);
        float r=@roughness.bufferIndex(@ixy);

        if(isfinite(h))
        {
            uint u=f2ord(h);
            atomic_min(&stats[0],u);
            atomic_max(&stats[1],u);
        }

        if(isfinite(r))
        {
            uint u=f2ord(r);
            atomic_min(&stats[2],u);
            atomic_max(&stats[3],u);
        }

        return;
    }

    if(@Iteration==2)
    {
        float hmn=ord2f(stats[0]),hmx=ord2f(stats[1]);
        float rmn=ord2f(stats[2]),rmx=ord2f(stats[3]);

        float h=@height.bufferIndex(@ixy);
        float r=@roughness.bufferIndex(@ixy);

        h01[idx]=hmx>hmn?clamp((h-hmn)/(hmx-hmn),0.0f,1.0f):0.0f;
        r01[idx]=rmx>rmn?clamp((r-rmn)/(rmx-rmn),0.0f,1.0f):0.0f;
        return;
    }

    if(@Iteration==3)
    {
        par[idx]=idx;
        return;
    }

    if(@Iteration>=UNION_START && @Iteration<ROOT_STAGE)
    {
        float th=fmax(clamp(@threshold,0.0f,1.0f)*0.25f,1e-7f);
        float hinf=fmax(@height_influence,0.0f);

        int l=(@ix?@ix-1:@xres-1)+@iy*@xres;
        int u=@ix+(@iy?@iy-1:@yres-1)*@xres;

        int lv=qlevel(r01[idx],th,@offset);

        if(qlevel(r01[l],th,@offset)==lv && fabs(h01[idx]-h01[l])*hinf<=th)
            hook(par,idx,l);

        if(qlevel(r01[u],th,@offset)==lv && fabs(h01[idx]-h01[u])*hinf<=th)
            hook(par,idx,u);

        int p=par[idx];
        p=par[p];
        p=par[p];
        p=par[p];
        atomic_min(&par[idx],p);

        return;
    }

    if(@Iteration==ROOT_STAGE)
    {
        int r=idx;

        for(int i=0;i<16;i++)
        {
            int n=par[r];
            if(n==r) break;
            r=n;
        }

        par[idx]=r;
        ids[idx]=r;

        if(@make_preview)
            @preview.set(idcolor(r));

        return;
    }
}