Texture2D<float> Depth : register(t36);

cbuffer Constants : register(b0)
{
	uint dsRate; //downsampling rate
};


float main(float4 screenPos : SV_POSITION) : SV_DEPTH
{
	int x = int(screenPos.x);
	int y = int(screenPos.y);

	float maxDepth = 0;

	for (int i = 0; i < dsRate; i++)
	{
		for (int j = 0; j < dsRate; j++)
		{
			maxDepth += Depth[int2(dsRate*x + j, dsRate*y + i)];
		}
	}
	return maxDepth / (dsRate*dsRate);
}