RWTexture2D<float4> DeinterleavedPosition : register(u0);
RWTexture2D<float4> DeinterleavedNormal : register(u1);
RWTexture2D<float4> DeinterleavedAlbedo : register(u2);
RWTexture2D<float4> DeinterleavedSpecular : register(u3);

Texture2D<float4> texPosition		: register(t0);
Texture2D<float4> texNormal		    : register(t1);
Texture2D<float4> texAlbedo			: register(t2);
Texture2D<float4> texSpecular		: register(t3);

cbuffer Constants : register(b0)
{
	uint n;
	uint m;
	uint tileWidth;
	uint tileHeight;
}

[numthreads(16, 16, 1)]
void main( uint3 DTid : SV_DispatchThreadID)
{
	int tileX = DTid.x%n;
	int tileY = DTid.y%m;
	int2 deinterleavedPos = int2(tileX * tileWidth + DTid.x / n, tileY * tileHeight + DTid.y / m);

	DeinterleavedPosition[deinterleavedPos] = texPosition[DTid.xy];
	DeinterleavedNormal[deinterleavedPos] = texNormal[DTid.xy];
	DeinterleavedAlbedo[deinterleavedPos] = texAlbedo[DTid.xy];
	DeinterleavedSpecular[deinterleavedPos] = texSpecular[DTid.xy];
}