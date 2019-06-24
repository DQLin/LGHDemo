#define GROUPSIZE 16
#define RADIUS 8

Texture2D<float> Discontinuity : register(t0);
Texture2D<float3> AnalyticInterleaved : register(t1);
RWTexture2D<float3> Analytic : register(u0);

cbuffer CSConstants : register(b0)
{
	int axis0;
	int axis1;
	int imgWidth;
	int imgHeight;
}

groupshared float3 lds[(GROUPSIZE + 2 * RADIUS)*GROUPSIZE];
groupshared float lds_discon[(GROUPSIZE + 2 * RADIUS)*GROUPSIZE];

static const float GaussianWeight[RADIUS + 1] = { 0.003924,0.008962,0.018331,0.033585,0.055119,0.081029,0.106701,0.125858,0.132980 };

// separable 17x17 geometry aware gaussian blur

[numthreads(GROUPSIZE, GROUPSIZE, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint3 Gid : SV_GroupID, uint3 GTid : SV_GroupThreadID, uint GI : SV_GroupIndex)
{
	int2 center = DTid.xy;
	int2 axis = int2(axis0, axis1);

	int xOffset = axis.x * RADIUS;
	int yOffset = axis.y * RADIUS;

	int ldsWidth = GROUPSIZE + 2 * xOffset;
	int ldsHeight = GROUPSIZE + 2 * yOffset;
	int ldsCount = ldsWidth * ldsHeight;
	int groupSize = GROUPSIZE * GROUPSIZE;
	int2 groupOffset = int2(GROUPSIZE*Gid.x - xOffset, GROUPSIZE*Gid.y - yOffset);

	for (int i = 0; i < ldsCount; i += groupSize)
	{
		float id = i + GI;
		if (id < ldsCount)
		{
			int xpos = groupOffset.x + (id % ldsWidth);
			int ypos = groupOffset.y + (id / ldsWidth);

			lds[id] = AnalyticInterleaved[int2(xpos, ypos)];
			lds_discon[id] = Discontinuity[int2(xpos, ypos)];
		}
	}
	GroupMemoryBarrierWithGroupSync();

	int2 lpos = int2(GTid.x + xOffset, GTid.y + yOffset);
	int lpos1D = lpos.y * ldsWidth + lpos.x;

	float3 colorSum = GaussianWeight[RADIUS] * lds[lpos1D];
	float weightSum = GaussianWeight[RADIUS];

	bool InDiscon = lds_discon[lpos1D] == 1;

	//explore -x/-y
	for (int i = 1; i <= RADIUS; i++)
	{
		int2 tap = lpos - i * axis;
		int tap1D = tap.y * ldsWidth + tap.x;
		if (lds_discon[tap1D] == 1 && !InDiscon || any(groupOffset + tap) < 0) break;
		float weight = GaussianWeight[8 - i];
		colorSum += lds[tap1D] * weight;
		weightSum += weight;
	}

	if (!InDiscon)
	{
		//explore +x/+y
		for (int i = 1; i <= RADIUS; i++)
		{
			int2 tap = lpos + i * axis;
			int tap1D = tap.y * ldsWidth + tap.x;
			if (lds_discon[tap1D] == 1 || any(groupOffset + tap > int2(imgWidth, imgHeight))) break;
			float weight = GaussianWeight[8 - i];
			colorSum += lds[tap1D] * weight;
			weightSum += weight;
		}
	}

	float3 finalColor = colorSum / weightSum;

	Analytic[center] = finalColor;
}