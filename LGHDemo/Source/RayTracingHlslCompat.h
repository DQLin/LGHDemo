#pragma once

struct RayTraceMeshInfo
{
	unsigned int m_indexOffsetBytes;
	unsigned int m_uvAttributeOffsetBytes;
	unsigned int m_normalAttributeOffsetBytes;
	unsigned int m_tangentAttributeOffsetBytes;
	unsigned int m_bitangentAttributeOffsetBytes;
	unsigned int m_positionAttributeOffsetBytes;
	unsigned int m_attributeStrideBytes;
	unsigned int m_materialInstanceId;
	float diffuse[3];
};