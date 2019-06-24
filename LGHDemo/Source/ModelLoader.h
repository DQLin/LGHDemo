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
// Author(s):   Alex Nankervis
//              James Stanard
//

#pragma once

#include "VectorMath.h"
#include "TextureManager.h"
#include "GpuBuffer.h"
#include "CPUModel.h"

using namespace Math;

class Model1
{
public:

	CPUModel* cpumodel;

	Matrix4 m_modelMatrix;

	unsigned int indexSize;

	Model1();
	~Model1();

	void Clear();

	enum
	{
		attrib_mask_0 = (1 << 0),
		attrib_mask_1 = (1 << 1),
		attrib_mask_2 = (1 << 2),
		attrib_mask_3 = (1 << 3),
		attrib_mask_4 = (1 << 4),
		attrib_mask_5 = (1 << 5),
		attrib_mask_6 = (1 << 6),
		attrib_mask_7 = (1 << 7),
		attrib_mask_8 = (1 << 8),
		attrib_mask_9 = (1 << 9),
		attrib_mask_10 = (1 << 10),
		attrib_mask_11 = (1 << 11),
		attrib_mask_12 = (1 << 12),
		attrib_mask_13 = (1 << 13),
		attrib_mask_14 = (1 << 14),
		attrib_mask_15 = (1 << 15),

		// friendly name aliases
		attrib_mask_position = attrib_mask_0,
		attrib_mask_texcoord0 = attrib_mask_1,
		attrib_mask_normal = attrib_mask_2,
		attrib_mask_tangent = attrib_mask_3,
		attrib_mask_bitangent = attrib_mask_4,
	};

	enum
	{
		attrib_0 = 0,
		attrib_1 = 1,
		attrib_2 = 2,
		attrib_3 = 3,
		attrib_4 = 4,
		attrib_5 = 5,
		attrib_6 = 6,
		attrib_7 = 7,
		attrib_8 = 8,
		attrib_9 = 9,
		attrib_10 = 10,
		attrib_11 = 11,
		attrib_12 = 12,
		attrib_13 = 13,
		attrib_14 = 14,
		attrib_15 = 15,

		// friendly name aliases
		attrib_position = attrib_0,
		attrib_texcoord0 = attrib_1,
		attrib_normal = attrib_2,
		attrib_tangent = attrib_3,
		attrib_bitangent = attrib_4,

		maxAttribs = 16
	};

	enum
	{
		attrib_format_none = 0,
		attrib_format_ubyte,
		attrib_format_byte,
		attrib_format_ushort,
		attrib_format_short,
		attrib_format_float,

		attrib_formats
	};

	struct BoundingBox
	{
		Vector3 min;
		Vector3 max;
	};

	struct Header
	{
		uint32_t meshCount;
		uint32_t materialCount;
		uint32_t vertexDataByteSize;
		uint32_t indexDataByteSize;
		uint32_t vertexDataByteSizeDepth;
		BoundingBox boundingBox;
	};
	Header m_Header;

	struct Attrib
	{
		uint16_t offset; // byte offset from the start of the vertex
		uint16_t normalized; // if true, integer formats are interpreted as [-1, 1] or [0, 1]
		uint16_t components; // 1-4
		uint16_t format;
	};
	struct Mesh
	{
		BoundingBox boundingBox;

		unsigned int materialIndex;

		unsigned int attribsEnabled;
		unsigned int attribsEnabledDepth;
		unsigned int vertexStride;
		unsigned int vertexStrideDepth;
		Attrib attrib[maxAttribs];
		Attrib attribDepth[maxAttribs];

		unsigned int vertexDataByteOffset;
		unsigned int vertexCount;
		unsigned int indexDataByteOffset;
		unsigned int indexCount;

		unsigned int vertexDataByteOffsetDepth;
		unsigned int vertexCountDepth;

		Mesh() 
		{
			attrib[attrib_position].offset = 0;
			attrib[attrib_tangent].offset = 3;
			attrib[attrib_normal].offset = 5;
			attrib[attrib_tangent].offset = 8;
			attrib[attrib_bitangent].offset = 11;
		};
	};
	Mesh *m_pMesh;

	struct Material
	{
		Vector3 diffuse;
		Vector3 specular;
		Vector3 ambient;
		Vector3 emissive;
		Vector3 transparent; // light passing through a transparent surface is multiplied by this filter color
		float opacity;
		float shininess; // specular exponent
		float specularStrength; // multiplier on top of specular color

		enum { maxTexPath = 128 };
		enum { texCount = 3 };
		char texDiffusePath[maxTexPath];
		char texSpecularPath[maxTexPath];
		char texEmissivePath[maxTexPath];
		char texNormalPath[maxTexPath];
		char texLightmapPath[maxTexPath];
		char texReflectionPath[maxTexPath];

		enum { maxMaterialName = 128 };
		char name[maxMaterialName];
	};
	Material *m_pMaterial;

	unsigned char *m_pVertexData;
	unsigned char *m_pIndexData;
	StructuredBuffer m_VertexBuffer;
	ByteAddressBuffer m_IndexBuffer;
	uint32_t m_VertexStride;

	// optimized for depth-only rendering
	unsigned char *m_pVertexDataDepth;
	unsigned char *m_pIndexDataDepth;
	StructuredBuffer m_VertexBufferDepth;
	ByteAddressBuffer m_IndexBufferDepth;
	uint32_t m_VertexStrideDepth;

	Vector4 m_SceneBoundingSphere;

	std::vector<bool> m_pMaterialIsCutout;

	virtual bool Load(const char* filename)
	{
		std::string filename_str(filename);
		if (filename_str.substr(filename_str.find_last_of(".")) == ".h3d")
		{
			return LoadDemoScene(filename);
		}
		else return LoadAssimpModel(filename);
	}

	const BoundingBox& GetBoundingBox() const
	{
		return m_Header.boundingBox;
	}

	const D3D12_CPU_DESCRIPTOR_HANDLE* GetSRVs(uint32_t materialIdx) const
	{
		return m_SRVs.data() + materialIdx * 3;
	}

	void ComputeSceneBoundingSphere()
	{
		Vector3 anchor[3] = { m_pMesh[0].boundingBox.min, 0, 0 };
		Vector3 center;
		float radius;
		float maxDist = 0;
		for (int k = 0; k < 3; k++)
		{
			for (unsigned int meshIndex = 0; meshIndex < m_Header.meshCount; meshIndex++)
			{
				const Mesh *mesh = m_pMesh + meshIndex;
				Vector3 verts[8] = {
				Vector3(mesh->boundingBox.min.GetX(), mesh->boundingBox.min.GetY(), mesh->boundingBox.min.GetZ()),
				Vector3(mesh->boundingBox.min.GetX(), mesh->boundingBox.min.GetY(), mesh->boundingBox.max.GetZ()),
				Vector3(mesh->boundingBox.min.GetX(), mesh->boundingBox.max.GetY(), mesh->boundingBox.min.GetZ()),
				Vector3(mesh->boundingBox.min.GetX(), mesh->boundingBox.max.GetY(), mesh->boundingBox.max.GetZ()),
				Vector3(mesh->boundingBox.max.GetX(), mesh->boundingBox.min.GetY(), mesh->boundingBox.min.GetZ()),
				Vector3(mesh->boundingBox.max.GetX(), mesh->boundingBox.min.GetY(), mesh->boundingBox.max.GetZ()),
				Vector3(mesh->boundingBox.max.GetX(), mesh->boundingBox.max.GetY(), mesh->boundingBox.min.GetZ()),
				Vector3(mesh->boundingBox.max.GetX(), mesh->boundingBox.max.GetY(), mesh->boundingBox.max.GetZ())
				};

				for (int j = 0; j < 8; j++)
				{
					float dist = Length(verts[j] - anchor[k]);
					if (dist > maxDist)
					{
						if (k < 2) {
							maxDist = dist;
							anchor[k + 1] = verts[j];
						}
						else {
							Vector3 extra = verts[j];
							center = center + 0.5*(dist - radius)*Normalize(extra - center);
							radius = 0.5*(dist + radius);
						}
					}
				}
			}
			if (k == 1)
			{
				center = (0.5f*(anchor[1] + anchor[2]));
				radius = 0.5f * Length(anchor[1] - anchor[2]);
			}
		}

		m_SceneBoundingSphere = Vector4(center, radius);
	}

	D3D12_CPU_DESCRIPTOR_HANDLE m_BlueNoiseSRV[3];

protected:

	bool LoadAssimpModel(const char *filename);
	bool LoadDemoScene(const char *filename);

	void ComputeMeshBoundingBox(unsigned int meshIndex, BoundingBox &bbox) const;
	void ComputeGlobalBoundingBox(BoundingBox &bbox) const;
	void ComputeAllBoundingBoxes();

	void LoadAssimpTextures(CPUModel& model);
	void LoadTextures();
	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> m_SRVs;
	std::vector<CPUTexture> cpuTexs;
};
