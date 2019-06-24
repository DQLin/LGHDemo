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
// Author:  James Stanard 
//

#include "pch.h"
#include "FXAA.h"
#include "GraphicsCore.h"
#include "BufferManager.h"
#include "CommandContext.h"

#include "FXAAPass1_RGB_CS.h"
#include "FXAAPass1_Luma_CS.h"
#include "FXAAPass2HCS.h"
#include "FXAAPass2VCS.h"
#include "FXAAPass2HDebugCS.h"
#include "FXAAPass2VDebugCS.h"

// These shaders are when typed UAV loads of complex formats is available
#include "FXAAPass1_RGB2_CS.h"
#include "FXAAPass1_Luma2_CS.h"
#include "FXAAPass2H2CS.h"
#include "FXAAPass2V2CS.h"
#include "FXAAPass2HDebug2CS.h"
#include "FXAAPass2VDebug2CS.h"

#include "FXAAResolveWorkQueueCS.h"


using namespace Graphics;

namespace FXAA
{
    RootSignature RootSig;
    ComputePSO Pass1HdrCS;
    ComputePSO Pass1LdrCS;
    ComputePSO ResolveWorkCS;
    ComputePSO Pass2HCS;
    ComputePSO Pass2VCS;
    ComputePSO Pass2HDebugCS;
    ComputePSO Pass2VDebugCS;
    IndirectArgsBuffer IndirectParameters;

    BoolVar Enable("Graphics/AA/FXAA/Enable", true);
    BoolVar DebugDraw("Graphics/AA/FXAA/Debug", false);

    // With a properly encoded luma buffer, [0.25 = "low", 0.2 = "medium", 0.15 = "high", 0.1 = "ultra"]
    NumVar ContrastThreshold("Graphics/AA/FXAA/Contrast Threshold", 0.175f, 0.05f, 0.5f, 0.025f);

    // Controls how much to blur isolated pixels that have little-to-no edge length.
    NumVar SubpixelRemoval("Graphics/AA/FXAA/Subpixel Removal", 0.50f, 0.0f, 1.0f, 0.25f);

    // This is for testing the performance of computing luma on the fly rather than reusing
    // the luma buffer output of tone mapping.
    BoolVar ForceOffPreComputedLuma("Graphics/AA/FXAA/Always Recompute Log-Luma", false);
}

void FXAA::Initialize( void )
{
    RootSig.Reset(3, 1);
    RootSig.InitStaticSampler(0, SamplerLinearClampDesc);
    RootSig[0].InitAsConstants(0, 7);
    RootSig[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 5);
    RootSig[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 6);
    RootSig.Finalize(L"FXAA");

#define CreatePSO( ObjName, ShaderByteCode ) \
    ObjName.SetRootSignature(RootSig); \
    ObjName.SetComputeShader(ShaderByteCode, sizeof(ShaderByteCode) ); \
    ObjName.Finalize();

    CreatePSO(ResolveWorkCS, g_pFXAAResolveWorkQueueCS);
    if (g_bTypedUAVLoadSupport_R11G11B10_FLOAT)
    {
        CreatePSO(Pass1LdrCS, g_pFXAAPass1_RGB2_CS);    // Use RGB and recompute log-luma; pre-computed luma is unavailable
        CreatePSO(Pass1HdrCS, g_pFXAAPass1_Luma2_CS);   // Use pre-computed luma
        CreatePSO(Pass2HCS, g_pFXAAPass2H2CS);
        CreatePSO(Pass2VCS, g_pFXAAPass2V2CS);
        CreatePSO(Pass2HDebugCS, g_pFXAAPass2HDebug2CS);
        CreatePSO(Pass2VDebugCS, g_pFXAAPass2VDebug2CS);
    }
    else
    {
        CreatePSO(Pass1LdrCS, g_pFXAAPass1_RGB_CS);     // Use RGB and recompute log-luma; pre-computed luma is unavailable
        CreatePSO(Pass1HdrCS, g_pFXAAPass1_Luma_CS);    // Use pre-computed luma
        CreatePSO(Pass2HCS, g_pFXAAPass2HCS);
        CreatePSO(Pass2VCS, g_pFXAAPass2VCS);
        CreatePSO(Pass2HDebugCS, g_pFXAAPass2HDebugCS);
        CreatePSO(Pass2VDebugCS, g_pFXAAPass2VDebugCS);
    }
#undef CreatePSO

    __declspec(align(16)) const uint32_t initArgs[6] = { 0, 1, 1, 0, 1, 1 };
    IndirectParameters.Create(L"FXAA Indirect Parameters", 2, sizeof(D3D12_DISPATCH_ARGUMENTS), initArgs);
}

void FXAA::Shutdown(void)
{
    IndirectParameters.Destroy();
}

void FXAA::Render( ComputeContext& Context, bool bUsePreComputedLuma )
{
    ScopedTimer _prof(L"FXAA", Context);

    if (ForceOffPreComputedLuma)
        bUsePreComputedLuma = false;

    ColorBuffer& Target = g_bTypedUAVLoadSupport_R11G11B10_FLOAT ? g_SceneColorBuffer : g_PostEffectsBuffer;

    Context.SetRootSignature(RootSig);
    Context.SetConstants(0, 1.0f / Target.GetWidth(), 1.0f / Target.GetHeight(), (float)ContrastThreshold, (float)SubpixelRemoval);
    Context.SetConstant(0, g_FXAAWorkQueue.GetElementCount() - 1, 4);

    // Apply algorithm to each quarter of the screen separately to reduce maximum size of work buffers.
    uint32_t BlockWidth = Target.GetWidth() / 2;
    uint32_t BlockHeight = Target.GetHeight() / 2;

    for (uint32_t x = 0; x < Target.GetWidth(); x += BlockWidth)
    {
        for (uint32_t y = 0; y < Target.GetHeight(); y += BlockHeight)
        {
            // Pass 1
            Context.SetConstant(0, x, 5);
            Context.SetConstant(0, y, 6);

            // Begin by analysing the luminance buffer and setting aside high-contrast pixels in
            // work queues to be processed later.  There are horizontal edge and vertical edge work
            // queues so that the shader logic is simpler for each type of edge.
            // Counter values do not need to be reset because they are read and cleared at once.

            Context.TransitionResource(Target, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Context.TransitionResource(g_FXAAWorkQueue, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Context.TransitionResource(g_FXAAColorQueue, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            D3D12_CPU_DESCRIPTOR_HANDLE Pass1UAVs[] =
            {
                g_FXAAWorkCounters.GetUAV(),
                g_FXAAWorkQueue.GetUAV(),
                g_FXAAColorQueue.GetUAV(),
                g_LumaBuffer.GetUAV()
            };

            D3D12_CPU_DESCRIPTOR_HANDLE Pass1SRVs[] =
            {
                Target.GetSRV(),
                g_LumaBuffer.GetSRV()
            };

            if (bUsePreComputedLuma)
            {
                Context.SetPipelineState(Pass1HdrCS);
                Context.TransitionResource(g_LumaBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                Context.SetDynamicDescriptors(1, 0, _countof(Pass1UAVs) - 1, Pass1UAVs);
                Context.SetDynamicDescriptors(2, 0, _countof(Pass1SRVs), Pass1SRVs);
            }
            else
            {
                Context.SetPipelineState(Pass1LdrCS);
                Context.TransitionResource(g_LumaBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                Context.SetDynamicDescriptors(1, 0, _countof(Pass1UAVs), Pass1UAVs);
                Context.SetDynamicDescriptors(2, 0, _countof(Pass1SRVs) - 1, Pass1SRVs);
            }

            Context.Dispatch2D(BlockWidth, BlockHeight);

            // Pass 2

            // The next phase involves converting the work queues to DispatchIndirect parameters.
            // The queues are also padded out to 64 elements to simplify the final consume logic.
            Context.SetPipelineState(ResolveWorkCS);
            Context.TransitionResource(IndirectParameters, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Context.InsertUAVBarrier(g_FXAAWorkCounters);

            Context.SetDynamicDescriptor(1, 0, IndirectParameters.GetUAV());
            Context.SetDynamicDescriptor(1, 1, g_FXAAWorkQueue.GetUAV());
            Context.SetDynamicDescriptor(1, 2, g_FXAAWorkCounters.GetUAV());

            Context.Dispatch(1, 1, 1);

            Context.InsertUAVBarrier(g_FXAAWorkCounters);
            Context.TransitionResource(IndirectParameters, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
            Context.TransitionResource(g_FXAAWorkQueue, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Context.TransitionResource(g_FXAAColorQueue, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Context.TransitionResource(Target, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

            Context.SetDynamicDescriptor(1, 0, Target.GetUAV());
            Context.SetDynamicDescriptor(2, 0, g_LumaBuffer.GetSRV());
            Context.SetDynamicDescriptor(2, 1, g_FXAAWorkQueue.GetSRV());
            Context.SetDynamicDescriptor(2, 2, g_FXAAColorQueue.GetSRV());

            // The final phase involves processing pixels on the work queues and writing them
            // back into the color buffer.  Because the two source pixels required for linearly
            // blending are held in the work queue, this does not require also sampling from
            // the target color buffer (i.e. no read/modify/write, just write.)

            Context.SetPipelineState(DebugDraw ? Pass2HDebugCS : Pass2HCS);
            Context.DispatchIndirect(IndirectParameters, 0);
            Context.SetPipelineState(DebugDraw ? Pass2VDebugCS : Pass2VCS);
            Context.DispatchIndirect(IndirectParameters, 12);

            Context.InsertUAVBarrier(Target);
        }
    }
}
