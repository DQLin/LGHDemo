Texture2D<float3> AnalyticInterleaved : register(t0);
RWTexture2D<float3> Analytic : register(u0);

cbuffer CSConstants : register(b0)
{
	int scrWidth;
	int scrHeight;
	int n;
	int m;
}

[numthreads(16, 16, 1)]
void main( uint3 DTid : SV_DispatchThreadID )
{
	int2 pos = int2(DTid.xy);
	int tileWidth = scrWidth / n;
	int tileHeight = scrHeight / m;
	int2 deinterleavedCoord = int2((pos.x % n)*tileWidth + pos.x / n, (pos.y % m)*tileHeight + pos.y / m);
	Analytic[pos] = AnalyticInterleaved[deinterleavedCoord];
}