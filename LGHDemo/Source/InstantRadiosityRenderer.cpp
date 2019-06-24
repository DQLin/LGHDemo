#include "InstantRadiosityRenderer.h"

void InstantRadiosityRenderer::Initialize(Model1 * model, int numModels, int scrWidth, int scrHeight)
{
	vplManager.Initialize(model, numModels);
	resultBuffer.Create(L"IR result", scrWidth, scrHeight, 1, DXGI_FORMAT_R32G32B32A32_FLOAT);
	vplManager.InitializeIRViews(&resultBuffer);
}

void InstantRadiosityRenderer::GenerateVPLs(GraphicsContext & context, Vector3 lightDirection, float lightIntensity)
{
	vplManager.GenerateVPLs(context, 12.3, lightDirection, lightIntensity);
}

void InstantRadiosityRenderer::Render(GraphicsContext& gfxContext, int scrWidth, int scrHeight, int currentVPL)
{
	vplManager.ComputeInstantRadiosity(gfxContext, scrWidth, scrHeight, currentVPL);
}

