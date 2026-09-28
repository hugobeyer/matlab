#import "sdf.h"

#runover layer

#bind layer !&height float port=height
#bind layer !&mask float port=mask
#bind layer !&sdf float port=sdf
#bind layer !&rim float port=rim

#bind parm seed int val=1
#bind parm scale float val=0.5
#bind parm density float val=0.4
#bind parm depth float val=0.5
#bind parm variation float val=0.5
#bind parm clustering float val=0.55
#bind parm edge float val=0.5
#bind parm detail float val=0.5
#bind parm lip_extension float val=0.5
#bind parm lip_variation float val=0.5

#define TAU 6.28318530717958647692f
#define REF_RES 2048.0f

static uint hash_u32(uint x)
{
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static float rnd1(uint x)
{
    return (float)(hash_u32(x) & 0x00ffffffu) / 16777215.0f;
}

static float2 rnd2(uint x)
{
    return (float2)(rnd1(x*1664525u+1013904223u), rnd1(x*22695477u+1u));
}

static int wrap_i(int x, int n)
{
    int r = x % n;
    return r < 0 ? r+n : r;
}

static int2 wrap_i2(int2 p, int n)
{
    return (int2)(wrap_i(p.x,n),wrap_i(p.y,n));
}

static uint cell_hash(int2 p, int seed, int salt)
{
    return hash_u32((uint)(p.x*73856093) ^ (uint)(p.y*19349663) ^ (uint)(seed*83492791) ^ (uint)(salt*2654435761u));
}

static uint tiled_hash(int2 p, int period, int seed, int salt)
{
    return cell_hash(wrap_i2(p,period),seed,salt);
}

static float noise_tiled(float2 uv, int period, int seed)
{
    period = max(period,1);
    float2 p = uv*(float)period;
    int2 i = convert_int2(floor(p));
    float2 f = p-convert_float2(i);
    f = f*f*(3.0f-2.0f*f);

    float a = rnd1(tiled_hash(i+(int2)(0,0),period,seed,0));
    float b = rnd1(tiled_hash(i+(int2)(1,0),period,seed,0));
    float c = rnd1(tiled_hash(i+(int2)(0,1),period,seed,0));
    float d = rnd1(tiled_hash(i+(int2)(1,1),period,seed,0));

    return mix(mix(a,b,f.x),mix(c,d,f.x),f.y);
}

static float fbm4(float2 uv, int period, int seed)
{
    period = max(period,1);
    float n = 0.0f;
    n += noise_tiled(uv+(float2)(0.173f,0.417f),period,seed)*0.533333f;
    n += noise_tiled(uv+(float2)(0.619f,0.231f),period*2,seed+71)*0.277333f;
    n += noise_tiled(uv+(float2)(0.347f,0.783f),period*4,seed+173)*0.144213f;
    n += noise_tiled(uv+(float2)(0.811f,0.529f),period*8,seed+283)*0.074991f;
    return n/1.029870f;
}

static float2 rot2(float2 p, float a)
{
    float c = cos(a), s = sin(a);
    return (float2)(c*p.x-s*p.y,s*p.x+c*p.y);
}

static float sd_ellipse(float2 p, float2 r)
{
    r = fmax(r,(float2)(1e-6f,1e-6f));
    float2 q = p/r;
    return (length(q)-1.0f)*(r.x+r.y)*0.5f;
}

static float layered_pit(float2 q, float radius, uint h, int layers, float shrink, float spread, float stretch, float chamfer_ratio, float chamfer_jitter)
{
    float out = 1e9f;

    for (int j=0; j<layers; ++j)
    {
        uint jh = hash_u32(h+(uint)j*2654435761u);
        float t = layers<=1 ? 0.0f : (float)j/(float)(layers-1);

        float sc = mix(1.0f,shrink,t);
        float2 off = (rnd2(jh+31u)-(float2)(0.5f))*radius*spread*t;
        float2 p = rot2(q-off,rnd1(jh+61u)*TAU);

        float sx = max(1.0f+(rnd1(jh+91u)-0.5f)*2.0f*stretch,0.25f);
        float sy = max(1.0f+(rnd1(jh+121u)-0.5f)*2.0f*stretch,0.25f);

        float d = sd_ellipse(p,(float2)(radius*sc*sx,radius*sc*sy));

        if (j == 0) out = d;
        else
        {
            float ch = radius*chamfer_ratio*(1.0f+(rnd1(jh+151u)-0.5f)*2.0f*chamfer_jitter);
            out = sdfOpUnionChamfer(out,d,max(ch,0.0f));
        }
    }

    return out;
}

@KERNEL
{
    float2 uv = @P.texture;

    float Uscale = clamp(@scale,0.0f,1.0f);
    float Udensity = clamp(@density,0.0f,1.0f);
    float Udepth = clamp(@depth,0.0f,1.0f);
    float Uvar = clamp(@variation,0.0f,1.0f);
    float Ucluster = clamp(@clustering,0.0f,1.0f);
    float Uedge = clamp(@edge,0.0f,1.0f);
    float Udetail = clamp(@detail,0.0f,1.0f);
    float UlipExt = clamp(@lip_extension,0.0f,1.0f);
    float UlipVar = clamp(@lip_variation,0.0f,1.0f);

    float res = max(max((float)@xres,(float)@yres),1.0f);
    float resScale = res/REF_RES;

    // Actual output pixel — ONLY sampling / AA.
    float pixelUV = 1.0f/res;

    // 2048-reference artistic pixel converted to current resolution.
    // Example: 8px @ 2048 -> 16px @ 4096 -> same UV/material size.
    float refPxUV = 1.0f/REF_RES;

    int cells = clamp((int)floor(24.0f+Uscale*80.0f+0.5f),24,104);
    float freq = (float)cells;

    float pixelGP = pixelUV*freq;
    float refPixelGP = refPxUV*freq;

    float radiusPx2048 = mix(12.0f,2.0f,Uscale);
    float radiusPxCurrent = radiusPx2048*resScale;
    float radiusUV = radiusPxCurrent/res;

    float sizeVar = mix(0.25f,0.85f,Uvar);
    float sizeMin = max(radiusUV*(1.0f-sizeVar),0.75f/REF_RES);
    float sizeMax = radiusUV*(1.0f+sizeVar);

    float depthValue = Udepth*0.68f;
    float depthVar = mix(0.12f,0.75f,Uvar);

    int layers = clamp((int)floor(8.0f+Udetail*8.0f+0.5f),8,16);
    int overlaps = clamp((int)floor(2.0f+Udetail*3.0f+0.5f),2,5);

    float layerShrink = mix(0.80f,0.64f,Uvar);
    float layerSpread = mix(0.20f,0.50f,Uvar);
    float overlapSpread = mix(0.45f,0.85f,Uvar);
    float stretch = mix(0.02f,0.08f,Uvar);

    float chamferRatio = mix(0.65f,1.15f,Uedge);
    float chamferJitter = mix(0.30f,0.85f,Uvar);

    // Reference-space detail dimensions.
    float broadPx2048 = 8.0f*mix(1.35f,0.75f,Udetail);
    float microPx2048 = 6.0f*mix(1.35f,0.70f,Udetail);
    float floorPx2048 = broadPx2048*1.75f;

    // Current texture equivalent pixel widths.
    float broadPx = broadPx2048*resScale;
    float microPx = microPx2048*resScale;
    float floorPx = floorPx2048*resScale;

    // Convert current-pixel size to UV frequency.
    int broadPeriod = max((int)floor(res/max(broadPx,1.0f)+0.5f),1);
    int microPeriod = max((int)floor(res/max(microPx,1.0f)+0.5f),1);
    int floorPeriod = max((int)floor(res/max(floorPx,1.0f)+0.5f),1);

    float edgeGain = mix(0.20f,1.80f,Uedge);
    float broadAmount = 3.0f*edgeGain;
    float microAmount = 4.0f*edgeGain;

    float clusterNoise = noise_tiled(uv,3,@seed+991);
    float cluster = mix(1.0f,clusterNoise,Ucluster);

    float2 gp = uv*freq;
    int2 base = convert_int2(floor(gp));

    float finalSdf = 1e9f;
    float finalDepth = 0.0f;
    uint finalHash = 0u;
    int found = 0;

    for (int oy=-1; oy<=1; ++oy)
    {
        for (int ox=-1; ox<=1; ++ox)
        {
            int2 rawCell = base+(int2)(ox,oy);
            int2 cell = wrap_i2(rawCell,cells);
            uint h = cell_hash(cell,@seed,17);

            float localDensity = clamp(Udensity*mix(0.30f,1.70f,cluster),0.0f,1.0f);
            if (rnd1(h+11u)>localDensity) continue;

            float2 center = convert_float2(rawCell)+rnd2(h+31u);
            float2 q = gp-center;

            float baseRadiusUV = mix(sizeMin,sizeMax,rnd1(h+51u));
            float baseRadius = max(baseRadiusUV*freq,refPixelGP);

            float bound = baseRadius*(2.5f+overlapSpread+layerSpread);
            if (length(q)>bound) continue;

            float cellSdf = 1e9f;

            for (int k=0; k<overlaps; ++k)
            {
                uint kh = hash_u32(h+(uint)k*2246822519u);

                float2 off = (rnd2(kh+71u)-(float2)(0.5f))*baseRadius*overlapSpread;
                float passScale = mix(0.62f,1.0f,rnd1(kh+101u));
                float passRadius = baseRadius*passScale;

                float d = layered_pit(q-off,passRadius,kh,layers,layerShrink,layerSpread,stretch,chamferRatio,chamferJitter);

                if (k == 0) cellSdf = d;
                else
                {
                    float ch = passRadius*chamferRatio*(1.0f+(rnd1(kh+131u)-0.5f)*2.0f*chamferJitter);
                    cellSdf = sdfOpUnionChamfer(cellSdf,d,max(ch,0.0f));
                }
            }

            float dpth = max(depthValue*mix(1.0f-depthVar,1.0f+depthVar,rnd1(h+181u)),0.0f);

            if (!found)
            {
                finalSdf = cellSdf;
                finalDepth = dpth;
                finalHash = h;
                found = 1;
            }
            else
            {
                float old = finalSdf;
                float ch = baseRadius*chamferRatio*0.65f;
                finalSdf = sdfOpUnionChamfer(finalSdf,cellSdf,ch);

                if (cellSdf<old)
                {
                    finalDepth = dpth;
                    finalHash = h;
                }
            }
        }
    }

    if (!found)
    {
        @height.set(0.5f);
        @mask.set(0.0f);
        @sdf.set(1.0f);
        @rim.set(0.0f);
    }
    else
    {
        float broadNoise = fbm4(
            uv+(float2)(rnd1(finalHash+701u)*0.137f,rnd1(finalHash+709u)*0.137f),
            broadPeriod,@seed+1301
        );

        float microNoise = noise_tiled(
            uv+(float2)(rnd1(finalHash+727u)*0.173f,rnd1(finalHash+733u)*0.173f),
            microPeriod,@seed+1709
        );

        broadNoise = (broadNoise-0.5f)*2.0f;
        microNoise = (microNoise-0.5f)*2.0f;

        // IMPORTANT:
        // amplitude uses 2048-reference pixel distance,
        // NOT the actual output pixel.
        float broadAmp = refPixelGP*broadPx2048*broadAmount;
        float microAmp = refPixelGP*microPx2048*microAmount;

        float edgeZone = max(
            refPixelGP*mix(14.0f,28.0f,Uedge),
            sizeMax*freq*0.75f
        );

        float edgeInfluence = 1.0f-clamp(fabs(finalSdf)/edgeZone,0.0f,1.0f);
        edgeInfluence = edgeInfluence*edgeInfluence*(3.0f-2.0f*edgeInfluence);

        float detailedSdf = finalSdf+
            broadNoise*broadAmp*edgeInfluence+
            microNoise*microAmp*edgeInfluence;

        // AA alone follows true output resolution.
        float aa = max(pixelGP*1.25f,1e-6f);

        float inside = clamp(0.5f-detailedSdf/(2.0f*aa),0.0f,1.0f);
        inside = inside*inside*(3.0f-2.0f*inside);

        float radiusMaxGP = sizeMax*freq;
        float wallWidth = mix(1.4f,2.6f,Udepth);
        float wallHardness = mix(2.0f,4.0f,Uedge);

        float depthExtent = max(radiusMaxGP*wallWidth,refPixelGP*2.0f);
        float cavity = clamp(-detailedSdf/depthExtent,0.0f,1.0f);

        float wall = clamp(1.0f-pow(1.0f-cavity,wallHardness),0.0f,1.0f);

        float floorZone = clamp((cavity-0.35f)/0.65f,0.0f,1.0f);
        floorZone = floorZone*floorZone*(3.0f-2.0f*floorZone);

        float floorNoise = fbm4(uv+(float2)(0.219f,0.731f),floorPeriod,@seed+2213);
        floorNoise = (floorNoise-0.5f)*2.0f;

        float floorRoughness = Udetail*0.042f;
        float floorDepth = floorNoise*floorRoughness*finalDepth*floorZone;

        // -----------------------------------------------------
        // RESOLUTION-STABLE LIP
        // -----------------------------------------------------

        int lipBroadPeriod = max(broadPeriod/2,1);
        int lipMicroPeriod = max(microPeriod/2,1);

        float lipNoiseA = fbm4(uv+(float2)(0.413f,0.127f),lipBroadPeriod,@seed+3109);
        float lipNoiseB = noise_tiled(uv+(float2)(0.731f,0.293f),lipMicroPeriod,@seed+3511);

        lipNoiseA = (lipNoiseA-0.5f)*2.0f;
        lipNoiseB = (lipNoiseB-0.5f)*2.0f;

        float lipVarNoise = lipNoiseA*0.72f+lipNoiseB*0.28f;

        // User thinks in normalized extension.
        // Internally this is a 2048-reference width.
        float rimPx2048 = mix(3.0f,12.0f,UlipExt);
        float rimPxCurrent = rimPx2048*resScale;

        // Dividing by actual res produces same material-space width.
        float rimWidthUV = rimPxCurrent/res;
        float rimWidthGP = max(rimWidthUV*freq,refPixelGP);

        float widthVar = max(1.0f+lipVarNoise*UlipVar*0.75f,0.20f);
        float localRimWidth = rimWidthGP*widthVar;

        // Extension is also based on the reference/material distance.
        float lipOffset = rimWidthGP*mix(0.0f,1.8f,UlipExt);
        lipOffset *= max(1.0f+lipVarNoise*UlipVar*0.85f,0.10f);

        float rimDistance = fabs(detailedSdf-lipOffset);

        float rimMask = 1.0f-clamp(rimDistance/max(localRimWidth,refPixelGP),0.0f,1.0f);
        rimMask = rimMask*rimMask*(3.0f-2.0f*rimMask);

        float outerReach = rimWidthGP*(2.0f+UlipExt*3.0f);
        float rimRange = 1.0f-clamp(fabs(detailedSdf)/max(outerReach,refPixelGP),0.0f,1.0f);
        rimRange = rimRange*rimRange*(3.0f-2.0f*rimRange);
        rimMask *= rimRange;

        float rimBreak = clamp(
            0.80f+
            broadNoise*0.50f+
            microNoise*0.18f+
            lipVarNoise*UlipVar*0.70f,
            0.0f,1.0f
        );

        float lipCut = clamp(
            0.90f+lipNoiseA*mix(0.08f,0.75f,UlipVar),
            0.0f,1.0f
        );

        rimMask *= rimBreak*lipCut;

        float rimSide = clamp(1.0f-cavity*1.75f,0.0f,1.0f);
        float heightVar = max(1.0f+lipVarNoise*UlipVar*0.70f,0.10f);

        float rimHeight = depthValue*
            mix(0.008f,0.035f,Uedge)*
            mix(0.75f,1.45f,UlipExt)*
            heightVar;

        float rimRelief = rimMask*rimSide*rimHeight;

        // Wider lower shoulder behind the lip.
        float shoulderOffset = lipOffset*0.32f;
        float shoulderWidth = localRimWidth*mix(1.15f,2.1f,UlipExt);

        float shoulder = 1.0f-clamp(
            fabs(detailedSdf-shoulderOffset)/max(shoulderWidth,refPixelGP),
            0.0f,1.0f
        );

        shoulder = shoulder*shoulder*(3.0f-2.0f*shoulder);
        shoulder *= clamp(0.68f+lipNoiseA*0.32f,0.0f,1.0f);

        float shoulderHeight = shoulder*rimSide*rimHeight*0.30f*UlipExt;

        float pitDepth = wall*finalDepth;
        float h = 0.5f-pitDepth-floorDepth+rimRelief+shoulderHeight;

        @height.set(clamp(h,0.0f,1.0f));
        @mask.set(inside);
        @sdf.set(detailedSdf/freq);
        @rim.set(clamp(rimMask+shoulder*0.35f,0.0f,1.0f));
    }
}