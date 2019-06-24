// Copyright (c) 2019, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).

StructuredBuffer<float4> leveloneAttribs[4] : register(t0);
StructuredBuffer<float> leveloneWeights : register(t4);
RWStructuredBuffer<float4> levelAttribs[4] : register(u0);
RWStructuredBuffer<float> levelWeights : register(u4);

cbuffer CSConstants : register(b0)
{
	int level;
	int highestLevel;
	int leveloneRes;
	float cellSize;
	float3 corner;
};

[numthreads(4, 4, 4)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	//DTid is vert id
	int factor = (1 << (level - 1));
	int3 starts = max(0, factor * (DTid - 1));
	int3 ends = min(leveloneRes, factor * (DTid + 1));

	float3 positionSum = 0;
	float3 normalSum = 0;
	float3 colorSum = 0;
	float3 stdevSum = 0;
	float weightSum = 0;

	int levelRes = (1 << (highestLevel - level)) + 1;

	[unroll]
	for (int k = starts.z; k < ends.z; k++)
	{
		for (int j = starts.y; j < ends.y; j++)
		{
			for (int i = starts.x; i < ends.x; i++)
			{
				// find level one linear Id
				int nid = i + j * leveloneRes + k * leveloneRes * leveloneRes;
				float weight = leveloneWeights[nid];
				if (weight > 0)
				{
					float3 p = leveloneAttribs[0][nid].xyz;
					float3 normPos = (p - corner) / cellSize;
					int3 normId = int3(normPos);
					if ((normId.x == DTid.x - 1 || normId.x == DTid.x) &&
						(normId.y == DTid.y - 1 || normId.y == DTid.y) &&
						(normId.z == DTid.z - 1 || normId.z == DTid.z))  //within range
					{
						float3 interp = normPos - normId;
						float weight = ((normId.x < DTid.x) ? interp.x : (1 - interp.x)) *
							((normId.y < DTid.y) ? interp.y : (1 - interp.y)) *
							((normId.z < DTid.z) ? interp.z : (1 - interp.z));
						positionSum += weight * p;
						normalSum += weight * leveloneAttribs[1][nid].xyz;
						colorSum += weight * leveloneAttribs[2][nid].xyz;
						stdevSum += weight * p * p;
						weightSum += weight;					
					}
				}
			}
		}
	}

	int nid = DTid.x + DTid.y * levelRes + DTid.z * levelRes * levelRes;
	if (weightSum > 0)
	{
		levelAttribs[0][nid] = float4(positionSum, 1);
		levelAttribs[1][nid] = float4(normalSum, 1);
		levelAttribs[2][nid] = float4(colorSum, 1);
		levelAttribs[3][nid] = float4(stdevSum, 1);
		levelWeights[nid] = weightSum;
	}
	else
	{
		levelWeights[nid] = 0;
	}
}