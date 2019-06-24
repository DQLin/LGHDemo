// Copyright (c) 2019, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).

StructuredBuffer<float4> leveloneAttribs[4] : register(t0);
StructuredBuffer<float> leveloneWeights : register(t4);
RWStructuredBuffer<float4> levelTaskAttribs[4] : register(u0);
RWStructuredBuffer<float> levelTaskWeights : register(u4);

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

[numthreads(4, 4, 4)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	//DTid is vert id
	int factor = (1 << (level - 1));
	int3 vertId = DTid / taskdivRate;
	int3 starts = factor * (vertId - 1);
	int stepSize = (2 * factor) / taskdivRate;
	int3 taskId = DTid % taskdivRate;
	starts += stepSize * taskId;
	int3 ends = starts + stepSize;

	starts = min(leveloneRes, max(0, starts));
	ends = min(leveloneRes, max(0, ends));

	float3 positionSum = 0;
	float3 normalSum = 0;
	float3 colorSum = 0;
	float3 stdevSum = 0;
	float weightSum = 0;

	int levelRes = (1 << (highestLevel - level)) + 1;

	//static const int highestLevelRes = 2;

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
					if ((normId.x == vertId.x - 1 || normId.x == vertId.x) &&
						(normId.y == vertId.y - 1 || normId.y == vertId.y) &&
						(normId.z == vertId.z - 1 || normId.z == vertId.z))  //within range
					{
						float3 interp = normPos - normId;
						float weight = ((normId.x < vertId.x) ? interp.x : (1 - interp.x)) *
							((normId.y < vertId.y) ? interp.y : (1 - interp.y)) *
							((normId.z < vertId.z) ? interp.z : (1 - interp.z));
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

	int nid = vertId.x + vertId.y * levelRes + vertId.z * levelRes * levelRes;
	int taskId1D = taskId.z*taskdivRate*taskdivRate + taskId.y*taskdivRate + taskId.x;
	int arrayId = numTasksPerVertex * nid + taskId1D;
	if (weightSum > 0)
	{
		levelTaskAttribs[0][arrayId] = float4(positionSum, 1);
		levelTaskAttribs[1][arrayId] = float4(normalSum, 1);
		levelTaskAttribs[2][arrayId] = float4(colorSum, 1);
		levelTaskAttribs[3][arrayId] = float4(stdevSum, 1);
		levelTaskWeights[arrayId] = weightSum;
	}
	else
	{
		levelTaskWeights[arrayId] = 0;
	}
}