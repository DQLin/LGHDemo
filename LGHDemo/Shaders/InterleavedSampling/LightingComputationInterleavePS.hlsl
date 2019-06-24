// Copyright (c) 2019, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).

#include "../RandomGenerator.hlsli"

#define USELOCK

struct PSInput
{
	float4 position : SV_POSITION;
	nointerpolation float4 vposition : VPOSITION;
	nointerpolation float3 vnormal : VNORMAL;
	nointerpolation float3 vcolor : VCOLOR;
	nointerpolation float3 vdev : VDEV;
	nointerpolation uint vlevel : VLEVEL;
	nointerpolation uint instanceId : INSTANCEID;
};

cbuffer PSConstants : register(b0)
{
	int scrWidth;
	int scrHeight;
	float invNumPaths;
	int numLevels;
	float4 halton[4];
	int tileOffset;
	float devScale;
	int shadowRate;
	int minLevel;
	int minShadowLevel;
	int frameId;
	int temporalRandom;
	float sceneRadius;
}

Texture2D<float4> texPosition		: register(t32);
Texture2D<float4> texNormal		    : register(t33);
Texture2D<float4> texAlbedo			: register(t34);
Texture2D<float4> texSpecular		: register(t35);

Texture2D<float4> blueNoiseTex0 : register(t64);
Texture2D<float4> blueNoiseTex1 : register(t65);
Texture2D<float4> blueNoiseTex2 : register(t66);

globallycoherent RWTexture2D<float> runningSum : register(u2);
globallycoherent RWTexture2D<uint> lockImage : register(u3);
globallycoherent RWTexture2D<uint> vplSampleBuffer : register(u4);
globallycoherent RWTexture2D<float> pdfBuffer : register(u5);

SamplerState samp : register(s0);

static const float PI = 3.141592654;

float hash(float2 st)
{
	return frac(1.0e4 * sin(17.0*st.x + 0.1*st.y) *
		(0.1 + abs(sin(13.0*st.y + st.x)))
	);
}

float hash3D(float3 stq)
{
	return hash(float2(hash(stq.xy), stq.z));
}

float3 lightingFunction(float3 sp, float3 sn, float3 lp, float3 ln, float3 c, float3 dev,
	uint level, uint instanceId, int2 screenPos)
{
	// analytic
	float3 lightDir = normalize(lp - sp);
	float dist = length(lp - sp);
	float3 diffuse = max(dot(sn, lightDir), 0.0) * c * invNumPaths;
	float bias = 0.01 * sceneRadius;
	bias *= bias;
	diffuse *= max(dot(ln, -lightDir), 0.0) / (dist*dist + bias);

	if (level >= minShadowLevel && shadowRate > 0)
	{
		if (temporalRandom)
		{
			RandomSequence RandSequence;
			for (int yoff = 0; yoff < shadowRate; yoff++)
			{
				for (int xoff = 0; xoff < shadowRate; xoff++)
				{
					int id = yoff * shadowRate + xoff;
					RandomSequence_Initialize(RandSequence,
						(shadowRate * scrWidth * screenPos.y + shadowRate * screenPos.x) + id, instanceId + tileOffset + frameId);
					RandSequence.Type = 0;

					float3 randomPos;
					{
						float3 rand_1;
						randomPos = RandomSequence_GenerateSample3D(RandSequence);
						rand_1 = RandomSequence_GenerateSample3D(RandSequence);
						if (rand_1.x > (cos(randomPos.x*PI) + 1)*0.5) randomPos.x -= 1.0;
						if (rand_1.y > (cos(randomPos.y*PI) + 1)*0.5) randomPos.y -= 1.0;
						if (rand_1.z > (cos(randomPos.z*PI) + 1)*0.5) randomPos.z -= 1.0;
					}
					float3 rlp = lp + devScale * randomPos * dev;
					float3 rlightDir = normalize(rlp - sp);
					float rdist = length(rlp - sp);
					float3 rdiffuse = max(dot(sn, rlightDir), 0.0) * c * invNumPaths;
					rdiffuse *= max(dot(ln, -rlightDir), 0.0) / (rdist*rdist + bias); 
					float pdf = 0.299*rdiffuse.x + 0.587*rdiffuse.y + 0.114*rdiffuse.z;

					if (pdf > 0.0)
					{
						bool keepWaiting = true;
						int2 subScreenPos = shadowRate * screenPos + int2(xoff, yoff);

#ifdef USELOCK
						while (keepWaiting)
						{
							uint lock;
							InterlockedCompareExchange(lockImage[subScreenPos], 0, 1, lock);
							if (lock == 0)
							{
#endif
								float sum = runningSum[subScreenPos];
								sum += pdf;
								float2 subPos = float2(screenPos)+float2(0.5 + xoff * 1.0, 0.5 + yoff * 1.0) / shadowRate;
								//roll a dice
								float prob = RandomSequence_GenerateSample1D(RandSequence);
								if (prob < pdf / sum)
								{ //replace value in buffer
									vplSampleBuffer[subScreenPos] = instanceId + tileOffset;
									pdfBuffer[subScreenPos] = pdf;
								}
								runningSum[subScreenPos] = sum;
#ifdef USELOCK
								lockImage[subScreenPos] = 0;
								keepWaiting = false;
							}
						}
#endif
					}
				}
			}
		}
		else
		{
			float4 noise0 = blueNoiseTex0[screenPos & int2(127, 127)];
			float4 noise1 = blueNoiseTex1[screenPos & int2(127, 127)];

			for (int yoff = 0; yoff < shadowRate; yoff++)
			{
				for (int xoff = 0; xoff < shadowRate; xoff++)
				{
					int id = yoff * shadowRate + xoff;
					float3 randomPos;
					{
						randomPos = float3(frac(halton[id].x + noise0.x), frac(halton[id].y + noise0.y), frac(halton[id].z + noise0.z));
						float3 rand_1 = float3(frac(halton[id].w + noise1.x), frac(halton[id].x + noise1.y), frac(halton[id].y + noise1.z));
						if (rand_1.x > (cos(randomPos.x*PI) + 1)*0.5) randomPos.x -= 1.0;
						if (rand_1.y > (cos(randomPos.y*PI) + 1)*0.5) randomPos.y -= 1.0;
						if (rand_1.z > (cos(randomPos.z*PI) + 1)*0.5) randomPos.z -= 1.0;
					}
					
					float3 rlp = lp + devScale * randomPos * dev;
					float3 rlightDir = normalize(rlp - sp);
					float rdist = length(rlp - sp);
					float3 rdiffuse = max(dot(sn, rlightDir), 0.0) * c * invNumPaths;
					rdiffuse *= max(dot(ln, -rlightDir), 0.0) / (rdist*rdist + bias); 
					float pdf = 0.299*rdiffuse.x + 0.587*rdiffuse.y + 0.114*rdiffuse.z;

					if (pdf > 0.0)
					{
						bool keepWaiting = true;
						int2 subScreenPos = shadowRate * screenPos + int2(xoff, yoff);

#ifdef USELOCK
						while (keepWaiting)
						{
							uint lock;
							InterlockedCompareExchange(lockImage[subScreenPos], 0, 1, lock);
							if (lock == 0)
							{
#endif
								float sum = runningSum[subScreenPos];
								sum += pdf;
								float2 subPos = float2(screenPos)+float2(0.5 + xoff * 1.0, 0.5 + yoff * 1.0) / shadowRate;
								//roll a dice
								float prob = hash3D(float3(subPos, float(instanceId + tileOffset)));
								if (prob < pdf / sum)
								{ //replace value in buffer
									vplSampleBuffer[subScreenPos] = instanceId + tileOffset;
									pdfBuffer[subScreenPos] = pdf;
								}
								runningSum[subScreenPos] = sum;
#ifdef USELOCK
								lockImage[subScreenPos] = 0;
								keepWaiting = false;
							}
						}
#endif
					}
				}
			}
		}
	}
	return diffuse;
}

[earlydepthstencil]
float3 main(PSInput input) : SV_TARGET0
{
	int2 pixelPos = input.position.xy;
	float3 surfaceWorldPos = texPosition[pixelPos];
	float lightRadius = input.vposition.w;
	float dist = length(surfaceWorldPos - input.vposition.xyz);

	float ratio;
	if (dist < lightRadius)
	{
		if (input.vlevel == minLevel) ratio = 1;
		else ratio = (dist - 0.5 * lightRadius) / (0.5*lightRadius);
	}
	else
	{
		if (input.vlevel < numLevels - 1) ratio = (2 * lightRadius - dist) / lightRadius;
		else ratio = 1;
	}

	if (ratio <= 0)
	{
		discard;
	}

	float3 surfaceNormal = texNormal[pixelPos];

	if (dot(surfaceNormal, input.vposition.xyz - surfaceWorldPos) <= 0) discard;

	float3 result = lightingFunction(surfaceWorldPos, surfaceNormal, input.vposition.xyz, input.vnormal,
		ratio*input.vcolor, input.vdev, input.vlevel, input.instanceId, pixelPos);
	return result;
}