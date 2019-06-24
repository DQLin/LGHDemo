// Modified from the NVIDIA SVGF sample code

#include "SVGFCommon.hlsli"

Texture2D<float4> texInS : register(t0);
Texture2D<float4> texInU : register(t1);
Texture2D<float4> texInM : register(t2);
Texture2D<float4> texPosition : register(t3);
Texture2D<float4> texNormal : register(t4);
Texture2D<uint> texHistoryLength : register(t5);

RWTexture2D<float4> texOutputS : register(u0);
RWTexture2D<float4> texOutputU : register(u1);

cbuffer CSConstants : register(b0)
{
	float c_phi;
	float n_phi;
	float z_phi;
}

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	const int2 center = DTid.xy;

	const float epsVariance = 1e-10;

	uint historyLength = texHistoryLength[center];

	if (historyLength < 4)
	{
		const float4 centerS = texInS[center];
		const float4 centerU = texInU[center];
		const float4 centerM = texInM[center];
		const float centerSl = luminance(centerS.rgb);
		const float centerUl = luminance(centerU.rgb);
		const float3 centerNormal = texNormal[center].xyz;
		const float3 centerPosition = texPosition[center].xyz;

		// variance for direct and indirect, filtered using 3x3 gaussian blur
		const float2 var = computeVarianceCenter(center, texInS, texInU);

		const float phiSl = c_phi * sqrt(max(0.0, epsVariance + var.r));
		const float phiUl = c_phi * sqrt(max(0.0, epsVariance + var.g));

		float sumWeight_S = 1.0;
		float sumWeight_U = 1.0;
		float3 sum_S = centerS.rgb;
		float3 sum_U = centerU.rgb;
		float4 sum_M = centerM;

		for (int yOffset = -3; yOffset <= 3; yOffset++)
		{
			for (int xOffset = -3; xOffset <= 3; xOffset++)
			{
				if (xOffset != 0 || yOffset != 0)
				{
					int2 tap = center + int2(xOffset, yOffset);
					float4 tapS = texInS[tap];
					float4 tapU = texInU[tap];
					float4 tapM = texInM[tap];

					float3 tapNormal = texNormal[tap].xyz;
					float3 tapPosition = texPosition[tap].xyz;

					float tapSl = luminance(tapS.rgb);
					float tapUl = luminance(tapU.rgb);

					float n_w = pow(max(0.0, dot(centerNormal, tapNormal)), n_phi);

					float z_w = abs(centerPosition - tapPosition) / z_phi;

					float S_w = exp(0.0 - max(z_w, 0.0)) * n_w;
					float U_w = exp(0.0 - max(z_w, 0.0)) * n_w;

					sum_S += S_w * tapS;
					sum_U += U_w * tapU;
					sumWeight_S += S_w;
					sumWeight_U += U_w;
					sum_M += tapM * float4(S_w.xx, U_w.xx);
				}
			}
		}

		// Clamp sums to >0 to avoid NaNs.
		sumWeight_S = max(sumWeight_S, 1e-6f);
		sumWeight_U = max(sumWeight_U, 1e-6f);

		sum_S /= sumWeight_S;
		sum_U /= sumWeight_U;
		sum_M /= float4(sumWeight_S.xx, sumWeight_U.xx);

		// compute variance for direct and indirect illumination using first and second moments
		float2 variance = sum_M.ga - sum_M.rb * sum_M.rb;

		// give the variance a boost for the first frames
		variance *= 4.0 / max(1.0,historyLength);

		texOutputS[center] = float4(sum_S, variance.r);
		texOutputU[center] = float4(sum_U, variance.g);
	}
	else
	{
		texOutputS[center] = texInS[center];
		texOutputU[center] = texInU[center];
	}
}

