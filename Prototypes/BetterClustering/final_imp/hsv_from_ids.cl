#runover layer

#bind layer albedo float3 border=WRAP
#bind layer id int

#bind layer !&out float3

#bind parm seed int val=0

#bind ramp color_ramp float3 val=0
#bind parm ramp_mix_min float val=0
#bind parm ramp_mix_max float val=0.15

#bind parm hue_min float val=-1
#bind parm hue_max float val=1

#bind parm sat_min float val=0.9
#bind parm sat_max float val=1.1

#bind parm val_min float val=0.9
#bind parm val_max float val=1.1

static uint hash32(uint x)
{
    x^=x>>16;x*=0x7feb352du;
    x^=x>>15;x*=0x846ca68bu;
    x^=x>>16;
    return x;
}

static float rnd(uint x)
{
    return (float)(hash32(x)&0x00ffffffu)*(1.0f/16777215.0f);
}

static float3 rgb2hsv_local(float3 c)
{
    float mx=fmax(c.x,fmax(c.y,c.z));
    float mn=fmin(c.x,fmin(c.y,c.z));
    float d=mx-mn;

    float h=0.0f;
    float s=mx>1e-8f?d/mx:0.0f;

    if(d>1e-8f)
    {
        if(mx==c.x) h=(c.y-c.z)/d+(c.y<c.z?6.0f:0.0f);
        else if(mx==c.y) h=(c.z-c.x)/d+2.0f;
        else h=(c.x-c.y)/d+4.0f;
        h*=1.0f/6.0f;
    }

    return (float3)(h,s,mx);
}

static float3 hsv2rgb_local(float3 c)
{
    float h=c.x-floor(c.x);
    float s=clamp(c.y,0.0f,1.0f);
    float v=clamp(c.z,0.0f,1.0f);

    float x=h*6.0f;
    int i=(int)floor(x);
    float f=x-(float)i;

    float p=v*(1.0f-s);
    float q=v*(1.0f-s*f);
    float t=v*(1.0f-s*(1.0f-f));

    if(i==0) return (float3)(v,t,p);
    if(i==1) return (float3)(q,v,p);
    if(i==2) return (float3)(p,v,t);
    if(i==3) return (float3)(p,q,v);
    if(i==4) return (float3)(t,p,v);

    return (float3)(v,p,q);
}

@KERNEL
{
    int iid=@id.bufferIndex(@ixy);
    uint s=(uint)iid^((uint)@seed*2246822519u);

    float rc=rnd(s^0x7E95761Eu);
    float rm=rnd(s^0xB5297A4Du);
    float rh=rnd(s^0xA341316Cu);
    float rs=rnd(s^0xC8013EA4u);
    float rv=rnd(s^0xAD90777Du);

    float mixv=@ramp_mix_min+(@ramp_mix_max-@ramp_mix_min)*rm;

    float hmin=clamp(@hue_min,-1.0f,1.0f)*180.0f;
    float hmax=clamp(@hue_max,-1.0f,1.0f)*180.0f;
    float hdeg=hmin+(hmax-hmin)*rh;

    float svar=@sat_min+(@sat_max-@sat_min)*rs;
    float vvar=@val_min+(@val_max-@val_min)*rv;

    float3 c=@albedo.bufferIndex(@ixy);

    float3 rampc=@color_ramp(rc);
    c=mix(c,rampc,clamp(mixv,0.0f,1.0f));

    float3 hsv=rgb2hsv_local(c);

    hsv.x+=hdeg*(1.0f/360.0f);
    hsv.x-=floor(hsv.x);

    hsv.y=clamp(hsv.y*svar,0.0f,1.0f);
    hsv.z=clamp(hsv.z*vvar,0.0f,1.0f);

    @out.set(hsv2rgb_local(hsv));
}