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
inline int evenup(int x){x=max(x,2);return (x&1)?x+1:x;}
inline int multipleup(int x,int n){x=max(x,n);return ((x+n-1)/n)*n;}

inline uint ihash(uint x){
    x^=x>>16;x*=0x7feb352du;
    x^=x>>15;x*=0x846ca68bu;
    x^=x>>16;
    return x;
}

inline uint hash2i(int x,int y,int s){
    return ihash(
        (uint)x*0x9e3779b9u ^
        (uint)y*0x85ebca6bu ^
        (uint)s*0xc2b2ae35u);
}

inline float hash01i(int x,int y,int s){
    return (float)(hash2i(x,y,s)&0x00ffffffu)*(1.0f/16777216.0f);
}

inline float2 hash22i(int x,int y,int s){
    uint a=hash2i(x,y,s),b=ihash(a^0x68bc21ebu);
    return (float2)(
        (float)(a&0x00ffffffu)*(1.0f/16777216.0f),
        (float)(b&0x00ffffffu)*(1.0f/16777216.0f));
}

inline int makeid(int x,int y,int nx,int ny,int s){
    int id=(int)(hash2i(imod(x,nx),imod(y,ny),s)&0x7fffffffu);
    return id?id:1;
}

inline int uniform_ny(float2 res,int nx){
    return max(iround((float)nx*(res.y/fmax(res.x,1.0f))),1);
}

inline int uniform_hex_ny(float2 res,int nx){
    nx=max(nx,1);
    return evenup(max(
        iround((float)nx*(res.y/fmax(res.x,1.0f))*1.15470053838f),
        2));
}

inline float minpitch(float2 res,int nx,int ny){
    return fmin(
        res.x/(float)max(nx,1),
        res.y/(float)max(ny,1));
}

inline int motifrect(
    float2 lp,int2 mb,float4 r,
    float2 res,int ux,int uy,
    float gap,int s,int salt)
{
    float ex=fmin(lp.x-r.x,r.z-lp.x)*(res.x/(float)ux);
    float ey=fmin(lp.y-r.y,r.w-lp.y)*(res.y/(float)uy);

    if(fmin(ex,ey)<gap)return 0;

    int ax=mb.x+(int)r.x;
    int ay=mb.y+(int)r.y;

    return makeid(ax,ay,ux,uy,s+salt);
}

inline int grid_pattern(
    float2 uv,float2 res,
    int nx,int ny,int mode,
    float offset,float gap,int s)
{
    nx=max(nx,1);
    ny=max(ny,1);

    if(mode==2)
    {
        nx=evenup(nx);
        ny=evenup(ny);

        float2 p=uv*(float2)((float)nx,(float)ny);

        float2 q=(float2)(
            (p.x+p.y)*0.5f,
            (p.y-p.x)*0.5f);

        int2 c=convert_int2(floor(q));
        float2 f=frc2(q);
        float2 a=fabs(f-0.5f);

        float edge=0.5f-fmax(a.x,a.y);

        float gx=(float)nx/fmax(res.x,1.0f);
        float gy=(float)ny/fmax(res.y,1.0f);
        float pixgrad=0.5f*sqrt(gx*gx+gy*gy);

        float ep=edge/fmax(pixgrad,1.0e-8f);

        if(ep<gap)return 0;

        int ix=c.x-c.y;
        int iy=c.x+c.y;

        return makeid(
            imod(ix,nx),
            imod(iy,ny),
            nx,ny,s);
    }

    if(mode==1)ny=evenup(ny);

    float2 p=uv*(float2)((float)nx,(float)ny);
    int row=(int)floor(p.y);

    if(mode==1)
        p.x-=(row&1)?offset:0.0f;

    int col=(int)floor(p.x);
    float2 f=frc2(p);

    float ex=fmin(f.x,1.0f-f.x)*(res.x/(float)nx);
    float ey=fmin(f.y,1.0f-f.y)*(res.y/(float)ny);

    return fmin(ex,ey)<gap
        ?0
        :makeid(col,row,nx,ny,s);
}

inline int running_bond(
    float2 uv,float2 res,
    int nx,int ny,
    float offset,float gap,float jitter,int s)
{
    nx=max(nx,1);
    ny=evenup(ny);

    float2 p=uv*(float2)((float)nx,(float)ny);

    int row=(int)floor(p.y);
    int wr=imod(row,ny);

    float fy=frc(p.y);
    float ey=fmin(fy,1.0f-fy)*(res.y/(float)ny);

    if(ey<gap)return 0;

    float j=clamp(jitter,0.0f,1.0f);

    float rowshift=
        ((row&1)?offset:0.0f)
        +(hash01i(-71,wr,s+911)-0.5f)*j;

    float xw=frc((p.x-rowshift)/(float)nx)*(float)nx;

    float amp=0.8f*j;
    float sumw=0.0f;

    for(int i=0;i<nx;i++){
        float r=hash01i(i,wr,s+157)*2.0f-1.0f;
        sumw+=fmax(1.0f+r*amp,0.15f);
    }

    float norm=(float)nx/fmax(sumw,1.0e-8f);

    float acc=0.0f,wsel=1.0f;
    int col=0;

    for(int i=0;i<nx;i++){
        float r=hash01i(i,wr,s+157)*2.0f-1.0f;
        float w=fmax(1.0f+r*amp,0.15f)*norm;

        if(xw<acc+w || i==nx-1){
            col=i;
            wsel=w;
            break;
        }

        acc+=w;
    }

    float lx=xw-acc;
    float ex=fmin(lx,wsel-lx)*(res.x/(float)nx);

    return ex<gap
        ?0
        :makeid(col,row,nx,ny,s+11);
}

inline int herringbone(
    float2 uv,float2 res,
    int nx,float gap,int s)
{
    int ux=multipleup(max(nx,2)*2,4);
    int uy=multipleup(uniform_ny(res,ux),4);

    float2 p=uv*(float2)((float)ux,(float)uy);

    int x=(int)floor(p.x);
    int y=(int)floor(p.y);

    float2 f=frc2(p);

    int k=imod(x+y,4);

    int ax=x,ay=y;
    int vertical=0;

    float2 lp=(float2)(0.0f,0.0f);

    if(k==0){
        lp=f;
    }
    else if(k==1){
        ax=x-1;
        lp=(float2)(1.0f+f.x,f.y);
    }
    else if(k==2){
        vertical=1;
        lp=f;
    }
    else{
        vertical=1;
        ay=y-1;
        lp=(float2)(f.x,1.0f+f.y);
    }

    float sx=res.x/(float)ux;
    float sy=res.y/(float)uy;

    float ex,ey;

    if(!vertical){
        ex=fmin(lp.x,2.0f-lp.x)*sx;
        ey=fmin(lp.y,1.0f-lp.y)*sy;
    }
    else{
        ex=fmin(lp.x,1.0f-lp.x)*sx;
        ey=fmin(lp.y,2.0f-lp.y)*sy;
    }

    return fmin(ex,ey)<gap
        ?0
        :makeid(ax,ay,ux,uy,s+17);
}

inline int basketweave(
    float2 uv,float2 res,
    int nx,float gap,int s)
{
    int ux=multipleup(max(nx,2)*2,4);
    int uy=multipleup(uniform_ny(res,ux),4);

    float2 p=uv*(float2)((float)ux,(float)uy);

    int x=(int)floor(p.x);
    int y=(int)floor(p.y);

    int mx=(int)floor((float)x*0.5f);
    int my=(int)floor((float)y*0.5f);

    int bx=mx*2;
    int by=my*2;

    float2 lp=p-(float2)((float)bx,(float)by);

    int orient=(mx+my)&1;

    float sx=res.x/(float)ux;
    float sy=res.y/(float)uy;

    int ax,ay;
    float ex,ey;

    if(!orient){
        int sub=clamp((int)floor(lp.y),0,1);

        ax=bx;
        ay=by+sub;

        float ly=lp.y-(float)sub;

        ex=fmin(lp.x,2.0f-lp.x)*sx;
        ey=fmin(ly,1.0f-ly)*sy;
    }
    else{
        int sub=clamp((int)floor(lp.x),0,1);

        ax=bx+sub;
        ay=by;

        float lx=lp.x-(float)sub;

        ex=fmin(lx,1.0f-lx)*sx;
        ey=fmin(lp.y,2.0f-lp.y)*sy;
    }

    return fmin(ex,ey)<gap
        ?0
        :makeid(ax,ay,ux,uy,s+31);
}

inline float2 hexpoint(
    int cx,int cy,float2 pitch)
{
    return (float2)(
        ((float)cx+0.5f+((cy&1)?0.5f:0.0f))*pitch.x,
        ((float)cy+0.5f)*pitch.y);
}

inline int hex_pattern(
    float2 uv,float2 res,
    int nx,int ny,float gap,int s)
{
    nx=max(nx,1);
    ny=evenup(ny);

    float2 pitch=(float2)(
        res.x/(float)nx,
        res.y/(float)ny);

    float2 pp=uv*res;

    int brow=(int)floor(pp.y/pitch.y);
    int bcol=(int)floor(pp.x/pitch.x);

    float best=1.0e30f;
    int bx=0,by=0;
    float2 bestp=(float2)(0.0f,0.0f);

    for(int oy=-2;oy<=2;oy++){
        int cy=brow+oy;

        for(int ox=-2;ox<=2;ox++){
            int cx=bcol+ox;

            float2 cp=hexpoint(cx,cy,pitch);
            float2 d=cp-pp;
            float ds=dot(d,d);

            if(ds<best){
                best=ds;
                bx=cx;
                by=cy;
                bestp=cp;
            }
        }
    }

    float edge=1.0e30f;

    for(int oy=-2;oy<=2;oy++){
        for(int ox=-2;ox<=2;ox++){
            if(ox==0 && oy==0)continue;

            int cx=bx+ox;
            int cy=by+oy;

            float2 cp=hexpoint(cx,cy,pitch);

            float2 a=bestp-pp;
            float2 b=cp-pp;
            float2 delta=b-a;

            float dl2=dot(delta,delta);

            if(dl2>1.0e-8f){
                float d=fabs(dot(
                    0.5f*(a+b),
                    delta*rsqrt(dl2)));

                edge=fmin(edge,d);
            }
        }
    }

    return edge<gap
        ?0
        :makeid(bx,by,nx,ny,s+53);
}

inline int octagon_square(
    float2 uv,float2 res,
    int nx,int ny,float gap,int s)
{
    nx=max(nx,1);
    ny=max(ny,1);

    float2 p=uv*(float2)((float)nx,(float)ny);
    int2 c=convert_int2(floor(p));

    float2 f=frc2(p);
    float2 q=f-0.5f;
    float2 a=fabs(q);

    float cp=minpitch(res,nx,ny);

    const float cut=0.2928932188f;
    const float octdiag=0.7071067812f;

    if(a.x+a.y<=octdiag){
        float axis=(0.5f-fmax(a.x,a.y))*cp;
        float diag=(octdiag-(a.x+a.y))*0.70710678f*cp;
        float edge=fmin(axis,diag);

        return edge<gap
            ?0
            :makeid(c.x,c.y,nx,ny,s+71);
    }

    int sx=q.x>0.0f;
    int sy=q.y>0.0f;

    int vx=c.x+sx;
    int vy=c.y+sy;

    float dx=fabs(f.x-(sx?1.0f:0.0f));
    float dy=fabs(f.y-(sy?1.0f:0.0f));

    float edge=(cut-dx-dy)*0.70710678f*cp;

    return edge<gap
        ?0
        :makeid(vx,vy,nx,ny,s+7919);
}

inline float2 voropoint(
    int cx,int cy,
    int nx,int ny,
    int s,float jitter,
    int flagstone)
{
    int wx=imod(cx,nx);
    int wy=imod(cy,ny);

    float2 r=hash22i(wx,wy,s);

    if(flagstone)
        r=0.15f+0.70f*r;

    r=(r-0.5f)*jitter+0.5f;

    return (float2)(
        (float)cx+r.x,
        (float)cy+r.y);
}

inline int periodic_voronoi(
    float2 uv,float2 res,
    int nx,int ny,
    float jitter,float gap,
    int s,int flagstone)
{
    nx=max(nx,1);
    ny=max(ny,1);

    float2 pitch=(float2)(
        res.x/(float)nx,
        res.y/(float)ny);

    float2 p=uv*(float2)((float)nx,(float)ny);
    int2 base=convert_int2(floor(p));

    float best=1.0e30f;
    int bx=0,by=0;
    float2 besta=(float2)(0.0f,0.0f);

    for(int oy=-1;oy<=1;oy++){
        for(int ox=-1;ox<=1;ox++){
            int cx=base.x+ox;
            int cy=base.y+oy;

            float2 fp=voropoint(
                cx,cy,nx,ny,
                s,jitter,flagstone);

            float2 d=(fp-p)*pitch;

            if(flagstone)d.y*=1.22f;

            float ds=dot(d,d);

            if(ds<best){
                best=ds;
                bx=cx;
                by=cy;
                besta=d;
            }
        }
    }

    float edge=1.0e30f;

    for(int oy=-2;oy<=2;oy++){
        for(int ox=-2;ox<=2;ox++){
            if(ox==0 && oy==0)continue;

            int cx=bx+ox;
            int cy=by+oy;

            float2 fp=voropoint(
                cx,cy,nx,ny,
                s,jitter,flagstone);

            float2 b=(fp-p)*pitch;

            if(flagstone)b.y*=1.22f;

            float2 delta=b-besta;
            float dl2=dot(delta,delta);

            if(dl2>1.0e-8f){
                float d=fabs(dot(
                    0.5f*(besta+b),
                    delta*rsqrt(dl2)));

                edge=fmin(edge,d);
            }
        }
    }

    return edge<gap
        ?0
        :makeid(bx,by,nx,ny,s+(flagstone?101:127));
}

inline int hopscotch(
    float2 uv,float2 res,
    int nx,float gap,int s)
{
    int ux=multipleup(max(nx,3),3);
    int uy=multipleup(uniform_ny(res,ux),3);

    float2 p=uv*(float2)((float)ux,(float)uy);

    int mx=(int)floor(p.x/3.0f);
    int my=(int)floor(p.y/3.0f);

    int2 mb=(int2)(mx*3,my*3);

    float2 lp=p-convert_float2(mb);

    float4 r;
    int salt;

    if(lp.x<2.0f && lp.y<2.0f){
        r=(float4)(0.0f,0.0f,2.0f,2.0f);
        salt=211;
    }
    else if(lp.x>=2.0f && lp.y<1.0f){
        r=(float4)(2.0f,0.0f,3.0f,1.0f);
        salt=223;
    }
    else if(lp.x>=2.0f){
        r=(float4)(2.0f,1.0f,3.0f,3.0f);
        salt=227;
    }
    else{
        r=(float4)(0.0f,2.0f,2.0f,3.0f);
        salt=229;
    }

    return motifrect(
        lp,mb,r,
        res,ux,uy,
        gap,s,salt);
}

inline int french(
    float2 uv,float2 res,
    int nx,float gap,int s)
{
    int ux=multipleup(max(nx,6),6);
    int uy=multipleup(uniform_ny(res,ux),6);

    float2 p=uv*(float2)((float)ux,(float)uy);

    int mx=(int)floor(p.x/6.0f);
    int my=(int)floor(p.y/6.0f);

    int2 mb=(int2)(mx*6,my*6);

    float2 lp=p-convert_float2(mb);

    float4 r;
    int salt;

    if(lp.y<1.0f){
        if(lp.x<2.0f){
            r=(float4)(0,0,2,2);salt=307;
        }
        else if(lp.x<3.0f){
            r=(float4)(2,0,3,2);salt=311;
        }
        else{
            r=(float4)(3,0,6,1);salt=313;
        }
    }
    else if(lp.y<2.0f){
        if(lp.x<2.0f){
            r=(float4)(0,0,2,2);salt=307;
        }
        else if(lp.x<3.0f){
            r=(float4)(2,0,3,2);salt=311;
        }
        else if(lp.x<4.0f){
            r=(float4)(3,1,4,3);salt=317;
        }
        else{
            r=(float4)(4,1,6,3);salt=331;
        }
    }
    else if(lp.y<3.0f){
        if(lp.x<3.0f){
            r=(float4)(0,2,3,3);salt=337;
        }
        else if(lp.x<4.0f){
            r=(float4)(3,1,4,3);salt=317;
        }
        else{
            r=(float4)(4,1,6,3);salt=331;
        }
    }
    else if(lp.y<4.0f){
        if(lp.x<2.0f){
            r=(float4)(0,3,2,6);salt=347;
        }
        else if(lp.x<4.0f){
            r=(float4)(2,3,4,4);salt=349;
        }
        else{
            r=(float4)(4,3,6,5);salt=353;
        }
    }
    else if(lp.y<5.0f){
        if(lp.x<2.0f){
            r=(float4)(0,3,2,6);salt=347;
        }
        else if(lp.x<3.0f){
            r=(float4)(2,4,3,6);salt=359;
        }
        else if(lp.x<4.0f){
            r=(float4)(3,4,4,5);salt=367;
        }
        else{
            r=(float4)(4,3,6,5);salt=353;
        }
    }
    else{
        if(lp.x<2.0f){
            r=(float4)(0,3,2,6);salt=347;
        }
        else if(lp.x<3.0f){
            r=(float4)(2,4,3,6);salt=359;
        }
        else if(lp.x<4.0f){
            r=(float4)(3,5,4,6);salt=373;
        }
        else{
            r=(float4)(4,5,6,6);salt=379;
        }
    }

    return motifrect(
        lp,mb,r,
        res,ux,uy,
        gap,s,salt);
}

@KERNEL
{
    float2 res=convert_float2(@res);
    float2 uv=(convert_float2(@ixy)+(float2)(0.5f,0.5f))/res;
    uv=frc2(uv);

    int nx=max(@cellsx,1);
    int ny=max(@cellsy,1);

    float g=fmax(@gap,0.0f);
    float j=clamp(@jitter,0.0f,1.0f);

    int id=0;

    if(@pattern==0){
        int mode=clamp(@gridmode,0,2);

        if(mode==2){
            int dx=evenup(nx);
            int dy=evenup(uniform_ny(res,dx));

            id=grid_pattern(
                uv,res,
                dx,dy,
                2,@rowoffset,
                g,@seed);
        }
        else{
            id=grid_pattern(
                uv,res,
                nx,ny,
                mode,@rowoffset,
                g,@seed);
        }
    }
    else if(@pattern==1){
        id=running_bond(
            uv,res,
            nx,ny,
            @rowoffset,
            g,j,@seed);
    }
    else if(@pattern==2){
        id=herringbone(
            uv,res,nx,
            g,@seed);
    }
    else if(@pattern==3){
        id=basketweave(
            uv,res,nx,
            g,@seed);
    }
    else if(@pattern==4){
        int hny=uniform_hex_ny(res,nx);

        id=hex_pattern(
            uv,res,
            nx,hny,
            g,@seed);
    }
    else if(@pattern==5){
        int ony=uniform_ny(res,nx);

        id=octagon_square(
            uv,res,
            nx,ony,
            g,@seed);
    }
    else if(@pattern==6){
        id=periodic_voronoi(
            uv,res,
            nx,ny,
            j,g,@seed,1);
    }
    else if(@pattern==7){
        id=periodic_voronoi(
            uv,res,
            nx,ny,
            j,g,@seed,0);
    }
    else if(@pattern==8){
        id=hopscotch(
            uv,res,nx,
            g,@seed);
    }
    else{
        id=french(
            uv,res,nx,
            g,@seed);
    }

    @ids.set(id);
}