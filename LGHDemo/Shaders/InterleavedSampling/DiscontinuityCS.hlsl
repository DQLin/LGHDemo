Texture2D<float> texLinearDepth : register(t0);
Texture2D<float> texGradLinearDepth : register(t1);
Texture2D<float4> texNormal			: register(t2);
RWTexture2D<float> Discontinuity : register(u0);

cbuffer CSConstants : register(b0)
{
	float zDiff;
	float norDiff;
}

[numthreads(16, 16, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	int2 center = DTid.xy;
	int2 offsets[3] = { int2(1,0), int2(0,1), int2(1,1) };
	float centerZ = texLinearDepth[center];
	float GradZ = texGradLinearDepth[center];
	float3 centerN = texNormal[center].rgb;

	bool isDiscon = false;
	for (int tapId = 0; tapId < 3; tapId++)
	{
		int2 tap = center + offsets[tapId];
		float3 tapN = texNormal[tap].rgb;
		float tapZ = texLinearDepth[tap];

		bool zDiscon = abs(tapZ - centerZ) / (GradZ + 1e-4) > zDiff;
		bool nDiscon = 1 - dot(tapN, centerN) > norDiff;
		if (zDiscon || nDiscon)
		{
			isDiscon = true;
			break;
		}
	} 
	if (isDiscon) Discontinuity[center] = 1;
	else Discontinuity[center] = 0;
}