#include "ToneMappingUtility.hlsli"
#include "PresentRS.hlsli"

Texture2D<float3> ColorTex  : register(t0);
Texture2D<float4> Overlay   : register(t1);

SamplerState BilinearClamp  : register(s0);

struct PSOutput
{
    float3 HdrOutput : SV_Target0;
};

cbuffer CB0 : register(b0)
{
    float2 RcpDstSize;
    float PaperWhite;
    float MaxBrightness;
    uint DebugMode;
}

/**
    SV_Position
    when SV_Position is declared for input to a shader, it can have one of 2 interpolation modes specified:
a.linearNoPerspective or b.linearNoPerspectiveCentroid, where the latter causes centroid-snapped xyzw
values to be provided when multisample antialiasing. When used in a shader, SV_Position describes the pixel location.
Available in all shaders to get the pixel center with a 0.5 offset.
*/
[RootSignature(Present_RootSig)]
PSOutput main(float4 position : SV_Position, float2 uv : TexCoord0)
{
    PSOutput Out;

    // 暂时注释
    float4 UI = Overlay.SampleLevel(BilinearClamp, position.xy * RcpDstSize, 0);
    float3 HDR = ColorTex[(int2)position.xy];
    // or -mf
    // float3 HDR = ColorTex.Sample(BilinearClamp, uv);     
    float3 SDR = TM_Stanard(HDR);

    // Better to blend in linear space (unlike the hardware compositor)
    // 暂时注释
    UI.rgb = RemoveSRGBCurve(UI.rgb);

    // SDR was not explicitly clamped to [0, 1] on input, but it will be on output
    // 暂时注释
    SDR = saturate(SDR) * (1 - UI.a) + UI.rgb;

    HDR = REC709toREC2020(HDR);
    // 暂时注释
    UI.rgb = REC709toREC2020(UI.rgb) * PaperWhite;
    SDR = REC709toREC2020(SDR) * PaperWhite;

    // Tone map while in Rec.2020.  This allows values to taper to the maximum of the display.
    HDR = TM_Stanard(HDR * PaperWhite / MaxBrightness) * MaxBrightness;

    // Composite HDR buffer with UI
    // 暂时注释
    HDR = HDR * (1 - UI.a) + UI.rgb;

    float3 FinalOutput;
    switch (DebugMode)
    {
    case 0: FinalOutput = HDR; break;
    case 1: FinalOutput = SDR; break;
    default: FinalOutput = (position.x * RcpDstSize.x < 0.5 ? HDR : SDR); break;
    }

    // Current values are specified in nits.  Normalize to max specified brightness.
    Out.HdrOutput = ApplyREC2084Curve(FinalOutput / 10000.0);

    // Out.HdrOutput = float3(0.2, 0.4, 0.4);
    // Out.HdrOutput = float3(position * RcpDstSize, 0.0);

    return Out;
}
