// Copyright (c) 2019, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).

#include "IRRayLib.hlsli"

[shader("raygeneration")]
void RayGen()
{
	float3 output = 0;

	int2 pixelPos = DispatchRaysIndex().xy;

	float3 SurfacePosition = texPosition[pixelPos].xyz;
	float3 lightPosition = g_vplPositions[currentVPL].xyz;
	float3 lightDir = lightPosition - SurfacePosition;
	float dist = length(lightDir);
	lightDir = lightDir / dist; //normalize

	// cast shadow ray
	RayDesc rayDesc = { SurfacePosition,
						0.001 * sceneRadius,
						lightDir,
						dist };

	ShadowRayPayload payload;
	payload.RayHitT = dist;

	TraceRay(g_accel, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
		~0, 0, 1, 0, rayDesc, payload);
	if (payload.RayHitT == dist) // no occlusion
	{
		float3 SurfaceNormal = texNormal[pixelPos].xyz;

		float3 lightNormal = g_vplNormals[currentVPL].xyz;
		float3 lightColor = g_vplColors[currentVPL].xyz;

		output = max(dot(SurfaceNormal, lightDir), 0.0) * lightColor;
		float bias = 0.01 * sceneRadius;
		bias *= bias;
		output *= max(dot(lightNormal, -lightDir), 0.0) / (dist*dist + bias);
	}

	resultBuffer[pixelPos] += float4(output * invNumPaths, 0);
}
