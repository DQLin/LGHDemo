// Copyright (c) 2019, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).

#include "DefaultBlockSize.hlsli"

StructuredBuffer<float4> levelTaskAttribs[4] : register(t0);
StructuredBuffer<float> levelTaskWeights : register(t4);
RWStructuredBuffer<float4> levelAttribs[4] : register(u0);
RWStructuredBuffer<float> levelWeights : register(u4);

cbuffer CSConstants : register(b0)
{
	int level;
	int highestLevel;
	int leveloneRes;
	float cellSize;
	float3 corner;
	int pad;
	int taskdivRate; //taskdivRate == k -> kxkxk threads gathering for one vertex
	int numTasksPerVertex;
	int numVerts;
};

[numthreads(DEFAULT_BLOCK_SIZE, 1, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	if (DTid.x < numVerts)
	{
		float3 positionSum = 0;
		float3 normalSum = 0;
		float3 colorSum = 0;
		float3 stdevSum = 0;
		float weightSum = 0;

		for (int i = 0; i < numTasksPerVertex; i++)
		{
			int taskId = numTasksPerVertex * DTid.x + i;
			float weight = levelTaskWeights[taskId];
			if (weight > 0)
			{
				positionSum += levelTaskAttribs[0][taskId].xyz;
				normalSum += levelTaskAttribs[1][taskId].xyz;
				colorSum += levelTaskAttribs[2][taskId].xyz;
				stdevSum += levelTaskAttribs[3][taskId].xyz;
				weightSum += weight;
			}
		}

		if (weightSum > 0)
		{
			//normalize and store
			levelAttribs[0][DTid.x] = float4(positionSum, 0);
			levelAttribs[1][DTid.x] = float4(normalSum, 0);
			levelAttribs[2][DTid.x] = float4(colorSum, 0);
			levelAttribs[3][DTid.x] = float4(stdevSum, 0);
			levelWeights[DTid.x] = weightSum;
		}
		else
		{
			levelWeights[DTid.x] = 0;
		}
	}
}