Texture2D<float4> vplSampleBuffer : register(t0);
RWTexture2D<float4> sharedVplSampleBuffer : register(u0);

cbuffer CSConstants : register(b0)
{
	int scrWidth;
	int scrHeight;
	int n;
	int m;
	int shadowRate;
}

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
	int2 pos = int2(DTid.xy);
	int tileWidth = scrWidth / n;
	int tileHeight = scrHeight / m;
	int2 deinterleavedCoord = int2((pos.x % n)*tileWidth + pos.x / n, (pos.y % m)*tileHeight + pos.y / m);

	for (int yoff = 0; yoff < shadowRate; yoff++)
	{
		for (int xoff = 0; xoff < shadowRate; xoff++)
		{
			int2 subPixelPos = shadowRate * pos + int2(xoff, yoff);
			int2 deinterleavedSubPos = shadowRate * deinterleavedCoord + int2(xoff, yoff);
			sharedVplSampleBuffer[subPixelPos] = vplSampleBuffer[deinterleavedSubPos];
		}
	}
}
