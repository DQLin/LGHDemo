#pragma once
#include "CommandContext.h"
#include "TemporalEffects.h"
#include "SVGFReprojectionCS.h"
#include "SVGFAtrousCS.h"
#include "SVGFMomentsFilterCS.h"

class SVGFDenoiser
{
public:
	ColorBuffer m_IntegratedS[3];
	ColorBuffer m_IntegratedU[3];
	ColorBuffer m_IntegratedM[2];
	ColorBuffer m_HistoryLength;
	ComputePSO m_SVGFReprojectionCS;
	ComputePSO m_SVGFAtrousCS;
	ComputePSO m_SVGFMomentsFilterCS;

	static NumVar m_CPhi;
	static ExpVar m_NPhi;
	static NumVar m_PPhi;

	bool IsInitialized;

	SVGFDenoiser() { IsInitialized = false;  };

	~SVGFDenoiser()
	{
		RecycleResources();
	}
	
	void Initialize(ComputeContext& Context, RootSignature& m_ComputeRootSig)
	{
		if (!IsInitialized)
		{
#define CreatePSO( ObjName, ShaderByteCode ) \
    ObjName.SetRootSignature(m_ComputeRootSig); \
    ObjName.SetComputeShader(ShaderByteCode, sizeof(ShaderByteCode) ); \
    ObjName.Finalize();

			CreatePSO(m_SVGFReprojectionCS, g_pSVGFReprojectionCS);
			CreatePSO(m_SVGFAtrousCS, g_pSVGFAtrousCS);
			CreatePSO(m_SVGFMomentsFilterCS, g_pSVGFMomentsFilterCS);
#undef CreatePSO

#define CreateBuffer( BufferName, BufferDebugName, Count ) \
	for (int i = 0; i < Count; i++) { \
		BufferName[i].Create(BufferDebugName + std::to_wstring(i), Graphics::g_SceneColorBuffer.GetWidth(), \
			Graphics::g_SceneColorBuffer.GetHeight(), i, DXGI_FORMAT_R16G16B16A16_FLOAT); \
	}
			CreateBuffer(m_IntegratedS, L"IntergratedS", 3);
			CreateBuffer(m_IntegratedU, L"IntergratedU", 3);
			CreateBuffer(m_IntegratedM, L"IntergratedM", 2);
#undef CreateBuffer

			m_HistoryLength.Create(L"HistoryLength", Graphics::g_SceneColorBuffer.GetWidth(),
				Graphics::g_SceneColorBuffer.GetHeight(), 1, DXGI_FORMAT_R8_UINT);
			Context.TransitionResource(m_HistoryLength, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, true);
			Context.ClearUAV(m_HistoryLength);
			Context.Flush(true);
			IsInitialized = true;
		}
	}

	void RecycleResources()
	{
		if (IsInitialized)
		{
			m_IntegratedS[0].Destroy();
			m_IntegratedS[1].Destroy();
			m_IntegratedS[2].Destroy();
			m_IntegratedU[0].Destroy();
			m_IntegratedU[1].Destroy();
			m_IntegratedU[2].Destroy();
			m_IntegratedM[0].Destroy();
			m_IntegratedM[1].Destroy();
			m_HistoryLength.Destroy();
			IsInitialized = false;
		}
	}

	void Reproject(ComputeContext& Context, ColorBuffer& TexCurS, ColorBuffer& TexCurU, bool disable);

	void Filter(ComputeContext& Context, ColorBuffer& ResultBuffer);

	void FilterMoments(ComputeContext& Context);

};
