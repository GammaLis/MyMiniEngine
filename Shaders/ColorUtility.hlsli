// Filament/shaders/src/common_graphics.fs
#ifndef COLORUTILITY_HLSLI
#define COLORUTILITY_HLSLI

float Luminance(float3 rgb)
{
	return dot(rgb, float3(0.2126, 0.7152, 0.0722));
}

void Unpremultiply(inout float4 color)
{
	color.rgb /= max(color.a, 0.04);
}

float3 YCbCrToRgb(float luminance, float2 cbcr)
{
	// Taken from https://developer.apple.com/documentation/arkit/arframe/2867984-capturedimage
    static const float4x4 ycbcrToRgbTransform = 
    {
        1.0000,  1.0000,  1.0000, 0.0000,
        0.0000, -0.3441,  1.7720, 0.0000,
        1.4020, -0.7141,  0.0000, 0.0000,
        -0.7010, 0.5291, -0.8860, 1.0000
    };
    return mul(ycbcrToRgbTransform, float4(luminance, cbcr, 1.0)).rgb;
}

// tone mapping operations
// The input must be in the [0, 1] range.
float3 Inverse_Tonemap_Unreal(float3 x)
{
	return (x * -0.155) / (x - 1.019);
}

// 
/* Applies the inverse of the tone mapping operator to the specified HDR or LDR
 * sRGB (non-linear) color and returns a linear sRGB color. The inverse tone mapping
 * operator may be an approximation of the real inverse operation.*/
 float3 InverseTonemapSRGB(float3 color)
 {
 	// sRGB input 
 	color = clamp(color, 0.0, 1.0);
 	return Inverse_Tonemap_Unreal(color);
 }

/* Applies the inverse of the tone mapping operator to the specified HDR or LDR
 * linear RGB color and returns a linear RGB color. The inverse tone mapping operator
 * may be an approximation of the real inverse operation.*/
float3 InverseTonemap(float3 color)
{
	// linear input
	color = clamp(color, 0.0, 1.0);
	return Inverse_Tonemap_Unreal(pow(color, (1.0 / 2.2)));
}

// decodes the specified RGBM value to linear HDR RGB
float3 DecodeRGBM(float4 c)
{
	c.rgb *= (c.a * 16.0);
	return c.rgb * c.rgb;
}

#endif // COLORUTILITY_HLSLI
