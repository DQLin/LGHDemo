// Modified from the NVIDIA SVGF sample code

#include "SVGFCommon.hlsli"

Texture2D<float4> texInS : register(t0);
Texture2D<float4> texInU : register(t1);
Texture2D<float4> texPosition : register(t2);
Texture2D<float4> texNormal : register(t3);
RWTexture2D<float4> texOutputS : register(u0);
RWTexture2D<float4> texOutputU : register(u1);
RWTexture2D<float3> resultRatio : register(u2);

cbuffer CSConstants : register(b0)
{
	float c_phi;
	float n_phi;
	float z_phi;
	float stepWidth;
	int iter;
	int maxIter;
}

[numthreads(16, 16, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	const int2 center = DTid.xy;

	const float epsVariance = 1e-10;
	const float kernel[3] = { 1.0, 2.0 / 3.0, 1.0 / 6.0 };

	const float4 centerS = texInS[center];
	const float4 centerU = texInU[center];
	const float centerSl = luminance(centerS.rgb);
	const float centerUl = luminance(centerU.rgb);
	const float3 centerNormal = texNormal[center].xyz;
	const float centerPosition = texPosition[center].xyz;

	// variance for direct and indirect, filtered using 3x3 gaussian blur
	const float2 var = computeVarianceCenter(center, texInS, texInU);

	const float phiSl = c_phi * sqrt(max(0.0, epsVariance + var.r));
	const float phiUl = c_phi * sqrt(max(0.0, epsVariance + var.g));

	float sumWeight_S = 1.0;
	float sumWeight_U = 1.0;
	float4 sum_S = centerS;
	float4 sum_U = centerU;

	for (int yOffset = -2; yOffset <= 2; yOffset++)
	{
		for (int xOffset = -2; xOffset <= 2; xOffset++)
		{
			if (xOffset != 0 || yOffset != 0)
			{
				int2 tap = center + stepWidth * int2(xOffset, yOffset);
				float4 tapS = texInS[tap];
				float4 tapU = texInU[tap];
				float3 tapNormal = texNormal[tap].xyz;
				float3 tapPosition = texPosition[tap].xyz;

				float tapSl = luminance(tapS.rgb);
				float tapUl = luminance(tapU.rgb);

				float kernelWeight = kernel[abs(xOffset)] * kernel[abs(yOffset)];

				float Sl_w = abs(centerSl - tapSl) / phiSl;
				float Ul_w = abs(centerUl - tapUl) / phiUl;

				float n_w = pow(max(0.0, dot(centerNormal, tapNormal)), n_phi);

				float z_w = abs(centerPosition - tapPosition) / z_phi;

				float S_w = exp(0.0 - max(Sl_w, 0.0) - max(z_w, 0.0)) * n_w * kernelWeight;
				float U_w = exp(0.0 - max(Ul_w, 0.0) - max(z_w, 0.0)) * n_w * kernelWeight;

				sum_S += float4(S_w.xxx, S_w * S_w) * tapS;
				sum_U += float4(U_w.xxx, U_w * U_w) * tapU;
				sumWeight_S += S_w;
				sumWeight_U += U_w;
			}
		}
	}

	sum_S = float4(sum_S / float4(sumWeight_S.xxx, sumWeight_S * sumWeight_S));
	sum_U = float4(sum_U / float4(sumWeight_U.xxx, sumWeight_U * sumWeight_U));

	texOutputS[center] = sum_S;
	texOutputU[center] = sum_U;

	if (iter == maxIter - 1) //final pass
	{
		float3 temp;
		if (sum_U.x < 0.00001 || sum_U.y < 0.00001 || sum_U.z < 0.00001) temp = 1.0;
		else
		{
			temp.x = (sum_S.x / sum_U.x);
			temp.y = (sum_S.y / sum_U.y);
			temp.z = (sum_S.z / sum_U.z);
		}
		resultRatio[center] = saturate(temp); 
	}
}

