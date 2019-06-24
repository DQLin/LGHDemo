#pragma once

// Copyright (c) 2019, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).
// Permission is hereby granted, free of charge, to any person obtaining a copy 
// of this software and associated documentation files (the "Software"), to deal 
// in the Software without restriction, including without limitation the rights 
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell 
// copies of the Software, and to permit persons to whom the Software is 
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all 
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE 
// SOFTWARE.

//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
// Developed by Minigraph
//
// Author(s):  Alex Nankervis
//             James Stanard
//

#include <atlbase.h>
#include "dxgi1_3.h"
#include "GameCore.h"
#include "GraphicsCore.h"
#include "CameraController.h"
#include "BufferManager.h"
#include "Camera.h"
#include "ModelLoader.h"
#include "GpuBuffer.h"
#include "ReadbackBuffer.h"
#include "CommandContext.h"
#include "SamplerManager.h"
#include "TemporalEffects.h"
#include "MotionBlur.h"
#include "DepthOfField.h"
#include "PostEffects.h"
#include "SSAO.h"
#include "FXAA.h"
#include "SystemTime.h"
#include "TextRenderer.h"
#include "ShadowCamera.h"
#include "ParticleEffectManager.h"
#include "GameInput.h"
#include "Quad.h"
#include "LGHRenderer.h"
#include "InstantRadiosityRenderer.h"

#include "DepthViewerVS.h"
#include "DepthViewerPS.h"
#include "ModelViewerVS.h"
#include "ModelViewerPS.h"
#include "ScreenShaderVS.h"
#include "ScreenShaderPS.h"
#include "LGHBuilder.h"
#include "RaytracingHlslCompat.h"
#include <chrono>

//#define GENERATE_IR_GROUND_TRUTH
//#define ENABLE_TEAPOT

using namespace GameCore;
using namespace Math;
using namespace Graphics;

bool ParseSceneFile(const std::string SceneFile, std::string& modelPath, Camera& m_Camera, bool& isCameraInitialized,
	float& sunIntensity, float& sunOrientation, float& sunInclination, int& imgWidth, int &imgHeight);

class LGHDemo : public GameCore::IGameApp
{
public:

	LGHDemo(void) { m_ModelFile = ""; }
	LGHDemo(const std::string& modelFile, bool isCameraInitialized, const Camera& camera, 
								float sunIntensity, float sunOrientation, float sunInclination);

	virtual void Startup(void) override;
	virtual void Cleanup(void) override;

	virtual void Update(float deltaT) override;
	virtual void RenderScene(void) override;

private:


	__declspec(align(16)) struct ModelViewerConstants
	{
		Vector3 sunDirection;
		Vector3 sunLight;
		Vector3 diffuseColor;
		Vector3 specularColor;
	};

	enum eObjectFilter { kOpaque = 0x1, kCutout = 0x2, kTransparent = 0x4, kAll = 0xF, kNone = 0x0 };
	void RenderObjects(GraphicsContext& gfxContext, int modelId, const Matrix4& ViewProjMat, ModelViewerConstants psConstants, eObjectFilter Filter = kAll);
	void GetSubViewportAndScissor(int i, int j, int rate, D3D12_VIEWPORT& viewport, D3D12_RECT& scissor);

	std::string m_ModelFile;
	bool m_IsCameraInitialized = false;

	Camera m_Camera;
	std::auto_ptr<CameraController> m_CameraController;
	Matrix4 m_ViewProjMatrix;
	D3D12_VIEWPORT m_MainViewport;
	D3D12_RECT m_MainScissor;

	RootSignature m_RootSig;
	RootSignature m_ComputeRootSig;
	GraphicsPSO m_DepthPSO;
	GraphicsPSO m_CutoutDepthPSO;
	GraphicsPSO m_ModelPSO;
	GraphicsPSO m_CutoutModelPSO;
	GraphicsPSO m_ShadowPSO;
	GraphicsPSO m_CutoutShadowPSO;
	GraphicsPSO m_ScreenPSO;
	GraphicsPSO m_FillPSO;

	D3D12_CPU_DESCRIPTOR_HANDLE m_DefaultSampler;
	D3D12_CPU_DESCRIPTOR_HANDLE m_ShadowSampler;
	D3D12_CPU_DESCRIPTOR_HANDLE m_BiasedDefaultSampler;
	D3D12_CPU_DESCRIPTOR_HANDLE m_GBuffer[4];
	D3D12_CPU_DESCRIPTOR_HANDLE m_ExtraTextures[2];

	std::vector<Model1> m_Models;
	Quad  m_quad;

	Vector4 m_SceneSphere;
	Vector3 m_SunDirection;
	ShadowCamera m_SunShadow;

	const int maxUpdateFrames = 1;

	int frameId;
	bool hasGeometryChange = false;

#ifdef GENERATE_IR_GROUND_TRUTH
	InstantRadiosityRenderer irRenderer;
#else
	LGHRenderer lgiRenderer;
#endif
};