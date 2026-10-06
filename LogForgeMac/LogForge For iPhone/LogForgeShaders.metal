#include <metal_stdlib>

using namespace metal;


// ============================================================
// LogForge V210 V2
//
// HLG BT.2020 10-bit 4:2:2 V210
//              |
//              v
//        HLG inverse OETF
//              |
//              v
//   HLG reference-white normalization
//              |
//              v
//       Apple Log OETF
//              |
//              v
//       BT.2020 Y'CbCr
//              |
//              v
//       10-bit V210
//
// One Metal thread = 6 pixels
//
// ============================================================



// ============================================================
// HLG constants
// ============================================================

constant float HLG_A = 0.17883277f;
constant float HLG_B = 0.28466892f;
constant float HLG_C = 0.55991073f;


// ============================================================
// HLG reference white
//
// BT.2100 HLG:
//
// E = 0.75
//
// inverse OETF gives:
//
//     0.26496256
//
// This is the HLG reference-white level used to normalize
// scene-linear values before Apple Log encoding.
//
// Therefore:
//
//     AppleLogLinear = HLGLinear / 0.26496256
//
// ============================================================

constant float HLG_REFERENCE_WHITE = 0.26496256f;


// ============================================================
// Apple Log Profile
//
// Apple Log:
//
// R >= 0.01:
//
//     y = gamma * log2(R + beta) + delta
//
// R0 <= R < 0.01:
//
//     y = sigma * (R - R0)^2
//
// R < R0:
//
//     y = 0
//
// ============================================================

constant float APPLE_LOG_R0    = -0.05641088f;
constant float APPLE_LOG_RT    =  0.01000000f;
constant float APPLE_LOG_SIGMA = 47.28711236f;
constant float APPLE_LOG_BETA  =  0.00964052f;
constant float APPLE_LOG_GAMMA = 0.08550479f;
constant float APPLE_LOG_DELTA = 0.69336945f;


// ============================================================
// Helpers
// ============================================================

inline float clamp01(
    float value
)
{
    return clamp(
        value,
        0.0f,
        1.0f
    );
}


// ============================================================
// HLG inverse OETF
//
// Input:
//
//     HLG encoded signal [0, 1]
//
// Output:
//
//     relative scene-linear light
// ============================================================

inline float hlgInverseOETF(
    float E
)
{
    E = clamp01(E);

    if (E <= 0.5f)
    {
        return (
            E * E
        ) / 3.0f;
    }

    return (
        exp(
            (E - HLG_C) / HLG_A
        )
        + HLG_B
    ) / 12.0f;
}


// ============================================================
// HLG RGB -> scene linear RGB
// ============================================================

inline float3 hlgToSceneLinear(
    float3 hlgRGB
)
{
    float r =
        hlgInverseOETF(
            hlgRGB.r
        );

    float g =
        hlgInverseOETF(
            hlgRGB.g
        );

    float b =
        hlgInverseOETF(
            hlgRGB.b
        );

    return float3(
        r,
        g,
        b
    );
}


// ============================================================
// Normalize HLG scene-linear RGB
//
// HLG reference white = 0.26496256
//
// Apple Log's normalized linear domain uses:
//
//     reference white = 1.0
//
// ============================================================

inline float3 normalizeHLGReferenceWhite(
    float3 sceneLinear
)
{
    return
        sceneLinear /
        HLG_REFERENCE_WHITE;
}


// ============================================================
// Apple Log encoding
// ============================================================

inline float appleLogEncode(
    float R
)
{
    // --------------------------------------------------------
    // Main logarithmic section
    // --------------------------------------------------------

    if (R >= APPLE_LOG_RT)
    {
        return
            APPLE_LOG_GAMMA *
            log2(
                R + APPLE_LOG_BETA
            )
            +
            APPLE_LOG_DELTA;
    }

    // --------------------------------------------------------
    // Toe section
    // --------------------------------------------------------

    if (R >= APPLE_LOG_R0)
    {
        float d =
            R - APPLE_LOG_R0;

        return
            APPLE_LOG_SIGMA *
            d *
            d;
    }

    // --------------------------------------------------------
    // Below Apple Log domain
    // --------------------------------------------------------

    return 0.0f;
}


// ============================================================
// Apple Log RGB
// ============================================================

inline float3 appleLogEncodeRGB(
    float3 linearRGB
)
{
    return float3(
        appleLogEncode(
            linearRGB.r
        ),

        appleLogEncode(
            linearRGB.g
        ),

        appleLogEncode(
            linearRGB.b
        )
    );
}


// ============================================================
// V210 unpack
// ============================================================

inline uint unpack10(
    uint word,
    uint shift
)
{
    return
        (
            word >> shift
        )
        &
        0x3ffu;
}


// ============================================================
// V210 pack
// ============================================================

inline uint pack10(
    uint value,
    uint shift
)
{
    return
        (
            value &
            0x3ffu
        )
        << shift;
}


// ============================================================
// BT.2020 Y'CbCr -> RGB
//
// Non-constant luminance BT.2020.
//
// Input:
//
//     Y  = normalized video-range luma
//     Cb = centered chroma
//     Cr = centered chroma
//
// ============================================================

inline float3 bt2020YCbCrToRGB(
    float Y,
    float Cb,
    float Cr
)
{
    float R =
        Y
        +
        1.4746f * Cr;

    float G =
        Y
        -
        0.16455f * Cb
        -
        0.57135f * Cr;

    float B =
        Y
        +
        1.8814f * Cb;

    return float3(
        R,
        G,
        B
    );
}


// ============================================================
// BT.2020 RGB -> Y'CbCr
// ============================================================

inline float3 bt2020RGBToYCbCr(
    float3 rgb
)
{
    float R = rgb.r;
    float G = rgb.g;
    float B = rgb.b;

    float Y =
        0.2627f * R
        +
        0.6780f * G
        +
        0.0593f * B;

    float Cb =
        (
            B - Y
        )
        /
        1.8814f;

    float Cr =
        (
            R - Y
        )
        /
        1.4746f;

    return float3(
        Y,
        Cb,
        Cr
    );
}


// ============================================================
// 10-bit video-range decode
//
// Luma:
//
//     black = 64
//     white = 940
//
// Chroma:
//
//     center = 512
//     excursion = 448
//
// ============================================================

inline float decodeY10(
    uint value
)
{
    return
        (
            float(value) -
            64.0f
        )
        /
        876.0f;
}


inline float decodeC10(
    uint value
)
{
    return
        (
            float(value) -
            512.0f
        )
        /
        896.0f;
}


// ============================================================
// 10-bit video-range encode
// ============================================================

inline uint encodeY10(
    float value
)
{
    float encoded =
        value * 876.0f
        +
        64.0f;

    encoded =
        clamp(
            round(encoded),
            0.0f,
            1023.0f
        );

    return uint(
        encoded
    );
}


inline uint encodeC10(
    float value
)
{
    float encoded =
        value * 896.0f
        +
        512.0f;

    encoded =
        clamp(
            round(encoded),
            0.0f,
            1023.0f
        );

    return uint(
        encoded
    );
}


// ============================================================
// Process one pixel
//
// HLG BT.2020 YCbCr
//        |
//        v
// HLG BT.2020 RGB
//        |
//        v
// Scene-linear
//        |
//        v
// Reference-white normalization
//        |
//        v
// Apple Log
//        |
//        v
// BT.2020 YCbCr
//
// ============================================================

inline float3 processPixel(
    uint Y10,
    uint Cb10,
    uint Cr10
)
{
    // --------------------------------------------------------
    // Decode 10-bit video range
    // --------------------------------------------------------

    float Y =
        decodeY10(
            Y10
        );

    float Cb =
        decodeC10(
            Cb10
        );

    float Cr =
        decodeC10(
            Cr10
        );

    // --------------------------------------------------------
    // BT.2020 YCbCr -> HLG RGB
    // --------------------------------------------------------

    float3 hlgRGB =
        bt2020YCbCrToRGB(
            Y,
            Cb,
            Cr
        );

    // --------------------------------------------------------
    // HLG -> scene linear
    // --------------------------------------------------------

    float3 sceneLinear =
        hlgToSceneLinear(
            hlgRGB
        );

    // --------------------------------------------------------
    // HLG reference white -> Apple Log linear domain
    //
    // THIS is the important V2 correction.
    // --------------------------------------------------------

    float3 normalizedLinear =
        normalizeHLGReferenceWhite(
            sceneLinear
        );

    // --------------------------------------------------------
    // Apple Log
    // --------------------------------------------------------

    float3 appleLogRGB =
        appleLogEncodeRGB(
            normalizedLinear
        );

    // --------------------------------------------------------
    // Apple Log RGB -> BT.2020 YCbCr
    // --------------------------------------------------------

    float3 outputYCbCr =
        bt2020RGBToYCbCr(
            appleLogRGB
        );

    return outputYCbCr;
}


// ============================================================
// Kernel
// ============================================================

kernel void logForgeV210LegacyKernel(
    device const uint *inputBuffer
        [[buffer(0)]],

    device uint *outputBuffer
        [[buffer(1)]],

    constant uint &width
        [[buffer(2)]],

    constant uint &height
        [[buffer(3)]],

    constant uint &inputBytesPerRow
        [[buffer(4)]],

    constant uint &outputBytesPerRow
        [[buffer(5)]],

    uint2 gid
        [[thread_position_in_grid]]
)
{
    // --------------------------------------------------------
    // Bounds
    // --------------------------------------------------------

    if (gid.y >= height)
    {
        return;
    }

    uint pixelStart =
        gid.x * 6u;

    if (pixelStart >= width)
    {
        return;
    }

    // --------------------------------------------------------
    // Row pointers
    // --------------------------------------------------------

    device const char *inputBytes =
        (
            device const char *
        )inputBuffer;

    device char *outputBytes =
        (
            device char *
        )outputBuffer;

    device const uint *src =
        (
            device const uint *
        )(
            inputBytes
            +
            gid.y *
            inputBytesPerRow
            +
            gid.x *
            16u
        );

    device uint *dst =
        (
            device uint *
        )(
            outputBytes
            +
            gid.y *
            outputBytesPerRow
            +
            gid.x *
            16u
        );

    // --------------------------------------------------------
    // Load V210 group
    // --------------------------------------------------------

    uint w0 = src[0];
    uint w1 = src[1];
    uint w2 = src[2];
    uint w3 = src[3];

    // --------------------------------------------------------
    // V210 layout
    //
    // w0:
    //     Cb0 Y0 Cr0
    //
    // w1:
    //     Y1 Cb2 Y2
    //
    // w2:
    //     Cr2 Y3 Cb4
    //
    // w3:
    //     Y4 Cr4 Y5
    // --------------------------------------------------------

    uint Cb0 =
        unpack10(
            w0,
            0
        );

    uint Y0 =
        unpack10(
            w0,
            10
        );

    uint Cr0 =
        unpack10(
            w0,
            20
        );


    uint Y1 =
        unpack10(
            w1,
            0
        );

    uint Cb2 =
        unpack10(
            w1,
            10
        );

    uint Y2 =
        unpack10(
            w1,
            20
        );


    uint Cr2 =
        unpack10(
            w2,
            0
        );

    uint Y3 =
        unpack10(
            w2,
            10
        );

    uint Cb4 =
        unpack10(
            w2,
            20
        );


    uint Y4 =
        unpack10(
            w3,
            0
        );

    uint Cr4 =
        unpack10(
            w3,
            10
        );

    uint Y5 =
        unpack10(
            w3,
            20
        );

    // --------------------------------------------------------
    // Determine number of valid pixels
    // --------------------------------------------------------

    uint remaining =
        width -
        pixelStart;

    uint validPixels =
        min(
            remaining,
            6u
        );

    // --------------------------------------------------------
    // Process pixels
    // --------------------------------------------------------

    float3 p0 =
        processPixel(
            Y0,
            Cb0,
            Cr0
        );

    float3 p1;

    if (validPixels >= 2u)
    {
        p1 =
            processPixel(
                Y1,
                Cb0,
                Cr0
            );
    }
    else
    {
        p1 = p0;
    }


    float3 p2;

    if (validPixels >= 3u)
    {
        p2 =
            processPixel(
                Y2,
                Cb2,
                Cr2
            );
    }
    else
    {
        p2 = p1;
    }


    float3 p3;

    if (validPixels >= 4u)
    {
        p3 =
            processPixel(
                Y3,
                Cb2,
                Cr2
            );
    }
    else
    {
        p3 = p2;
    }


    float3 p4;

    if (validPixels >= 5u)
    {
        p4 =
            processPixel(
                Y4,
                Cb4,
                Cr4
            );
    }
    else
    {
        p4 = p3;
    }


    float3 p5;

    if (validPixels >= 6u)
    {
        p5 =
            processPixel(
                Y5,
                Cb4,
                Cr4
            );
    }
    else
    {
        p5 = p4;
    }

    // --------------------------------------------------------
    // 4:2:2 chroma reconstruction
    //
    // Two RGB pixels share one Cb/Cr sample.
    //
    // --------------------------------------------------------

    float CbOut0 =
        (
            p0.g +
            p1.g
        )
        *
        0.5f;

    float CrOut0 =
        (
            p0.b +
            p1.b
        )
        *
        0.5f;


    float CbOut1 =
        (
            p2.g +
            p3.g
        )
        *
        0.5f;

    float CrOut1 =
        (
            p2.b +
            p3.b
        )
        *
        0.5f;


    float CbOut2 =
        (
            p4.g +
            p5.g
        )
        *
        0.5f;

    float CrOut2 =
        (
            p4.b +
            p5.b
        )
        *
        0.5f;

    // --------------------------------------------------------
    // Quantize back to 10-bit
    // --------------------------------------------------------

    uint outY0 =
        encodeY10(
            p0.r
        );

    uint outY1 =
        encodeY10(
            p1.r
        );

    uint outY2 =
        encodeY10(
            p2.r
        );

    uint outY3 =
        encodeY10(
            p3.r
        );

    uint outY4 =
        encodeY10(
            p4.r
        );

    uint outY5 =
        encodeY10(
            p5.r
        );


    uint outCb0 =
        encodeC10(
            CbOut0
        );

    uint outCr0 =
        encodeC10(
            CrOut0
        );


    uint outCb1 =
        encodeC10(
            CbOut1
        );

    uint outCr1 =
        encodeC10(
            CrOut1
        );


    uint outCb2 =
        encodeC10(
            CbOut2
        );

    uint outCr2 =
        encodeC10(
            CrOut2
        );

    // --------------------------------------------------------
    // Pack V210
    // --------------------------------------------------------

    uint outW0 =
        pack10(
            outCb0,
            0
        )
        |
        pack10(
            outY0,
            10
        )
        |
        pack10(
            outCr0,
            20
        );


    uint outW1 =
        pack10(
            outY1,
            0
        )
        |
        pack10(
            outCb1,
            10
        )
        |
        pack10(
            outY2,
            20
        );


    uint outW2 =
        pack10(
            outCr1,
            0
        )
        |
        pack10(
            outY3,
            10
        )
        |
        pack10(
            outCb2,
            20
        );


    uint outW3 =
        pack10(
            outY4,
            0
        )
        |
        pack10(
            outCr2,
            10
        )
        |
        pack10(
            outY5,
            20
        );

    // --------------------------------------------------------
    // Store
    // --------------------------------------------------------

    dst[0] = outW0;
    dst[1] = outW1;
    dst[2] = outW2;
    dst[3] = outW3;
}

// Production pipeline: Float parameter ABI [exposure, shadows, highlights, saturation, full range, centered].
inline float lfSpline(float v) {
    float x=abs(v);
    if(x<1) return ((13.0f/11*x-453.0f/209)*x-3.0f/209)*x+1;
    if(x<2) {float t=x-1; return ((-6.0f/11*t+270.0f/209)*t-156.0f/209)*t;}
    if(x<3) {float t=x-2; return ((1.0f/11*t-45.0f/209)*t+26.0f/209)*t;}
    return 0;
}
inline float3 lfRaw(device const uint* row, int px, uint width) {
    uint x=uint(clamp(px,0,int(width)-1)), g=(x/6)*4, k=x%6;
    uint4 q=uint4(row[g],row[g+1],row[g+2],row[g+3]);
    uint yy[6]={q.x>>10,q.y,q.y>>20,q.z>>10,q.w,q.w>>20};
    uint cb[3]={q.x,q.y>>10,q.z>>20}, cr[3]={q.x>>20,q.z,q.w>>10};
    return float3(yy[k]&1023,cb[k/2]&1023,cr[k/2]&1023);
}
inline float lfHLG(float v) {
    const float a=.17883277f,b=1-4*a,c=.5f-a*log(4*a);
    return v<0 ? -v*v/3 : (v<=.5f ? v*v/3 : (exp((v-c)/a)+b)/12);
}
inline float3 lfTransform(float3 v,constant float* p) {
    v=float3(lfHLG(v.x),lfHLG(v.y),lfHLG(v.z))*(exp2(p[0])/lfHLG(.75f));
    // Identity tone controls must not subtract and add a large luminance:
    // that loses small channel values in Float and fails the CPU reference.
    if(p[1]!=0 || p[2]!=0 || p[3]!=1) {
        float y=dot(v,float3(.2627f,.678f,.0593f));
        float ev=y>0?log2(y/.18f):-6,t=min(abs(ev)/6,1.0f),w=t*t*(3-2*t);
        v=(y+p[3]*(v-y))*exp2((ev<0?p[1]:-p[2])*w);
    }
    return appleLogEncodeRGB(v);
}

inline float3 lfPixel(device const uint* row,int px,uint width,constant float* p) {
    int x=clamp(px,0,int(width)-1); float pos=(float(x)-(p[5]>0 ? .5f:0))/2;
    int base=int(floor(pos)); float cb=0,cr=0,total=0;
    for(int j=base-2;j<=base+3;j++) {float w=lfSpline(pos-j);float3 v=lfRaw(row,j*2,width);cb+=v.y*w;cr+=v.z*w;total+=w;}
    cb=(cb/total-512)/(p[4]>0?1023:896);cr=(cr/total-512)/(p[4]>0?1023:896);
    float y=(lfRaw(row,x,width).x-(p[4]>0?0:64))/(p[4]>0?1023:876);
    float3 v=float3(y+1.4746f*cr,y-(.0593f*1.8814f/.678f)*cb-(.2627f*1.4746f/.678f)*cr,y+1.8814f*cb);
    v=lfTransform(v,p);
    y=dot(v,float3(.2627f,.678f,.0593f));
    return float3(y,(v.z-y)/1.8814f,(v.x-y)/1.4746f);
}

kernel void logForgeV210Kernel(device const uint* input [[buffer(0)]],device uint* output [[buffer(1)]],
    constant uint& width [[buffer(2)]],constant uint& height [[buffer(3)]],
    constant uint& si [[buffer(4)]],constant uint& so [[buffer(5)]],constant float* p [[buffer(6)]],uint2 gid [[thread_position_in_grid]]) {
    if(gid.y>=height || gid.x*6>=width) return;
    device const uint* row=(device const uint*)((device const char*)input+gid.y*si);
    device uint* dst=(device uint*)((device char*)output+gid.y*so)+gid.x*4;
    uint yy[6],cb[3],cr[3];
    for(int k=0;k<6;k++) yy[k]=encodeY10(lfPixel(row,int(gid.x)*6+k,width,p).x);
    for(int k=0;k<3;k++) {
        int x=min(int(gid.x)*6+k*2,int(width)-2);float3 v=0;float total=0;
        for(int j=-5;j<=5;j++) {float w=lfSpline(float(j)/2);v+=lfPixel(row,x+j,width,p)*w;total+=w;}
        cb[k]=encodeC10(v.y/total);cr[k]=encodeC10(v.z/total);
    }
    dst[0]=cb[0]|(yy[0]<<10)|(cr[0]<<20);dst[1]=yy[1]|(cb[1]<<10)|(yy[2]<<20);
    dst[2]=cr[1]|(yy[3]<<10)|(cb[2]<<20);dst[3]=yy[4]|(cr[2]<<10)|(yy[5]<<20);
}

kernel void logForgeColorReferenceKernel(device const float4* input [[buffer(0)]],device float4* output [[buffer(1)]],
    constant float* p [[buffer(2)]],constant uint& count [[buffer(3)]],uint gid [[thread_position_in_grid]]) {
    if(gid<count) output[gid]=float4(lfTransform(input[gid].xyz,p),0);
}
