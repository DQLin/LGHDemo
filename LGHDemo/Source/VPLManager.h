#pragma once
#include "CommandContext.h"
#include "RTXHelper.h"
#include "ModelLoader.h"
#include "BufferManager.h"
#include "LightRayGen.h"
#include "LightHit.h"
#include "ShadowHit.h"
#include "IRRayGen.h"
#include "LGHShadowRayGen.h"

using Microsoft::WRL::ComPtr;

enum RaytracingTypes
{
	LightTracing = 0,
	LGHShadowTracing,
	IRTracing,
	NumRaytracingTypes
};

__declspec(align(16)) struct LightTracingConstants
{
	Math::Vector3 sunDirection;
	Math::Vector3 sunLight;
	Math::Vector4 sceneSphere;
	int DispatchOffset;
	int maxDepth;
};


__declspec(align(16)) struct IRTracingConstants
{
	unsigned int currentVPL;
	float invNumPaths;
	float sceneRadius;
};

__declspec(align(16)) struct LGHShadowTracingConstants
{
	Vector4 halton[4];
	float invNumPaths;
	int shadowRate;
	int numLevels;
	int minLevel;
	float baseRadius;
	float devScale;
	float alpha;
	int temporalRandom;
	int frameId;
	float sceneRadius;
};

class VPLManager
{
public:
	enum class RaytracingAPI {
		FallbackLayer,
		DirectXRaytracing,
		Auto
	};

	VPLManager() : m_raytracingAPI(RaytracingAPI::Auto) {};
	//change this if you want to generate more than 10M vpls
	static const int MAXIMUM_NUM_VPLS = 11000000;

	UINT numVPLs, numPaths;
	std::vector<StructuredBuffer> VPLBuffers;
	unsigned maxRayRecursion;
	int numFramesUpdated;
	Vector4 sceneBoundingSphere;

	void Initialize(Model1* _model, int numModels, int _maxUpdateFrames = 1, int _maxRayRecursion = 30);
	bool GenerateVPLs(GraphicsContext & context, float VPLEmissionLevel, const Vector3& lightDirection, float lightIntensity, int maxDepth = 3, bool hasRegenRequest=false);
	void ComputeInstantRadiosity(GraphicsContext & context, int scrWidth, int scrHeight, int currentVPL);
	void CastLGHShadowRays(GraphicsContext & context, int scrWidth, int scrHeight, int shadowRate, int numLevels, 
		int minLevel, float baseRadius, float devScale, bool temporalRandom, float alpha, int frameId, int interleaveRate = 1);
	void InitializeLGHViews(const D3D12_CPU_DESCRIPTOR_HANDLE & shadowedStochasticUAV, 
						const D3D12_CPU_DESCRIPTOR_HANDLE & unshadowedStochasticUAV, 
						const D3D12_CPU_DESCRIPTOR_HANDLE & vplSampleBufferUAV, 
						const D3D12_CPU_DESCRIPTOR_HANDLE & runningSumBufferUAV, 
						const D3D12_CPU_DESCRIPTOR_HANDLE & sampleColorBufferSRV,
						ColorBuffer* deinterleavedPosition,
						ColorBuffer* deinterleavedNormal);
	void SwitchToInterleavedPositionAndNormal(const D3D12_CPU_DESCRIPTOR_HANDLE & deinterleavedPositionSRV, const D3D12_CPU_DESCRIPTOR_HANDLE & deinterleavedNormalSRV);
	void SwitchToNormalPositionAndNormal();
	void InitializeIRViews(ColorBuffer* resultBuffer);
	void InitializeLGHSrvs(
		const D3D12_CPU_DESCRIPTOR_HANDLE & blueNoise1SRV, 
		const D3D12_CPU_DESCRIPTOR_HANDLE & blueNoise2SRV, 
		const D3D12_CPU_DESCRIPTOR_HANDLE & blueNoise3SRV);
	void UpdateLGHSrvs(const D3D12_CPU_DESCRIPTOR_HANDLE & lightPositionSRV,
		const D3D12_CPU_DESCRIPTOR_HANDLE & lightNormalSRV,
		const D3D12_CPU_DESCRIPTOR_HANDLE & lightColorSRV,
		const D3D12_CPU_DESCRIPTOR_HANDLE & lightDevSRV);

	void UpdateAccelerationStructure();

private:

	enum VPLAttributes
	{
		POSITION = 0,
		NORMAL,
		COLOR
	};

	void BuildAccelerationStructures();
	void InitializeViews(const Model1& model);
	void InitializeSceneInfo();
	void SerializeAndCreateRaytracingRootSignature(D3D12_ROOT_SIGNATURE_DESC & desc, ComPtr<ID3D12RootSignature>* rootSig);
	void InitializeRaytracingRootSignatures();
	void InitializeRaytracingStateObjects();
	void InitializeRaytracingShaderTable();
	void MergeBoundingSpheres(Vector4& base, Vector4 in);

	//// Hardware DXR
	RaytracingAPI m_raytracingAPI;

	//Hardware
	ComPtr<ID3D12Device5> m_dxrDevice;
	ComPtr<ID3D12GraphicsCommandList5> m_dxrCommandList;
	std::vector<ComPtr<ID3D12StateObjectPrototype>> m_dxrStateObjects;

	//Fallback
	ComPtr<ID3D12RaytracingFallbackDevice> m_fallbackDevice;
	ComPtr<ID3D12RaytracingFallbackCommandList> m_fallbackCommandList;
	std::vector<ComPtr<ID3D12RaytracingFallbackStateObject>> m_fallbackStateObjects;
	////

	ByteAddressBuffer          m_lightConstantBuffer;
	ByteAddressBuffer		   m_IRConstantBuffer;
	ByteAddressBuffer		   m_lghShadowConstantBuffer;

	//scene related
	std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> m_GpuSceneMaterialSrvs;
	D3D12_CPU_DESCRIPTOR_HANDLE m_SceneMeshInfo;
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> m_SceneIndices;

	//AS
	std::vector<ComPtr<ID3D12Resource>>   m_bvh_bottomLevelAccelerationStructures;
	ComPtr<ID3D12Resource>   m_bvh_topLevelAccelerationStructure;
	WRAPPED_GPU_POINTER m_bvh_topLevelAccelerationStructurePointer;
	ID3D12Resource* pInstanceDataBuffer;
	ByteAddressBuffer scratchBuffer;
	std::vector<UINT> BLASDescriptorIndex;

	// Root signatures
	ComPtr<ID3D12RootSignature> m_raytracingGlobalRootSignature;
	ComPtr<ID3D12RootSignature> m_raytracingLocalRootSignature;

	Model1* m_Models;
	int numModels;
	int numMeshTotal;
	D3D12_GPU_DESCRIPTOR_HANDLE m_VPLUavs;
	D3D12_GPU_DESCRIPTOR_HANDLE m_SceneSrvs;
	// for LGH shadow tracing
	D3D12_GPU_DESCRIPTOR_HANDLE m_LGHSamplingSrvs;
	D3D12_GPU_DESCRIPTOR_HANDLE m_SUUavs;
	D3D12_GPU_DESCRIPTOR_HANDLE m_PositionNormalSrvs;

	D3D12_CPU_DESCRIPTOR_HANDLE m_PositionDescriptorHeapHandle, m_NormalDescriptorHeapHandle; //for switching between interleaved and normal

	D3D12_CPU_DESCRIPTOR_HANDLE m_LGHDescriptorHeapHandle[4];

	// for IR shadow tracing
	D3D12_GPU_DESCRIPTOR_HANDLE m_LGHSrvs;
	D3D12_GPU_DESCRIPTOR_HANDLE m_BlueNoiseSrvs;

	D3D12_GPU_DESCRIPTOR_HANDLE m_GBufferSrvs;
	ColorBuffer* IRResultBuffer;
	D3D12_GPU_DESCRIPTOR_HANDLE m_ResultUav;

	std::unique_ptr<DescriptorHeapStack> m_pRaytracingDescriptorHeap;
	RaytracingDispatchRayInputs m_RaytracingInputs[NumRaytracingTypes];
	StructuredBuffer    m_hitShaderMeshInfoBuffer;

	int maxUpdateFrames;

	Vector3 lastLightDirection;
	float lastLightIntensity;

	bool Use16BitIndex;
};