// Copyright (c) 2019, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).

#define POSITION 0
#define NORMAL 1
#define COLOR 2
#define STDEV 3

RWStructuredBuffer<float4> levelAttribs[4] : register(u0);
RWStructuredBuffer<float> levelWeights : register(u4);
StructuredBuffer<float4> vplAttribs[3] : register(t0); //sorted

cbuffer CSConstants : register(b0)
{
	int numVpls;
	int level;
	int highestLevel;
	float cellSize;
	float3 corner;
};

void InterlockedAddFloat3(RWStructuredBuffer<float4> resource, uint addr, float3 value)
{
	uint i_val = asuint(value.x);
	uint tmp0 = 0;
	uint tmp1;

	[allow_uav_condition]
	while (true)
	{
		InterlockedCompareExchange(resource[addr].x, tmp0, i_val, tmp1);
		if (tmp1 == tmp0)
			break;

		tmp0 = tmp1;
		i_val = asuint(value.x + asfloat(tmp1));
	}

	i_val = asuint(value.y);
	tmp0 = 0;

	[allow_uav_condition]
	while (true)
	{
		InterlockedCompareExchange(resource[addr].y, tmp0, i_val, tmp1);
		if (tmp1 == tmp0)
			break;

		tmp0 = tmp1;
		i_val = asuint(value.y + asfloat(tmp1));
	}

	i_val = asuint(value.z);
	tmp0 = 0;

	[allow_uav_condition]
	while (true)
	{
		InterlockedCompareExchange(resource[addr].z, tmp0, i_val, tmp1);
		if (tmp1 == tmp0)
			break;

		tmp0 = tmp1;
		i_val = asuint(value.z + asfloat(tmp1));
	}
}

void InterlockedAddFloat(RWStructuredBuffer<float> resource, uint addr, float value)
{
	uint i_val = asuint(value);
	uint tmp0 = 0;
	uint tmp1;

	[allow_uav_condition]
	while (true)
	{
		InterlockedCompareExchange(resource[addr], tmp0, i_val, tmp1);
		if (tmp1 == tmp0)
			break;

		tmp0 = tmp1;
		i_val = asuint(value + asfloat(tmp1));
	}
}


[numthreads(1024, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	int tid = DTid.x / 8;
	if (tid < numVpls)
	{
		float3 normPos = (vplAttribs[POSITION][tid].xyz - corner) / cellSize;
		int levelRes = (1 << (highestLevel - level)) + 1;
		int3 cellId = int3(normPos);
		float3 interp = normPos - int3(normPos);

		int vId = DTid.x % 8;
		float weight = ((vId & 1) ? interp.x : (1 - interp.x)) * ((vId & 2) ? interp.y : (1 - interp.y)) * ((vId & 4) ? interp.z : (1 - interp.z));
		int3 vertId = cellId + int3(vId % 2, (vId % 4) / 2, vId / 4);
		int nid = vertId.x + vertId.y * levelRes + vertId.z * levelRes * levelRes;

		bool keepWaiting = true;
		uint lock;

		float4 deltaAttribs[4];
		deltaAttribs[0] = vplAttribs[0][tid];
		deltaAttribs[1] = weight * vplAttribs[1][tid];
		deltaAttribs[2] = weight * vplAttribs[2][tid];
		deltaAttribs[3] = weight * deltaAttribs[0] * deltaAttribs[0];
		deltaAttribs[0] *= weight;
	
		InterlockedAddFloat3(levelAttribs[0], nid, deltaAttribs[0]);
		InterlockedAddFloat3(levelAttribs[1], nid, deltaAttribs[1]);
		InterlockedAddFloat3(levelAttribs[2], nid, deltaAttribs[2]);
		InterlockedAddFloat3(levelAttribs[3], nid, deltaAttribs[3]);
		InterlockedAddFloat(levelWeights, nid, weight);
	}

}