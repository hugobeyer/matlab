#runover layer

#bind layer !&ids int

#bind parm pattern int val=0
#bind parm gridmode int val=0
#bind parm cellsx int val=8
#bind parm cellsy int val=8
#bind parm gap float val=4.0
#bind parm rowoffset float val=0.5
#bind parm aspect float val=2.0
#bind parm jitter float val=0.35
#bind parm seed int val=1

inline float frc(float x){return x-floor(x);}
inline float2 frc2(float2 p){return p-floor(p);}
inline int imod(int x,int n){int r=x%n;return r<0?r+n:r;}
inline int iround(float x){return (int)floor(x+0.5f);}

inline uint ihash(uint x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;}
inline uint hash2i(int x,int y,int s){return ihash((uint)x*0x9e3779b9u^(uint)y*0x85ebca6bu^(uint)s*0xc2b2ae35u);}
inline float hash01i(int x,int y,int s){return (float)(hash2i(x,y,s)&0x00ffffffu)*(1.0f/16777216.0f);}
inline float2 hash22i(int x,int y,int s){uint a=hash2i(x,y,s),b=ihash(a^0x68bc21ebu);return (float2)((float)(a&0x00ffffffu)*(1.0f/16777216.0f),(float)(b&0x00ffffffu)*(1.0f/16777216.0f));}

inline int makeid(int x,int y,int nx,int ny,int s){int id=(int)(hash2i(imod(x,nx),imod(y,ny),s)&0x7fffffffu);return id?id:1;}
inline float cellpx(float2 res,int nx,int ny){return fmin(res.x/(float)max(nx,1),res.y/(float)max(ny,1));}
inline int uniform_ny(float2 res,int nx){return max(iround((float)nx*(res.y/fmax(res.x,1.0f))),1);}

inline int grid_pattern(float2 uv,float2 res,int nx,int ny,int mode,float offset,float gap,int s){
    float2 p=uv*(float2)((float)nx,(float)ny);

    if(mode==2){
        float2 q=(float2)(p.x+p.y,p.y-p.x)*0.5f;
        int2 c=convert_int2(floor(q));
        float2 f=fabs(frc2(q)-0.5f);
        float e=0.5f-fmax(f.x,f.y);
        float grad=0.5f*sqrt(((float)nx/res.x)*((float)nx/res.x)+((float)ny/res.y)*((float)ny/res.y));
        float ep=e/fmax(grad,1e-8f);
        return ep<gap?0:makeid(c.x,c.y,nx,ny,s);
    }

    int y=(int)floor(p.y);
    if(mode==1)p.x-=(y&1)?offset:0.0f;

    int x=(int)floor(p.x);
    float2 f=frc2(p);
    float ex=fmin(f.x,1.0f-f.x)*(res.x/(float)nx);
    float ey=fmin(f.y,1.0f-f.y)*(res.y/(float)ny);

    return fmin(ex,ey)<gap?0:makeid(x,y,nx,ny,s);
}

inline int running_bond(float2 uv,float2 res,int nx,int ny,float offset,float gap,float jitter,int s){
    nx=max(nx,1);ny=max(ny,1);

    float2 p=uv*(float2)((float)nx,(float)ny);
    int row=(int)floor(p.y),wr=imod(row,ny);

    float fy=frc(p.y);
    float ey=fmin(fy,1.0f-fy)*(res.y/(float)ny);
    if(ey<gap)return 0;

    float j=clamp(jitter,0.0f,1.0f);
    float ro=offset+(hash01i(-17,wr,s+911)-0.5f)*j;

    float xw=(p.x-ro)/(float)nx;
    xw=frc(xw)*(float)nx;

    float amp=0.85f*j,sumw=0.0f;
    for(int i=0;i<nx;i++){
        float w=1.0f+(hash01i(i,wr,s+157)*2.0f-1.0f)*amp;
        sumw+=fmax(w,0.1f);
    }

    float norm=(float)nx/fmax(sumw,1e-8f),acc=0.0f,wsel=1.0f;
    int col=0;
    for(int i=0;i<nx;i++){
        float w=fmax(1.0f+(hash01i(i,wr,s+157)*2.0f-1.0f)*amp,0.1f)*norm;
        if(xw<acc+w || i==nx-1){col=i;wsel=w;break;}
        acc+=w;
    }

    float lx=xw-acc;
    float ex=fmin(lx,wsel-lx)*(res.x/(float)nx);

    return ex<gap?0:makeid(col,row,nx,ny,s+11);
}

inline int herringbone(float2 uv,float2 res,int nx,int ny,float gap,int s){
    nx=max(nx,2);ny=max(ny,2);

    float2 p=uv*(float2)((float)nx,(float)ny);
    int2 c=convert_int2(floor(p));
    float2 f=frc2(p);

    int parity=(c.x+c.y)&1;
    float edge;
    int x,y;

    if(!parity){
        float fy=frc(p.y*0.5f)*2.0f;
        float ex=fmin(f.x,1.0f-f.x)*(res.x/(float)nx);
        float ey=fmin(fy,2.0f-fy)*0.5f*(res.y/(float)ny);
        edge=fmin(ex,ey);
        x=c.x;y=(int)floor(p.y*0.5f)*2;
    }else{
        float fx=frc(p.x*0.5f)*2.0f;
        float ex=fmin(fx,2.0f-fx)*0.5f*(res.x/(float)nx);
        float ey=fmin(f.y,1.0f-f.y)*(res.y/(float)ny);
        edge=fmin(ex,ey);
        x=(int)floor(p.x*0.5f)*2;y=c.y;
    }

    return edge<gap?0:makeid(x,y,nx,ny,s+17);
}

inline int basketweave(float2 uv,float2 res,int nx,int ny,float gap,int s){
    nx=max(nx,2);ny=max(ny,2);

    float2 p=uv*(float2)((float)nx,(float)ny);
    int2 c=convert_int2(floor(p));
    float2 f=frc2(p);

    int orient=(c.x+c.y)&1;
    int sub;
    float edge;

    if(!orient){
        sub=f.y>=0.5f;
        float fy=frc(f.y*2.0f);
        float ex=fmin(f.x,1.0f-f.x)*(res.x/(float)nx);
        float ey=fmin(fy,1.0f-fy)*0.5f*(res.y/(float)ny);
        edge=fmin(ex,ey);
    }else{
        sub=f.x>=0.5f;
        float fx=frc(f.x*2.0f);
        float ex=fmin(fx,1.0f-fx)*0.5f*(res.x/(float)nx);
        float ey=fmin(f.y,1.0f-f.y)*(res.y/(float)ny);
        edge=fmin(ex,ey);
    }

    if(edge<gap)return 0;
    int id=makeid(c.x,c.y,nx,ny,s+31);
    return id^(sub?0x03579bdf:0);
}

inline int hex_pattern(float2 uv,float2 res,int nx,int ny,float gap,int s){
    nx=max(nx,1);ny=max(ny,2);if(ny&1)ny++;
    float2 p=uv*(float2)((float)nx,(float)ny);
    int row=(int)floor(p.y);
    float py=frc(p.y)-0.5f;
    float px=p.x-((row&1)?0.5f:0.0f);
    int col=(int)floor(px);
    px=frc(px)-0.5f;
    float2 a=fabs((float2)(px,py));
    float d=fmax(a.x*0.8660254f+a.y*0.5f,a.y);
    float ep=(0.5f-d)*cellpx(res,nx,ny);
    return ep<gap?0:makeid(col,row,nx,ny,s+53);
}

inline int octagon_square(float2 uv,float2 res,int nx,int ny,float gap,int s){
    float2 p=uv*(float2)((float)nx,(float)ny);
    int2 c=convert_int2(floor(p));
    float2 f=frc2(p),q=f-0.5f,a=fabs(q);
    float cp=cellpx(res,nx,ny);
    const float cut=0.2928932188f;

    if((a.x+a.y)<=(1.0f-cut)){
        float axis=(0.5f-fmax(a.x,a.y))*cp;
        float diag=(1.0f-a.x-a.y)*0.70710678f*cp;
        return fmin(axis,diag)<gap?0:makeid(c.x,c.y,nx,ny,s+71);
    }

    int sx=q.x>0.0f,sy=q.y>0.0f;
    int vx=c.x+sx,vy=c.y+sy;
    float2 v=(float2)(fabs(f.x-(sx?1.0f:0.0f)),fabs(f.y-(sy?1.0f:0.0f)));
    float ep=(cut-v.x-v.y)*0.70710678f*cp;
    return ep<gap?0:makeid(vx,vy,nx,ny,s+7919);
}

inline int periodic_voronoi(float2 uv,float2 res,int nx,int ny,float jitter,float gap,int s,int flagstone){
    nx=max(nx,1);ny=max(ny,1);

    float2 p=uv*(float2)((float)nx,(float)ny);
    int2 b=convert_int2(floor(p));

    float best=1e20f,second=1e20f;
    int bx=0,by=0;

    for(int oy=-2;oy<=2;oy++)for(int ox=-2;ox<=2;ox++){
        int cx=b.x+ox,cy=b.y+oy;
        int wx=imod(cx,nx),wy=imod(cy,ny);
        float2 r=hash22i(wx,wy,s);
        if(flagstone)r=0.15f+0.70f*r;
        r=(r-0.5f)*jitter+0.5f;
        float2 d=p-((float2)((float)cx,(float)cy)+r);
        if(flagstone)d.y*=1.28f;
        float ds=dot(d,d);
        if(ds<best){second=best;best=ds;bx=wx;by=wy;}
        else if(ds<second)second=ds;
    }

    float edge=0.5f*(sqrt(second)-sqrt(best));
    float ep=edge*cellpx(res,nx,ny);
    return ep<gap?0:makeid(bx,by,nx,ny,s+(flagstone?101:127));
}

@KERNEL
{
    int nx=max(@cellsx,1),ny=max(@cellsy,1);
    float2 res=convert_float2(@res);
    float2 uv=(convert_float2(@ixy)+(float2)(0.5f,0.5f))/res;
    uv=frc2(uv);

    float g=fmax(@gap,0.0f);
    int sny=uniform_ny(res,nx);
    int id=0;

    if(@pattern==0) id=grid_pattern(uv,res,nx,clamp(@gridmode,0,2)==2?sny:ny,clamp(@gridmode,0,2),@rowoffset,g,@seed);
    else if(@pattern==1) id=running_bond(uv,res,nx,ny,@rowoffset,g,@jitter,@seed);
    else if(@pattern==2) id=herringbone(uv,res,nx,ny,g,@seed);
    else if(@pattern==3) id=basketweave(uv,res,nx,sny,g,@seed);
    else if(@pattern==4) id=hex_pattern(uv,res,nx,ny,g,@seed);
    else if(@pattern==5) id=octagon_square(uv,res,nx,sny,g,@seed);
    else if(@pattern==6) id=periodic_voronoi(uv,res,nx,ny,clamp(@jitter,0.0f,0.95f),g,@seed,1);
    else id=periodic_voronoi(uv,res,nx,ny,clamp(@jitter,0.0f,0.95f),g,@seed,0);

    @ids.set(id);
}