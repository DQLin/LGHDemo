#pragma once
#include "ViewHelper.h"
#include "ColorBuffer.h"
#include "dxgi1_3.h"
#include "GameCore.h"
#include "GraphicsCore.h"
#include "LGHBuilder.h"
#include "BufferManager.h"
#include "VPLManager.h"
#include "Cube.h"
#include "Quad.h"
#include "SVGFDenoiser.h"

#define OPTIMIZED

class LGHRenderer
{
public:

	LGHRenderer() {};
	void Initialize(Model1 * model, int numModels, int scrWidth, int scrHeight);
	void GenerateLightingGridHierarchy(GraphicsContext& context, Vector3 lightDirection, float lightIntensity, bool hasSceneChange = false);
	void Render(GraphicsContext& gfxContext, const ViewConfig& viewConfig, int frameId, bool hasSceneChange = false);
	void RenderInterleaved(GraphicsContext& gfxContext, const ViewConfig& viewConfig, int frameId);

	ColorBuffer m_AnalyticBuffer;
	ColorBuffer m_SURatioBuffer;

//Controls
	static const char* drawLevelsOptionsText[2];
	enum DrawLevelsOptions { skipVPLs = 0, includeVPLs };
	static EnumVar m_DrawLevels;

	static const char* shadowLevelsOptionsText[2];
	enum ShadowLevelsOptions { fromS2 = 0, fromMinLevel };
	static EnumVar m_ShadowLevels;

	static NumVar m_DevScale;
	static NumVar m_CPhi;
	static NumVar m_NPhi;
	static NumVar m_PPhi;
	static NumVar m_Alpha;
	static NumVar m_DisconZDiff;
	static NumVar m_DisconNDiff;
	static BoolVar m_IndirectShadow;
	static BoolVar m_TemporalRandom;
	static IntVar m_ShadowRate;
	static IntVar m_MaxDepth;
	static IntVar m_WaveletStrength;

	static const char* shadowFilterTypeText[3];
	enum class ShadowFilterType { Atrous, SVGF, Bilateral };
	static EnumVar m_ShadowFilterType;

	static const char* interleaveRateOptionsText[3];
	enum InterleaveRateOptions { NoInterleave = 1, Interleave2x2 = 2, Interleave4x4 = 4};
	const int interleaveRates[3] = { NoInterleave, Interleave2x2, Interleave4x4 };
	static EnumVar m_InterleaveRate;

	static const char* PresetVPLEmissionOrderOfMagnitudeText[6];
	const float PresetEmissionLevels[5] = { 0.4, 1.25, 3.9, 12.3, 39.0 };
	static EnumVar m_PresetVPLOrderOfMagnitude;

	static NumVar m_VPLEmissionLevel;

	ColorBuffer m_ShadowedStochasticBuffer[3];
	ColorBuffer m_UnshadowedStochasticBuffer[3];


private:
	DrawLevelsOptions lastDrawLevelOption;
	InterleaveRateOptions lastInterleaveRateOption;
	float lastVPLEmissionLevel;
	int lastPresetOrderOfMagnitude;
	int lastMaxDepth;

	Cube m_cube;
	Quad m_quad;
	Model1* m_Model;
	RootSignature m_RootSig;
	RootSignature m_ComputeRootSig;

	VPLManager vplManager;
	LGHBuilder gpuLightingGridBuilder;

	// INTERLEAVED_SAMPLING
	ColorBuffer m_ScenePositionBufferArray;
	ColorBuffer m_SceneNormalBufferArray;
	ColorBuffer m_SceneAlbedoBufferArray;
	ColorBuffer m_SceneSpecularBufferArray;
public:
	ColorBuffer m_DiscontinuityBuffer;
private:
	ColorBuffer m_AnalyticBufferDeinterleave;
	DepthBuffer m_SceneDepthBufferDeinterleave;
	//
	ColorBuffer m_runningSumBuffer;
	ColorBuffer m_lockImageBuffer;
	ColorBuffer m_vplSampleBuffer;
	ColorBuffer m_sampleColorBuffer;

	ColorBuffer m_NoiseEstimationBuffer[2];

	// pipeline state objects
	GraphicsPSO m_LightingComputationPSO;
	GraphicsPSO m_LightingComputationInterleavePSO;

	GraphicsPSO m_DepthDownsamplingPSO;
	GraphicsPSO m_DepthDownsampling2PSO;
	GraphicsPSO m_ScreenPSO;
	GraphicsPSO m_FillPSO;
	//
	
	GraphicsPSO m_ComputeGradLinearDepthPSO;

	// WAVELET_FILTER
	ComputePSO m_AtrousFilterPSO;

	// BILATERAL_FILTER
	ComputePSO m_NoiseEstimationPSO;
	ComputePSO m_DenoiseNEPSO;
	ComputePSO m_BilateralFilteringPSO;

	// INTERLEAVED_SAMPLING
	ComputePSO m_DeinterleavePSO;
	ComputePSO m_DiscontinuityPSO;
	ComputePSO m_InterleavePSO;
	ComputePSO m_BlurInterleavePSO;
	D3D12_CPU_DESCRIPTOR_HANDLE m_GBufferArraySrvs[4];
	//
	D3D12_CPU_DESCRIPTOR_HANDLE m_GBufferSrvs[4];
	D3D12_CPU_DESCRIPTOR_HANDLE m_LGHSamplingHandles[4];
	D3D12_CPU_DESCRIPTOR_HANDLE m_BilateralFilteringSrvs[6];
	D3D12_CPU_DESCRIPTOR_HANDLE m_BilateralFilteringUavs[3];
	public:
	SVGFDenoiser svgfDenoiser;
	private:
	void InitBuffers(int scrWidth, int scrHeight);
	void InitInterleavedBuffers(int scrWidth, int scrHeight);
	void InitRootSignatures();
	void InitComputePSOs();
	void InitPSOs();

	void GenerateDiscontinuityBuffer(ComputeContext & cptContext, int scrWidth, int scrHeight);

	void WaveletFiltering(ComputeContext& cptContext, int scrWidth, int scrHeight);
	void BilateralFiltering(ComputeContext& cptContext, const ViewConfig& viewConfig);
	void SVGFiltering(ComputeContext& cptContext, RootSignature& m_ComputeRootSig);

	void ComputeLinearDepthGradient(GraphicsContext& gfxContext, const ViewConfig& viewConfig);

	void GetSubViewportAndScissor(int i, int j, int rate, const ViewConfig& viewConfig, D3D12_VIEWPORT & viewport, D3D12_RECT & scissor);
	void ReinterleaveAndBlur(ComputeContext & cptContext, int scrWidth, int scrHeight, ColorBuffer & srcBuffer, ColorBuffer & dstBuffer);
};