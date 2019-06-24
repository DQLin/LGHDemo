// Copyright (c) 2019, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).

#include "../RandomGenerator.hlsli"
#include "HitCommon.hlsli"

cbuffer ShadowTracingConstants : register(b0)
{
	float4 halton[4];
	float invNumPaths;
	int shadowRate;
	int numLevels;
	int minLevel;
	float baseRadius;
	float devScale;
	float alpha;
	int temporalRandom;
	int frameId;
	float sceneRadius;
}

StructuredBuffer<float4> g_lightPositions : register(t9);
StructuredBuffer<float4> g_lightNormals : register(t10);
StructuredBuffer<float4> g_lightColors : register(t11);
StructuredBuffer<float4> g_lightDevs : register(t12);
Texture2D<float4> blueNoiseTex0 : register(t13);
Texture2D<float4> blueNoiseTex1 : register(t14);

Texture2D<float4> texPosition : register(t32);
Texture2D<float4> texNormal : register(t33);
Texture2D<uint> vplSampleBuffer : register(t64);
Texture2D<float> runningSum : register(t65);
Texture2D<float> pdfBuffer : register(t66);
RWTexture2D<float3> ShadowedStochastic : register(u12);
RWTexture2D<float3> UnshadowedStochastic : register(u13);


[shader("raygeneration")]
void RayGen()
{
	float3 output = 0;

	int2 pixelPos = DispatchRaysIndex().xy;
	float3 SurfacePosition = texPosition[pixelPos].xyz;
	float3 SurfaceNormal = texNormal[pixelPos].xyz;

	float3 U = 0, S = 0;
	float bias = 0.01 * sceneRadius;
	bias *= bias;

	if (temporalRandom)
	{
		RandomSequence RandSequence;
		for (int yoff = 0; yoff < shadowRate; yoff++)
		{
			for (int xoff = 0; xoff < shadowRate; xoff++)
			{
				int id = yoff * shadowRate + xoff;

				int2 subScreenPos = shadowRate * pixelPos + int2(xoff, yoff);
				uint currentVPL = vplSampleBuffer[subScreenPos];
				float pdf = pdfBuffer[subScreenPos];
				float3 lightPosition = g_lightPositions[currentVPL].xyz;
				float3 lightColor = g_lightColors[currentVPL].xyz;
				float3 lightNormal = g_lightNormals[currentVPL].xyz;
				float3 lightDev = g_lightDevs[currentVPL].xyz;

				float sum = runningSum[subScreenPos];

				int level = g_lightPositions[currentVPL].w;
				float dist = length(lightPosition - SurfacePosition);

				float lightRadius = alpha * baseRadius * (1 << level);
				float ratio;
				if (dist < lightRadius)
				{
					if (level == minLevel) ratio = 1;
					else ratio = (dist - 0.5 * lightRadius) / (0.5*lightRadius);
				}
				else
				{
					if (level < numLevels - 1) ratio = (2 * lightRadius - dist) / lightRadius;
					else ratio = 1;
				}
				if (ratio <= 0)
				{
					continue;
				}

				// use the same random seed as in lighting computation step
				RandomSequence_Initialize(RandSequence,
					shadowRate * DispatchRaysDimensions().x * subScreenPos.y + subScreenPos.x, currentVPL + frameId);
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

				float3 randLightPos = lightPosition + devScale * randomPos * lightDev;
				float3 randLightDir = normalize(randLightPos - SurfacePosition);
				float rdist = length(randLightPos - SurfacePosition);

				float3 output = max(dot(SurfaceNormal, randLightDir), 0.0) * lightColor * ratio;
				output *= max(dot(lightNormal, -randLightDir), 0.0) / (rdist*rdist + bias);
				float3 uRadiance = sum * output * invNumPaths / pdf;
				if (!isnan(uRadiance.r) && !isnan(uRadiance.g) && !isnan(uRadiance.b)) //uRadiance = 0.0;
				{
					U += (1.f / (shadowRate*shadowRate)) * uRadiance;

					// cast shadow ray
					RayDesc rayDesc = { SurfacePosition,
										0.001 * sceneRadius, //ray offset (need to tune)
										randLightDir,
										rdist };

					ShadowRayPayload payload;
					payload.RayHitT = rdist;

					TraceRay(g_accel, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
						~0, 0, 1, 0, rayDesc, payload);

					if (payload.RayHitT == rdist) // no occlusion
					{
						S += (1.f / (shadowRate*shadowRate)) * uRadiance;
					}
				}
			}
		}
	}
	else
	{
		float4 noise0 = blueNoiseTex0[pixelPos & int2(127, 127)];
		float4 noise1 = blueNoiseTex1[pixelPos & int2(127, 127)];

		for (int yoff = 0; yoff < shadowRate; yoff++)
		{
			for (int xoff = 0; xoff < shadowRate; xoff++)
			{
				int2 subScreenPos = shadowRate * pixelPos + int2(xoff, yoff);
				uint currentVPL = vplSampleBuffer[subScreenPos];
				float pdf = pdfBuffer[subScreenPos];
				float3 lightPosition = g_lightPositions[currentVPL].xyz;
				float3 lightColor = g_lightColors[currentVPL].xyz;
				float3 lightNormal = g_lightNormals[currentVPL].xyz;
				float3 lightDev = g_lightDevs[currentVPL].xyz;

				float sum = runningSum[subScreenPos];

				int level = g_lightPositions[currentVPL].w;
				float dist = length(lightPosition - SurfacePosition);

				float lightRadius = alpha * baseRadius * (1 << level);
				float ratio;
				if (dist < lightRadius)
				{
					if (level == minLevel) ratio = 1;
					else ratio = (dist - 0.5 * lightRadius) / (0.5*lightRadius);
				}
				else
				{
					if (level < numLevels - 1) ratio = (2 * lightRadius - dist) / lightRadius;
					else ratio = 1;
				}
				if (ratio <= 0)
				{
					continue;
				}

				int id = yoff * shadowRate + xoff;
				float3 randomPos;
				{
					randomPos = float3(frac(halton[id].x + noise0.x), frac(halton[id].y + noise0.y), frac(halton[id].z + noise0.z));
					float3 rand_1 = float3(frac(halton[id].w + noise1.x), frac(halton[id].x + noise1.y), frac(halton[id].y + noise1.z));
					if (rand_1.x > (cos(randomPos.x*PI) + 1)*0.5) randomPos.x -= 1.0;
					if (rand_1.y > (cos(randomPos.y*PI) + 1)*0.5) randomPos.y -= 1.0;
					if (rand_1.z > (cos(randomPos.z*PI) + 1)*0.5) randomPos.z -= 1.0;
				}

				float3 randLightPos = lightPosition + devScale * randomPos * lightDev;
				float3 randLightDir = normalize(randLightPos - SurfacePosition);
				float rdist = length(randLightPos - SurfacePosition);

				float3 output = max(dot(SurfaceNormal, randLightDir), 0.0) * lightColor * ratio;
				output *= max(dot(lightNormal, -randLightDir), 0.0) / (rdist*rdist + bias);
				float3 uRadiance = sum * output * invNumPaths / pdf;
				if (!isnan(uRadiance.r) && !isnan(uRadiance.g) && !isnan(uRadiance.b)) //uRadiance = 0.0;
				{
					U += (1.f / (shadowRate*shadowRate)) * uRadiance;

					// cast shadow ray
					RayDesc rayDesc = { SurfacePosition,
										0.001 * sceneRadius, //ray offset (need to tune)
										randLightDir,
										rdist };

					ShadowRayPayload payload;
					payload.RayHitT = rdist;

					TraceRay(g_accel, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
						~0, 0, 1, 0, rayDesc, payload);

					if (payload.RayHitT == rdist) // no occlusion
					{
						S += (1.f / (shadowRate*shadowRate)) * uRadiance;
					}
				}
			}
		}
	}

	ShadowedStochastic[pixelPos] = !isfinite(S) ? 0 : S;
	UnshadowedStochastic[pixelPos] = !isfinite(U) ? 0 : U;
}
