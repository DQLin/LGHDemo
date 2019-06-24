#include "LGHRenderer.h"
#include "AtrousFilterCS.h"
#include "LightingComputationVS.h"
#include "LightingComputationPS.h"
#include "DeinterleaveGBufferCS.h"
#include "DiscontinuityCS.h"
#include "InterleaveCS.h"
#include "BlurInterleaveCS.h"
#include "LightingComputationInterleaveVS.h"
#include "LightingComputationInterleavePS.h"
#include "ScreenShaderVS.h"
#include "ScreenShaderPS.h"
#include "DownsamplingPS.h"
#include "Downsampling2PS.h"
#include "NoiseEstimationCS.h"
#include "DenoiseNECS.h"
#include "BilateralFilteringCS.h"
#include "ComputeGradLinearDepthPS.h"

NumVar LGHRenderer::m_DevScale("Application/LGH/DevScale", 0.5f, 0.0f, 1.0f, 0.1f);
NumVar LGHRenderer::m_CPhi("Application/Filtering/Wavelet/CPhi", 0.5f, 0.0f, 4.0f, 0.01f);
NumVar LGHRenderer::m_NPhi("Application/Filtering/Wavelet/NPhi", 0.1f, 0.0f, 1.0f, 0.01f);
NumVar LGHRenderer::m_PPhi("Application/Filtering/Wavelet/PPhi", 1500, 0.0f, 2e4f, 100.f);
IntVar LGHRenderer::m_WaveletStrength("Application/Filtering/Wavelet/Strength", 1, 0, 2, 1);
NumVar LGHRenderer::m_DisconNDiff("Application/Filtering/Interleave/NDiff", 0.5f, 0.0f, 1.0f, 0.01f);
NumVar LGHRenderer::m_DisconZDiff("Application/Filtering/Interleave/ZDiff", 8.f, 0.0f, 64.f, 1.f);
NumVar LGHRenderer::m_Alpha("Application/LGH/Alpha", 1.0, 1.0, 4.0, 1.0);
BoolVar LGHRenderer::m_IndirectShadow("Application/Enable Indirect Shadow", true);
BoolVar LGHRenderer::m_TemporalRandom("Application/Enable Temporal Random", false);
IntVar LGHRenderer::m_MaxDepth("Application/VPL/Max Ray Depth", 3, 1, 10, 1);

const char* LGHRenderer::shadowFilterTypeText[3] = { "Wavelet", "SVGF", "Bilateral" };
EnumVar LGHRenderer::m_ShadowFilterType("Application/Filter Type for Shadows", 0, 3, shadowFilterTypeText);

const char* LGHRenderer::drawLevelsOptionsText[2] = { "Skip VPLs", "Include VPLs" };
EnumVar LGHRenderer::m_DrawLevels("Application/LGH/Draw Levels", 0, 2, drawLevelsOptionsText);

const char* LGHRenderer::shadowLevelsOptionsText[2] = { "From S2 ", "From minLevel" };
EnumVar LGHRenderer::m_ShadowLevels("Application/LGH/Shadow Levels", 0, 2, shadowLevelsOptionsText);
IntVar LGHRenderer::m_ShadowRate("Application/LGH/Shadow rate", 2, 1, 2, 1);

const char* LGHRenderer::interleaveRateOptionsText[3] = { "N/A", "2x2", "4x4" };
EnumVar LGHRenderer::m_InterleaveRate("Application/LGH/Interleave Rate", 0, 3, interleaveRateOptionsText);

const char* LGHRenderer::PresetVPLEmissionOrderOfMagnitudeText[6] = { "Custom", "10^3", "10^4", "10^5", "10^6", "10^7" };
EnumVar LGHRenderer::m_PresetVPLOrderOfMagnitude("Application/VPL/Preset Density Level", 3, 6, PresetVPLEmissionOrderOfMagnitudeText);

NumVar LGHRenderer::m_VPLEmissionLevel("Application/VPL/Density", 3.9, 0.1, 40.0, 0.1);

void LGHRenderer::InitBuffers(int scrWidth, int scrHeight)
{
	if (interleaveRates[m_InterleaveRate] > 1)
	{
		InitInterleavedBuffers(scrWidth, scrHeight);
	}

	m_AnalyticBuffer.Create(L"Analytic Buffer", scrWidth, scrHeight, 1, DXGI_FORMAT_R11G11B10_FLOAT);

	m_runningSumBuffer.Create(L"Running Sum Buffer", m_ShadowRate*scrWidth, m_ShadowRate*scrHeight, 1, DXGI_FORMAT_R16_FLOAT);
	m_lockImageBuffer.Create(L"Lock Image Buffer", m_ShadowRate*scrWidth, m_ShadowRate*scrHeight, 1, DXGI_FORMAT_R32_UINT);
	m_vplSampleBuffer.Create(L"VPL Id Sample Buffer", m_ShadowRate*scrWidth, m_ShadowRate*scrHeight, 1, DXGI_FORMAT_R32_UINT);
	m_sampleColorBuffer.Create(L"Sample Color Buffer", m_ShadowRate*scrWidth, m_ShadowRate*scrHeight, 1, DXGI_FORMAT_R16_FLOAT); //pdf

	m_ShadowedStochasticBuffer[0].Create(L"ShadowedStochasticBuffer", scrWidth, scrHeight, 1, DXGI_FORMAT_R11G11B10_FLOAT);
	m_UnshadowedStochasticBuffer[0].Create(L"UnshadowedStochasticBuffer", scrWidth, scrHeight, 1, DXGI_FORMAT_R11G11B10_FLOAT);
	m_NoiseEstimationBuffer[0].Create(L"NoiseEstimationBuffer", scrWidth, scrHeight, 1, DXGI_FORMAT_R8_UNORM);

	m_ShadowedStochasticBuffer[1].Create(L"ShadowedStochasticBuffer1", scrWidth, scrHeight, 1, DXGI_FORMAT_R11G11B10_FLOAT);
	m_UnshadowedStochasticBuffer[1].Create(L"UnshadowedStochasticBuffer1", scrWidth, scrHeight, 1, DXGI_FORMAT_R11G11B10_FLOAT);
	m_NoiseEstimationBuffer[1].Create(L"NoiseEstimationBuffer1", scrWidth, scrHeight, 1, DXGI_FORMAT_R8_UNORM);

	m_ShadowedStochasticBuffer[2].Create(L"ShadowedStochasticBuffer2", scrWidth, scrHeight, 1, DXGI_FORMAT_R11G11B10_FLOAT);
	m_UnshadowedStochasticBuffer[2].Create(L"UnshadowedStochasticBuffer2", scrWidth, scrHeight, 1, DXGI_FORMAT_R11G11B10_FLOAT);

	m_SURatioBuffer.Create(L"SURatioBuffer", scrWidth, scrHeight, 1, DXGI_FORMAT_R11G11B10_FLOAT);

	m_LGHSamplingHandles[0] = m_runningSumBuffer.GetUAV();
	m_LGHSamplingHandles[1] = m_lockImageBuffer.GetUAV();
	m_LGHSamplingHandles[2] = m_vplSampleBuffer.GetUAV();
	m_LGHSamplingHandles[3] = m_sampleColorBuffer.GetUAV();

	m_GBufferSrvs[0] = Graphics::g_ScenePositionBuffer.GetSRV();
	m_GBufferSrvs[1] = Graphics::g_SceneNormalBuffer.GetSRV();
	m_GBufferSrvs[2] = Graphics::g_SceneAlbedoBuffer.GetSRV();
	m_GBufferSrvs[3] = Graphics::g_SceneSpecularBuffer.GetSRV();
}

void LGHRenderer::InitInterleavedBuffers(int scrWidth, int scrHeight)
{
	m_ScenePositionBufferArray.Create(L"Position Subbuffers", scrWidth, scrHeight, 1, DXGI_FORMAT_R16G16B16A16_FLOAT);
	m_SceneNormalBufferArray.Create(L"Normal Subbuffers", scrWidth, scrHeight, 1, DXGI_FORMAT_R8G8B8A8_SNORM);
	m_SceneAlbedoBufferArray.Create(L"Albedo Subbuffers", scrWidth, scrHeight, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
	m_SceneSpecularBufferArray.Create(L"Specular Subbuffers", scrWidth, scrHeight, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
	m_DiscontinuityBuffer.Create(L"DiscontinuityBuffer", scrWidth, scrHeight, 1, DXGI_FORMAT_R8_UNORM);
	m_AnalyticBufferDeinterleave.Create(L"Analytic Buffer Deinterleave", scrWidth, scrHeight, 1, DXGI_FORMAT_R11G11B10_FLOAT);
	m_SceneDepthBufferDeinterleave.Create(L"Scene Depth Buffer Deinterleave", scrWidth, scrHeight, 1, DXGI_FORMAT_D32_FLOAT);

	m_GBufferArraySrvs[0] = m_ScenePositionBufferArray.GetSRV();
	m_GBufferArraySrvs[1] = m_SceneNormalBufferArray.GetSRV();
	m_GBufferArraySrvs[2] = m_SceneAlbedoBufferArray.GetSRV();
	m_GBufferArraySrvs[3] = m_SceneSpecularBufferArray.GetSRV();

}

void LGHRenderer::InitRootSignatures()
{
	//Initialize root signature
	m_RootSig.Reset(8, 2);
	SamplerDesc DefaultSamplerDesc;
	DefaultSamplerDesc.MaxAnisotropy = 8;
	m_RootSig.InitStaticSampler(0, DefaultSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig.InitStaticSampler(1, Graphics::SamplerShadowDesc, D3D12_SHADER_VISIBILITY_PIXEL);

	m_RootSig[0].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_VERTEX);
	m_RootSig[1].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 3, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 32, 6, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[4].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 64, 3, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[5].InitAsConstants(1, 2, D3D12_SHADER_VISIBILITY_VERTEX);
	m_RootSig[6].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 4, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[7].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig.Finalize(L"LGHGIRenderer", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	//Initialize compute root signature
	m_ComputeRootSig.Reset(3, 2);
	m_ComputeRootSig.InitStaticSampler(0, Graphics::SamplerPointClampDesc);
	m_ComputeRootSig.InitStaticSampler(1, Graphics::SamplerLinearClampDesc);
	m_ComputeRootSig[0].InitAsConstantBuffer(0);
	m_ComputeRootSig[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 12);
	m_ComputeRootSig[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 12);
	m_ComputeRootSig.Finalize(L"LGHGI Image Processing");
}

void LGHRenderer::InitComputePSOs()
{
#define CreateComputePSO( ObjName, ShaderByteCode ) \
    ObjName.SetRootSignature(m_ComputeRootSig); \
    ObjName.SetComputeShader(ShaderByteCode, sizeof(ShaderByteCode) ); \
    ObjName.Finalize();

//WAVELET
	CreateComputePSO(m_AtrousFilterPSO, g_pAtrousFilterCS);
//BILATERAL
	CreateComputePSO(m_NoiseEstimationPSO, g_pNoiseEstimationCS);
	CreateComputePSO(m_DenoiseNEPSO, g_pDenoiseNECS);
	CreateComputePSO(m_BilateralFilteringPSO, g_pBilateralFilteringCS);

//INTERLEAVED_SAMPLING
	CreateComputePSO(m_DeinterleavePSO, g_pDeinterleaveGBufferCS);
	CreateComputePSO(m_DiscontinuityPSO, g_pDiscontinuityCS);
	CreateComputePSO(m_InterleavePSO, g_pInterleaveCS);
	CreateComputePSO(m_BlurInterleavePSO, g_pBlurInterleaveCS);
}

void LGHRenderer::InitPSOs()
{
	D3D12_INPUT_ELEMENT_DESC lghVertElem[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "VPOSRADIUS",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
		{ "NORMAL",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 2, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
		{ "COLOR",  0,  DXGI_FORMAT_R32G32B32A32_FLOAT, 3, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
		{ "DEV",    0,  DXGI_FORMAT_R32G32B32A32_FLOAT, 4, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 }
	};

	D3D12_INPUT_ELEMENT_DESC screenVertElem[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
	};

	DXGI_FORMAT sceneColorFormat = Graphics::g_SceneColorBuffer.GetFormat();
	DXGI_FORMAT sceneDepthFormat = Graphics::g_SceneDepthBuffer.GetFormat();

	//Initialize PSO
	m_LightingComputationPSO.SetRootSignature(m_RootSig);
	m_LightingComputationPSO.SetRasterizerState(Graphics::RasterizerDefaultCw);
	m_LightingComputationPSO.SetBlendState(Graphics::BlendAdditive);
	m_LightingComputationPSO.SetDepthStencilState(Graphics::DepthStateReadOnlyReversed);
	m_LightingComputationPSO.SetInputLayout(_countof(lghVertElem), lghVertElem);
	m_LightingComputationPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	m_LightingComputationPSO.SetRenderTargetFormats(1, &sceneColorFormat, sceneDepthFormat);
	m_LightingComputationPSO.SetVertexShader(g_pLightingComputationVS, sizeof(g_pLightingComputationVS));
	m_LightingComputationPSO.SetPixelShader(g_pLightingComputationPS, sizeof(g_pLightingComputationPS));
	m_LightingComputationPSO.Finalize();

	m_LightingComputationInterleavePSO.SetRootSignature(m_RootSig);
	m_LightingComputationInterleavePSO.SetRasterizerState(Graphics::RasterizerDefaultCw);
	m_LightingComputationInterleavePSO.SetBlendState(Graphics::BlendAdditive);
	m_LightingComputationInterleavePSO.SetDepthStencilState(Graphics::DepthStateReadOnlyReversed);
	m_LightingComputationInterleavePSO.SetInputLayout(_countof(lghVertElem), lghVertElem);
	m_LightingComputationInterleavePSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	m_LightingComputationInterleavePSO.SetRenderTargetFormats(1, &sceneColorFormat, sceneDepthFormat);
	m_LightingComputationInterleavePSO.SetVertexShader(g_pLightingComputationInterleaveVS, sizeof(g_pLightingComputationInterleaveVS));
	m_LightingComputationInterleavePSO.SetPixelShader(g_pLightingComputationInterleavePS, sizeof(g_pLightingComputationInterleavePS));
	m_LightingComputationInterleavePSO.Finalize();

	m_ComputeGradLinearDepthPSO.SetRootSignature(m_RootSig);
	m_ComputeGradLinearDepthPSO.SetRasterizerState(Graphics::RasterizerDefault);
	m_ComputeGradLinearDepthPSO.SetBlendState(Graphics::BlendDisable);
	m_ComputeGradLinearDepthPSO.SetDepthStencilState(Graphics::DepthStateDisabled);
	m_ComputeGradLinearDepthPSO.SetInputLayout(_countof(screenVertElem), screenVertElem);
	m_ComputeGradLinearDepthPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	m_ComputeGradLinearDepthPSO.SetRenderTargetFormats(1, &Graphics::g_GradLinearDepth.GetFormat(), sceneDepthFormat);
	m_ComputeGradLinearDepthPSO.SetVertexShader(g_pScreenShaderVS, sizeof(g_pScreenShaderVS));
	m_ComputeGradLinearDepthPSO.SetPixelShader(g_pComputeGradLinearDepthPS, sizeof(g_pComputeGradLinearDepthPS));
	m_ComputeGradLinearDepthPSO.Finalize();

	m_DepthDownsamplingPSO.SetRootSignature(m_RootSig);
	m_DepthDownsamplingPSO.SetRasterizerState(Graphics::RasterizerDefault);
	m_DepthDownsamplingPSO.SetBlendState(Graphics::BlendNoColorWrite);
	m_DepthDownsamplingPSO.SetDepthStencilState(Graphics::DepthStateReadWrite);
	m_DepthDownsamplingPSO.SetInputLayout(_countof(screenVertElem), screenVertElem);
	m_DepthDownsamplingPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	m_DepthDownsamplingPSO.SetRenderTargetFormats(0, nullptr, sceneDepthFormat);
	m_DepthDownsamplingPSO.SetVertexShader(g_pScreenShaderVS, sizeof(g_pScreenShaderVS));
	m_DepthDownsamplingPSO.SetPixelShader(g_pDownsamplingPS, sizeof(g_pDownsamplingPS));
	m_DepthDownsamplingPSO.Finalize();

	m_DepthDownsampling2PSO = m_DepthDownsamplingPSO;
	m_DepthDownsampling2PSO.SetVertexShader(g_pScreenShaderVS, sizeof(g_pScreenShaderVS));
	m_DepthDownsampling2PSO.SetPixelShader(g_pDownsampling2PS, sizeof(g_pDownsampling2PS));
	m_DepthDownsampling2PSO.Finalize();
	//
}

void LGHRenderer::BilateralFiltering(ComputeContext& cptContext, const ViewConfig& viewConfig)
{
	if (m_IndirectShadow)
	{
		int scrWidth = viewConfig.m_MainViewport.Width;
		int scrHeight = viewConfig.m_MainViewport.Height;
		__declspec(align(16)) struct
		{
			int tileWidth;
			int tileHeight;
		} csConstants;

		csConstants.tileWidth = scrWidth;
		csConstants.tileHeight = scrHeight;

		// noise estimation
		cptContext.TransitionResource(m_ShadowedStochasticBuffer[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(m_UnshadowedStochasticBuffer[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(m_NoiseEstimationBuffer[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cptContext.SetDynamicConstantBufferView(0, sizeof(csConstants), &csConstants);
		cptContext.SetDynamicDescriptor(1, 0, m_NoiseEstimationBuffer[0].GetUAV());
		D3D12_CPU_DESCRIPTOR_HANDLE noiseEstSrvs[2] = { m_ShadowedStochasticBuffer[0].GetSRV(),
														m_UnshadowedStochasticBuffer[0].GetSRV() };
		cptContext.SetDynamicDescriptors(2, 2, _countof(noiseEstSrvs), noiseEstSrvs);
		cptContext.SetPipelineState(m_NoiseEstimationPSO);
		cptContext.Dispatch2D(viewConfig.m_MainViewport.Width, viewConfig.m_MainViewport.Height, 16, 16);

		// denoise noise estimation
		cptContext.TransitionResource(m_NoiseEstimationBuffer[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(m_NoiseEstimationBuffer[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cptContext.SetDynamicDescriptor(1, 1, m_NoiseEstimationBuffer[1].GetUAV());
		cptContext.SetDynamicDescriptor(2, 0, m_NoiseEstimationBuffer[0].GetSRV());
		cptContext.SetPipelineState(m_DenoiseNEPSO);
		cptContext.Dispatch2D(viewConfig.m_MainViewport.Width, viewConfig.m_MainViewport.Height, 16, 16);

		cptContext.Flush();

		// denoise X
		__declspec(align(16)) struct
		{
			Vector4 axis0axis1FarNear;
			Vector4 camera_projInfo;
			int tileWidth;
			int tileHeight;
		} csConstants2;

		csConstants2.tileWidth = viewConfig.m_MainViewport.Width;
		csConstants2.tileHeight = viewConfig.m_MainViewport.Height;
		csConstants2.axis0axis1FarNear.SetX(1);
		csConstants2.axis0axis1FarNear.SetY(0);
		csConstants2.axis0axis1FarNear.SetZ(viewConfig.m_Camera.GetFarClip());
		csConstants2.axis0axis1FarNear.SetW(viewConfig.m_Camera.GetNearClip());
		csConstants2.camera_projInfo = Vector4(-2.0f / (viewConfig.m_MainViewport.Width * viewConfig.m_Camera.GetViewMatrix()[0].GetX()),
			-2.0f / (viewConfig.m_MainViewport.Height * viewConfig.m_Camera.GetViewMatrix()[1].GetY()),
			(1.0f - viewConfig.m_Camera.GetViewMatrix()[2].GetX()) / viewConfig.m_Camera.GetViewMatrix()[0].GetX(),
			(1.0f - viewConfig.m_Camera.GetViewMatrix()[2].GetY()) / viewConfig.m_Camera.GetViewMatrix()[1].GetY());

		cptContext.TransitionResource(m_ShadowedStochasticBuffer[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cptContext.TransitionResource(m_UnshadowedStochasticBuffer[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cptContext.TransitionResource(m_SURatioBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		cptContext.TransitionResource(m_NoiseEstimationBuffer[1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(Graphics::g_SceneNormalBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(Graphics::g_SceneDepthBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(Graphics::g_SceneAlbedoBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		m_BilateralFilteringUavs[0] = m_ShadowedStochasticBuffer[1].GetUAV();
		m_BilateralFilteringUavs[1] = m_UnshadowedStochasticBuffer[1].GetUAV();
		m_BilateralFilteringUavs[2] = m_SURatioBuffer.GetUAV();

		m_BilateralFilteringSrvs[0] = m_NoiseEstimationBuffer[1].GetSRV();
		m_BilateralFilteringSrvs[1] = m_ShadowedStochasticBuffer[0].GetSRV();
		m_BilateralFilteringSrvs[2] = m_UnshadowedStochasticBuffer[0].GetSRV();
		m_BilateralFilteringSrvs[3] = Graphics::g_SceneNormalBuffer.GetSRV();
		m_BilateralFilteringSrvs[4] = Graphics::g_SceneDepthBuffer.GetDepthSRV();
		m_BilateralFilteringSrvs[5] = Graphics::g_SceneAlbedoBuffer.GetSRV();

		cptContext.SetDynamicConstantBufferView(0, sizeof(csConstants2), &csConstants2);
		cptContext.SetDynamicDescriptors(1, 2, _countof(m_BilateralFilteringUavs), m_BilateralFilteringUavs);
		cptContext.SetDynamicDescriptors(2, 1, _countof(m_BilateralFilteringSrvs), m_BilateralFilteringSrvs);
		cptContext.SetPipelineState(m_BilateralFilteringPSO);
		cptContext.Dispatch2D(viewConfig.m_MainViewport.Width, viewConfig.m_MainViewport.Height, 16, 16);

		cptContext.Flush();
		// denoise Y

		cptContext.TransitionResource(m_ShadowedStochasticBuffer[1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(m_UnshadowedStochasticBuffer[1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(m_ShadowedStochasticBuffer[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cptContext.TransitionResource(m_UnshadowedStochasticBuffer[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		m_BilateralFilteringUavs[0] = m_ShadowedStochasticBuffer[0].GetUAV();
		m_BilateralFilteringUavs[1] = m_UnshadowedStochasticBuffer[0].GetUAV();
		m_BilateralFilteringSrvs[1] = m_ShadowedStochasticBuffer[1].GetSRV();
		m_BilateralFilteringSrvs[2] = m_UnshadowedStochasticBuffer[1].GetSRV();
		csConstants2.axis0axis1FarNear.SetX(0);
		csConstants2.axis0axis1FarNear.SetY(1);
		cptContext.SetDynamicConstantBufferView(0, sizeof(csConstants2), &csConstants2);
		cptContext.SetDynamicDescriptors(1, 2, _countof(m_BilateralFilteringUavs), m_BilateralFilteringUavs);
		cptContext.SetDynamicDescriptors(2, 1, _countof(m_BilateralFilteringSrvs), m_BilateralFilteringSrvs);
		cptContext.Dispatch2D(viewConfig.m_MainViewport.Width, viewConfig.m_MainViewport.Height, 16, 16);
	}
}

void LGHRenderer::WaveletFiltering(ComputeContext& cptContext, int scrWidth, int scrHeight)
{
	if (m_IndirectShadow)
	{
		__declspec(align(16)) struct
		{
			float c_phi;
			float n_phi;
			float p_phi;
			float stepWidth;
			int iter;
			int maxIter;
		} csConstants;

		csConstants.n_phi = m_NPhi;
		csConstants.p_phi = m_PPhi;
		csConstants.c_phi = m_CPhi;
		csConstants.maxIter = 5;
		csConstants.stepWidth = 1;

		cptContext.TransitionResource(Graphics::g_SceneNormalBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(Graphics::g_ScenePositionBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(m_ShadowedStochasticBuffer[2], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(m_UnshadowedStochasticBuffer[2], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(m_AnalyticBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(m_SURatioBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		cptContext.SetDynamicDescriptor(2, 4, Graphics::g_SceneNormalBuffer.GetSRV());
		cptContext.SetDynamicDescriptor(2, 5, Graphics::g_ScenePositionBuffer.GetSRV());
		if (m_WaveletStrength < 2)
		{
			cptContext.SetDynamicDescriptor(2, 6, m_ShadowedStochasticBuffer[2].GetSRV());
			cptContext.SetDynamicDescriptor(2, 7, m_UnshadowedStochasticBuffer[2].GetSRV());
		}
		else
		{
			cptContext.SetDynamicDescriptor(2, 6, m_ShadowedStochasticBuffer[0].GetSRV());
			cptContext.SetDynamicDescriptor(2, 7, m_UnshadowedStochasticBuffer[0].GetSRV());
		}

		cptContext.SetDynamicDescriptor(2, 8, m_AnalyticBuffer.GetSRV());

		cptContext.SetPipelineState(m_AtrousFilterPSO);

		for (int iter = 0; iter < 5; iter++) // do 5 iterations of a-trous
		{
			int readIndex = iter % 2;
			int writeIndex = (iter + 1) % 2;
			csConstants.iter = iter;
			cptContext.TransitionResource(m_ShadowedStochasticBuffer[readIndex], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			cptContext.TransitionResource(m_UnshadowedStochasticBuffer[readIndex], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			cptContext.TransitionResource(m_ShadowedStochasticBuffer[writeIndex], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			cptContext.TransitionResource(m_UnshadowedStochasticBuffer[writeIndex], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			cptContext.SetDynamicConstantBufferView(0, sizeof(csConstants), &csConstants);
			cptContext.SetDynamicDescriptor(1, 2, m_ShadowedStochasticBuffer[writeIndex].GetUAV());
			cptContext.SetDynamicDescriptor(1, 3, m_UnshadowedStochasticBuffer[writeIndex].GetUAV());
			cptContext.SetDynamicDescriptor(1, 4, m_SURatioBuffer.GetUAV());
			cptContext.SetDynamicDescriptor(2, 2, m_ShadowedStochasticBuffer[readIndex].GetSRV());
			cptContext.SetDynamicDescriptor(2, 3, m_UnshadowedStochasticBuffer[readIndex].GetSRV());
			csConstants.c_phi *= 0.25; // stdev halves as image get smoother
			csConstants.stepWidth *= 2;
			cptContext.Dispatch2D(scrWidth, scrHeight, 16, 16);
			
			if (m_WaveletStrength == 1 && iter == 0)
			{
				cptContext.TransitionResource(m_ShadowedStochasticBuffer[writeIndex], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				cptContext.TransitionResource(m_UnshadowedStochasticBuffer[writeIndex], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
				cptContext.SetDynamicDescriptor(2, 6, m_ShadowedStochasticBuffer[writeIndex].GetSRV());
				cptContext.SetDynamicDescriptor(2, 7, m_UnshadowedStochasticBuffer[writeIndex].GetSRV());
			}
		}
	}
}

void LGHRenderer::SVGFiltering(ComputeContext& cptContext, RootSignature& m_ComputeRootSig)
{
	if (m_IndirectShadow)
	{
		if (!svgfDenoiser.IsInitialized) svgfDenoiser.Initialize(cptContext, m_ComputeRootSig);

		svgfDenoiser.Reproject(cptContext, m_ShadowedStochasticBuffer[2], m_UnshadowedStochasticBuffer[2], !m_TemporalRandom);
		svgfDenoiser.FilterMoments(cptContext);
		svgfDenoiser.Filter(cptContext, m_SURatioBuffer);
	}
}

void LGHRenderer::ComputeLinearDepthGradient(GraphicsContext & gfxContext, const ViewConfig& viewConfig)
{
	int frameParity = TemporalEffects::GetFrameIndexMod2();
	gfxContext.SetRootSignature(m_RootSig);
	gfxContext.TransitionResource(Graphics::g_LinearDepth[frameParity],
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	gfxContext.TransitionResource(Graphics::g_GradLinearDepth,
		D3D12_RESOURCE_STATE_RENDER_TARGET, true);
	gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	gfxContext.SetIndexBuffer(m_quad.m_IndexBuffer.IndexBufferView());
	gfxContext.SetVertexBuffer(0, m_quad.m_VertexBuffer.VertexBufferView());
	gfxContext.SetDynamicDescriptor(3, 0, Graphics::g_LinearDepth[frameParity].GetSRV());
	gfxContext.SetPipelineState(m_ComputeGradLinearDepthPSO);
	gfxContext.SetRenderTarget(Graphics::g_GradLinearDepth.GetRTV());
	gfxContext.SetViewportAndScissor(viewConfig.m_MainViewport, viewConfig.m_MainScissor);
	gfxContext.DrawIndexed(m_quad.indicesPerInstance, 0, 0);
}


void LGHRenderer::Initialize(Model1* model, int numModels, int scrWidth, int scrHeight)
{
	lastDrawLevelOption = (DrawLevelsOptions)(int)m_DrawLevels;
	lastInterleaveRateOption = (InterleaveRateOptions)interleaveRates[m_InterleaveRate];
	lastVPLEmissionLevel = m_VPLEmissionLevel;
	lastPresetOrderOfMagnitude = m_PresetVPLOrderOfMagnitude;
	lastMaxDepth = m_MaxDepth;
	m_Model = model;
	m_PPhi = 0.66f * model[0].m_SceneBoundingSphere.GetW();
	InitBuffers(scrWidth, scrHeight);
	InitRootSignatures();
	InitComputePSOs();
	InitPSOs();
	m_cube.Init();
	m_quad.Init();
	vplManager.Initialize(m_Model, numModels);

	vplManager.InitializeLGHViews(m_ShadowedStochasticBuffer[2].GetUAV(), m_UnshadowedStochasticBuffer[2].GetUAV(),
		m_vplSampleBuffer.GetSRV(), m_runningSumBuffer.GetSRV(), m_sampleColorBuffer.GetSRV(), interleaveRates[m_InterleaveRate] > 1 ? &m_ScenePositionBufferArray : nullptr,
		interleaveRates[m_InterleaveRate] > 1 ? &m_SceneNormalBufferArray : nullptr);
	vplManager.InitializeLGHSrvs(m_Model->m_BlueNoiseSRV[0], m_Model->m_BlueNoiseSRV[1], m_Model->m_BlueNoiseSRV[2]);
}

void LGHRenderer::GenerateLightingGridHierarchy(GraphicsContext& context, Vector3 lightDirection, float lightIntensity, bool hasSceneChange)
{
	bool drawLevelsChanged = false;
	bool vplsUpdated = false;
	if (lastDrawLevelOption != m_DrawLevels)
	{
		lastDrawLevelOption = (DrawLevelsOptions)(int)m_DrawLevels;
		drawLevelsChanged = true;
	}

	if (hasSceneChange) vplManager.UpdateAccelerationStructure();

	bool isFirstTime = vplManager.numFramesUpdated == -1;

	bool hasRequiredVPLsChange = m_VPLEmissionLevel != lastVPLEmissionLevel || m_MaxDepth != lastMaxDepth;

	if (m_PresetVPLOrderOfMagnitude != 0 && lastPresetOrderOfMagnitude != m_PresetVPLOrderOfMagnitude)
	{
		hasRequiredVPLsChange = true;
		m_VPLEmissionLevel = PresetEmissionLevels[m_PresetVPLOrderOfMagnitude-1];
	}
	else if (hasRequiredVPLsChange && m_MaxDepth == lastMaxDepth)
	{
		m_PresetVPLOrderOfMagnitude = 0; //set preset to custom
	}

	vplsUpdated = vplManager.GenerateVPLs(context, m_VPLEmissionLevel, lightDirection, lightIntensity, m_MaxDepth, hasSceneChange || hasRequiredVPLsChange);

	if (vplsUpdated)
	{
		printf("%d paths sampled!\n", vplManager.numPaths);
		printf("%d VPLs generated!\n", vplManager.numVPLs);
	}

	bool isReinit = false;
	if (hasRequiredVPLsChange)
	{
		lastVPLEmissionLevel = m_VPLEmissionLevel;
		lastPresetOrderOfMagnitude = m_PresetVPLOrderOfMagnitude;
		lastMaxDepth = m_MaxDepth;
		int newNumLevels = gpuLightingGridBuilder.CalculateNumLevels(vplManager.numVPLs);
		if (newNumLevels != gpuLightingGridBuilder.highestLevel + 1)
		{
			isFirstTime = true;
			isReinit = true;
		}
	}

	if (isFirstTime) gpuLightingGridBuilder.Init(context.GetComputeContext(), vplManager.numVPLs, vplManager.VPLBuffers, m_DrawLevels == includeVPLs, isReinit);
	else gpuLightingGridBuilder.numVPLs = vplManager.numVPLs;

	if (gpuLightingGridBuilder.CheckUpdate(context.GetComputeContext(), interleaveRates[m_InterleaveRate], vplsUpdated, drawLevelsChanged))
	{
		vplManager.UpdateLGHSrvs(gpuLightingGridBuilder.InstanceBuffers[0].GetSRV(),
			gpuLightingGridBuilder.InstanceBuffers[1].GetSRV(),
			gpuLightingGridBuilder.InstanceBuffers[2].GetSRV(),
			gpuLightingGridBuilder.InstanceBuffers[3].GetSRV());
	}
}

void LGHRenderer::RenderInterleaved(GraphicsContext& gfxContext, const ViewConfig& viewConfig, int frameId)
{
	int interleaveRate = interleaveRates[m_InterleaveRate];
	ComputeContext& cptContext = gfxContext.GetComputeContext();

	{
		ScopedTimer _prof(L"Render LGH", gfxContext);

		// deinterleave G buffer
		{
			cptContext.SetRootSignature(m_ComputeRootSig);

			__declspec(align(16)) struct
			{
				unsigned int n;
				unsigned int m;
				unsigned int tileWidth;
				unsigned int tileHeight;
			} csConstants;

			csConstants.n = interleaveRate;
			csConstants.m = interleaveRate;
			csConstants.tileWidth = viewConfig.m_MainViewport.Width / interleaveRate;
			csConstants.tileHeight = viewConfig.m_MainViewport.Height / interleaveRate;

			cptContext.TransitionResource(Graphics::g_ScenePositionBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			cptContext.TransitionResource(Graphics::g_SceneNormalBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			cptContext.TransitionResource(Graphics::g_SceneAlbedoBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			cptContext.TransitionResource(Graphics::g_SceneSpecularBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

			D3D12_CPU_DESCRIPTOR_HANDLE GBufferSrvs[4] = { Graphics::g_ScenePositionBuffer.GetSRV(),
				Graphics::g_SceneNormalBuffer.GetSRV(), Graphics::g_SceneAlbedoBuffer.GetSRV(),
				Graphics::g_SceneSpecularBuffer.GetSRV() };

			D3D12_CPU_DESCRIPTOR_HANDLE SubGBufferUavs[4] = { m_ScenePositionBufferArray.GetUAV(),
															  m_SceneNormalBufferArray.GetUAV(),
						m_SceneAlbedoBufferArray.GetUAV(), m_SceneSpecularBufferArray.GetUAV() };

			cptContext.SetDynamicConstantBufferView(0, sizeof(csConstants), &csConstants);
			cptContext.SetDynamicDescriptors(1, 0, _countof(SubGBufferUavs), SubGBufferUavs);
			cptContext.SetDynamicDescriptors(2, 0, _countof(GBufferSrvs), GBufferSrvs);

			cptContext.SetPipelineState(m_DeinterleavePSO);
			cptContext.Dispatch2D(viewConfig.m_MainViewport.Width, viewConfig.m_MainViewport.Height, 16, 16);
		}

		//deinterleave depth buffer
		{
			__declspec(align(16)) struct
			{
				int tileWidth;
				int tileHeight;
				int downsamplingRate;
			} downsamplingConstant;

			downsamplingConstant.tileWidth = viewConfig.m_MainViewport.Width / interleaveRate;
			downsamplingConstant.tileHeight = viewConfig.m_MainViewport.Height / interleaveRate;
			downsamplingConstant.downsamplingRate = interleaveRate;
			//downsampling depth buffer
			gfxContext.SetDynamicDescriptor(3, 4, Graphics::g_SceneDepthBuffer.GetDepthSRV());
			gfxContext.SetDynamicConstantBufferView(1, sizeof(downsamplingConstant), &downsamplingConstant);
			gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
			gfxContext.SetIndexBuffer(m_quad.m_IndexBuffer.IndexBufferView());
			gfxContext.SetVertexBuffer(0, m_quad.m_VertexBuffer.VertexBufferView());
			gfxContext.SetPipelineState(m_DepthDownsamplingPSO);
			gfxContext.TransitionResource(Graphics::g_SceneDepthBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			gfxContext.TransitionResource(m_SceneDepthBufferDeinterleave, D3D12_RESOURCE_STATE_DEPTH_WRITE);
			gfxContext.SetDepthStencilTarget(m_SceneDepthBufferDeinterleave.GetDSV());
			gfxContext.SetViewportAndScissor(viewConfig.m_MainViewport, viewConfig.m_MainScissor);
			gfxContext.DrawIndexed(m_quad.indicesPerInstance, 0, 0);
			gfxContext.Flush();
		}

		gfxContext.Flush();

		// deferred rendering 
		{
			gfxContext.SetRootSignature(m_RootSig);
			gfxContext.TransitionResource(m_AnalyticBufferDeinterleave, D3D12_RESOURCE_STATE_RENDER_TARGET);
			gfxContext.TransitionResource(m_ScenePositionBufferArray, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			gfxContext.TransitionResource(m_SceneNormalBufferArray, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			gfxContext.TransitionResource(m_SceneAlbedoBufferArray, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			gfxContext.TransitionResource(m_SceneSpecularBufferArray, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			gfxContext.TransitionResource(m_SceneDepthBufferDeinterleave, D3D12_RESOURCE_STATE_DEPTH_READ);
			gfxContext.TransitionResource(gpuLightingGridBuilder.InstanceBuffers[0], D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
			gfxContext.TransitionResource(gpuLightingGridBuilder.InstanceBuffers[1], D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
			gfxContext.TransitionResource(gpuLightingGridBuilder.InstanceBuffers[2], D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
			gfxContext.TransitionResource(gpuLightingGridBuilder.InstanceBuffers[3], D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
			gfxContext.TransitionResource(m_sampleColorBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			gfxContext.TransitionResource(m_vplSampleBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			gfxContext.TransitionResource(m_lockImageBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			gfxContext.TransitionResource(m_runningSumBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);

			gfxContext.ClearColor(m_AnalyticBufferDeinterleave);
			gfxContext.ClearUAV(m_runningSumBuffer);
			gfxContext.ClearUAV(m_lockImageBuffer);


			for (int i = 0; i < interleaveRate; i++)
			{
				for (int j = 0; j < interleaveRate; j++)
				{
					D3D12_VIEWPORT subView;
					D3D12_RECT subScissor;
					GetSubViewportAndScissor(i, j, interleaveRate, viewConfig, subView, subScissor);

					gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
					gfxContext.SetIndexBuffer(m_cube.m_IndexBuffer.IndexBufferView());

					const D3D12_VERTEX_BUFFER_VIEW VBViews[] = { m_cube.m_VertexBuffer.VertexBufferView(),
												gpuLightingGridBuilder.InstanceBuffers[0].VertexBufferView(),
												gpuLightingGridBuilder.InstanceBuffers[1].VertexBufferView(),
												gpuLightingGridBuilder.InstanceBuffers[2].VertexBufferView(),
												gpuLightingGridBuilder.InstanceBuffers[3].VertexBufferView() };

					gfxContext.SetVertexBuffers(0, 5, VBViews);

					gfxContext.SetDynamicDescriptors(3, 0, _countof(m_GBufferArraySrvs), m_GBufferArraySrvs);
					gfxContext.SetDynamicDescriptor(4, 0, m_Model[0].m_BlueNoiseSRV[0]);
					gfxContext.SetDynamicDescriptor(4, 1, m_Model[0].m_BlueNoiseSRV[1]);
					gfxContext.SetDynamicDescriptor(4, 2, m_Model[0].m_BlueNoiseSRV[2]);
					gfxContext.SetDynamicDescriptors(6, 0, _countof(m_LGHSamplingHandles), m_LGHSamplingHandles);

					gfxContext.SetPipelineState(m_LightingComputationInterleavePSO);

					gfxContext.SetViewportAndScissor(subView, subScissor);

					gfxContext.SetRenderTarget(m_AnalyticBufferDeinterleave.GetRTV(), m_SceneDepthBufferDeinterleave.GetDSV_ReadOnly());

					__declspec(align(16)) struct VSConstants
					{
						Matrix4 modelToProjection;
						XMFLOAT3 viewerPos;
						float baseRadius;
						float alpha;
					} vsConstants;

					vsConstants.modelToProjection = viewConfig.m_Camera.GetViewProjMatrix();
					XMStoreFloat3(&vsConstants.viewerPos, viewConfig.m_Camera.GetPosition());
					vsConstants.baseRadius = gpuLightingGridBuilder.baseRadius;
					vsConstants.alpha = m_Alpha;
					gfxContext.SetDynamicConstantBufferView(0, sizeof(vsConstants), &vsConstants);

					__declspec(align(16)) struct
					{
						int scrWidth;
						int scrHeight;
						float invNumPaths;
						int numLevels;
						Vector4 halton[4];
						int tileOffset; //in instance array
						float devScale;
						int shadowRate;
						int minLevel;
						int minShadowLevel;
						int frameId;
						int temporalRandom;
						float sceneRadius;
					} psConstants;

					psConstants.scrWidth = viewConfig.m_MainViewport.Width;
					psConstants.scrHeight = viewConfig.m_MainViewport.Height;
					psConstants.invNumPaths = (interleaveRate*interleaveRate) / (float)vplManager.numPaths; // this estimates the number of paths in the tile
					psConstants.numLevels = gpuLightingGridBuilder.highestLevel + 1;
					for (int i = 1; i <= 4; i++)
					{
						psConstants.halton[i - 1] = Vector4(Halton(i, 2), Halton(i, 3), Halton(i, 5), Halton(i, 7));
					}
					psConstants.tileOffset = gpuLightingGridBuilder.offsetOfTile[i*interleaveRate + j];
					psConstants.devScale = m_DevScale;
					psConstants.shadowRate = m_ShadowRate;
					psConstants.minLevel = m_DrawLevels == skipVPLs;
					psConstants.minShadowLevel = m_ShadowLevels == fromS2 ? 2 : psConstants.minLevel;
					psConstants.frameId = frameId;
					psConstants.temporalRandom = m_TemporalRandom;
					psConstants.sceneRadius = vplManager.sceneBoundingSphere.GetW();

					gfxContext.SetDynamicConstantBufferView(1, sizeof(psConstants), &psConstants);
					gfxContext.DrawIndexedInstanced(m_cube.indicesPerInstance,
						gpuLightingGridBuilder.numInstanceOfTile[i*interleaveRate + j],
						0, 0,
						gpuLightingGridBuilder.offsetOfTile[i*interleaveRate + j]);
				}
			}
		}
	}


	if (m_IndirectShadow)
	{
		gfxContext.TransitionResource(m_ShadowedStochasticBuffer[2], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		gfxContext.TransitionResource(m_UnshadowedStochasticBuffer[2], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
		gfxContext.ClearUAV(m_ShadowedStochasticBuffer[2]);
		gfxContext.ClearUAV(m_UnshadowedStochasticBuffer[2]);

		gfxContext.TransitionResource(m_runningSumBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(m_vplSampleBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(gpuLightingGridBuilder.InstanceBuffers[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(gpuLightingGridBuilder.InstanceBuffers[1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(gpuLightingGridBuilder.InstanceBuffers[2], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(gpuLightingGridBuilder.InstanceBuffers[3], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		vplManager.CastLGHShadowRays(gfxContext, viewConfig.m_MainViewport.Width, viewConfig.m_MainViewport.Height, m_ShadowRate,
			gpuLightingGridBuilder.highestLevel + 1, m_DrawLevels == skipVPLs, gpuLightingGridBuilder.baseRadius, 
			m_DevScale, m_TemporalRandom, m_Alpha, frameId, interleaveRate);
		gfxContext.Flush(true);

	}

	{
		ScopedTimer _prof(L"Filtering", gfxContext);

		ComputeLinearDepthGradient(gfxContext, viewConfig);
		GenerateDiscontinuityBuffer(cptContext, viewConfig.m_MainViewport.Width, viewConfig.m_MainViewport.Height);

		if (m_IndirectShadow)
		{
			ReinterleaveAndBlur(cptContext, viewConfig.m_MainViewport.Width, viewConfig.m_MainViewport.Height,
				m_ShadowedStochasticBuffer[2], m_ShadowedStochasticBuffer[0]);
			ReinterleaveAndBlur(cptContext, viewConfig.m_MainViewport.Width, viewConfig.m_MainViewport.Height,
				m_UnshadowedStochasticBuffer[2], m_UnshadowedStochasticBuffer[0]);
			gfxContext.CopyBuffer(m_ShadowedStochasticBuffer[2], m_ShadowedStochasticBuffer[0]);
			gfxContext.CopyBuffer(m_UnshadowedStochasticBuffer[2], m_UnshadowedStochasticBuffer[0]);
			gfxContext.Flush();

			switch ((ShadowFilterType)(int)m_ShadowFilterType)
			{
			case ShadowFilterType::Atrous:
				svgfDenoiser.RecycleResources();
				WaveletFiltering(cptContext, viewConfig.m_MainViewport.Width, viewConfig.m_MainViewport.Height);
				break;
			case ShadowFilterType::SVGF:
				SVGFiltering(cptContext, m_ComputeRootSig);
				break;
			case ShadowFilterType::Bilateral:
				svgfDenoiser.RecycleResources();
				BilateralFiltering(cptContext, viewConfig);
				break;
			default:
				break;
			}
		}

		ReinterleaveAndBlur(cptContext, viewConfig.m_MainViewport.Width, viewConfig.m_MainViewport.Height,
			m_AnalyticBufferDeinterleave, m_AnalyticBuffer);
	}
}

void LGHRenderer::Render(GraphicsContext& gfxContext, const ViewConfig& viewConfig, int frameId, bool hasSceneChange)
{
	if (hasSceneChange) vplManager.UpdateAccelerationStructure();
	if (interleaveRates[m_InterleaveRate] > 1) {
		if (m_ScenePositionBufferArray.GetWidth() == 0) InitInterleavedBuffers(viewConfig.m_MainViewport.Width, 
			viewConfig.m_MainViewport.Height); // buffer not initialized
		if (lastInterleaveRateOption != (InterleaveRateOptions)interleaveRates[m_InterleaveRate])
		{
			vplManager.SwitchToInterleavedPositionAndNormal(m_ScenePositionBufferArray.GetSRV(), m_SceneNormalBufferArray.GetSRV());
			lastInterleaveRateOption = (InterleaveRateOptions)interleaveRates[m_InterleaveRate];
		}
		RenderInterleaved(gfxContext, viewConfig, frameId); 
		return;
	}
	else
	{
		if (lastInterleaveRateOption != (InterleaveRateOptions)interleaveRates[m_InterleaveRate])
		{
			vplManager.SwitchToNormalPositionAndNormal();
			lastInterleaveRateOption = (InterleaveRateOptions)interleaveRates[m_InterleaveRate];
		}
	}

	// deferred rendering 
	//use compute shader to figure out depth
	{
		ScopedTimer _prof(L"Render LGH", gfxContext);

		gfxContext.SetRootSignature(m_RootSig);
		gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		gfxContext.SetIndexBuffer(m_cube.m_IndexBuffer.IndexBufferView());

		const D3D12_VERTEX_BUFFER_VIEW VBViews[] = { m_cube.m_VertexBuffer.VertexBufferView(),
													gpuLightingGridBuilder.InstanceBuffers[0].VertexBufferView(),
													gpuLightingGridBuilder.InstanceBuffers[1].VertexBufferView(),
													gpuLightingGridBuilder.InstanceBuffers[2].VertexBufferView(),
													gpuLightingGridBuilder.InstanceBuffers[3].VertexBufferView() };

		gfxContext.SetVertexBuffers(0, 5, VBViews);
		gfxContext.SetDynamicDescriptors(3, 0, _countof(m_GBufferSrvs), m_GBufferSrvs);
		gfxContext.SetDynamicDescriptor(4, 0, m_Model[0].m_BlueNoiseSRV[0]);
		gfxContext.SetDynamicDescriptor(4, 1, m_Model[0].m_BlueNoiseSRV[1]);
		gfxContext.SetDynamicDescriptor(4, 2, m_Model[0].m_BlueNoiseSRV[2]);
		gfxContext.SetDynamicDescriptors(6, 0, _countof(m_LGHSamplingHandles), m_LGHSamplingHandles);
		gfxContext.SetPipelineState(m_LightingComputationPSO);

		gfxContext.TransitionResource(m_runningSumBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		gfxContext.TransitionResource(m_lockImageBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		gfxContext.TransitionResource(m_vplSampleBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		gfxContext.TransitionResource(m_AnalyticBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
		gfxContext.TransitionResource(m_sampleColorBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		gfxContext.TransitionResource(gpuLightingGridBuilder.InstanceBuffers[0], D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		gfxContext.TransitionResource(gpuLightingGridBuilder.InstanceBuffers[1], D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		gfxContext.TransitionResource(gpuLightingGridBuilder.InstanceBuffers[2], D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		gfxContext.TransitionResource(gpuLightingGridBuilder.InstanceBuffers[3], D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		gfxContext.TransitionResource(Graphics::g_ScenePositionBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(Graphics::g_SceneNormalBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(Graphics::g_SceneAlbedoBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(Graphics::g_SceneSpecularBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(Graphics::g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ, true);

		gfxContext.ClearUAV(m_runningSumBuffer);
		gfxContext.ClearUAV(m_lockImageBuffer);

		gfxContext.ClearColor(m_AnalyticBuffer);
		gfxContext.SetRenderTarget(m_AnalyticBuffer.GetRTV(), Graphics::g_SceneDepthBuffer.GetDSV_ReadOnly());
		gfxContext.SetViewportAndScissor(viewConfig.m_MainViewport, viewConfig.m_MainScissor);

		__declspec(align(16)) struct VSConstants
		{
			Matrix4 modelToProjection;
			XMFLOAT3 viewerPos;
			float baseRadius;
			float alpha;
		} vsConstants;

		vsConstants.modelToProjection = viewConfig.m_Camera.GetViewProjMatrix();
		XMStoreFloat3(&vsConstants.viewerPos, viewConfig.m_Camera.GetPosition());
		vsConstants.baseRadius = gpuLightingGridBuilder.baseRadius;
		vsConstants.alpha = m_Alpha;
		gfxContext.SetDynamicConstantBufferView(0, sizeof(vsConstants), &vsConstants);

		__declspec(align(16)) struct
		{
			int scrWidth;
			int scrHeight;
			float invNumPaths;
			int numLevels;
			Vector4 halton[4];
			int minSampleId;
			float devScale;
			int shadowRate;
			int minLevel;
			int minShadowLevel;
			int frameId;
			int temporalRandom;
			float sceneRadius;
		} psConstants;

		psConstants.frameId = frameId % 1024;
		psConstants.scrWidth = viewConfig.m_MainViewport.Width;
		psConstants.scrHeight = viewConfig.m_MainViewport.Height;
		psConstants.invNumPaths = 1.f / vplManager.numPaths;
		psConstants.numLevels = gpuLightingGridBuilder.highestLevel + 1;
		for (int i = 1; i <= 4; i++)
		{
			psConstants.halton[i - 1] = Vector4(Halton(i, 2), Halton(i, 3), Halton(i, 5), Halton(i, 7));
		}

		psConstants.minSampleId = gpuLightingGridBuilder.levelSizes[1] + (m_DrawLevels == includeVPLs) * gpuLightingGridBuilder.levelSizes[0];
		psConstants.devScale = m_DevScale;
		psConstants.minLevel = m_DrawLevels == skipVPLs;
		psConstants.minShadowLevel = m_ShadowLevels == fromS2 ? 2 : psConstants.minLevel;
		psConstants.shadowRate = m_IndirectShadow ? m_ShadowRate : 0;
		psConstants.temporalRandom = m_TemporalRandom;
		psConstants.sceneRadius = vplManager.sceneBoundingSphere.GetW();
		gfxContext.SetDynamicConstantBufferView(1, sizeof(psConstants), &psConstants);
		gfxContext.DrawIndexedInstanced(m_cube.indicesPerInstance, gpuLightingGridBuilder.numInstances, 0, 0, 0);

		gfxContext.Flush(true);
	}

	if (m_IndirectShadow)
	{
		gfxContext.TransitionResource(m_ShadowedStochasticBuffer[2], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		gfxContext.TransitionResource(m_UnshadowedStochasticBuffer[2], D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
		gfxContext.ClearUAV(m_ShadowedStochasticBuffer[2]);
		gfxContext.ClearUAV(m_UnshadowedStochasticBuffer[2]);


		gfxContext.TransitionResource(m_runningSumBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(m_vplSampleBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(gpuLightingGridBuilder.InstanceBuffers[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(gpuLightingGridBuilder.InstanceBuffers[1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(gpuLightingGridBuilder.InstanceBuffers[2], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		gfxContext.TransitionResource(gpuLightingGridBuilder.InstanceBuffers[3], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		vplManager.CastLGHShadowRays(gfxContext, viewConfig.m_MainViewport.Width, viewConfig.m_MainViewport.Height, m_ShadowRate,
			gpuLightingGridBuilder.highestLevel + 1, m_DrawLevels == skipVPLs, gpuLightingGridBuilder.baseRadius, m_DevScale, 
			m_TemporalRandom, m_Alpha, frameId);
		gfxContext.CopyBuffer(m_ShadowedStochasticBuffer[0], m_ShadowedStochasticBuffer[2]);
		gfxContext.CopyBuffer(m_UnshadowedStochasticBuffer[0], m_UnshadowedStochasticBuffer[2]);

		gfxContext.Flush(true);

		{
			ScopedTimer _prof(L"Filtering", gfxContext);

			ComputeContext& cptContext = gfxContext.GetComputeContext();
			cptContext.SetRootSignature(m_ComputeRootSig);

			switch ((ShadowFilterType)(int)m_ShadowFilterType)
			{
			case ShadowFilterType::Atrous:
				svgfDenoiser.RecycleResources();
				WaveletFiltering(cptContext, viewConfig.m_MainViewport.Width, viewConfig.m_MainViewport.Height);
				break;
			case ShadowFilterType::SVGF:
				ComputeLinearDepthGradient(gfxContext, viewConfig);
				SVGFiltering(cptContext, m_ComputeRootSig);
				break;
			case ShadowFilterType::Bilateral:
				svgfDenoiser.RecycleResources();
				BilateralFiltering(cptContext, viewConfig);
				break;
			default:
				break;
			}
		}
	}
}

void LGHRenderer::GetSubViewportAndScissor(int i, int j, int rate, const ViewConfig& viewConfig, D3D12_VIEWPORT & viewport, D3D12_RECT & scissor)
{
	viewport.Width = viewConfig.m_MainViewport.Width / rate;
	viewport.Height = viewConfig.m_MainViewport.Height / rate;
	viewport.TopLeftX = 0.5 + viewport.Width * j;
	viewport.TopLeftY = 0.5 + viewport.Height * i;

	scissor = viewConfig.m_MainScissor;
}

void LGHRenderer::ReinterleaveAndBlur(ComputeContext& cptContext, int scrWidth, int scrHeight, 
	ColorBuffer& srcBuffer, ColorBuffer& dstBuffer)
{
	__declspec(align(16)) struct
	{
		int scrWidth;
		int scrHeight;
		int n;
		int m;
	} csConstants1 = { scrWidth, scrHeight, interleaveRates[m_InterleaveRate], interleaveRates[m_InterleaveRate] };

	// interleave
	cptContext.TransitionResource(srcBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(dstBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.SetDynamicConstantBufferView(0, sizeof(csConstants1), &csConstants1);
	cptContext.SetDynamicDescriptor(1, 0, dstBuffer.GetUAV());
	cptContext.SetDynamicDescriptor(2, 0, srcBuffer.GetSRV());
	cptContext.SetPipelineState(m_InterleavePSO);
	cptContext.Dispatch2D(scrWidth, scrHeight, 16, 16);
	cptContext.Flush();

	__declspec(align(16)) struct
	{
		int axis0;
		int axis1;
		int imgWidth;
		int imgHeight;
	} csConstants2;
	csConstants2.imgWidth = scrWidth;
	csConstants2.imgHeight = scrHeight;

	//blur X
	csConstants2.axis0 = 1;
	csConstants2.axis1 = 0;

	cptContext.TransitionResource(m_DiscontinuityBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(dstBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
	cptContext.TransitionResource(srcBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
	cptContext.SetDynamicConstantBufferView(0, sizeof(csConstants2), &csConstants2);
	D3D12_CPU_DESCRIPTOR_HANDLE m_BlurSrvs[2] = { m_DiscontinuityBuffer.GetSRV(), dstBuffer.GetSRV() };
	cptContext.SetDynamicDescriptor(1, 0, srcBuffer.GetUAV());
	cptContext.SetDynamicDescriptors(2, 0, _countof(m_BlurSrvs), m_BlurSrvs);
	cptContext.SetPipelineState(m_BlurInterleavePSO);
	cptContext.Dispatch2D(scrWidth, scrHeight, 16, 16);
	cptContext.Flush();

	//blur Y
	csConstants2.axis0 = 0;
	csConstants2.axis1 = 1;
	cptContext.TransitionResource(srcBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
	cptContext.TransitionResource(dstBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
	cptContext.SetDynamicConstantBufferView(0, sizeof(csConstants2), &csConstants2);
	D3D12_CPU_DESCRIPTOR_HANDLE m_BlurSrvs2[2] = { m_DiscontinuityBuffer.GetSRV(), srcBuffer.GetSRV() };
	cptContext.SetDynamicDescriptor(1, 0, dstBuffer.GetUAV());
	cptContext.SetDynamicDescriptors(2, 0, _countof(m_BlurSrvs2), m_BlurSrvs2);
	cptContext.Dispatch2D(scrWidth, scrHeight, 16, 16);
	cptContext.Flush();
}

void LGHRenderer::GenerateDiscontinuityBuffer(ComputeContext& cptContext, int scrWidth, int scrHeight)
{
	__declspec(align(16)) struct
	{
		float ZDiff;
		float NDiff;
	} csConstants;

	uint32_t Src = TemporalEffects::GetFrameIndexMod2();

	D3D12_CPU_DESCRIPTOR_HANDLE m_DiscontinuitiySrvs[3] = { Graphics::g_LinearDepth[Src].GetSRV(),
															Graphics::g_GradLinearDepth.GetSRV(),
															Graphics::g_SceneNormalBuffer.GetSRV() };

	csConstants.ZDiff = m_DisconZDiff;
	csConstants.NDiff = m_DisconNDiff;
	cptContext.TransitionResource(Graphics::g_LinearDepth[Src], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(Graphics::g_GradLinearDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(Graphics::g_SceneNormalBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(m_DiscontinuityBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.SetDynamicConstantBufferView(0, sizeof(csConstants), &csConstants);
	cptContext.SetDynamicDescriptor(1, 0, m_DiscontinuityBuffer.GetUAV());
	cptContext.SetDynamicDescriptors(2, 0, _countof(m_DiscontinuitiySrvs), m_DiscontinuitiySrvs);
	cptContext.SetPipelineState(m_DiscontinuityPSO);
	cptContext.Dispatch2D(scrWidth, scrHeight, 16, 16);
	cptContext.Flush();
}

