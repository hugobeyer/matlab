#runover layer

#bind layer id int

#bind layer !&uv float2
#bind layer !&gray float

#bind layer !&minx int
#bind layer !&maxx int
#bind layer !&miny int
#bind layer !&maxy int

#bind parm seed int val=0
#bind parm rotate_random int val=1

#bind parm scale_min float val=1
#bind parm scale_max float val=1
#bind parm bias_min float val=0
#bind parm bias_max float val=0

#bind parm tile_scale_x float val=1
#bind parm tile_scale_y float val=1
#bind parm mirror_x int val=0
#bind parm mirror_y int val=0

#bind parm invert int val=0

static uint hash32(uint x)
{
    x^=x>>16;
    x*=0x7feb352du;
    x^=x>>15;
    x*=0x846ca68bu;
    x^=x>>16;
    return x;
}

static float rnd(uint x)
{
    return (float)(hash32(x)&0x00ffffffu)*(1.0f/16777215.0f);
}

static int wrapdelta(int p,int a,int size)
{
    int d=p-a;
    int h=size/2;
    if(d>h) d-=size;
    if(d<-h) d+=size;
    return d;
}

static float frac_local(float x)
{
    return x-floor(x);
}

static float tilecoord(float u,float s,int mirror)
{
    float x=u*fmax(s,1e-6f);
    float cell=floor(x);
    float t=x-cell;
    if(mirror && (((int)cell)&1)) t=1.0f-t;
    return t;
}

@KERNEL
{
    int idx=@ix+@iy*@xres;
    int count=@xres*@yres;

    int iid=clamp(@id.bufferIndex(@ixy),0,count-1);

    global volatile int *mnx=(global volatile int *)@minx.data;
    global volatile int *mxx=(global volatile int *)@maxx.data;
    global volatile int *mny=(global volatile int *)@miny.data;
    global volatile int *mxy=(global volatile int *)@maxy.data;

    if(@Iteration==0)
    {
        mnx[idx]=2147483647;
        mxx[idx]=-2147483647;
        mny[idx]=2147483647;
        mxy[idx]=-2147483647;
        return;
    }

    int ax=iid%@xres;
    int ay=iid/@xres;

    int dx=wrapdelta(@ix,ax,@xres);
    int dy=wrapdelta(@iy,ay,@yres);

    if(@Iteration==1)
    {
        atomic_min(&mnx[iid],dx);
        atomic_max(&mxx[iid],dx);
        atomic_min(&mny[iid],dy);
        atomic_max(&mxy[iid],dy);
        return;
    }

    int x0=mnx[iid],x1=mxx[iid],y0=mny[iid],y1=mxy[iid];

    float sx=fmax((float)(x1-x0),1.0f);
    float sy=fmax((float)(y1-y0),1.0f);

    float2 p=(float2)(
        ((float)(dx-x0))/sx,
        ((float)(dy-y0))/sy
    );

    p=clamp(p,0.0f,1.0f);

    uint s=(uint)iid^((uint)@seed*2246822519u);

    float ang=@rotate_random ? rnd(s^0xA341316Cu)*6.28318530718f : 0.0f;
    float sc=@scale_min+(@scale_max-@scale_min)*rnd(s^0xC8013EA4u);
    float bs=@bias_min+(@bias_max-@bias_min)*rnd(s^0xAD90777Du);

    sc=fmax(sc,1e-6f);

    float cs=cos(ang),sn=sin(ang);

    float2 q=(p-(float2)(0.5f))*sc;
    float2 r=(float2)(q.x*cs-q.y*sn,q.x*sn+q.y*cs)+(float2)(0.5f);

    float ux=tilecoord(r.x,@tile_scale_x,@mirror_x);
    float uy=tilecoord(r.y,@tile_scale_y,@mirror_y);

    @uv.set((float2)(ux,uy));

    float g=clamp(ux+bs,0.0f,1.0f);
    if(@invert) g=1.0f-g;

    @gray.set(g);
}