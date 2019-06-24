//NOTE: This file combines the code of MiniEngine TAA (Microsoft) and SVGF (NVIDIA)

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

/**********************************************************************************************************************
# Copyright (c) 2018, NVIDIA CORPORATION. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
# following conditions are met:
#  * Redistributions of code must retain the copyright notice, this list of conditions and the following disclaimer.
#  * Neither the name of NVIDIA CORPORATION nor the names of its contributors may be used to endorse or promote products
#    derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT
# SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
# OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**********************************************************************************************************************/

#include "SVGFCommon.hlsli"

RWTexture2D<float4> texOutS : register(u0);
RWTexture2D<float4> texOutU : register(u1);
RWTexture2D<float4> texOutM : register(u2);
RWTexture2D<uint> texHistoryLength : register(u3);

Texture2D<uint> VelocityBuffer : register(t0);
Texture2D<float> CurDepth : register(t1);
Texture2D<float> PreDepth : register(t2);
Texture2D<float4> texPrevS : register(t3);
Texture2D<float4> texPrevU : register(t4);
Texture2D<float4> texPrevM : register(t5);
Texture2D<float3> texCurS : register(t6);
Texture2D<float3> texCurU : register(t7);
Texture2D<float> GradDepth : register(t8);

SamplerState LinearSampler : register(s1);

cbuffer CSConstants : register(b0)
{
	float2 RcpBufferDim;    // 1 / width, 1 / height
	float RcpSpeedLimiter;
	float gAlpha;
	float gMomentsAlpha;
	int disableTAA;
}

float UnpackXY(uint x)
{
	return f16tof32((x & 0x1FF) << 4 | (x >> 9) << 15) * 32768.0;
}

float UnpackZ(uint x)
{
	return f16tof32((x & 0x7FF) << 2 | (x >> 11) << 15) * 128.0;
}

float3 UnpackVelocity(uint Velocity)
{
	return float3(UnpackXY(Velocity & 0x3FF), UnpackXY((Velocity >> 10) & 0x3FF), UnpackZ(Velocity >> 20));
}

float2 STtoUV(float2 ST)
{
	return (ST + 0.5) * RcpBufferDim;
}

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID, uint GI : SV_GroupIndex, uint3 GTid : SV_GroupThreadID, uint3 Gid : SV_GroupID)
{
	uint2 ST = DTid.xy;

	float3 curS = texCurS[ST];
	float3 curU = texCurU[ST];

	float4 moments;
	moments.r = luminance(curS);
	moments.b = luminance(curU);
	moments.g = moments.r * moments.r;
	moments.a = moments.b * moments.b;

	if (disableTAA)
	{
		texOutM[ST] = moments;
		texOutS[ST] = float4(curS, 0);
		texOutU[ST] = float4(curU, 0);
		texHistoryLength[ST] = 0;
		return;
	}

	float CompareDepth = CurDepth[ST];
	float3 Velocity = UnpackVelocity(VelocityBuffer[ST]);
	CompareDepth += Velocity.z;

	float TemporalDepth = PreDepth[int2(ST + Velocity.xy + 0.5)];

	uint historyLength = texHistoryLength[ST];

	float GradZ = GradDepth[ST];

	bool success = abs(TemporalDepth - CompareDepth) / (GradZ + 1e-4) <= 8.0; 
	if (success)
	{
		historyLength++;
		historyLength = min(historyLength, 32);
		float4 prevS = texPrevS.SampleLevel(LinearSampler, STtoUV(ST + Velocity.xy), 0);
		float4 prevU = texPrevU.SampleLevel(LinearSampler, STtoUV(ST + Velocity.xy), 0);
		float4 prevM = texPrevM.SampleLevel(LinearSampler, STtoUV(ST + Velocity.xy), 0);
		const float alpha = max(gAlpha, 1.0 / historyLength);
		const float alphaMoments = max(gMomentsAlpha, 1.0 / historyLength);
		float4 OutS, OutU;
		OutS = lerp(prevS, float4(curS, 0), alpha);
		OutU = lerp(prevU, float4(curU, 0), alpha);

		moments = lerp(prevM, moments, alphaMoments);
		float2 variance = max(float2(0, 0), moments.ga - moments.rb * moments.rb);
		OutS.a = variance.r;
		OutU.a = variance.g;

		texOutM[ST] = moments;
		texOutS[ST] = OutS;
		texOutU[ST] = OutU;
	}
	else
	{
		// temporal variance not available, need to use spatial variance
		historyLength = 0;
		texOutS[ST] = float4(curS, 0);
		texOutU[ST] = float4(curU, 0);
		texOutM[ST] = moments;
	}

	texHistoryLength[ST] = historyLength;
}
