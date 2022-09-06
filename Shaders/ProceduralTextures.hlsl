//*********************************************************************************
//
// This file is based on or incorporates material from the projects listed below 
// (Third Party OSS). The original copyright notice and the license under which 
// Microsoft received such Third Party OSS, are set forth below. Such licenses 
// and notices are provided for informational purposes only. Microsoft licenses 
// the Third Party OSS to you under the licensing terms for the Microsoft product 
// or service. Microsoft reserves all other rights not expressly granted under 
// this agreement, whether by implication, estoppel or otherwise.
//
// MIT License
// Copyright(c) 2013 Inigo Quilez
//
// Permission is hereby granted, free of charge, to any person obtaining a copy 
// of this software and associated documentation files(the Software), to deal 
// in the Software without restriction, including without limitation the rights 
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell 
// copies of the Software, and to permit persons to whom the Software is furnished 
// to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all 
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS 
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS 
// OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
// WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR 
// IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
//*********************************************************************************

#ifndef PROCEDURAL_TEXTURES_INCLUDED
#define PROCEDURAL_TEXTURES_INCLUDED

//**********************************************************************************************
// 
// A list of analytical procedural textures.
// More info here: http://iquilezles.org/www/articles/filterableprocedurals/filterableprocedurals.htm
//
//**********************************************************************************************

// Box filtered checkerboard
float FilteredCheckers(in float2 uv, in float2 dpdx, in float2 dpdy)
{
	float2 w = max(abs(dpdx), abs(dpdy)); // Filter kernel
	float2 a = uv + 0.5 * w;
	float2 b = uv - 0.5 * w;

    // Analytical integral (box filter).
	float2 i = 2.0 * (abs(frac(b * 0.5) - 0.5) -
					  abs(frac(a * 0.5) - 0.5)) / w;
	return 0.5 - 0.5 * i.x * i.y;
}

// Box filtered grid
// ratio - center fill to border ratio.
float FilteredGrid(in float2 uv, in float2 dpdx, in float2 dpdy, in float N = 10.0)
{
	float2 w = max(abs(dpdx), abs(dpdy)); // Filter kernel
	float2 a = uv + 0.5 * w;
	float2 b = uv - 0.5 * w;

    // Analytical integral (box filter).
	float2 i = (floor(a) + min(frac(a) * N, 1.0) -
				floor(b) - min(frac(b) * N, 1.0)) / (N * w);
	return (1.0 - i.x) * (1.0 - i.y);
}

// Box filtered squares
float FilteredSquares(in float2 p, in float2 dpdx, in float dpdy, in float N = 3.0)
{
	float2 w = max(abs(dpdx), abs(dpdy));
	float2 a = p + 0.5 * w;
	float2 b = p - 0.5 * w;
	float2 i = (floor(a) + min(frac(a) * N, 1.0) -
				floor(b) - min(frac(b) * N, 1.0)) / (N * w);
	return 1.0 - i.x * i.y;
}

// Box filtered crosses
float FilteredCrosses(in float2 p, in float2 dpdx, in float2 dpdy, in float N = 3.0)
{
	float2 w = max(abs(dpdx), abs(dpdy));
	float2 a = p + 0.5 * w;
	float2 b = p - 0.5 * w;
	float2 i = (floor(a) + min(frac(a) * N, 1.0) -
				floor(b) - min(frac(b) * N, 1.0)) / (N * w);
	return 1.0 - i.x - i.y + 2.0 * i.x * i.y;
}

// Box filtered XOR pattern
float FilteredXor(in float2 p, in float2 dpdx, in float2 dpdy)
{
	float xor = 0.0f;
	[unroll]
	for (int i = 0; i < 8; ++i)
	{
		float2 w = max(abs(dpdx), abs(dpdy)) + 0.01;
		float2 a = p + 0.5 * w;
		float2 b = p - 0.5 * w;
		float2 f = 2.0 * (abs(frac(b * 0.5) - 0.5) -
						  abs(frac(a * 0.5) - 0.5)) / w;

		xor += 0.5 - 0.5 * f.x * f.y;

		dpdx *= 0.5;
		dpdy *= 0.5;
		p *= 0.5;
		xor *= 0.5;
	}
	return xor;
}

#endif // PROCEDURAL_TEXTURES_INCLUDED
