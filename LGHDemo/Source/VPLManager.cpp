//NOTE: Some functions for DXR environment intialization are modified from the Microsoft D3D12 Raytracing Samples

#include "VPLManager.h"
#include "ReadbackBuffer.h"
#include "RaytracingHlslCompat.h"

#include "CommandContext.h"
#include <D3D12RaytracingHelpers.hpp>
#include <intsafe.h>

void VPLManager::MergeBoundingSpheres(Vector4& base, Vector4 in)
{
	float baseRadius = base.GetW();
	float inRadius = in.GetW();
	Vector3 c1, c2;
	float R, r;

	if (baseRadius >= inRadius)
	{
		c1 = Vector3(base);
		c2 = Vector3(in);
		R = baseRadius;
		r = inRadius;
	}
	else
	{
		c1 = Vector3(in);
		c2 = Vector3(base);
		R = inRadius;
		r = baseRadius;
	}

	Vector3 d = c2 - c1;
	float dMag = sqrt(d.GetX()*d.GetX() + d.GetY()*d.GetY() + d.GetZ()*d.GetZ());
	d = d / dMag;

	float deltaRadius = 0.5f * (std::max(R, dMag + r) - R);
	Vector3 center = c1 + deltaRadius * d;
	float radius = R + deltaRadius;
	base = Vector4(center, radius);
}


void VPLManager::Initialize(Model1* _model, int _numModels, int _maxUpdateFrames /*= 1*/, int _maxRayRecursion /*= 30*/)
{
	m_Models = _model;
	numModels = _numModels;

	if (numModels > 0)
		if (m_Models[0].indexSize == 2) Use16BitIndex = true;
		else Use16BitIndex = false;

	sceneBoundingSphere = m_Models[0].m_SceneBoundingSphere;
	for (int modelId = 1; modelId < numModels; modelId++)
		MergeBoundingSpheres(sceneBoundingSphere, m_Models[modelId].m_SceneBoundingSphere);

	for (int modelId = 0; modelId < numModels; modelId++)
		numMeshTotal += m_Models[modelId].m_Header.meshCount;

	lastLightIntensity = -1;

	if (m_raytracingAPI == RaytracingAPI::Auto)
	{
		if (FAILED(Graphics::g_Device->QueryInterface(IID_PPV_ARGS(&m_dxrDevice))))
		{
			std::cout << "DirectX Raytracing interfact not found, switching to the fallback layer" << std::endl;
			CreateRaytracingFallbackDeviceFlags createDeviceFlags = CreateRaytracingFallbackDeviceFlags::ForceComputeFallback;
			ThrowIfFailed(D3D12CreateRaytracingFallbackDevice(Graphics::g_Device, createDeviceFlags, 0, IID_PPV_ARGS(&m_fallbackDevice)), L"Couldn't initialize fallback device");
			m_raytracingAPI = RaytracingAPI::FallbackLayer;
		}
		else m_raytracingAPI = RaytracingAPI::DirectXRaytracing;
	}
	else if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		CreateRaytracingFallbackDeviceFlags createDeviceFlags = CreateRaytracingFallbackDeviceFlags::ForceComputeFallback;
		ThrowIfFailed(D3D12CreateRaytracingFallbackDevice(Graphics::g_Device, createDeviceFlags, 0, IID_PPV_ARGS(&m_fallbackDevice)), L"Couldn't initialize fallback device");
	}
	else // DirectX Raytracing
	{
		ThrowIfFailed(Graphics::g_Device->QueryInterface(IID_PPV_ARGS(&m_dxrDevice)), L"Couldn't get DirectX Raytracing interface for the device.\n");
	}

	using namespace Math;
	VPLBuffers.resize(3);
	VPLBuffers[POSITION].Create(L"VPL Position Buffer", MAXIMUM_NUM_VPLS, sizeof(Vector3));
	VPLBuffers[NORMAL].Create(L"VPL Normal Buffer", MAXIMUM_NUM_VPLS, sizeof(Vector3));
	VPLBuffers[COLOR].Create(L"VPL Color Buffer", MAXIMUM_NUM_VPLS, sizeof(Vector3));
	// allocate dedicated descriptor heap for ray tracing
	m_pRaytracingDescriptorHeap = std::unique_ptr<DescriptorHeapStack>(
		new DescriptorHeapStack(*Graphics::g_Device, 8000, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 0));
	D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1;
	HRESULT hr = Graphics::g_Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1));

	m_lightConstantBuffer.Create(L"Hit Constant Buffer", 1, sizeof(LightTracingConstants));
	m_IRConstantBuffer.Create(L"IR Constant Buffer", 1, sizeof(IRTracingConstants));
	m_lghShadowConstantBuffer.Create(L"Rand Shadow Constant Buffer", 1, sizeof(LGHShadowTracingConstants));

	m_LGHDescriptorHeapHandle[0].ptr = 0;
	m_LGHDescriptorHeapHandle[1].ptr = 0;
	m_LGHDescriptorHeapHandle[2].ptr = 0;
	m_LGHDescriptorHeapHandle[3].ptr = 0;

	InitializeSceneInfo();
	InitializeViews(*_model);
	numFramesUpdated = -1;
	maxRayRecursion = _maxRayRecursion;
	maxUpdateFrames = _maxUpdateFrames;
	BuildAccelerationStructures();
	InitializeRaytracingRootSignatures();
	InitializeRaytracingStateObjects();
	InitializeRaytracingShaderTable();
}

void VPLManager::UpdateAccelerationStructure()
{
	const UINT numBottomLevels = numModels;
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC topLevelAccelerationStructureDesc = {};
	topLevelAccelerationStructureDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	topLevelAccelerationStructureDesc.Inputs.NumDescs = numBottomLevels;
	topLevelAccelerationStructureDesc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE |
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
	topLevelAccelerationStructureDesc.Inputs.pGeometryDescs = nullptr;
	topLevelAccelerationStructureDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

	D3D12_RAYTRACING_FALLBACK_INSTANCE_DESC* instanceDescs_Fallback;
	D3D12_RAYTRACING_INSTANCE_DESC* instanceDescs;

	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		pInstanceDataBuffer->Map(0, nullptr, reinterpret_cast<void**>(&instanceDescs_Fallback));
	}
	else // DirectX Raytracing
	{
		pInstanceDataBuffer->Map(0, nullptr, reinterpret_cast<void**>(&instanceDescs));
	}

	int meshIdOffset = 0;

	auto setTransform = [&](auto* instanceDesc, int i)
	{
		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 4; c++)
			{
				Matrix4 temp = m_Models[i].m_modelMatrix;
				Vector4 t = c == 0 ? temp.GetX() : c == 1 ? temp.GetY() : c == 2 ? temp.GetZ() : temp.GetW();
				instanceDesc->Transform[r][c] = r == 0 ? t.GetX() : r == 1 ? t.GetY() : t.GetZ();
			}
	};

	for (UINT i = 0; i < numBottomLevels; i++)
	{
		if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
		{
			D3D12_RAYTRACING_FALLBACK_INSTANCE_DESC &instanceDesc = instanceDescs_Fallback[i];
			setTransform(&instanceDesc, i);
		}
		else
		{
			D3D12_RAYTRACING_INSTANCE_DESC &instanceDesc = instanceDescs[i];
			setTransform(&instanceDesc, i);
		}

	}
	pInstanceDataBuffer->Unmap(0, 0);

	topLevelAccelerationStructureDesc.Inputs.InstanceDescs = pInstanceDataBuffer->GetGPUVirtualAddress();
	topLevelAccelerationStructureDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Create Acceleration Structure");
	ID3D12GraphicsCommandList *pCommandList = gfxContext.GetCommandList();

	{
		D3D12_RESOURCE_BARRIER uavBarrier = {};
		uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		uavBarrier.UAV.pResource = m_bvh_topLevelAccelerationStructure.Get();
		pCommandList->ResourceBarrier(1, &uavBarrier);
		gfxContext.FlushResourceBarriers();
	}

	topLevelAccelerationStructureDesc.SourceAccelerationStructureData = m_bvh_topLevelAccelerationStructure->GetGPUVirtualAddress();
	topLevelAccelerationStructureDesc.DestAccelerationStructureData = m_bvh_topLevelAccelerationStructure->GetGPUVirtualAddress();
	topLevelAccelerationStructureDesc.ScratchAccelerationStructureData = scratchBuffer.GetGpuVirtualAddress();

	auto UpdateAccelerationStructure = [&](auto* raytracingCommandList)
	{
		raytracingCommandList->BuildRaytracingAccelerationStructure(&topLevelAccelerationStructureDesc, 0, nullptr);
	};

	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		m_fallbackDevice->QueryRaytracingCommandList(pCommandList, IID_PPV_ARGS(&m_fallbackCommandList));
		// Set the descriptor heaps to be used during acceleration structure build for the Fallback Layer.
		ID3D12DescriptorHeap *descriptorHeaps[] = { &m_pRaytracingDescriptorHeap->GetDescriptorHeap() };
		m_fallbackCommandList->SetDescriptorHeaps(ARRAYSIZE(descriptorHeaps), descriptorHeaps);
		auto uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);
		pCommandList->ResourceBarrier(1, &uavBarrier);
		//
		UpdateAccelerationStructure(m_fallbackCommandList.Get());
		m_bvh_topLevelAccelerationStructurePointer = m_fallbackDevice->GetWrappedPointerSimple(
			m_pRaytracingDescriptorHeap->AllocateBufferUav(*m_bvh_topLevelAccelerationStructure.Get()),
			m_bvh_topLevelAccelerationStructure->GetGPUVirtualAddress());
	}
	else
	{
		ThrowIfFailed(pCommandList->QueryInterface(IID_PPV_ARGS(&m_dxrCommandList)), L"Couldn't get DirectX Raytracing interface for the command list.\n");
		UpdateAccelerationStructure(m_dxrCommandList.Get());
		m_bvh_topLevelAccelerationStructurePointer.GpuVA = m_bvh_topLevelAccelerationStructure->GetGPUVirtualAddress();
	}

	gfxContext.Finish(true);
}

void VPLManager::BuildAccelerationStructures()
{
	const UINT numBottomLevels = numModels;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topLevelPrebuildInfo;
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC topLevelAccelerationStructureDesc = {};
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS &topLevelInputs = topLevelAccelerationStructureDesc.Inputs;
	topLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	topLevelInputs.NumDescs = numBottomLevels;
	topLevelInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
	topLevelInputs.pGeometryDescs = nullptr;
	topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		m_fallbackDevice->GetRaytracingAccelerationStructurePrebuildInfo(&topLevelInputs, &topLevelPrebuildInfo);
	}
	else
	{
		m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&topLevelInputs, &topLevelPrebuildInfo);
	}

	std::vector<std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>> geometryDescs(numModels);

	for (int modelId = 0; modelId < numModels; modelId++)
	{
		int numMeshes = m_Models[modelId].m_Header.meshCount;
		geometryDescs[modelId].resize(numMeshes);

		for (UINT i = 0; i < numMeshes; i++)
		{
			auto &mesh = m_Models[modelId].m_pMesh[i];

			D3D12_RAYTRACING_GEOMETRY_DESC &desc = geometryDescs[modelId][i];
			desc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
			desc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

			D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC &trianglesDesc = desc.Triangles;
			trianglesDesc.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
			trianglesDesc.VertexCount = mesh.vertexCount;
			trianglesDesc.VertexBuffer.StartAddress = m_Models[modelId].m_VertexBuffer.GetGpuVirtualAddress() +
				(mesh.vertexDataByteOffset + mesh.attrib[Model1::attrib_position].offset);
			trianglesDesc.IndexBuffer = m_Models[modelId].m_IndexBuffer.GetGpuVirtualAddress() + mesh.indexDataByteOffset;
			trianglesDesc.VertexBuffer.StrideInBytes = mesh.vertexStride;
			trianglesDesc.IndexCount = mesh.indexCount;
			trianglesDesc.IndexFormat = (Use16BitIndex && modelId == 0) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
			trianglesDesc.Transform3x4 = 0;
			assert(trianglesDesc.IndexCount % 3 == 0);
		}
	}

	UINT64 scratchBufferSizeNeeded = topLevelPrebuildInfo.ScratchDataSizeInBytes;

	std::vector<UINT64> bottomLevelAccelerationStructureSize(numBottomLevels);
	std::vector<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC> bottomLevelAccelerationStructureDescs(numBottomLevels);
	for (UINT i = 0; i < numBottomLevels; i++)
	{
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC &bottomLevelAccelerationStructureDesc = bottomLevelAccelerationStructureDescs[i];
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS &bottomLevelInputs = bottomLevelAccelerationStructureDesc.Inputs;
		bottomLevelInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
		bottomLevelInputs.NumDescs = geometryDescs[i].size();
		bottomLevelInputs.pGeometryDescs = geometryDescs[i].data();
		bottomLevelInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
		bottomLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottomLevelprebuildInfo;
		if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
		{
			m_fallbackDevice->GetRaytracingAccelerationStructurePrebuildInfo(&bottomLevelInputs, &bottomLevelprebuildInfo);
		}
		else
		{
			m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&bottomLevelInputs, &bottomLevelprebuildInfo);
		}

		bottomLevelAccelerationStructureSize[i] = bottomLevelprebuildInfo.ResultDataMaxSizeInBytes;
		scratchBufferSizeNeeded = std::max(bottomLevelprebuildInfo.ScratchDataSizeInBytes, scratchBufferSizeNeeded);
	}

	scratchBuffer.Create(L"Acceleration Structure Scratch Buffer", (UINT)scratchBufferSizeNeeded, 1);

	D3D12_RESOURCE_STATES initialResourceState;
	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		initialResourceState = m_fallbackDevice->GetAccelerationStructureResourceState();
	}
	else // DirectX Raytracing
	{
		initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
	}

	D3D12_HEAP_PROPERTIES defaultHeapDesc = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	auto topLevelDesc = CD3DX12_RESOURCE_DESC::Buffer(topLevelPrebuildInfo.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	Graphics::g_Device->CreateCommittedResource(
		&defaultHeapDesc,
		D3D12_HEAP_FLAG_NONE,
		&topLevelDesc,
		initialResourceState,
		nullptr,
		IID_PPV_ARGS(&m_bvh_topLevelAccelerationStructure));

	topLevelAccelerationStructureDesc.DestAccelerationStructureData = m_bvh_topLevelAccelerationStructure->GetGPUVirtualAddress();
	topLevelAccelerationStructureDesc.ScratchAccelerationStructureData = scratchBuffer.GetGpuVirtualAddress();

	D3D12_RAYTRACING_FALLBACK_INSTANCE_DESC* instanceDescs_Fallback = nullptr;
	D3D12_RAYTRACING_INSTANCE_DESC* instanceDescs = nullptr;

	auto AllocateUploadBuffer = [&](ID3D12Device* pDevice, UINT64 datasize, ID3D12Resource **ppResource, void** pMappedData, const wchar_t* resourceName = nullptr)
	{
		auto uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(datasize);
		ThrowIfFailed(pDevice->CreateCommittedResource(
			&uploadHeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(ppResource)));
		if (resourceName)
		{
			(*ppResource)->SetName(resourceName);
		}
		(*ppResource)->Map(0, nullptr, pMappedData);
		memset(*pMappedData, 0, datasize);
	};

	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		AllocateUploadBuffer(Graphics::g_Device, numBottomLevels * sizeof(D3D12_RAYTRACING_FALLBACK_INSTANCE_DESC),
			&pInstanceDataBuffer, (void**)&instanceDescs_Fallback, L"InstanceDescs");
	}
	else // DirectX Raytracing
	{
		AllocateUploadBuffer(Graphics::g_Device, numBottomLevels * sizeof(D3D12_RAYTRACING_INSTANCE_DESC),
			&pInstanceDataBuffer, (void**)&instanceDescs, L"InstanceDescs");
	}

	m_bvh_bottomLevelAccelerationStructures.resize(numBottomLevels);
	int meshIdOffset = 0;

	BLASDescriptorIndex.resize(numBottomLevels);

	auto setTransform = [&](auto* instanceDesc, int i)
	{
		// Identity matrix
		ZeroMemory(instanceDesc->Transform, sizeof(instanceDesc->Transform));
		for (int r = 0; r < 3; r++)
			for (int c = 0; c < 4; c++)
			{
				//weird, indexing does not work here
				Matrix4 temp = m_Models[i].m_modelMatrix;
				Vector4 t = r == 0 ? temp.GetX() : r == 1 ? temp.GetY() : r == 2 ? temp.GetZ() : temp.GetW();
				instanceDesc->Transform[r][c] = c == 0 ? t.GetX() : c == 1 ? t.GetY() : c == 2 ? t.GetZ() : t.GetW();
			}
	};

	for (UINT i = 0; i < numBottomLevels; i++)
	{
		auto &bottomLevelStructure = m_bvh_bottomLevelAccelerationStructures[i];

		if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
		{
			initialResourceState = m_fallbackDevice->GetAccelerationStructureResourceState();
		}
		else // DirectX Raytracing
		{
			initialResourceState = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
		}

		auto bottomLevelDesc = CD3DX12_RESOURCE_DESC::Buffer(bottomLevelAccelerationStructureSize[i], D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		Graphics::g_Device->CreateCommittedResource(
			&defaultHeapDesc,
			D3D12_HEAP_FLAG_NONE,
			&bottomLevelDesc,
			initialResourceState,
			nullptr,
			IID_PPV_ARGS(&bottomLevelStructure));

		bottomLevelAccelerationStructureDescs[i].DestAccelerationStructureData = bottomLevelStructure->GetGPUVirtualAddress();
		bottomLevelAccelerationStructureDescs[i].ScratchAccelerationStructureData = scratchBuffer.GetGpuVirtualAddress();
		BLASDescriptorIndex[i] = m_pRaytracingDescriptorHeap->AllocateBufferUav(*bottomLevelStructure.Get());

		if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
		{
			D3D12_RAYTRACING_FALLBACK_INSTANCE_DESC &instanceDesc = instanceDescs_Fallback[i];
			setTransform(&instanceDesc, i);
			instanceDesc.AccelerationStructure = m_fallbackDevice->GetWrappedPointerSimple(BLASDescriptorIndex[i],
				m_bvh_bottomLevelAccelerationStructures[i]->GetGPUVirtualAddress());
			instanceDesc.Flags = 0;
			instanceDesc.InstanceID = i;
			instanceDesc.InstanceMask = 1;
			instanceDesc.InstanceContributionToHitGroupIndex = meshIdOffset;
		}
		else
		{
			D3D12_RAYTRACING_INSTANCE_DESC &instanceDesc = instanceDescs[i];
			setTransform(&instanceDesc, i);
			instanceDesc.AccelerationStructure = m_bvh_bottomLevelAccelerationStructures[i]->GetGPUVirtualAddress();
			instanceDesc.Flags = 0;
			instanceDesc.InstanceID = i;
			instanceDesc.InstanceMask = 1;
			instanceDesc.InstanceContributionToHitGroupIndex = meshIdOffset;
		}

		meshIdOffset += m_Models[i].m_Header.meshCount;
	}

	pInstanceDataBuffer->Unmap(0, 0);

	topLevelInputs.InstanceDescs = pInstanceDataBuffer->GetGPUVirtualAddress();//  instanceDataBuffer.GetGpuVirtualAddress();
	topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Build Acceleration Structures");
	ID3D12GraphicsCommandList *pCommandList = gfxContext.GetCommandList();

	auto BuildAccelerationStructure = [&](auto* raytracingCommandList)
	{
		for (UINT i = 0; i < bottomLevelAccelerationStructureDescs.size(); i++)
		{
			raytracingCommandList->BuildRaytracingAccelerationStructure(&bottomLevelAccelerationStructureDescs[i], 0, nullptr);
			pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(m_bvh_bottomLevelAccelerationStructures[i].Get()));
		}
		raytracingCommandList->BuildRaytracingAccelerationStructure(&topLevelAccelerationStructureDesc, 0, nullptr);
	};

	// Build acceleration structure.
	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		m_fallbackDevice->QueryRaytracingCommandList(pCommandList, IID_PPV_ARGS(&m_fallbackCommandList));
		// Set the descriptor heaps to be used during acceleration structure build for the Fallback Layer.
		ID3D12DescriptorHeap *descriptorHeaps[] = { &m_pRaytracingDescriptorHeap->GetDescriptorHeap() };
		m_fallbackCommandList->SetDescriptorHeaps(ARRAYSIZE(descriptorHeaps), descriptorHeaps);
		BuildAccelerationStructure(m_fallbackCommandList.Get());
		m_bvh_topLevelAccelerationStructurePointer = m_fallbackDevice->GetWrappedPointerSimple(
			m_pRaytracingDescriptorHeap->AllocateBufferUav(*m_bvh_topLevelAccelerationStructure.Get()),
			m_bvh_topLevelAccelerationStructure->GetGPUVirtualAddress());
	}
	else
	{
		ThrowIfFailed(pCommandList->QueryInterface(IID_PPV_ARGS(&m_dxrCommandList)), L"Couldn't get DirectX Raytracing interface for the command list.\n");
		BuildAccelerationStructure(m_dxrCommandList.Get());
		m_bvh_topLevelAccelerationStructurePointer.GpuVA = m_bvh_topLevelAccelerationStructure->GetGPUVirtualAddress();
	}

	gfxContext.Finish(true);
}

void VPLManager::InitializeIRViews(ColorBuffer* resultBuffer)
{
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle;
	UINT srvDescriptorIndex;
	m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
	Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, Graphics::g_ScenePositionBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_GBufferSrvs = m_pRaytracingDescriptorHeap->GetGpuHandle(srvDescriptorIndex);

	m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
	Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, Graphics::g_SceneNormalBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
	Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, Graphics::g_SceneAlbedoBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_CPU_DESCRIPTOR_HANDLE uavHandle;
	UINT uavDescriptorIndex;

	m_pRaytracingDescriptorHeap->AllocateDescriptor(uavHandle, uavDescriptorIndex);
	Graphics::g_Device->CopyDescriptorsSimple(1, uavHandle, resultBuffer->GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_ResultUav = m_pRaytracingDescriptorHeap->GetGpuHandle(uavDescriptorIndex);

	IRResultBuffer = resultBuffer;
}

void VPLManager::InitializeLGHViews(const D3D12_CPU_DESCRIPTOR_HANDLE & shadowedStochasticUAV,
	const D3D12_CPU_DESCRIPTOR_HANDLE & unshadowedStochasticUAV,
	const D3D12_CPU_DESCRIPTOR_HANDLE & vplSampleBufferSRV,
	const D3D12_CPU_DESCRIPTOR_HANDLE & runningSumBufferSRV,
	const D3D12_CPU_DESCRIPTOR_HANDLE & sampleColorBufferSRV,
	ColorBuffer* deinterleavedPosition,
	ColorBuffer* deinterleavedNormal)
{
	D3D12_CPU_DESCRIPTOR_HANDLE uavHandle;
	UINT uavDescriptorIndex;

	m_pRaytracingDescriptorHeap->AllocateDescriptor(uavHandle, uavDescriptorIndex);
	Graphics::g_Device->CopyDescriptorsSimple(1, uavHandle, shadowedStochasticUAV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_SUUavs = m_pRaytracingDescriptorHeap->GetGpuHandle(uavDescriptorIndex);

	m_pRaytracingDescriptorHeap->AllocateDescriptor(uavHandle, uavDescriptorIndex);
	Graphics::g_Device->CopyDescriptorsSimple(1, uavHandle, unshadowedStochasticUAV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle;
	UINT srvDescriptorIndex;

	m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
	Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, vplSampleBufferSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_LGHSamplingSrvs = m_pRaytracingDescriptorHeap->GetGpuHandle(srvDescriptorIndex);

	m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
	Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, runningSumBufferSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
	Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, sampleColorBufferSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
	if (deinterleavedPosition)
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, deinterleavedPosition->GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	else
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, Graphics::g_ScenePositionBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	//memorize heap position
	m_PositionDescriptorHeapHandle = srvHandle;

	m_PositionNormalSrvs = m_pRaytracingDescriptorHeap->GetGpuHandle(srvDescriptorIndex);

	m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
	if (deinterleavedNormal)
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, deinterleavedNormal->GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	else
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, Graphics::g_SceneNormalBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	//memorize heap position
	m_NormalDescriptorHeapHandle = srvHandle;
}

void VPLManager::SwitchToInterleavedPositionAndNormal(const D3D12_CPU_DESCRIPTOR_HANDLE& deinterleavedPositionSRV,
	const D3D12_CPU_DESCRIPTOR_HANDLE& deinterleavedNormalSRV)
{
	Graphics::g_Device->CopyDescriptorsSimple(1, m_PositionDescriptorHeapHandle, deinterleavedPositionSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	Graphics::g_Device->CopyDescriptorsSimple(1, m_NormalDescriptorHeapHandle, deinterleavedNormalSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}


void VPLManager::SwitchToNormalPositionAndNormal()
{
	Graphics::g_Device->CopyDescriptorsSimple(1, m_PositionDescriptorHeapHandle, Graphics::g_ScenePositionBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	Graphics::g_Device->CopyDescriptorsSimple(1, m_NormalDescriptorHeapHandle, Graphics::g_SceneNormalBuffer.GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}


void VPLManager::InitializeViews(const Model1 & model)
{
	D3D12_CPU_DESCRIPTOR_HANDLE uavHandle;
	UINT uavDescriptorIndex;
	m_pRaytracingDescriptorHeap->AllocateDescriptor(uavHandle, uavDescriptorIndex);
	Graphics::g_Device->CopyDescriptorsSimple(1, uavHandle, VPLBuffers[0].GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_VPLUavs = m_pRaytracingDescriptorHeap->GetGpuHandle(uavDescriptorIndex);

	m_pRaytracingDescriptorHeap->AllocateDescriptor(uavHandle, uavDescriptorIndex);
	Graphics::g_Device->CopyDescriptorsSimple(1, uavHandle, VPLBuffers[1].GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	m_pRaytracingDescriptorHeap->AllocateDescriptor(uavHandle, uavDescriptorIndex);
	Graphics::g_Device->CopyDescriptorsSimple(1, uavHandle, VPLBuffers[2].GetUAV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle;
	UINT srvDescriptorIndex;
	m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
	Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, m_SceneMeshInfo, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_SceneSrvs = m_pRaytracingDescriptorHeap->GetGpuHandle(srvDescriptorIndex);

	UINT unused;

	for (int modelId = 0; modelId < numModels; modelId++)
	{
		m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, unused);
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, m_SceneIndices[modelId], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
	//fill empty
	for (int modelId = numModels; modelId < 2; modelId++)
	{
		m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, unused);
	}

	for (int modelId = 0; modelId < numModels; modelId++)
	{
		m_pRaytracingDescriptorHeap->AllocateBufferSrv(*const_cast<ID3D12Resource*>(m_Models[modelId].m_VertexBuffer.GetResource()));
	}
	//fill empty
	for (int modelId = numModels; modelId < 2; modelId++)
	{
		m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, unused);
	}

	for (int modelId = 0; modelId < numModels; modelId++)
		for (UINT i = 0; i < m_Models[modelId].m_Header.materialCount; i++)
		{
			UINT slot;
			m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, slot);
			Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, *m_Models[modelId].GetSRVs(i), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, unused);
			Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, m_Models[modelId].GetSRVs(i)[2], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			m_GpuSceneMaterialSrvs.push_back(m_pRaytracingDescriptorHeap->GetGpuHandle(slot));
		}
}

void VPLManager::InitializeLGHSrvs(
	const D3D12_CPU_DESCRIPTOR_HANDLE & blueNoise1SRV,
	const D3D12_CPU_DESCRIPTOR_HANDLE & blueNoise2SRV,
	const D3D12_CPU_DESCRIPTOR_HANDLE & blueNoise3SRV)
{
	D3D12_CPU_DESCRIPTOR_HANDLE srvHandle;
	UINT srvDescriptorIndex;

	m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
	Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, blueNoise1SRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	m_BlueNoiseSrvs = m_pRaytracingDescriptorHeap->GetGpuHandle(srvDescriptorIndex);

	m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
	Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, blueNoise2SRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
	Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, blueNoise3SRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

void VPLManager::UpdateLGHSrvs(const D3D12_CPU_DESCRIPTOR_HANDLE & lightPositionSRV,
	const D3D12_CPU_DESCRIPTOR_HANDLE & lightNormalSRV,
	const D3D12_CPU_DESCRIPTOR_HANDLE & lightColorSRV,
	const D3D12_CPU_DESCRIPTOR_HANDLE & lightDevSRV)
{
	if (m_LGHDescriptorHeapHandle[0].ptr)
	{
		Graphics::g_Device->CopyDescriptorsSimple(1, m_LGHDescriptorHeapHandle[0], lightPositionSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		Graphics::g_Device->CopyDescriptorsSimple(1, m_LGHDescriptorHeapHandle[1], lightNormalSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		Graphics::g_Device->CopyDescriptorsSimple(1, m_LGHDescriptorHeapHandle[2], lightColorSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		Graphics::g_Device->CopyDescriptorsSimple(1, m_LGHDescriptorHeapHandle[3], lightDevSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}
	else
	{
		D3D12_CPU_DESCRIPTOR_HANDLE srvHandle;
		UINT srvDescriptorIndex;

		m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, lightPositionSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_LGHDescriptorHeapHandle[0] = srvHandle;
		m_LGHSrvs = m_pRaytracingDescriptorHeap->GetGpuHandle(srvDescriptorIndex);

		m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, lightNormalSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_LGHDescriptorHeapHandle[1] = srvHandle;

		m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, lightColorSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_LGHDescriptorHeapHandle[2] = srvHandle;

		m_pRaytracingDescriptorHeap->AllocateDescriptor(srvHandle, srvDescriptorIndex);
		Graphics::g_Device->CopyDescriptorsSimple(1, srvHandle, lightDevSRV, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
		m_LGHDescriptorHeapHandle[3] = srvHandle;
	}
}

void VPLManager::InitializeSceneInfo()
{
	//
	// Mesh info
	//
	int meshIdOffset = 0;
	std::vector<RayTraceMeshInfo>  meshInfoData;

	for (int modelId = 0; modelId < numModels; modelId++)
	{
		Model1& model = m_Models[modelId];
		int numMeshes = model.m_Header.meshCount;
		for (UINT i = 0; i < numMeshes; ++i)
		{
			RayTraceMeshInfo meshInfo;

			meshInfo.m_indexOffsetBytes = model.m_pMesh[i].indexDataByteOffset;
			meshInfo.m_uvAttributeOffsetBytes = model.m_pMesh[i].vertexDataByteOffset + model.m_pMesh[i].attrib[Model1::attrib_texcoord0].offset;
			meshInfo.m_normalAttributeOffsetBytes = model.m_pMesh[i].vertexDataByteOffset + model.m_pMesh[i].attrib[Model1::attrib_normal].offset;
			meshInfo.m_positionAttributeOffsetBytes = model.m_pMesh[i].vertexDataByteOffset + model.m_pMesh[i].attrib[Model1::attrib_position].offset;
			meshInfo.m_tangentAttributeOffsetBytes = model.m_pMesh[i].vertexDataByteOffset + model.m_pMesh[i].attrib[Model1::attrib_tangent].offset;
			meshInfo.m_bitangentAttributeOffsetBytes = model.m_pMesh[i].vertexDataByteOffset + model.m_pMesh[i].attrib[Model1::attrib_bitangent].offset;
			meshInfo.m_attributeStrideBytes = model.m_pMesh[i].vertexStride;
			meshInfo.m_materialInstanceId = model.m_pMesh[i].materialIndex;
			meshInfo.diffuse[0] = model.m_pMaterial[meshInfo.m_materialInstanceId].diffuse.GetX();
			meshInfo.diffuse[1] = model.m_pMaterial[meshInfo.m_materialInstanceId].diffuse.GetY();
			meshInfo.diffuse[2] = model.m_pMaterial[meshInfo.m_materialInstanceId].diffuse.GetZ();
			meshInfoData.push_back(meshInfo);
		}
	}

	m_hitShaderMeshInfoBuffer.Create(L"RayTraceMeshInfo",
		(UINT)meshInfoData.size(),
		sizeof(meshInfoData[0]),
		meshInfoData.data());

	m_SceneIndices.resize(numModels);
	for (int modelId = 0; modelId < numModels; modelId++)
		m_SceneIndices[modelId] = m_Models[modelId].m_IndexBuffer.GetSRV();
	m_SceneMeshInfo = m_hitShaderMeshInfoBuffer.GetSRV();
}


void VPLManager::SerializeAndCreateRaytracingRootSignature(D3D12_ROOT_SIGNATURE_DESC& desc, ComPtr<ID3D12RootSignature>* rootSig)
{
	ComPtr<ID3DBlob> blob;
	ComPtr<ID3DBlob> error;

	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		ThrowIfFailed(m_fallbackDevice->D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error), error ? static_cast<wchar_t*>(error->GetBufferPointer()) : nullptr);
		ThrowIfFailed(m_fallbackDevice->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&(*rootSig))));
	}
	else // DirectX Raytracing
	{
		ThrowIfFailed(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error), error ? static_cast<wchar_t*>(error->GetBufferPointer()) : nullptr);
		ThrowIfFailed(Graphics::g_Device->CreateRootSignature(1, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&(*rootSig))));
	}
}

void VPLManager::InitializeRaytracingRootSignatures()
{
	// Global Root Signature
// This is a root signature that is shared across all raytracing shaders invoked during a DispatchRays() call.
	{
		CD3DX12_STATIC_SAMPLER_DESC staticSamplerDesc;
		staticSamplerDesc.Init(0, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, 0.f, 8U,
			D3D12_COMPARISON_FUNC_LESS_EQUAL, D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK, 0.f, D3D12_FLOAT32_MAX,
			D3D12_SHADER_VISIBILITY_ALL);

		CD3DX12_DESCRIPTOR_RANGE ranges[8]; // Perfomance TIP: Order from most frequent to least frequent.
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 1);
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
		ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 3, 2);
		ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 64);
		ranges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 12);
		ranges[5].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 32);
		ranges[6].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 9);
		ranges[7].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 13);

		CD3DX12_ROOT_PARAMETER rootParameters[10];
		rootParameters[0].InitAsDescriptorTable(1, ranges);
		rootParameters[1].InitAsConstantBufferView(0);
		rootParameters[2].InitAsDescriptorTable(1, ranges + 1);
		rootParameters[3].InitAsDescriptorTable(1, ranges + 2);
		rootParameters[4].InitAsDescriptorTable(1, ranges + 3);
		rootParameters[5].InitAsDescriptorTable(1, ranges + 4);
		rootParameters[6].InitAsDescriptorTable(1, ranges + 5);
		rootParameters[7].InitAsShaderResourceView(0);
		rootParameters[8].InitAsDescriptorTable(1, ranges + 6);
		rootParameters[9].InitAsDescriptorTable(1, ranges + 7);

		CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(ARRAYSIZE(rootParameters), rootParameters, 1,
			&staticSamplerDesc);
		SerializeAndCreateRaytracingRootSignature(globalRootSignatureDesc, &m_raytracingGlobalRootSignature);
	}


	// Local Root Signature
	{
		CD3DX12_ROOT_PARAMETER rootParameters[2];
		CD3DX12_DESCRIPTOR_RANGE range; // Perfomance TIP: Order from most frequent to least frequent.
		UINT sizeOfRootConstantInDwords = (sizeof(MaterialRootConstant) - 1) / sizeof(DWORD) + 1;
		range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 6, 0);
		rootParameters[0].InitAsDescriptorTable(1, &range);
		rootParameters[1].InitAsConstants(sizeOfRootConstantInDwords, 3);
		CD3DX12_ROOT_SIGNATURE_DESC localRootSignatureDesc(ARRAYSIZE(rootParameters), rootParameters);
		localRootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
		SerializeAndCreateRaytracingRootSignature(localRootSignatureDesc, &m_raytracingLocalRootSignature);
	}
}

void VPLManager::InitializeRaytracingStateObjects()
{
	// Create 7 subobjects that combine into a RTPSO:
	// Subobjects need to be associated with DXIL exports (i.e. shaders) either by way of default or explicit associations.
	// Default association applies to every exported shader entrypoint that doesn't have any of the same type of subobject associated with it.
	// This simple sample utilizes default shader association except for local root signature subobject
	// which has an explicit association specified purely for demonstration purposes.
	// 1 - DXIL library
	// 1 - Triangle hit group
	// 1 - Shader config
	// 2 - Local root signature and association
	// 1 - Global root signature
	// 1 - Pipeline config
	m_fallbackStateObjects.resize(NumRaytracingTypes);
	m_dxrStateObjects.resize(NumRaytracingTypes);

	CD3D12_STATE_OBJECT_DESC raytracingPipeline{ D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };

	// DXIL library
	// This contains the shaders and their entrypoints for the state object.
	// Since shaders are not considered a subobject, they need to be passed in via DXIL library subobjects.

	CD3D12_DXIL_LIBRARY_SUBOBJECT* rayGenLibSubobject;
	CD3D12_DXIL_LIBRARY_SUBOBJECT* anyHitLibSubobject;
	CD3D12_DXIL_LIBRARY_SUBOBJECT* closestHitLibSubobject;
	CD3D12_DXIL_LIBRARY_SUBOBJECT* missLibSubobject;

	////// Shadow Tracing

	rayGenLibSubobject = raytracingPipeline.CreateSubobject<CD3D12_DXIL_LIBRARY_SUBOBJECT>();
	rayGenLibSubobject->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE((void *)g_pLGHShadowRayGen, ARRAYSIZE(g_pLGHShadowRayGen)));
	rayGenLibSubobject->DefineExport(L"RayGen");

	anyHitLibSubobject = raytracingPipeline.CreateSubobject<CD3D12_DXIL_LIBRARY_SUBOBJECT>();
	anyHitLibSubobject->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE((void *)g_pShadowHit, ARRAYSIZE(g_pShadowHit)));
	anyHitLibSubobject->DefineExport(L"AnyHit");

	closestHitLibSubobject = raytracingPipeline.CreateSubobject<CD3D12_DXIL_LIBRARY_SUBOBJECT>();
	closestHitLibSubobject->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE((void *)g_pShadowHit, ARRAYSIZE(g_pShadowHit)));
	closestHitLibSubobject->DefineExport(L"Hit");

	missLibSubobject = raytracingPipeline.CreateSubobject<CD3D12_DXIL_LIBRARY_SUBOBJECT>();
	missLibSubobject->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE((void *)g_pShadowHit, ARRAYSIZE(g_pShadowHit)));
	missLibSubobject->DefineExport(L"Miss");

	auto hitGroup = raytracingPipeline.CreateSubobject<CD3D12_HIT_GROUP_SUBOBJECT>();
	hitGroup->SetClosestHitShaderImport(L"Hit");
	hitGroup->SetAnyHitShaderImport(L"AnyHit");
	hitGroup->SetHitGroupExport(L"HitGroup");
	hitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

	// Shader config
	// Defines the maximum sizes in bytes for the ray payload and attribute structure.
	auto shaderConfig = raytracingPipeline.CreateSubobject<CD3D12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
	UINT payloadSize = 4;  
	UINT attributeSize = sizeof(XMFLOAT2);  // float2 barycentrics
	shaderConfig->Config(payloadSize, attributeSize);

	// create local root signature subobjects
	auto localRootSignature = raytracingPipeline.CreateSubobject<CD3D12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
	localRootSignature->SetRootSignature(m_raytracingLocalRootSignature.Get());
	// Define explicit shader association for the local root signature. 
	{
		auto rootSignatureAssociation = raytracingPipeline.CreateSubobject<CD3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
		rootSignatureAssociation->SetSubobjectToAssociate(*localRootSignature);
		rootSignatureAssociation->AddExport(L"HitGroup");
	}

	// Global root signature
	// This is a root signature that is shared across all raytracing shaders invoked during a DispatchRays() call.
	auto globalRootSignature = raytracingPipeline.CreateSubobject<CD3D12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
	globalRootSignature->SetRootSignature(m_raytracingGlobalRootSignature.Get());

	// Pipeline config
	// Defines the maximum TraceRay() recursion depth.
	auto pipelineConfig = raytracingPipeline.CreateSubobject<CD3D12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
	// PERFOMANCE TIP: Set max recursion depth as low as needed 
	// as drivers may apply optimization strategies for low recursion depths.
	pipelineConfig->Config(maxRayRecursion);

		// Create the state object.
	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		ThrowIfFailed(m_fallbackDevice->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(&m_fallbackStateObjects[LGHShadowTracing])),
			L"Couldn't create DirectX Raytracing state object.\n");
		rayGenLibSubobject->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE((void *)g_pIRRayGen, ARRAYSIZE(g_pIRRayGen)));
		ThrowIfFailed(m_fallbackDevice->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(&m_fallbackStateObjects[IRTracing])),
			L"Couldn't create DirectX Raytracing state object.\n");
		shaderConfig->Config(20, attributeSize); //change it to 20 if use UE4 RANDOM
		rayGenLibSubobject->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE((void *)g_pLightRayGen, ARRAYSIZE(g_pLightRayGen)));
		anyHitLibSubobject->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE((void *)g_pLightHit, ARRAYSIZE(g_pLightHit)));
		closestHitLibSubobject->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE((void *)g_pLightHit, ARRAYSIZE(g_pLightHit)));
		missLibSubobject->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE((void *)g_pLightHit, ARRAYSIZE(g_pLightHit)));
		ThrowIfFailed(m_fallbackDevice->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(&m_fallbackStateObjects[LightTracing])),
			L"Couldn't create DirectX Raytracing state object.\n");
	}
	else // DirectX Raytracing
	{
		ThrowIfFailed(m_dxrDevice->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(&m_dxrStateObjects[LGHShadowTracing])),
			L"Couldn't create DirectX Raytracing state object.\n");
		rayGenLibSubobject->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE((void *)g_pIRRayGen, ARRAYSIZE(g_pIRRayGen)));
		ThrowIfFailed(m_dxrDevice->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(&m_dxrStateObjects[IRTracing])),
			L"Couldn't create DirectX Raytracing state object.\n");
		shaderConfig->Config(20, attributeSize); //change it to 20 if use UE4 RANDOM
		rayGenLibSubobject->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE((void *)g_pLightRayGen, ARRAYSIZE(g_pLightRayGen)));
		anyHitLibSubobject->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE((void *)g_pLightHit, ARRAYSIZE(g_pLightHit)));
		closestHitLibSubobject->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE((void *)g_pLightHit, ARRAYSIZE(g_pLightHit)));
		missLibSubobject->SetDXILLibrary(&CD3DX12_SHADER_BYTECODE((void *)g_pLightHit, ARRAYSIZE(g_pLightHit)));
		ThrowIfFailed(m_dxrDevice->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(&m_dxrStateObjects[LightTracing])),
			L"Couldn't create DirectX Raytracing state object.\n");
	}
}


void VPLManager::InitializeRaytracingShaderTable()
{
	const UINT shaderIdentifierSize = m_raytracingAPI == RaytracingAPI::FallbackLayer ? m_fallbackDevice->GetShaderIdentifierSize() : D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
#define ALIGN(alignment, num) ((((num) + alignment - 1) / alignment) * alignment)
	const UINT offsetToDescriptorHandle = ALIGN(sizeof(D3D12_GPU_DESCRIPTOR_HANDLE), shaderIdentifierSize);
	const UINT offsetToMaterialConstants = ALIGN(sizeof(UINT32), offsetToDescriptorHandle + sizeof(D3D12_GPU_DESCRIPTOR_HANDLE));
	const UINT shaderRecordSizeInBytes = ALIGN(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT, offsetToMaterialConstants + sizeof(MaterialRootConstant));

	std::vector<byte> pHitShaderTable(shaderRecordSizeInBytes * numMeshTotal);

	auto GetShaderTable = [=](auto *pPSO, byte *pShaderTable)
	{
		void *pHitGroupIdentifierData = pPSO->GetShaderIdentifier(L"HitGroup");

		int meshOffset = 0;
		int materialOffset = 0;
		for (int modelId = 0; modelId < numModels; modelId++)
		{
			int numMeshes = m_Models[modelId].m_Header.meshCount;
			int numMaterials = m_Models[modelId].m_Header.materialCount;

			for (UINT i = 0; i < numMeshes; i++)
			{
				byte *pShaderRecord = (meshOffset + i) * shaderRecordSizeInBytes + pShaderTable;
				memcpy(pShaderRecord, pHitGroupIdentifierData, shaderIdentifierSize);

				UINT materialIndex = materialOffset + m_Models[modelId].m_pMesh[i].materialIndex;
				memcpy(pShaderRecord + offsetToDescriptorHandle, &m_GpuSceneMaterialSrvs[materialIndex].ptr,
					sizeof(m_GpuSceneMaterialSrvs[materialIndex].ptr));
				MaterialRootConstant material;
				material.MeshInfoID = meshOffset + i;
				material.Use16bitIndex = m_Models[modelId].indexSize == 2;
				memcpy(pShaderRecord + offsetToMaterialConstants, &material, sizeof(material));
			}

			materialOffset += numMaterials;
			meshOffset += numMeshes;
		}
	};


	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		for (int i = 0; i < NumRaytracingTypes; i++)
		{
			GetShaderTable(m_fallbackStateObjects[i].Get(), pHitShaderTable.data());
			m_RaytracingInputs[i] = RaytracingDispatchRayInputs(*m_fallbackDevice.Get(), m_fallbackStateObjects[i].Get(),
				pHitShaderTable.data(), shaderRecordSizeInBytes, (UINT)pHitShaderTable.size(), L"RayGen", L"Miss");
		}
	}
	else
	{
		for (int i = 0; i < NumRaytracingTypes; i++)
		{
			ComPtr<ID3D12StateObjectPropertiesPrototype> stateObjectProperties;
			ThrowIfFailed(m_dxrStateObjects[i].As(&stateObjectProperties));
			GetShaderTable(stateObjectProperties.Get(), pHitShaderTable.data());
			m_RaytracingInputs[i] = RaytracingDispatchRayInputs(*m_dxrDevice.Get(), stateObjectProperties.Get(),
				pHitShaderTable.data(), shaderRecordSizeInBytes, (UINT)pHitShaderTable.size(), L"RayGen", L"Miss");
		}
	}
}

bool VPLManager::GenerateVPLs(GraphicsContext & context, float VPLEmissionLevel, const Vector3& lightDirection, float lightIntensity, int maxDepth /*= 3*/, bool hasRegenRequest/*=false*/)
{
	ScopedTimer _p0(L"Generate VPLs", context);

	int sqrtDispatchDim = VPLEmissionLevel * 100;
	if (lightIntensity != lastLightIntensity || lightDirection != lastLightDirection || hasRegenRequest)
	{
		if (numFramesUpdated != -1) numFramesUpdated = 0;
		lastLightIntensity = lightIntensity;
		lastLightDirection = lightDirection;
	}

	if (numFramesUpdated != maxUpdateFrames)
	{
		//ScopedTimer _p0(L"LightTracingShader", context);
		// Prepare constants
		LightTracingConstants hitShaderConstants = {};
		hitShaderConstants.sunDirection = -lightDirection;
		hitShaderConstants.sunLight = Vector3(1.0, 1.0, 1.0)*lightIntensity;
		hitShaderConstants.sceneSphere = sceneBoundingSphere;
		hitShaderConstants.DispatchOffset = numFramesUpdated == -1 ? 0 : numFramesUpdated * sqrtDispatchDim / maxUpdateFrames;
		hitShaderConstants.maxDepth = maxDepth;
		context.WriteBuffer(m_lightConstantBuffer, 0, &hitShaderConstants, sizeof(hitShaderConstants));
		context.TransitionResource(m_lightConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		context.TransitionResource(VPLBuffers[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		context.TransitionResource(VPLBuffers[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		context.TransitionResource(VPLBuffers[2], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		context.TransitionResource(m_hitShaderMeshInfoBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		context.FlushResourceBarriers();

		ID3D12GraphicsCommandList * pCommandList = context.GetCommandList();

		ID3D12DescriptorHeap *pDescriptorHeaps[] = { &m_pRaytracingDescriptorHeap->GetDescriptorHeap() };

		pCommandList->SetComputeRootSignature(m_raytracingGlobalRootSignature.Get());
		if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
		{
			m_fallbackDevice->QueryRaytracingCommandList(pCommandList, IID_PPV_ARGS(&m_fallbackCommandList));
			m_fallbackCommandList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);
		}
		else
		{
			ThrowIfFailed(pCommandList->QueryInterface(IID_PPV_ARGS(&m_dxrCommandList)), L"Couldn't get DirectX Raytracing interface for the command list.\n");
			m_dxrCommandList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);
		}

		pCommandList->SetComputeRootDescriptorTable(0, m_SceneSrvs);
		pCommandList->SetComputeRootConstantBufferView(1, m_lightConstantBuffer.GetGpuVirtualAddress());
		pCommandList->SetComputeRootDescriptorTable(3, m_VPLUavs);

		if (numFramesUpdated == 0)
		{
			context.ResetCounter(VPLBuffers[POSITION], 0);
			context.ResetCounter(VPLBuffers[NORMAL], 0);
		}

		D3D12_DISPATCH_RAYS_DESC dispatchRaysDesc = m_RaytracingInputs[LightTracing].GetDispatchRayDesc(sqrtDispatchDim, sqrtDispatchDim);

		if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
		{
			m_fallbackCommandList->SetTopLevelAccelerationStructure(7, m_bvh_topLevelAccelerationStructurePointer);
			m_fallbackCommandList->SetPipelineState1(m_fallbackStateObjects[LightTracing].Get());
			m_fallbackCommandList->DispatchRays(&dispatchRaysDesc);
		}
		else
		{
			m_dxrCommandList->SetComputeRootShaderResourceView(7, m_bvh_topLevelAccelerationStructurePointer.GpuVA);
			m_dxrCommandList->SetPipelineState1(m_dxrStateObjects[LightTracing].Get());
			m_dxrCommandList->DispatchRays(&dispatchRaysDesc);
		}

		if (numFramesUpdated == -1) numFramesUpdated = maxUpdateFrames;
		else if (numFramesUpdated >= 0) numFramesUpdated++;

		ReadbackBuffer readbackNumVplsBuffer, readbackNumPathsBuffer;
		readbackNumVplsBuffer.Create(L"ReadBackNumVplsBuffer", 1, sizeof(uint32_t));
		readbackNumPathsBuffer.Create(L"ReadBackNumPathsBuffer", 1, sizeof(uint32_t));
		
		context.CopyBuffer(readbackNumVplsBuffer, VPLBuffers[POSITION].GetCounterBuffer());
		context.CopyBuffer(readbackNumPathsBuffer, VPLBuffers[NORMAL].GetCounterBuffer());
		context.Flush(true);
		uint32_t* tempNumVpls = (uint32_t*)readbackNumVplsBuffer.Map();
		uint32_t* tempNumPaths = (uint32_t*)readbackNumPathsBuffer.Map();
		numVPLs = numFramesUpdated == maxUpdateFrames ? *tempNumVpls : std::max(numVPLs, *tempNumVpls);
		numPaths = numFramesUpdated == maxUpdateFrames ? *tempNumPaths : std::max(numPaths, *tempNumPaths);
		readbackNumVplsBuffer.Unmap();
		readbackNumPathsBuffer.Unmap();

		return true;
	}
	return false;
}

// brute force: go through each VPL for all screen pixels
void VPLManager::ComputeInstantRadiosity(GraphicsContext& context, int scrWidth, int scrHeight, int currentVPL)
{
	ScopedTimer _p0(L"BruteShadowTracingShader", context);
	if (currentVPL > numVPLs) return;

	// Prepare constants
	IRTracingConstants constants = {};
	constants.invNumPaths = 1.f/numPaths;
	constants.currentVPL = currentVPL;
	constants.sceneRadius = sceneBoundingSphere.GetW();
	context.WriteBuffer(m_IRConstantBuffer, 0, &constants, sizeof(constants));
	context.TransitionResource(m_IRConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, true);

	ID3D12GraphicsCommandList * pCommandList = context.GetCommandList();

	ID3D12DescriptorHeap *pDescriptorHeaps[] = { &m_pRaytracingDescriptorHeap->GetDescriptorHeap() };

	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		m_fallbackDevice->QueryRaytracingCommandList(pCommandList, IID_PPV_ARGS(&m_fallbackCommandList));
		m_fallbackCommandList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);
	}
	else
	{
		ThrowIfFailed(pCommandList->QueryInterface(IID_PPV_ARGS(&m_dxrCommandList)), L"Couldn't get DirectX Raytracing interface for the command list.\n");
		m_dxrCommandList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);
	}

	context.TransitionResource(*IRResultBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	if (currentVPL == 0)
	{
		context.TransitionResource(Graphics::g_ScenePositionBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		context.TransitionResource(Graphics::g_SceneNormalBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		context.TransitionResource(Graphics::g_SceneAlbedoBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);
		context.ClearUAV(*IRResultBuffer);
	}

	//transition result to UAV
	pCommandList->SetComputeRootSignature(m_raytracingGlobalRootSignature.Get());
	pCommandList->SetComputeRootDescriptorTable(0, m_GBufferSrvs);
	pCommandList->SetComputeRootConstantBufferView(1, m_IRConstantBuffer.GetGpuVirtualAddress());
	pCommandList->SetComputeRootDescriptorTable(2, m_ResultUav);
	pCommandList->SetComputeRootDescriptorTable(3, m_VPLUavs);

	D3D12_DISPATCH_RAYS_DESC dispatchRaysDesc = m_RaytracingInputs[IRTracing].GetDispatchRayDesc(scrWidth, scrHeight);

	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		m_fallbackCommandList->SetTopLevelAccelerationStructure(7, m_bvh_topLevelAccelerationStructurePointer);
		m_fallbackCommandList->SetPipelineState1(m_fallbackStateObjects[IRTracing].Get());
		m_fallbackCommandList->DispatchRays(&dispatchRaysDesc);

	}
	else // DirectX Raytracing
	{
		m_dxrCommandList->SetComputeRootShaderResourceView(7, m_bvh_topLevelAccelerationStructurePointer.GpuVA);
		m_dxrCommandList->SetPipelineState1(m_dxrStateObjects[IRTracing].Get());
		m_dxrCommandList->DispatchRays(&dispatchRaysDesc);
	}

	printf("Finish VPL (%d/%d)\n", currentVPL, numVPLs);
	context.Flush(true);
}

void VPLManager::CastLGHShadowRays(GraphicsContext& context, int scrWidth, int scrHeight, int shadowRate,
	int numLevels, int minLevel, float baseRadius, float devScale, bool temporalRandom,
	float alpha, int frameId, int interleaveRate)
{
	ScopedTimer _p0(L"Trace shadow rays", context);

	// Prepare constants
	LGHShadowTracingConstants shadowTracingConstants = {};
	shadowTracingConstants.invNumPaths = (interleaveRate * interleaveRate) / (float)numPaths;
	shadowTracingConstants.shadowRate = shadowRate;
	for (int i = 1; i <= 4; i++)
	{
		shadowTracingConstants.halton[i - 1] = Vector4(Halton(i, 2), Halton(i, 3), Halton(i, 5), Halton(i, 7));
	}
	shadowTracingConstants.minLevel = minLevel;
	shadowTracingConstants.numLevels = numLevels;
	shadowTracingConstants.baseRadius = baseRadius;
	shadowTracingConstants.devScale = devScale;
	shadowTracingConstants.alpha = alpha;
	shadowTracingConstants.temporalRandom = temporalRandom;
	shadowTracingConstants.frameId = frameId;
	shadowTracingConstants.sceneRadius = sceneBoundingSphere.GetW();

	context.TransitionResource(m_lghShadowConstantBuffer, D3D12_RESOURCE_STATE_COPY_DEST);
	context.WriteBuffer(m_lghShadowConstantBuffer, 0, &shadowTracingConstants, sizeof(shadowTracingConstants));
	context.TransitionResource(m_lghShadowConstantBuffer, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

	ID3D12GraphicsCommandList * pCommandList = context.GetCommandList();

	ID3D12DescriptorHeap *pDescriptorHeaps[] = { &m_pRaytracingDescriptorHeap->GetDescriptorHeap() };

	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		m_fallbackDevice->QueryRaytracingCommandList(pCommandList, IID_PPV_ARGS(&m_fallbackCommandList));
		m_fallbackCommandList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);
	}
	else
	{
		ThrowIfFailed(pCommandList->QueryInterface(IID_PPV_ARGS(&m_dxrCommandList)), L"Couldn't get DirectX Raytracing interface for the command list.\n");
		m_dxrCommandList->SetDescriptorHeaps(ARRAYSIZE(pDescriptorHeaps), pDescriptorHeaps);
	}

	context.TransitionResource(Graphics::g_ScenePositionBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	context.TransitionResource(Graphics::g_SceneNormalBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, true);

	pCommandList->SetComputeRootSignature(m_raytracingGlobalRootSignature.Get());
	pCommandList->SetComputeRootDescriptorTable(0, m_SceneSrvs);
	pCommandList->SetComputeRootDescriptorTable(9, m_BlueNoiseSrvs);
	pCommandList->SetComputeRootConstantBufferView(1, m_lghShadowConstantBuffer.GetGpuVirtualAddress());
	pCommandList->SetComputeRootDescriptorTable(8, m_LGHSrvs);
	pCommandList->SetComputeRootDescriptorTable(4, m_LGHSamplingSrvs);
	pCommandList->SetComputeRootDescriptorTable(5, m_SUUavs);
	pCommandList->SetComputeRootDescriptorTable(6, m_PositionNormalSrvs);

	D3D12_DISPATCH_RAYS_DESC dispatchRaysDesc = m_RaytracingInputs[LGHShadowTracing].GetDispatchRayDesc(
		scrWidth, scrHeight);
	if (m_raytracingAPI == RaytracingAPI::FallbackLayer)
	{
		m_fallbackCommandList->SetTopLevelAccelerationStructure(7, m_bvh_topLevelAccelerationStructurePointer);
		m_fallbackCommandList->SetPipelineState1(m_fallbackStateObjects[LGHShadowTracing].Get());
		m_fallbackCommandList->DispatchRays(&dispatchRaysDesc);

	}
	else // DirectX Raytracing
	{
		m_dxrCommandList->SetComputeRootShaderResourceView(7, m_bvh_topLevelAccelerationStructurePointer.GpuVA);
		m_dxrCommandList->SetPipelineState1(m_dxrStateObjects[LGHShadowTracing].Get());
		m_dxrCommandList->DispatchRays(&dispatchRaysDesc);
	}
}
