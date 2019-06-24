// Copyright (c) 2019, Daqi Lin <daqi@cs.utah.edu>
// All rights reserved.
// This code is licensed under the MIT License (MIT).

struct VSInput
{
	float4 position : POSITION;
	float4 vposradius : VPOSRADIUS;
	float3 normal : NORMAL;
	float3 color  : COLOR;
	float3 dev    : DEV;
	uint instanceId : SV_InstanceID;
};

struct VSOutput
{
	float4 position : SV_POSITION;
	nointerpolation float4 vposition : VPOSITION;
	nointerpolation float3 vnormal : VNORMAL;
	nointerpolation float3 vcolor : VCOLOR;
	nointerpolation float3 vdev : VDEV;
	nointerpolation uint vlevel : VLEVEL;
	nointerpolation uint instanceId : INSTANCEID;
};

cbuffer VSConstants : register(b0)
{
	float4x4 modelToProjection;
	float3 ViewerPos;
	float baseRadius;
	float alpha;
};

VSOutput main(VSInput input)
{
	VSOutput output;
	output.vposition = input.vposradius;
	output.vnormal = input.normal;
	output.vcolor = input.color;
	output.vdev = input.dev;
	int level = int(input.vposradius.w);
	output.vlevel = level;

	float radius = baseRadius * (1 << level);
	output.vposition.w = alpha * radius;
	float diameter = 2 * alpha * radius;

	float4x4 model = {
		diameter, 0, 0, output.vposition.x,
		0, diameter, 0, output.vposition.y,
		0, 0, diameter, output.vposition.z,
		0, 0, 0, 1
	};
	output.position = mul(model, float4(input.position.xyz, 1.0));
	output.position = mul(modelToProjection, output.position);
	output.instanceId = input.instanceId;
	return output;
}