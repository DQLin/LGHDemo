#pragma once
#include "ViewHelper.h"
#include "ColorBuffer.h"
#include "dxgi1_3.h"
#include "GameCore.h"
#include "GraphicsCore.h"
#include "BufferManager.h"
#include "VPLManager.h"

// a brute force VPL global illumination renderer
class InstantRadiosityRenderer
{
public:
	InstantRadiosityRenderer() {};
	void Initialize(Model1 * model, int numModels, int scrWidth, int scrHeight);
	void GenerateVPLs(GraphicsContext& context, Vector3 lightDirection, float lightIntensity);
	void Render(GraphicsContext & gfxContext, int scrWidth, int scrHeight, int currentVPL);

	VPLManager vplManager;
	ColorBuffer resultBuffer;
};