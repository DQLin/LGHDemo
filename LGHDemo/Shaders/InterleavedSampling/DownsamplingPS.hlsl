Texture2D<float> Depth : register(t36);

cbuffer Constants : register(b0)
{
	uint tileWidth;
	uint tileHeight;
	uint interleaveRate;
};

float main(float4 screenPos : SV_POSITION) : SV_DEPTH
{
	int x = int(screenPos.x) % tileWidth;
	int y = int(screenPos.y) % tileHeight;

	int xid = int(screenPos.x) / tileWidth;
	int yid = int(screenPos.y) / tileHeight;

	return Depth[int2(interleaveRate*x + xid, interleaveRate*y + yid)];
}

