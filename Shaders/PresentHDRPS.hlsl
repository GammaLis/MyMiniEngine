#include "ToneMappingUtility.hlsli"
#include "PresentRS.hlsli"

Texture2D<float3> ColorTex : register(t0);
Texture2D<float4> Overlay : register(t1);

SamplerState BilinearClamp : register(s0);

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

[RootSignature(Present_RootSig)]
PSOutput main(float4 position : SV_Position)
{
    PSOutput Out;

    // 暂时注释
    // float4 UI = Overlay.SampleLevel(BilinearClamp, position.xy * RcpDstSize, 0);
    float3 HDR = ColorTex[(int2)position.xy];
    float3 SDR = TM_Stanard(HDR);

    // Better to blend in linear space (unlike the hardware compositor)
    // 暂时注释
    // UI.rgb = RemoveSRGBCurve(UI.rgb);

    // SDR was not explicitly clamped to [0, 1] on input, but it will be on output
    // 暂时注释
    // SDR = saturate(SDR) * (1 - UI.a) + UI.rgb;

    HDR = REC709toREC2020(HDR);
    // 暂时注释
    // UI.rgb = REC709toREC2020(UI.rgb) * PaperWhite;
    SDR = REC709toREC2020(SDR) * PaperWhite;

    // Tone map while in Rec.2020.  This allows values to taper to the maximum of the display.
    HDR = TM_Stanard(HDR * PaperWhite / MaxBrightness) * MaxBrightness;

    // Composite HDR buffer with UI
    // 暂时注释
    // HDR = HDR * (1 - UI.a) + UI.rgb;

    float3 FinalOutput;
    switch (DebugMode)
    {
    case 0: FinalOutput = HDR; break;
    case 1: FinalOutput = SDR; break;
    default: FinalOutput = (position.x * RcpDstSize.x < 0.5 ? HDR : SDR); break;
    }

    // Current values are specified in nits.  Normalize to max specified brightness.
    Out.HdrOutput = ApplyREC2084Curve(FinalOutput / 10000.0);

    Out.HdrOutput = float3(0.2, 0.4, 0.4);

    return Out;
}
