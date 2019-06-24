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
// Author:  Alex Nankervis
//

#include "ModelLoader.h"
#include "Utility.h"
#include "TextureManager.h"
#include "GraphicsCore.h"
#include "DescriptorHeap.h"
#include "CommandContext.h"
#include <iostream>

bool Model1::LoadAssimpModel(const char *filename)
{
	CPUModel cpuModel(filename);
	std::vector<CPUVertex> vertexArray;
	std::vector<unsigned int> indexArray;
	int numMeshes = cpuModel.meshes.size(); 

	m_pMaterial = new Material[numMeshes]; // in our model each mesh has its own material
	m_pMesh = new Mesh[numMeshes];
	int numVerticesTotal = 0;
	int numIndicesTotal = 0;
	m_Header.meshCount = numMeshes;
	m_Header.materialCount = numMeshes;
	m_pMaterialIsCutout.resize(m_Header.materialCount, false);

	for (int meshId = 0; meshId < numMeshes; meshId++)
	{
		Mesh mesh;
		m_pMaterialIsCutout[meshId] = true;
		mesh.vertexCount = cpuModel.meshes[meshId].vertices.size();
		numVerticesTotal += mesh.vertexCount;
		mesh.vertexDataByteOffset = vertexArray.size() * sizeof(CPUVertex);
		vertexArray.insert(vertexArray.end(), cpuModel.meshes[meshId].vertices.begin(), cpuModel.meshes[meshId].vertices.end());
		mesh.indexCount = cpuModel.meshes[meshId].indices.size();
		numIndicesTotal += mesh.indexCount;
		mesh.indexDataByteOffset = indexArray.size() * sizeof(unsigned int);
		indexArray.insert(indexArray.end(), cpuModel.meshes[meshId].indices.begin(), cpuModel.meshes[meshId].indices.end());
		mesh.vertexStride = sizeof(CPUVertex);
		mesh.materialIndex = meshId;		
		m_pMesh[meshId] = mesh;

		m_pMaterial[meshId].diffuse = cpuModel.meshes[meshId].matDiffuseColor.GetVector3();
		m_pMaterial[meshId].specular = cpuModel.meshes[meshId].matSpecularColor.GetVector3();
	}
	m_VertexStride = sizeof(CPUVertex);
	m_VertexBuffer.Create(L"VertexBuffer", numVerticesTotal, sizeof(CPUVertex), vertexArray.data());
	m_IndexBuffer.Create(L"IndexBuffer", numIndicesTotal, sizeof(unsigned int), indexArray.data());

	m_Header.vertexDataByteSize = numVerticesTotal * sizeof(CPUVertex);
	m_Header.indexDataByteSize = numIndicesTotal * sizeof(unsigned int);

	m_Header.boundingBox.min = Vector3(INFINITY, INFINITY, INFINITY);
	m_Header.boundingBox.max = Vector3(-INFINITY, -INFINITY, -INFINITY);
	for (int i = 0; i < cpuModel.meshes.size(); i++)
	{
		for (int j = 0; j < cpuModel.meshes[i].vertices.size(); j++)
		{
			if (cpuModel.meshes[i].vertices[j].Position.x < m_Header.boundingBox.min.GetX()) m_Header.boundingBox.min.SetX(cpuModel.meshes[i].vertices[j].Position.x);
			if (cpuModel.meshes[i].vertices[j].Position.y < m_Header.boundingBox.min.GetY()) m_Header.boundingBox.min.SetY(cpuModel.meshes[i].vertices[j].Position.y);
			if (cpuModel.meshes[i].vertices[j].Position.z < m_Header.boundingBox.min.GetZ()) m_Header.boundingBox.min.SetZ(cpuModel.meshes[i].vertices[j].Position.z);
			if (cpuModel.meshes[i].vertices[j].Position.x > m_Header.boundingBox.max.GetX()) m_Header.boundingBox.max.SetX(cpuModel.meshes[i].vertices[j].Position.x);
			if (cpuModel.meshes[i].vertices[j].Position.y > m_Header.boundingBox.max.GetY()) m_Header.boundingBox.max.SetY(cpuModel.meshes[i].vertices[j].Position.y);
			if (cpuModel.meshes[i].vertices[j].Position.z > m_Header.boundingBox.max.GetZ()) m_Header.boundingBox.max.SetZ(cpuModel.meshes[i].vertices[j].Position.z);
		}
	}

	LoadAssimpTextures(cpuModel);
	m_SceneBoundingSphere = 
		Vector4(cpuModel.scene_sphere_pos.x, cpuModel.scene_sphere_pos.y, cpuModel.scene_sphere_pos.z, cpuModel.scene_sphere_radius);

	indexSize = 4;

	return true;
}

bool Model1::LoadDemoScene(const char *filename)
{
	FILE *file = nullptr;
	if (0 != fopen_s(&file, filename, "rb"))
		return false;

	bool ok = false;

	fread(&m_Header, sizeof(Header), 1, file);

	m_Header.meshCount--;
	m_pMesh = new Mesh[m_Header.meshCount];
	m_pMaterial = new Material[m_Header.materialCount];

	Mesh unusedMesh;
	int unusedMeshIndex = 4;

	fread(m_pMesh, sizeof(Mesh) * unusedMeshIndex, 1, file);
	fread(&unusedMesh, sizeof(Mesh), 1, file);
	fread(m_pMesh+unusedMeshIndex, sizeof(Mesh) * (m_Header.meshCount - unusedMeshIndex), 1, file);

	fread(m_pMaterial, sizeof(Material) * m_Header.materialCount, 1, file);

	m_VertexStride = m_pMesh[0].vertexStride;
	m_VertexStrideDepth = m_pMesh[0].vertexStrideDepth;

	uint32_t vertexSkip = m_pMesh[unusedMeshIndex].vertexDataByteOffset - unusedMesh.vertexDataByteOffset;
	uint32_t indexSkip = m_pMesh[unusedMeshIndex].indexDataByteOffset - unusedMesh.indexDataByteOffset;
	uint32_t vertexDepthSkip = m_pMesh[unusedMeshIndex].vertexDataByteOffsetDepth - unusedMesh.vertexDataByteOffsetDepth;

	m_Header.vertexDataByteSize -= vertexSkip;
	m_Header.indexDataByteSize -= indexSkip;
	m_Header.vertexDataByteSizeDepth -= vertexDepthSkip;

	for (uint32_t meshIndex = unusedMeshIndex; meshIndex < m_Header.meshCount; ++meshIndex)
	{
		Mesh& mesh = m_pMesh[meshIndex];
		mesh.vertexDataByteOffset -= vertexSkip;
		mesh.indexDataByteOffset -= indexSkip;
		mesh.vertexDataByteOffsetDepth -= vertexDepthSkip;
	}

	m_pVertexData = new unsigned char[m_Header.vertexDataByteSize];
	m_pIndexData = new unsigned char[m_Header.indexDataByteSize];
	m_pVertexDataDepth = new unsigned char[m_Header.vertexDataByteSizeDepth];
	m_pIndexDataDepth = new unsigned char[m_Header.indexDataByteSize];

	fread(m_pVertexData, m_pMesh[unusedMeshIndex].vertexDataByteOffset, 1, file);
	fseek(file, vertexSkip, SEEK_CUR);
	fread(m_pVertexData+m_pMesh[unusedMeshIndex].vertexDataByteOffset, m_Header.vertexDataByteSize - m_pMesh[unusedMeshIndex].vertexDataByteOffset, 1, file);
	fread(m_pIndexData, m_pMesh[unusedMeshIndex].indexDataByteOffset, 1, file);
	fseek(file, indexSkip, SEEK_CUR);
	fread(m_pIndexData+m_pMesh[unusedMeshIndex].indexDataByteOffset, m_Header.indexDataByteSize - m_pMesh[unusedMeshIndex].indexDataByteOffset, 1, file);
		
	fread(m_pVertexDataDepth, m_pMesh[unusedMeshIndex].vertexDataByteOffsetDepth, 1, file);
	fseek(file, vertexDepthSkip, SEEK_CUR);
	fread(m_pVertexDataDepth+ m_pMesh[unusedMeshIndex].vertexDataByteOffsetDepth, m_Header.vertexDataByteSizeDepth - m_pMesh[unusedMeshIndex].vertexDataByteOffsetDepth, 1, file);
	fread(m_pIndexDataDepth, m_pMesh[unusedMeshIndex].indexDataByteOffset, 1, file);
	fseek(file, indexSkip, SEEK_CUR);
	fread(m_pIndexDataDepth+ m_pMesh[unusedMeshIndex].indexDataByteOffset, m_Header.indexDataByteSize - m_pMesh[unusedMeshIndex].indexDataByteOffset, 1, file);

	m_VertexBuffer.Create(L"VertexBuffer", m_Header.vertexDataByteSize / m_VertexStride, m_VertexStride, m_pVertexData);
	m_IndexBuffer.Create(L"IndexBuffer", m_Header.indexDataByteSize / sizeof(uint16_t), sizeof(uint16_t), m_pIndexData);

	m_VertexBufferDepth.Create(L"VertexBufferDepth", m_Header.vertexDataByteSizeDepth / m_VertexStrideDepth, m_VertexStrideDepth, m_pVertexDataDepth);
	m_IndexBufferDepth.Create(L"IndexBufferDepth", m_Header.indexDataByteSize / sizeof(uint16_t), sizeof(uint16_t), m_pIndexDataDepth);

	LoadTextures();

	cpumodel = new CPUModel();

	for (uint32_t meshIndex = 0; meshIndex < m_Header.meshCount; ++meshIndex)
	{
		Mesh& mesh = m_pMesh[meshIndex];
		m_pMaterial[mesh.materialIndex].diffuse = Vector3(1);
	}

	ComputeSceneBoundingSphere();

	m_pMaterialIsCutout.resize(m_Header.materialCount);

	for (uint32_t i = 0; i < m_Header.materialCount; ++i)
	{
		const Model1::Material& mat = m_pMaterial[i];
		if (std::string(mat.texDiffusePath).find("thorn") != std::string::npos ||
			std::string(mat.texDiffusePath).find("plant") != std::string::npos ||
			std::string(mat.texDiffusePath).find("chain") != std::string::npos)
		{
			m_pMaterialIsCutout[i] = true;
		}
		else
		{
			m_pMaterialIsCutout[i] = false;
		}
	}

	indexSize = 2;

	ok = true;

h3d_load_fail:

	if (EOF == fclose(file))
		ok = false;

	return ok;
}

void Model1::LoadAssimpTextures(CPUModel& model)
{
	m_SRVs.resize(m_Header.materialCount * 3);
	cpuTexs.resize(m_Header.materialCount * 3);
	const Texture* MatTextures[3] = {};
	bool hasTexType[3] = { false, false, false };

	for (uint32_t materialIdx = 0; materialIdx < m_Header.materialCount; ++materialIdx)
	{
		hasTexType[0] = false;
		hasTexType[1] = false;
		hasTexType[2] = false;

		const Material& pMaterial = m_pMaterial[materialIdx];

		for (CPUTexture tex : model.meshes[materialIdx].textures)
		{
			int idx = tex.type == "texture_diffuse" ? 0 : tex.type == "texture_specular" ? 1 : 2;
			hasTexType[idx] = true;
			
			const ManagedTexture* temp = TextureManager::LoadFromRawData(tex.path, tex.width, tex.height, tex.data);
			m_SRVs[materialIdx * 3 + idx] = temp->GetSRV();
		}

		for (int idx = 0; idx < 3; idx++)
		{
			if (!hasTexType[idx])
			{
				std::string suffix = idx == 0 ? "" : idx == 1 ? "_specular" : "_normal";
				const Texture* temp = TextureManager::LoadFromFile("default" + suffix, idx != 2);
				m_SRVs[materialIdx * 3 + idx] = temp->GetSRV();
			}
		}
	}

	// load blue noise (we can get four independent random numbers from two 2D blue noise texture)
	MatTextures[0] = TextureManager::LoadDDSFromFile("LDR_RGBA_0.DDS", false);
	m_BlueNoiseSRV[0] = MatTextures[0]->GetSRV();
	MatTextures[1] = TextureManager::LoadDDSFromFile("LDR_RGBA_1.DDS", false);
	m_BlueNoiseSRV[1] = MatTextures[1]->GetSRV();
	MatTextures[2] = TextureManager::LoadDDSFromFile("LDR_RGBA_2.DDS", false);
	m_BlueNoiseSRV[2] = MatTextures[2]->GetSRV();
}

void Model1::LoadTextures()
{
	m_SRVs.resize(m_Header.materialCount * 3);

	const ManagedTexture* MatTextures[3] = {};

	for (uint32_t materialIdx = 0; materialIdx < m_Header.materialCount; ++materialIdx)
	{
		const Material& pMaterial = m_pMaterial[materialIdx];

		// Load diffuse
		MatTextures[0] = TextureManager::LoadFromFile(pMaterial.texDiffusePath, true);
		if (!MatTextures[0]->IsValid())
		{
			MatTextures[0] = TextureManager::LoadFromFile("default", true);
		}

		// Load specular
		MatTextures[1] = TextureManager::LoadFromFile(pMaterial.texSpecularPath, true);
		if (!MatTextures[1]->IsValid())
		{
			MatTextures[1] = TextureManager::LoadFromFile(std::string(pMaterial.texDiffusePath) + "_specular", true);
			if (!MatTextures[1]->IsValid())
			{
				MatTextures[1] = TextureManager::LoadFromFile("default_specular", true);
			}
		}

		// Load normal
		MatTextures[2] = TextureManager::LoadFromFile(pMaterial.texNormalPath, false);
		if (!MatTextures[2]->IsValid())
		{
			MatTextures[2] = TextureManager::LoadFromFile(std::string(pMaterial.texDiffusePath) + "_normal", false);
			if (!MatTextures[2]->IsValid())
			{
				MatTextures[2] = TextureManager::LoadFromFile("default_normal", false);
			}
		}		

		m_SRVs[materialIdx * 3 + 0] = MatTextures[0]->GetSRV();
		m_SRVs[materialIdx * 3 + 1] = MatTextures[1]->GetSRV();
		m_SRVs[materialIdx * 3 + 2] = MatTextures[2]->GetSRV();
	}

	// load blue noise (we can get four independent random numbers from two 2D blue noise texture)
	MatTextures[0] = TextureManager::LoadDDSFromFile("LDR_RGBA_0.DDS", false);
	m_BlueNoiseSRV[0] = MatTextures[0]->GetSRV();
	MatTextures[1] = TextureManager::LoadDDSFromFile("LDR_RGBA_1.DDS", false);
	m_BlueNoiseSRV[1] = MatTextures[1]->GetSRV();
	MatTextures[2] = TextureManager::LoadDDSFromFile("LDR_RGBA_2.DDS", false);
	m_BlueNoiseSRV[2] = MatTextures[2]->GetSRV();
}

Model1::Model1()
	: m_pMesh(nullptr)
	, m_pMaterial(nullptr)
	, m_pVertexData(nullptr)
	, m_pIndexData(nullptr)
	, m_pVertexDataDepth(nullptr)
	, m_pIndexDataDepth(nullptr)
	, m_modelMatrix(kIdentity)
{
	Clear();
}

Model1::~Model1()
{
	Clear();
}

void Model1::Clear()
{
	m_VertexBuffer.Destroy();
	m_IndexBuffer.Destroy();
	m_VertexBufferDepth.Destroy();
	m_IndexBufferDepth.Destroy();

	delete[] m_pMesh;
	m_pMesh = nullptr;
	m_Header.meshCount = 0;

	delete[] m_pMaterial;
	m_pMaterial = nullptr;
	m_Header.materialCount = 0;

	delete[] m_pVertexData;
	delete[] m_pIndexData;
	delete[] m_pVertexDataDepth;
	delete[] m_pIndexDataDepth;

	m_pVertexData = nullptr;
	m_Header.vertexDataByteSize = 0;
	m_pIndexData = nullptr;
	m_Header.indexDataByteSize = 0;
	m_pVertexDataDepth = nullptr;
	m_Header.vertexDataByteSizeDepth = 0;
	m_pIndexDataDepth = nullptr;

	m_Header.boundingBox.min = Vector3(0.0f);
	m_Header.boundingBox.max = Vector3(0.0f);
}

// assuming at least 3 floats for position
void Model1::ComputeMeshBoundingBox(unsigned int meshIndex, BoundingBox &bbox) const
{
	const Mesh *mesh = m_pMesh + meshIndex;

	if (mesh->vertexCount > 0)
	{
		unsigned int vertexStride = mesh->vertexStride;

		const float *p = (float*)(m_pVertexData + mesh->vertexDataByteOffset + mesh->attrib[attrib_position].offset);
		const float *pEnd = (float*)(m_pVertexData + mesh->vertexDataByteOffset + mesh->vertexCount * mesh->vertexStride + mesh->attrib[attrib_position].offset);
		bbox.min = Scalar(FLT_MAX);
		bbox.max = Scalar(-FLT_MAX);

		while (p < pEnd)
		{
			Vector3 pos(*(p + 0), *(p + 1), *(p + 2));

			bbox.min = Min(bbox.min, pos);
			bbox.max = Max(bbox.max, pos);

			(*(uint8_t**)&p) += vertexStride;
		}
	}
	else
	{
		bbox.min = Scalar(0.0f);
		bbox.max = Scalar(0.0f);
	}
}

void Model1::ComputeGlobalBoundingBox(BoundingBox &bbox) const
{
	if (m_Header.meshCount > 0)
	{
		bbox.min = Scalar(FLT_MAX);
		bbox.max = Scalar(-FLT_MAX);
		for (unsigned int meshIndex = 0; meshIndex < m_Header.meshCount; meshIndex++)
		{
			const Mesh *mesh = m_pMesh + meshIndex;

			bbox.min = Min(bbox.min, mesh->boundingBox.min);
			bbox.max = Max(bbox.max, mesh->boundingBox.max);
		}
	}
	else
	{
		bbox.min = Scalar(0.0f);
		bbox.max = Scalar(0.0f);
	}
}

void Model1::ComputeAllBoundingBoxes()
{
	for (unsigned int meshIndex = 0; meshIndex < m_Header.meshCount; meshIndex++)
	{
		Mesh *mesh = m_pMesh + meshIndex;
		ComputeMeshBoundingBox(meshIndex, mesh->boundingBox);
	}
	ComputeGlobalBoundingBox(m_Header.boundingBox);
}
