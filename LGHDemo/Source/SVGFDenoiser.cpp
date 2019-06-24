#include "SVGFDenoiser.h"

NumVar SVGFDenoiser::m_CPhi("Application/Filtering/SVGF/CPhi", 4.0f, 0.0f, 16.0f, 0.5f);
ExpVar SVGFDenoiser::m_NPhi("Application/Filtering/SVGF/NPhi", 128.f, 1.f, 10.f, 1.f);
NumVar SVGFDenoiser::m_PPhi("Application/Filtering/SVGF/PPhi", 100.f, 0.0f, 1000.f, 10.f);

void SVGFDenoiser::Reproject(ComputeContext & Context, ColorBuffer & TexCurS, ColorBuffer & TexCurU, bool disable)
{
	ScopedTimer _prof(L"SVGF Reproject", Context);

	uint32_t Src = TemporalEffects::GetFrameIndexMod2();
	uint32_t Dst = Src ^ 1;

	Context.SetPipelineState(m_SVGFReprojectionCS);

	__declspec(align(16)) struct ConstantBuffer
	{
		float RcpBufferDim[2];
		float RcpSeedLimiter;
		float gAlpha;
		float gMomentsAlpha;
		int disable;
	};

	ConstantBuffer cbv = {
		1.0f / Graphics::g_SceneColorBuffer.GetWidth(),
		1.0f / Graphics::g_SceneColorBuffer.GetHeight(),
		1.0f / TemporalEffects::TemporalSpeedLimit,
		0.2, 0.2, disable };

	Context.SetDynamicConstantBufferView(0, sizeof(cbv), &cbv);

	Context.TransitionResource(Graphics::g_VelocityBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	Context.TransitionResource(Graphics::g_LinearDepth[Src], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); //current
	Context.TransitionResource(Graphics::g_LinearDepth[Dst], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); //previous
	Context.TransitionResource(m_IntegratedS[2], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	Context.TransitionResource(m_IntegratedU[2], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	Context.TransitionResource(m_IntegratedM[Src], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	Context.TransitionResource(TexCurS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	Context.TransitionResource(TexCurU, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	Context.TransitionResource(Graphics::g_GradLinearDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	Context.TransitionResource(m_IntegratedS[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	Context.TransitionResource(m_IntegratedU[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	Context.TransitionResource(m_IntegratedM[Dst], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	Context.TransitionResource(m_HistoryLength, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	Context.SetDynamicDescriptor(1, 0, m_IntegratedS[1].GetUAV());
	Context.SetDynamicDescriptor(1, 1, m_IntegratedU[1].GetUAV());
	Context.SetDynamicDescriptor(1, 2, m_IntegratedM[Dst].GetUAV());
	Context.SetDynamicDescriptor(1, 3, m_HistoryLength.GetUAV());

	Context.SetDynamicDescriptor(2, 0, Graphics::g_VelocityBuffer.GetSRV());
	Context.SetDynamicDescriptor(2, 1, Graphics::g_LinearDepth[Src].GetSRV());
	Context.SetDynamicDescriptor(2, 2, Graphics::g_LinearDepth[Dst].GetSRV());
	Context.SetDynamicDescriptor(2, 3, m_IntegratedS[2].GetSRV());
	Context.SetDynamicDescriptor(2, 4, m_IntegratedU[2].GetSRV());
	Context.SetDynamicDescriptor(2, 5, m_IntegratedM[Src].GetSRV());
	Context.SetDynamicDescriptor(2, 6, TexCurS.GetSRV());
	Context.SetDynamicDescriptor(2, 7, TexCurU.GetSRV());
	Context.SetDynamicDescriptor(2, 8, Graphics::g_GradLinearDepth.GetSRV());

	Context.Dispatch2D(Graphics::g_SceneColorBuffer.GetWidth(),
		Graphics::g_SceneColorBuffer.GetHeight(), 16, 16);
}

void SVGFDenoiser::Filter(ComputeContext & Context, ColorBuffer & ResultBuffer)
{
	ScopedTimer _prof(L"SVGF Filter", Context);

	uint32_t Src = TemporalEffects::GetFrameIndexMod2();
	uint32_t Dst = Src ^ 1;

	Context.SetPipelineState(m_SVGFAtrousCS);

	__declspec(align(16)) struct ConstantBuffer
	{
		float c_phi;
		float n_phi;
		float z_phi;
		float stepWidth;
		int iter;
		int maxIter;
	};

	const int maxIter = 5;
	ConstantBuffer cbv = {
		m_CPhi, m_NPhi, m_PPhi, 1, 0, maxIter };

	Context.TransitionResource(Graphics::g_LinearDepth[Src], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	Context.TransitionResource(Graphics::g_GradLinearDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	Context.TransitionResource(Graphics::g_SceneNormalBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	Context.TransitionResource(m_HistoryLength, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	Context.TransitionResource(ResultBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	for (int iter = 0; iter < maxIter; iter++)
	{
		// route iter 1 result back to reprojection input
		int SUSrc = iter == 1 ? 2 : iter % 2;
		int SUDst = iter == 0 ? 2 : (iter + 1) % 2;
		cbv.iter = iter;
		Context.SetDynamicConstantBufferView(0, sizeof(cbv), &cbv);
		Context.TransitionResource(m_IntegratedS[SUSrc], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		Context.TransitionResource(m_IntegratedU[SUSrc], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		Context.TransitionResource(m_IntegratedS[SUDst], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		Context.TransitionResource(m_IntegratedU[SUDst], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		Context.SetDynamicDescriptor(1, 0, m_IntegratedS[SUDst].GetUAV());
		Context.SetDynamicDescriptor(1, 1, m_IntegratedU[SUDst].GetUAV());
		Context.SetDynamicDescriptor(1, 2, ResultBuffer.GetUAV());

		Context.SetDynamicDescriptor(2, 0, m_IntegratedS[SUSrc].GetSRV());
		Context.SetDynamicDescriptor(2, 1, m_IntegratedU[SUSrc].GetSRV());
		Context.SetDynamicDescriptor(2, 2, Graphics::g_ScenePositionBuffer.GetSRV());
		Context.SetDynamicDescriptor(2, 3, Graphics::g_SceneNormalBuffer.GetSRV());
		Context.SetDynamicDescriptor(2, 4, m_HistoryLength.GetSRV());

		Context.Dispatch2D(Graphics::g_SceneColorBuffer.GetWidth(),
			Graphics::g_SceneColorBuffer.GetHeight(), 16, 16);
		cbv.stepWidth *= 2;
	}
	Context.Flush(true);
}

void SVGFDenoiser::FilterMoments(ComputeContext & Context)
{
	ScopedTimer _prof(L"SVGF Filter Moments", Context);

	uint32_t Src = TemporalEffects::GetFrameIndexMod2();
	uint32_t Dst = Src ^ 1;

	Context.SetPipelineState(m_SVGFMomentsFilterCS);

	__declspec(align(16)) struct ConstantBuffer
	{
		float c_phi;
		float n_phi;
		float z_phi;
	};

	ConstantBuffer cbv = { m_CPhi, m_NPhi, m_PPhi };

	Context.SetDynamicConstantBufferView(0, sizeof(cbv), &cbv);
	Context.TransitionResource(m_IntegratedS[1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	Context.TransitionResource(m_IntegratedU[1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	Context.TransitionResource(m_IntegratedM[Dst], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	Context.TransitionResource(Graphics::g_LinearDepth[Src], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	Context.TransitionResource(Graphics::g_GradLinearDepth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	Context.TransitionResource(Graphics::g_SceneNormalBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	Context.TransitionResource(m_HistoryLength, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	Context.TransitionResource(m_IntegratedS[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	Context.TransitionResource(m_IntegratedU[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	Context.SetDynamicDescriptor(1, 0, m_IntegratedS[0].GetUAV());
	Context.SetDynamicDescriptor(1, 1, m_IntegratedU[0].GetUAV());

	Context.SetDynamicDescriptor(2, 0, m_IntegratedS[1].GetSRV());
	Context.SetDynamicDescriptor(2, 1, m_IntegratedU[1].GetSRV());
	Context.SetDynamicDescriptor(2, 2, m_IntegratedM[Dst].GetSRV());
	Context.SetDynamicDescriptor(2, 3, Graphics::g_ScenePositionBuffer.GetSRV());
	Context.SetDynamicDescriptor(2, 4, Graphics::g_SceneNormalBuffer.GetSRV());
	Context.SetDynamicDescriptor(2, 5, m_HistoryLength.GetSRV());

	Context.Dispatch2D(Graphics::g_SceneColorBuffer.GetWidth(),
		Graphics::g_SceneColorBuffer.GetHeight(), 16, 16);
}
