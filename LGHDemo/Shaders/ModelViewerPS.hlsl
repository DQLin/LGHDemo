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
// Author(s):	James Stanard
//				Alex Nankervis
//
// Thanks to Michal Drobot for his feedback.

// outdated warning about for-loop variable scope
#pragma warning (disable: 3078)
// single-iteration loop
#pragma warning (disable: 3557)

struct VSOutput
{
    sample float4 position : SV_Position;
    sample float3 worldPos : WorldPos;
    sample float2 uv : TexCoord0;
    sample float3 normal : Normal;
    sample float3 tangent : Tangent;
    sample float3 bitangent : Bitangent;
};

struct PSOutput
{
	float4 position : SV_TARGET0;
	float4 normal   : SV_TARGET1;
	float4 albedo   : SV_TARGET2;
	float4 specular : SV_TARGET3;
};

Texture2D<float3> texDiffuse		: register(t0);
Texture2D<float3> texSpecular		: register(t1);
Texture2D<float3> texNormal			: register(t2);
Texture2D<float> texSSAO			: register(t64);
Texture2D<float> texShadow			: register(t65);

cbuffer PSConstants : register(b0)
{
    float3 SunDirection;
    float3 SunColor;
	float3 diffuseColor;
	float3 specularColor;
}

SamplerState sampler0 : register(s0);
SamplerComparisonState shadowSampler : register(s1);

void AntiAliasSpecular( inout float3 texNormal, inout float gloss )
{
    float normalLenSq = dot(texNormal, texNormal);
    float invNormalLen = rsqrt(normalLenSq);
    texNormal *= invNormalLen;
    gloss = lerp(1, gloss, rcp(invNormalLen));
}

// Helper function for iterating over a sparse list of bits.  Gets the offset of the next
// set bit, clears it, and returns the offset.

PSOutput main(VSOutput vsOutput) : SV_Target
{
	PSOutput output;
    float3 diffuseAlbedo = texDiffuse.Sample(sampler0, vsOutput.uv);

    float gloss = 128.0;
    float3 normal;

	if (all(vsOutput.bitangent) == 0) //tangent space not defined
	{
		normal = vsOutput.normal;
	}
	else
    {
        normal = texNormal.Sample(sampler0, vsOutput.uv) * 2.0 - 1.0;
        AntiAliasSpecular(normal, gloss);
        float3x3 tbn = float3x3(normalize(vsOutput.tangent), normalize(vsOutput.bitangent), normalize(vsOutput.normal));
        normal = normalize(mul(normal, tbn));
    }
	
    float3 specularMask = texSpecular.Sample(sampler0, vsOutput.uv).g;

	if (any(isnan(normal))) normal = float3(1.0, 0.0, 0.0);

	output.position = float4(vsOutput.worldPos, 1.0);
	output.normal = float4(normal, 1.0);
	output.albedo = float4(diffuseColor * diffuseAlbedo, 1.0);
	output.specular = float4(specularColor * specularMask, 1.0);
    return output;
}
