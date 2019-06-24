// Inspired by the sample fragment shader provided in [Dammertz et al., 2010]

Texture2D<float3> ShadowedStochastic : register(t2);
Texture2D<float3> UnshadowedStochastic : register(t3);
Texture2D<float4> texNormal		    : register(t4);
Texture2D<float4> texPosition		: register(t5);
Texture2D<float3> SOriginal		: register(t6);
Texture2D<float3> UOriginal		: register(t7);
Texture2D<float3> Analytic		: register(t8);


RWTexture2D<float3> resultS : register(u2);
RWTexture2D<float3> resultU : register(u3);
RWTexture2D<float3> resultRatio : register(u4);

cbuffer CSConstants : register(b0)
{
	float c_phi;
	float n_phi;
	float p_phi;
	float stepWidth;
	int iter;
	int maxIter;
}

static const float kernel[3] = { 3.0 / 8.0, 1.0 / 4.0, 1.0 / 16.0 };

[numthreads(16, 16, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	float4 sum = 0.0;

	int2 center = DTid.xy;
	float3 centerColorS = ShadowedStochastic[center];
	float3 centerColorU = UnshadowedStochastic[center];
	float3 centerOriginalS = SOriginal[center];
	float3 centerOriginalU = UOriginal[center];
	float3 centerNormal = texNormal[center].xyz;
	float3 centerPosition = texPosition[center].xyz;

	float3 sum_S = 0.0;
	float3 sum_U = 0.0;
	float sumWeight_S = 0.0;
	float sumWeight_U = 0.0;

	float3 resultSTemp;
	float3 resultUTemp;

	for (int yOffset = -2; yOffset <= 2; yOffset++)
	{
		for (int xOffset = -2; xOffset <= 2; xOffset++)
		{
			int2 tap = center + stepWidth * int2(xOffset, yOffset);
			float3 tapColorS = ShadowedStochastic[tap];
			float3 tapColorU = UnshadowedStochastic[tap];
			float3 tapOriginalS = SOriginal[tap];
			float3 tapOriginalU = UOriginal[tap];
			float3 tapNormal = texNormal[tap].xyz;
			float3 tapPosition = texPosition[tap].xyz;
			float kernelWeight = kernel[abs(xOffset)] * kernel[abs(yOffset)];

			float3 diff = tapOriginalS - centerOriginalS;
			float dist2 = dot(diff, diff);
			float cS_w = min(exp(-dist2 / c_phi), 1.0);

			diff = tapOriginalU - centerOriginalU;
			dist2 = dot(diff, diff);
			float cU_w = min(exp(-dist2 / c_phi), 1.0);

			diff = tapNormal - centerNormal;
			dist2 = dot(diff, diff);
			float n_w = min(exp(-dist2 / n_phi), 1.0);

			diff = tapPosition - centerPosition;
			dist2 = dot(diff, diff);
			float p_w = min(exp(-dist2 / p_phi), 1.0);

			float weight = n_w * p_w * kernelWeight;
			
			sum_S += tapColorS * weight * cS_w;
			sum_U += tapColorU * weight * cU_w;
			sumWeight_S += weight * cS_w;
			sumWeight_U += weight * cU_w;
		}
	}

	resultSTemp = sum_S / sumWeight_S;
	resultUTemp = sum_U / sumWeight_U;

	resultS[center] = resultSTemp;
	resultU[center] = resultUTemp;

	if (iter == maxIter - 1) //final pass
	{
		float3 temp;
		if (resultUTemp.x < 0.00001 || resultUTemp.y < 0.00001 || resultUTemp.z < 0.00001) temp = 1.0;
		else
		{
			temp.x = (resultSTemp.x / resultUTemp.x);
			temp.y = (resultSTemp.y / resultUTemp.y);
			temp.z = (resultSTemp.z / resultUTemp.z);
		}
		resultRatio[center] =  temp;
	}
}