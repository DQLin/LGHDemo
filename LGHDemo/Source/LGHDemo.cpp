#include "LGHDemo.h"
ExpVar m_SunLightIntensity("Application/Lighting/Sun Light Intensity", 3.0f, 0.0f, 16.0f, 0.1f);
NumVar m_SunOrientation("Application/Lighting/Sun Orientation", 1.16, 0.0f, 10.0f, 0.01f);
NumVar m_SunInclination("Application/Lighting/Sun Inclination", 0.86, 0.0f, 1.0f, 0.01f);

NumVar ShadowDimX("Application/Lighting/Sun Shadow Dim X", 5000, 1, 10000, 1);
NumVar ShadowDimY("Application/Lighting/Sun Shadow Dim Y", 3000, 1, 10000, 1);
NumVar ShadowDimZ("Application/Lighting/Sun Shadow Dim Z", 3000, 1, 10000, 1);

NumVar ShadowCenterX("Application/Lighting/Sun Shadow Center X", 0, -1000, 1000, 1);
NumVar ShadowCenterY("Application/Lighting/Sun Shadow Center Y", -500, -1000, 1000, 1);
NumVar ShadowCenterZ("Application/Lighting/Sun Shadow Center Z", 0, -1000, 1000, 1);

BoolVar m_EnableShadowTAA("Application/Enable Shadow TAA", true);

BoolVar m_DirectLightingOnly("Application/Direct Lighting Only", false);

const char* debugViewNames[5] = { "N/A", "Unshadowed Stochastic", "Unshadowed Filtered",
"Shadowed Stochastic", "Shadowed Filtered" };
EnumVar DebugView("Application/Debug View", 0, 5, debugViewNames);

LGHDemo::LGHDemo(const std::string& modelFile, bool isCameraInitialized, const Camera& camera, 
	float sunIntensity, float sunOrientation, float sunInclination)
{
	m_IsCameraInitialized = isCameraInitialized;
	if (isCameraInitialized) m_Camera = camera;
	m_SunLightIntensity = sunIntensity;
	m_SunOrientation = sunOrientation;
	m_SunInclination = sunInclination;
	m_ModelFile = modelFile;
}

void LGHDemo::Startup(void)
{
	float costheta = cosf(m_SunOrientation);
	float sintheta = sinf(m_SunOrientation);
	float cosphi = cosf(m_SunInclination * 3.141592654f * 0.5f);
	float sinphi = sinf(m_SunInclination * 3.141592654f * 0.5f);
	m_SunDirection = Normalize(Vector3(costheta * cosphi, sinphi, sintheta * cosphi));

	m_MainViewport.TopLeftX = 0;
	m_MainViewport.TopLeftY = 0;
	m_MainViewport.Width = (float)g_SceneColorBuffer.GetWidth();
	m_MainViewport.Height = (float)g_SceneColorBuffer.GetHeight();
	m_MainViewport.MinDepth = 0.0f;
	m_MainViewport.MaxDepth = 1.0f;

	m_MainScissor.left = 0;
	m_MainScissor.top = 0;
	m_MainScissor.right = (LONG)g_SceneColorBuffer.GetWidth();
	m_MainScissor.bottom = (LONG)g_SceneColorBuffer.GetHeight();

	//create ray tracing descriptor heap
	D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1;
	HRESULT hr = g_Device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1));

	SamplerDesc DefaultSamplerDesc;
	DefaultSamplerDesc.MaxAnisotropy = 8;

	m_RootSig.Reset(8, 2);
	m_RootSig.InitStaticSampler(0, DefaultSamplerDesc, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig.InitStaticSampler(1, SamplerShadowDesc, D3D12_SHADER_VISIBILITY_PIXEL);

	m_RootSig[0].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_VERTEX);
	m_RootSig[1].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 3, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 32, 6, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[4].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 64, 3, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[5].InitAsConstants(1, 2, D3D12_SHADER_VISIBILITY_VERTEX);
	m_RootSig[6].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 4, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig[7].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1, D3D12_SHADER_VISIBILITY_PIXEL);
	m_RootSig.Finalize(L"ModelViewer", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	m_ComputeRootSig.Reset(3, 1);
	m_ComputeRootSig.InitStaticSampler(0, SamplerPointClampDesc);
	m_ComputeRootSig[0].InitAsConstantBuffer(0);
	m_ComputeRootSig[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 12);
	m_ComputeRootSig[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 12);

	m_ComputeRootSig.Finalize(L"Image Processing");

	DXGI_FORMAT ColorFormat = g_SceneColorBuffer.GetFormat();
	DXGI_FORMAT NormalFormat = g_SceneNormalBuffer.GetFormat();
	DXGI_FORMAT DepthFormat = g_SceneDepthBuffer.GetFormat();
	DXGI_FORMAT ShadowFormat = g_ShadowBuffer.GetFormat();

	DXGI_FORMAT GBufferColorFormats[] = { g_ScenePositionBuffer.GetFormat(), g_SceneNormalBuffer.GetFormat(), g_SceneAlbedoBuffer.GetFormat(),
										g_SceneSpecularBuffer.GetFormat() };

	D3D12_INPUT_ELEMENT_DESC vertElem[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

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

	// Depth-only (2x rate)
	m_DepthPSO.SetRootSignature(m_RootSig);
	m_DepthPSO.SetRasterizerState(RasterizerDefault);
	m_DepthPSO.SetBlendState(BlendNoColorWrite);
	m_DepthPSO.SetDepthStencilState(DepthStateReadWrite);
	m_DepthPSO.SetInputLayout(_countof(vertElem), vertElem);
	m_DepthPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	m_DepthPSO.SetRenderTargetFormats(0, nullptr, DepthFormat);
	m_DepthPSO.SetVertexShader(g_pDepthViewerVS, sizeof(g_pDepthViewerVS));
	m_DepthPSO.Finalize();

	// Depth-only shading but with alpha testing
	m_CutoutDepthPSO = m_DepthPSO;
	m_CutoutDepthPSO.SetPixelShader(g_pDepthViewerPS, sizeof(g_pDepthViewerPS));
	m_CutoutDepthPSO.SetRasterizerState(RasterizerTwoSided);
	m_CutoutDepthPSO.Finalize();

	// Depth-only but with a depth bias and/or render only backfaces
	m_ShadowPSO = m_DepthPSO;
	m_ShadowPSO.SetRasterizerState(RasterizerShadow);
	m_ShadowPSO.SetRenderTargetFormats(0, nullptr, g_ShadowBuffer.GetFormat());
	m_ShadowPSO.Finalize();

	// Shadows with alpha testing
	m_CutoutShadowPSO = m_ShadowPSO;
	m_CutoutShadowPSO.SetPixelShader(g_pDepthViewerPS, sizeof(g_pDepthViewerPS));
	m_CutoutShadowPSO.SetRasterizerState(RasterizerShadowTwoSided);
	m_CutoutShadowPSO.Finalize();

	// Full color pass
	m_ModelPSO = m_DepthPSO;
	m_ModelPSO.SetBlendState(BlendDisable);
	m_ModelPSO.SetDepthStencilState(DepthStateTestEqual);
	m_ModelPSO.SetRenderTargetFormats(4, GBufferColorFormats, DepthFormat);
	m_ModelPSO.SetVertexShader(g_pModelViewerVS, sizeof(g_pModelViewerVS));
	m_ModelPSO.SetPixelShader(g_pModelViewerPS, sizeof(g_pModelViewerPS));
	m_ModelPSO.Finalize();

	m_CutoutModelPSO = m_ModelPSO;
	m_CutoutModelPSO.SetRasterizerState(RasterizerTwoSided);
	m_CutoutModelPSO.Finalize();

	m_ScreenPSO.SetRootSignature(m_RootSig);
	m_ScreenPSO.SetRasterizerState(RasterizerDefault);
	m_ScreenPSO.SetBlendState(BlendDisable);
	m_ScreenPSO.SetDepthStencilState(DepthStateDisabled);
	m_ScreenPSO.SetInputLayout(_countof(screenVertElem), screenVertElem);
	m_ScreenPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
	m_ScreenPSO.SetRenderTargetFormats(1, &ColorFormat, DepthFormat);
	m_ScreenPSO.SetVertexShader(g_pScreenShaderVS, sizeof(g_pScreenShaderVS));
	m_ScreenPSO.SetPixelShader(g_pScreenShaderPS, sizeof(g_pScreenShaderPS));
	m_ScreenPSO.Finalize();

	m_ExtraTextures[0] = g_SSAOFullScreen.GetSRV(); //not used
	m_ExtraTextures[1] = g_ShadowBuffer.GetSRV();
	m_GBuffer[0] = g_ScenePositionBuffer.GetSRV();
	m_GBuffer[1] = g_SceneNormalBuffer.GetSRV();
	m_GBuffer[2] = g_SceneAlbedoBuffer.GetSRV();
	m_GBuffer[3] = g_SceneSpecularBuffer.GetSRV();

	TextureManager::Initialize(L"Textures/");

	// use demo scene
	if (m_ModelFile == "")
	{
#ifndef ENABLE_TEAPOT
		m_Models.resize(1);
		ASSERT(m_Models[0].Load("Models/sponza.h3d"), "Failed to load model");
		ASSERT(m_Models[0].m_Header.meshCount > 0, "Model contains no meshes");
#else
		m_Models.resize(2);
		ASSERT(m_Models[0].Load("Models/sponza.h3d"), "Failed to load model");
		ASSERT(m_Models[0].m_Header.meshCount > 0, "Model contains no meshes");

		ASSERT(m_Models[1].Load("Models/teapot.obj"), "Failed to load model");
		ASSERT(m_Models[1].m_Header.meshCount > 0, "Model contains no meshes");
		m_Models[1].m_modelMatrix = m_Models[1].m_modelMatrix.MakeScale(Vector3(3.5, 3.5, 3.5));

		Matrix4 RotMatrix;
		RotMatrix.SetX(Vector4(cos(3.1416), 0, -sin(3.1416), 0));
		RotMatrix.SetY(Vector4(0, 1, 0, 0));
		RotMatrix.SetZ(Vector4(sin(3.1416), 0, cos(3.1416), 0));
		RotMatrix.SetW(Vector4(0, 0, 0, 1));
		m_Models[1].m_modelMatrix = m_Models[1].m_modelMatrix * RotMatrix;
#endif
	}
	else
	{
		m_Models.resize(1);
		ASSERT(m_Models[0].Load(m_ModelFile.c_str()), "Failed to load model");
		ASSERT(m_Models[0].m_Header.meshCount > 0, "Model contains no meshes");
	}


#ifdef GENERATE_IR_GROUND_TRUTH
	irRenderer.Initialize(m_Models.data(), m_Models.size(), m_MainViewport.Width, m_MainViewport.Height);
#else
	lgiRenderer.Initialize(m_Models.data(), m_Models.size(), m_MainViewport.Width, m_MainViewport.Height);
#endif

	m_SceneSphere = m_Models[0].m_SceneBoundingSphere;
	Vector3 modelBoundingBoxExtent = m_Models[0].m_Header.boundingBox.max - m_Models[0].m_Header.boundingBox.min;
	float modelRadius = Length(modelBoundingBoxExtent) * .5f;

	if (m_ModelFile == "")
	{
		// lobby
		Vector3 eye(-899.81, 607.74, -36.63);
		m_Camera.SetEyeAtUp(eye, eye + Vector3(0.966, -0.259, 0.02), Vector3(kYUnitVector));

		m_Camera.SetZRange(1.0f, 10000.0f);
	}
	else if (!m_IsCameraInitialized)
	{
		Model1::BoundingBox bb = m_Models[0].GetBoundingBox();
		Vector3 eye(0.5*(bb.max + bb.min));
		m_Camera.SetEyeAtUp(eye, eye + Vector3(0, 0, -1), Vector3(kYUnitVector));
		m_Camera.SetZRange(0.0005 * modelRadius, 5 * modelRadius);
	}

	m_CameraController.reset(new CameraController(m_Camera, Vector3(kYUnitVector)));
	
	if (m_ModelFile != "")
	{
		m_CameraController->SetMoveSpeed(modelRadius);
		m_CameraController->SetStrafeSpeed(modelRadius);
		ShadowDimX = modelBoundingBoxExtent.GetX() * 2.f;
		ShadowDimY = modelBoundingBoxExtent.GetY() * 2.f;
		ShadowDimZ = modelBoundingBoxExtent.GetZ() * 2.f;
		ShadowCenterY = -0.5 * modelRadius;
		printf("model radius: %f\n", modelRadius);
	}

	m_ViewProjMatrix = m_Camera.GetViewProjMatrix();
	m_quad.Init();

	MotionBlur::Enable = false;
	TemporalEffects::EnableTAA = false;
	FXAA::Enable = true;
	PostEffects::EnableHDR = true;
	PostEffects::EnableAdaptation = false;
	SSAO::Enable = false;
	DepthOfField::Enable = false;

	frameId = -1;
}

void LGHDemo::Cleanup(void)
{
	for (int i = 0; i < m_Models.size(); i++) m_Models[i].Clear();
}

namespace Graphics
{
	extern EnumVar DebugZoom;
}

void LGHDemo::Update(float deltaT)
{
	//ScopedTimer _prof(L"Update State");

	if (GameInput::IsFirstPressed(GameInput::kLShoulder))
		DebugZoom.Decrement();
	else if (GameInput::IsFirstPressed(GameInput::kRShoulder))
		DebugZoom.Increment();

	hasGeometryChange = false;
	bool hasChange = false;
	Vector3 pos = m_Camera.GetPosition();
	Vector3 fwd = m_Camera.GetForwardVec();

	if (GameInput::IsFirstPressed(GameInput::kKey_0))
	{
		LGHRenderer::m_IndirectShadow = !LGHRenderer::m_IndirectShadow;
	}

	m_CameraController->Update(0.2*deltaT);

	// teapot moving controls

	//backward
	if (GameInput::IsPressed(GameInput::kKey_apostrophe))
	{
		m_Models[1].m_modelMatrix.SetW(Vector4(m_Models[1].m_modelMatrix.GetW().GetX() - 5.f,
			m_Models[1].m_modelMatrix.GetW().GetY(),
			m_Models[1].m_modelMatrix.GetW().GetZ(),
			m_Models[1].m_modelMatrix.GetW().GetW()));
		hasGeometryChange = true;
	}

	//forward
	if (GameInput::IsPressed(GameInput::kKey_lbracket))
	{
		m_Models[1].m_modelMatrix.SetW(Vector4(m_Models[1].m_modelMatrix.GetW().GetX() + 5.f,
			m_Models[1].m_modelMatrix.GetW().GetY(),
			m_Models[1].m_modelMatrix.GetW().GetZ(),
			m_Models[1].m_modelMatrix.GetW().GetW()));
		hasGeometryChange = true;
	}

	//left
	if (GameInput::IsPressed(GameInput::kKey_semicolon))
	{
		m_Models[1].m_modelMatrix.SetW(Vector4(m_Models[1].m_modelMatrix.GetW().GetX(),
			m_Models[1].m_modelMatrix.GetW().GetY(),
			m_Models[1].m_modelMatrix.GetW().GetZ() + 5.f,
			m_Models[1].m_modelMatrix.GetW().GetW()));
		hasGeometryChange = true;
	}

	//right
	if (GameInput::IsPressed(GameInput::kKey_backslash))
	{
		m_Models[1].m_modelMatrix.SetW(Vector4(m_Models[1].m_modelMatrix.GetW().GetX(),
			m_Models[1].m_modelMatrix.GetW().GetY(),
			m_Models[1].m_modelMatrix.GetW().GetZ() - 5.f,
			m_Models[1].m_modelMatrix.GetW().GetW()));
		hasGeometryChange = true;
	}

	//up
	if (GameInput::IsPressed(GameInput::kKey_p))
	{
		m_Models[1].m_modelMatrix.SetW(Vector4(m_Models[1].m_modelMatrix.GetW().GetX(),
			m_Models[1].m_modelMatrix.GetW().GetY() + 5.f,
			m_Models[1].m_modelMatrix.GetW().GetZ() ,
			m_Models[1].m_modelMatrix.GetW().GetW()));
		hasGeometryChange = true;
	}

	//down
	if (GameInput::IsPressed(GameInput::kKey_rbracket))
	{
		m_Models[1].m_modelMatrix.SetW(Vector4(m_Models[1].m_modelMatrix.GetW().GetX(),
			m_Models[1].m_modelMatrix.GetW().GetY() - 5.f,
			m_Models[1].m_modelMatrix.GetW().GetZ(),
			m_Models[1].m_modelMatrix.GetW().GetW()));
		hasGeometryChange = true;
	}

	//rotate
	if (GameInput::IsPressed(GameInput::kKey_slash))
	{
		Matrix4 RotMatrix;
		RotMatrix.SetX(Vector4(cos(0.5*deltaT), 0, -sin(0.5*deltaT), 0));
		RotMatrix.SetY(Vector4(0, 1, 0, 0));
		RotMatrix.SetZ(Vector4(sin(0.5*deltaT), 0, cos(0.5*deltaT), 0));
		RotMatrix.SetW(Vector4(0, 0, 0, 1));
		m_Models[1].m_modelMatrix = m_Models[1].m_modelMatrix * RotMatrix;

		hasGeometryChange = true;
	}

	m_ViewProjMatrix = m_Camera.GetViewProjMatrix();

	float costheta = cosf(m_SunOrientation);
	float sintheta = sinf(m_SunOrientation);
	float cosphi = cosf(m_SunInclination * 3.14159f * 0.5f);
	float sinphi = sinf(m_SunInclination * 3.14159f * 0.5f);
	m_SunDirection = Normalize(Vector3(costheta * cosphi, sinphi, sintheta * cosphi));

	// We use viewport offsets to jitter sample positions from frame to frame (for TAA.)
	// D3D has a design quirk with fractional offsets such that the implicit scissor
	// region of a viewport is floor(TopLeftXY) and floor(TopLeftXY + WidthHeight), so
	// having a negative fractional top left, e.g. (-0.25, -0.25) would also shift the
	// BottomRight corner up by a whole integer.  One solution is to pad your viewport
	// dimensions with an extra pixel.  My solution is to only use positive fractional offsets,
	// but that means that the average sample position is +0.5, which I use when I disable
	// temporal AA.

	if (m_EnableShadowTAA) { m_MainViewport.TopLeftX = 0.5;  m_MainViewport.TopLeftY = 0.5; }
	else TemporalEffects::GetJitterOffset(m_MainViewport.TopLeftX, m_MainViewport.TopLeftY);

	float oldWidth = m_MainViewport.Width;
	float oldHeight = m_MainViewport.Height;

	m_MainViewport.Width = (float)g_SceneColorBuffer.GetWidth();
	m_MainViewport.Height = (float)g_SceneColorBuffer.GetHeight();
	m_MainViewport.MinDepth = 0.0f;
	m_MainViewport.MaxDepth = 1.0f;

	m_MainScissor.left = 0;
	m_MainScissor.top = 0;
	m_MainScissor.right = (LONG)g_SceneColorBuffer.GetWidth();
	m_MainScissor.bottom = (LONG)g_SceneColorBuffer.GetHeight();

	if (m_MainViewport.Width != oldWidth || m_MainViewport.Height != oldHeight) hasChange = true;

	if (false) frameId = 0;
	else frameId++;
}

void LGHDemo::RenderObjects(GraphicsContext& gfxContext, int modelId, const Matrix4& ViewProjMat, ModelViewerConstants psConstants, eObjectFilter Filter)
{
	struct VSConstants
	{
		Matrix4 modelMatrix;
		Matrix4 modelToProjection;
		Matrix3 normalMatrix;
		XMFLOAT3 viewerPos;
	} vsConstants;
	vsConstants.modelMatrix = m_Models[modelId].m_modelMatrix;
	vsConstants.modelToProjection = ViewProjMat;
	vsConstants.normalMatrix = m_Models[modelId].m_modelMatrix.Get3x3();
	XMStoreFloat3(&vsConstants.viewerPos, m_Camera.GetPosition());

	gfxContext.SetDynamicConstantBufferView(0, sizeof(vsConstants), &vsConstants);
		
	uint32_t materialIdx = 0xFFFFFFFFul;

	uint32_t VertexStride = m_Models[modelId].m_VertexStride;

	for (uint32_t meshIndex = 0; meshIndex < m_Models[modelId].m_Header.meshCount; meshIndex++)
	{
		const Model1::Mesh& mesh = m_Models[modelId].m_pMesh[meshIndex];

		uint32_t indexCount = mesh.indexCount;
		uint32_t startIndex = mesh.indexDataByteOffset / m_Models[modelId].indexSize;
		uint32_t baseVertex = mesh.vertexDataByteOffset / VertexStride;

		if (mesh.materialIndex != materialIdx)
		{
			if (m_Models[modelId].m_pMaterialIsCutout[mesh.materialIndex] && !(Filter & kCutout) ||
				!m_Models[modelId].m_pMaterialIsCutout[mesh.materialIndex] && !(Filter & kOpaque))
				continue;

			materialIdx = mesh.materialIndex;
			gfxContext.SetDynamicDescriptors(2, 0, 3, m_Models[modelId].GetSRVs(materialIdx));
		}

		gfxContext.SetConstants(5, baseVertex, materialIdx);
		psConstants.diffuseColor = m_Models[modelId].m_pMaterial[mesh.materialIndex].diffuse;
		psConstants.specularColor = m_Models[modelId].m_pMaterial[mesh.materialIndex].specular;
		gfxContext.SetDynamicConstantBufferView(1, sizeof(psConstants), &psConstants);
		gfxContext.DrawIndexed(indexCount, startIndex, baseVertex);
	}
}

void LGHDemo::GetSubViewportAndScissor(int i, int j, int rate, D3D12_VIEWPORT & viewport, D3D12_RECT & scissor)
{
	viewport.Width = m_MainViewport.Width / rate;
	viewport.Height = m_MainViewport.Height / rate;
	viewport.TopLeftX = 0.5 + viewport.Width * j;
	viewport.TopLeftY = 0.5 + viewport.Height * i;

	scissor = m_MainScissor;
}


void LGHDemo::RenderScene(void)
{
	GraphicsContext& gfxContext = GraphicsContext::Begin(L"Scene Render");

#ifdef GENERATE_IR_GROUND_TRUTH
	irRenderer.GenerateVPLs(gfxContext, m_SunDirection, m_SunLightIntensity);
#else
	lgiRenderer.GenerateLightingGridHierarchy(gfxContext, m_SunDirection, m_SunLightIntensity, hasGeometryChange);
#endif

	std::chrono::high_resolution_clock::time_point t1;
	std::chrono::high_resolution_clock::time_point t2;
	std::chrono::duration<double> time_span;

#ifdef GENERATE_IR_GROUND_TRUTH
	if (frameId == 0) {
#endif

		uint32_t FrameIndexMod2 = TemporalEffects::GetFrameIndexMod2();

		ModelViewerConstants psConstants;
		psConstants.sunDirection = m_SunDirection;
		psConstants.sunLight = Vector3(1.0f, 1.0f, 1.0f) * m_SunLightIntensity;

		// Set the default state for command lists

		{
			ScopedTimer _prof(L"G-buffer generation", gfxContext);

			gfxContext.SetRootSignature(m_RootSig);
			gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			// can use vertex depth attribs instead
			{
				//ScopedTimer _prof(L"Z PrePass", gfxContext);

				for (int modelId = 0; modelId < m_Models.size(); modelId++)
				{
					gfxContext.SetIndexBuffer(m_Models[modelId].m_IndexBuffer.IndexBufferView());
					gfxContext.SetVertexBuffer(0, m_Models[modelId].m_VertexBuffer.VertexBufferView());

					{
						//ScopedTimer _prof(L"Opaque", gfxContext);
						gfxContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_WRITE, true);
						if (modelId == 0) gfxContext.ClearDepth(g_SceneDepthBuffer);

						gfxContext.SetPipelineState(m_DepthPSO);

						gfxContext.SetDepthStencilTarget(g_SceneDepthBuffer.GetDSV());
						gfxContext.SetViewportAndScissor(m_MainViewport, m_MainScissor);
						RenderObjects(gfxContext, modelId, m_ViewProjMatrix, psConstants, kOpaque);
					}
					{
						//ScopedTimer _prof(L"Cutout", gfxContext);
						gfxContext.SetPipelineState(m_CutoutDepthPSO);
						RenderObjects(gfxContext, modelId, m_ViewProjMatrix, psConstants, kCutout);
					}
				}
			}

			// Using MiniEngine's SSAO class to compute linearized depth
			SSAO::LinearizeDepth(gfxContext, m_Camera);

			{
				//ScopedTimer _prof(L"Main Render", gfxContext);

				for (int modelId = 0; modelId < m_Models.size(); modelId++)
				{
					gfxContext.SetIndexBuffer(m_Models[modelId].m_IndexBuffer.IndexBufferView());
					gfxContext.SetVertexBuffer(0, m_Models[modelId].m_VertexBuffer.VertexBufferView());

					{
						//ScopedTimer _prof(L"Render Shadow Map", gfxContext);

						m_SunShadow.UpdateMatrix(-m_SunDirection, Vector3(ShadowCenterX, ShadowCenterY, ShadowCenterZ), Vector3(ShadowDimX, ShadowDimY, ShadowDimZ),
							(uint32_t)g_ShadowBuffer.GetWidth(), (uint32_t)g_ShadowBuffer.GetHeight(), 16);

						g_ShadowBuffer.BeginRendering(gfxContext, modelId == 0);
						gfxContext.SetPipelineState(m_ShadowPSO);
						RenderObjects(gfxContext, modelId, m_SunShadow.GetViewProjMatrix(), psConstants, kOpaque);
						gfxContext.SetPipelineState(m_CutoutShadowPSO);
						RenderObjects(gfxContext, modelId, m_SunShadow.GetViewProjMatrix(), psConstants, kCutout);
						g_ShadowBuffer.EndRendering(gfxContext);
					}

					{
						//ScopedTimer _prof(L"Render G-buffer", gfxContext);

						gfxContext.TransitionResource(g_SSAOFullScreen, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

						gfxContext.SetDynamicDescriptors(4, 0, _countof(m_ExtraTextures), m_ExtraTextures);
						gfxContext.SetPipelineState(m_ModelPSO);

						gfxContext.TransitionResource(g_ScenePositionBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
						gfxContext.TransitionResource(g_SceneNormalBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
						gfxContext.TransitionResource(g_SceneAlbedoBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
						gfxContext.TransitionResource(g_SceneSpecularBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET);
						gfxContext.TransitionResource(g_SceneDepthBuffer, D3D12_RESOURCE_STATE_DEPTH_READ, true);
						if (modelId == 0) gfxContext.ClearColor(g_SceneAlbedoBuffer);
						if (modelId == 0) gfxContext.ClearColor(g_SceneSpecularBuffer);
						const D3D12_CPU_DESCRIPTOR_HANDLE gBufferHandles[] = { g_ScenePositionBuffer.GetRTV(), g_SceneNormalBuffer.GetRTV(),
							g_SceneAlbedoBuffer.GetRTV(), g_SceneSpecularBuffer.GetRTV() };
						gfxContext.SetRenderTargets(4, gBufferHandles, g_SceneDepthBuffer.GetDSV_DepthReadOnly());
						gfxContext.SetViewportAndScissor(m_MainViewport, m_MainScissor);

						RenderObjects(gfxContext, modelId, m_ViewProjMatrix, psConstants, kOpaque);

						gfxContext.SetPipelineState(m_CutoutModelPSO);
						RenderObjects(gfxContext, modelId, m_ViewProjMatrix, psConstants, kCutout);
					}
				}
			}

		}

#ifdef GENERATE_IR_GROUND_TRUTH
	}
#endif

	if (TemporalEffects::EnableTAA) m_EnableShadowTAA = false;

#ifndef GENERATE_IR_GROUND_TRUTH
	if (m_EnableShadowTAA || (LGHRenderer::ShadowFilterType)(int)lgiRenderer.m_ShadowFilterType == LGHRenderer::ShadowFilterType::SVGF)
	{
		MotionBlur::GenerateCameraVelocityBuffer(gfxContext, m_Camera, true);
	}
#endif

#ifdef GENERATE_IR_GROUND_TRUTH
	irRenderer.Render(gfxContext, m_MainViewport.Width, m_MainViewport.Height, frameId);
#else
	if (!m_DirectLightingOnly) lgiRenderer.Render(gfxContext, ViewConfig(m_Camera, m_MainViewport, m_MainScissor), frameId, hasGeometryChange);
#endif

#ifndef GENERATE_IR_GROUND_TRUTH
	TemporalEffects::ResolveCustomImage(gfxContext, lgiRenderer.m_SURatioBuffer, m_EnableShadowTAA);
#endif

	{
		ScopedTimer _prof(L"Compositing", gfxContext);
		__declspec(align(16)) struct
		{
			Matrix4 WorldToShadow;
			Vector3 ViewerPos;
			Vector3 SunDirection;
			Vector3 SunColor;
			Vector4 ShadowTexelSize;
			int scrWidth;
			int scrHeight;
			int shadowRate;
			int debugMode;
			int directOnly;
		} psConstants;

		psConstants.WorldToShadow = m_SunShadow.GetShadowMatrix();
		psConstants.ViewerPos = m_Camera.GetPosition();
		psConstants.SunDirection = m_SunDirection;
		psConstants.SunColor = Vector3(1.0f, 1.0f, 1.0f) * m_SunLightIntensity;
		psConstants.ShadowTexelSize = Vector4(1.0f / g_ShadowBuffer.GetWidth());
		psConstants.scrWidth = m_MainViewport.Width;
		psConstants.scrHeight = m_MainViewport.Height;
		psConstants.debugMode = DebugView > 0;
		psConstants.directOnly = m_DirectLightingOnly;
#ifdef GENERATE_IR_GROUND_TRUTH
		psConstants.shadowRate = 0;
#else
		psConstants.shadowRate = LGHRenderer::m_IndirectShadow ? LGHRenderer::m_ShadowRate : 0;
#endif
		gfxContext.SetRootSignature(m_RootSig);
		gfxContext.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
		gfxContext.SetIndexBuffer(m_quad.m_IndexBuffer.IndexBufferView());
		gfxContext.SetVertexBuffer(0, m_quad.m_VertexBuffer.VertexBufferView());
		gfxContext.TransitionResource(g_ShadowBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

#ifdef GENERATE_IR_GROUND_TRUTH
		gfxContext.TransitionResource(irRenderer.resultBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, true);
		D3D12_CPU_DESCRIPTOR_HANDLE compositeSrvs[3] = { irRenderer.resultBuffer.GetSRV(),
							irRenderer.resultBuffer.GetSRV(), g_ShadowBuffer.GetSRV() };
#else
		gfxContext.TransitionResource(lgiRenderer.m_AnalyticBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

		D3D12_CPU_DESCRIPTOR_HANDLE customView = lgiRenderer.m_SURatioBuffer.GetSRV();

		if (DebugView == 0)  gfxContext.TransitionResource(lgiRenderer.m_SURatioBuffer, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		else if (DebugView == 1)
		{
			gfxContext.TransitionResource(lgiRenderer.m_ShadowedStochasticBuffer[2], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			customView = lgiRenderer.m_ShadowedStochasticBuffer[2].GetSRV();
		}
		else if (DebugView == 2)
		{
			if ((LGHRenderer::ShadowFilterType)(int)lgiRenderer.m_ShadowFilterType == LGHRenderer::ShadowFilterType::SVGF)
			{
				gfxContext.TransitionResource(lgiRenderer.svgfDenoiser.m_IntegratedS[1], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
				customView = lgiRenderer.svgfDenoiser.m_IntegratedS[1].GetSRV();
			}
			else
			{
				gfxContext.TransitionResource(lgiRenderer.m_ShadowedStochasticBuffer[1], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
				customView = lgiRenderer.m_ShadowedStochasticBuffer[1].GetSRV();
			}
		}
		else if (DebugView == 3)
		{
			gfxContext.TransitionResource(lgiRenderer.m_UnshadowedStochasticBuffer[2], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			customView = lgiRenderer.m_UnshadowedStochasticBuffer[2].GetSRV();
		}
		else if (DebugView == 4)
		{
			if ((LGHRenderer::ShadowFilterType)(int)lgiRenderer.m_ShadowFilterType == LGHRenderer::ShadowFilterType::SVGF)
			{
				gfxContext.TransitionResource(lgiRenderer.svgfDenoiser.m_IntegratedU[1], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
				customView = lgiRenderer.svgfDenoiser.m_IntegratedU[1].GetSRV();
			}
			else
			{
				gfxContext.TransitionResource(lgiRenderer.m_UnshadowedStochasticBuffer[1], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
				customView = lgiRenderer.m_UnshadowedStochasticBuffer[1].GetSRV();

			}
		}

		D3D12_CPU_DESCRIPTOR_HANDLE compositeSrvs[3] = { lgiRenderer.m_AnalyticBuffer.GetSRV(), 
							customView, g_ShadowBuffer.GetSRV() };
#endif
		gfxContext.SetDynamicConstantBufferView(1, sizeof(psConstants), &psConstants);
		gfxContext.SetDynamicDescriptors(3, 0, _countof(m_GBuffer), m_GBuffer);
		gfxContext.SetDynamicDescriptors(4, 0, _countof(compositeSrvs), compositeSrvs);
		gfxContext.SetPipelineState(m_ScreenPSO);
		gfxContext.TransitionResource(g_SceneColorBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
		gfxContext.ClearColor(g_SceneColorBuffer);
		gfxContext.SetRenderTarget(g_SceneColorBuffer.GetRTV());
		gfxContext.SetViewportAndScissor(m_MainViewport, m_MainScissor);
		gfxContext.DrawIndexed(m_quad.indicesPerInstance, 0, 0);
	}

#ifndef GENERATE_IR_GROUND_TRUTH
	if (!m_EnableShadowTAA && (LGHRenderer::ShadowFilterType)(int)lgiRenderer.m_ShadowFilterType != LGHRenderer::ShadowFilterType::SVGF)
	{
		MotionBlur::GenerateCameraVelocityBuffer(gfxContext, m_Camera, true);
	}
#endif

	if (!m_EnableShadowTAA) TemporalEffects::ResolveImage(gfxContext);

	if (DepthOfField::Enable)
		DepthOfField::Render(gfxContext, m_Camera.GetNearClip(), m_Camera.GetFarClip());
	else
		MotionBlur::RenderObjectBlur(gfxContext, g_VelocityBuffer);

	hasGeometryChange = false;
	gfxContext.Finish();
}
