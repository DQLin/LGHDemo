// Modified from the Denoiser code in Heitz2018Shadow-reference-code for
// "Combining Analytic Direct Illumination and Stochastic Shadows"

#define RADIUS 10
#define GROUPSIZE 16

cbuffer Constants : register(b0)
{
	float4 axis0axis1FarNear; //first pass (1,0) second pass(0,1)
	float4 camera_projInfo;
	int imgWidth;
	int imgHeight;
}

struct TapKey {
	float csZ;
	float3 csPosition;
	float3 normal;
	float analytic;
};

RWTexture2D<float3> resultS : register(u2);
RWTexture2D<float3> resultU : register(u3);
RWTexture2D<float3> resultRatio : register(u4);

Texture2D<float> NoiseEstimation : register(t1);
Texture2D<float3> ShadowedStochastic : register(t2);
Texture2D<float3> UnshadowedStochastic : register(t3);
Texture2D<float4> texNormal		    : register(t4);
Texture2D<float> texDepth		: register(t5);
Texture2D<float3> texAnalytic		: register(t6);

SamplerState sampler0 : register(s0);

static const float DEPTH_WEIGHT = 1.0f;
static const float NORMAL_WEIGHT = 1.5f;
static const float PLANE_WEIGHT = 1.5f;
static const float ANALYTIC_WEIGHT = 0.09f;

float reconstructCSZ(float d) {
	return (2 * axis0axis1FarNear.w) / (axis0axis1FarNear.z + axis0axis1FarNear.w - d * (axis0axis1FarNear.z - axis0axis1FarNear.w));
}

//according to G3D doc, y in clip space is inverted. but that doesn't matter
float3 reconstructCSPosition(float2 S, float z, float4 projInfo) {
	return float3((S * float2(projInfo.x, projInfo.y) + float2(projInfo.z, projInfo.w)) * z, z);
}

float intensity(float3 color) {
	return 0.299*color.r + 0.587*color.g + 0.114*color.b;
}

TapKey getTapKey(float2 uv, float2 pixelPos) {
	TapKey key;
	if ((DEPTH_WEIGHT != 0.0) || (PLANE_WEIGHT != 0.0)) {
		float z = texDepth.SampleLevel(sampler0, uv, 0);
		key.csZ = reconstructCSZ(z);
	}
	if (PLANE_WEIGHT != 0.0) {
		key.csPosition = reconstructCSPosition(pixelPos, key.csZ, camera_projInfo);
	}
	if ((NORMAL_WEIGHT != 0.0) || (PLANE_WEIGHT != 0.0)) {
		float3 n = texNormal.SampleLevel(sampler0, uv, 0).rgb;
		key.normal = n;
	}
	if (ANALYTIC_WEIGHT != 0.0) {
		float3 a = texAnalytic.SampleLevel(sampler0, uv, 0);
		key.analytic = intensity(a);
	}
	return key;
}

float calculateBilateralWeight(TapKey center, TapKey tap) {

	float depthWeight = 1.0;
	float normalWeight = 1.0;
	float planeWeight = 1.0;
	float analyticWeight = 1.0;

	if (DEPTH_WEIGHT != 0.0) {
		depthWeight = max(0.0, 1.0 - abs(tap.csZ - center.csZ) * DEPTH_WEIGHT);
	}

	if (NORMAL_WEIGHT != 0.0) {
		float normalCloseness = dot(tap.normal, center.normal);
		normalCloseness = normalCloseness * normalCloseness;
		normalCloseness = normalCloseness * normalCloseness;

		float normalError = (1.0 - normalCloseness);
		normalWeight = max((1.0 - normalError * NORMAL_WEIGHT), 0.0);
	}

	if (PLANE_WEIGHT != 0.0) {
		float lowDistanceThreshold2 = 0.001;
		float3 dq = center.csPosition - tap.csPosition;
		float distance2 = dot(dq, dq);
		float planeError = max(abs(dot(dq, tap.normal)), abs(dot(dq, center.normal)));
		planeWeight = (distance2 < lowDistanceThreshold2) ? 1.0 :
			pow(max(0.0, 1.0 - 2.0 * PLANE_WEIGHT * planeError / sqrt(distance2)), 2.0);
	}

	if (ANALYTIC_WEIGHT != 0.0) {
		float aDiff = abs(tap.analytic - center.analytic) * 10.0;
		analyticWeight = max(0.0, 1.0 - (aDiff * ANALYTIC_WEIGHT));
	}

	return depthWeight * normalWeight * planeWeight * analyticWeight;
}


[numthreads(GROUPSIZE, GROUPSIZE, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 GTid : SV_GroupThreadID, uint3 Gid : SV_GroupID, uint GI : SV_GroupIndex)
{
	float noiseLevel;
	noiseLevel = NoiseEstimation.SampleLevel(sampler0, (DTid.xy + 0.5) / float2(imgWidth, imgHeight), 0);

	float gaussianRadius = saturate(noiseLevel * 1.5) * RADIUS;

	float3 resultSTemp = 0.0;
	float3 resultUTemp = 0.0;
	float2 center = DTid.xy + 0.5;
	float2 centerUV = center / float2(imgWidth, imgHeight);

	if (gaussianRadius > 0.5)
	{
		float3 sumS = 0, sumU = 0;
		float totalWeight = 0.0;
		TapKey key = getTapKey(centerUV, center);

		for (int r = -RADIUS; r <= RADIUS; ++r) {
			int2 tapOffset = int2(axis0axis1FarNear.xy * r);
			float2 tapLoc = center + tapOffset;
			float2 tapUV = tapLoc / float2(imgWidth, imgHeight);
			float gaussianParam = float(r) / gaussianRadius;
			float gaussian = exp(-gaussianParam * gaussianParam);
			float weight = gaussian * ((r == 0) ? 1.0 : calculateBilateralWeight(key, getTapKey(tapUV, tapLoc)));
			sumS += ShadowedStochastic.SampleLevel(sampler0, tapUV, 0) * weight;
			sumU += UnshadowedStochastic.SampleLevel(sampler0, tapUV, 0) * weight;
			totalWeight += weight;
		}
		resultSTemp = sumS / totalWeight;
		resultUTemp = sumU / totalWeight;
	}
	else
	{
		resultSTemp = ShadowedStochastic.SampleLevel(sampler0, centerUV, 0);
		resultUTemp = UnshadowedStochastic.SampleLevel(sampler0, centerUV, 0);
	}

	resultS[DTid.xy] = resultSTemp;
	resultU[DTid.xy] = resultUTemp;

	if (axis0axis1FarNear.x == 0.0)
	{ //second pass
		float3 temp;
		temp.x = (resultUTemp.x < 0.0001) ? 1.0 : (resultSTemp.x / resultUTemp.x);
		temp.y = (resultUTemp.y < 0.0001) ? 1.0 : (resultSTemp.y / resultUTemp.y);
		temp.z = (resultUTemp.z < 0.0001) ? 1.0 : (resultSTemp.z / resultUTemp.z);
		resultRatio[DTid.xy] = temp;
	}
}