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
// Author(s):  James Stanard
//             Alex Nankervis
//

cbuffer VSConstants : register(b0)
{
	float4x4 modelMatrix;
    float4x4 modelToProjection;
    float4x4 normalMatrix;
    float3 ViewerPos;
};

struct VSInput
{
    float3 position : POSITION;
    float2 texcoord0 : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
    float3 bitangent : BITANGENT;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 worldPos : WorldPos;
    float2 texCoord : TexCoord0;
    float3 normal : Normal;
    float3 tangent : Tangent;
    float3 bitangent : Bitangent;
};

VSOutput main(VSInput vsInput)
{
    VSOutput vsOutput;

	float4 worldPosition = mul(modelMatrix, float4(vsInput.position, 1.0));
    vsOutput.position = mul(modelToProjection, worldPosition);
    vsOutput.worldPos = worldPosition;
    vsOutput.texCoord = vsInput.texcoord0;

    vsOutput.normal = mul(float3x3(normalMatrix._m00_m01_m02, normalMatrix._m10_m11_m12, normalMatrix._m20_m21_m22), vsInput.normal);
    vsOutput.tangent = vsInput.tangent;
    vsOutput.bitangent = vsInput.bitangent;

    return vsOutput;
}
