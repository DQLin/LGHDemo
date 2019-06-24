// NOTE: Some part of this file is modified from model.h in the LearnOpenGL.com code repository (Author: Joey de Vries)
// which is licensed under the CC BY-NC 4.0 license

#pragma once
#include <glm/glm.hpp>
#include "CPUColor.h"
#include "ImageIO.h"
#include <iostream>
#include <vector>
#include <string>

#define DIFFUSETEX 0
#define SPECULARTEX 1
#define NORMALTEX 2

struct CPUVertex {
	glm::vec3 Position;
	glm::vec2 TexCoords;
	glm::vec3 Normal;
	glm::vec3 Tangent;
	glm::vec3 Bitangent;

	CPUVertex() {};
	CPUVertex(glm::vec3 position, glm::vec2 texcoords, glm::vec3 normal, glm::vec3 tangent, glm::vec3 bitangent)
		: Position(position), TexCoords(texcoords), Normal(normal), Tangent(tangent), Bitangent(bitangent) {};

	static void ConvertFromHalfFloatVertexChunk(std::vector<CPUVertex>& output, unsigned char* m_pVertexData, int numVerts)
	{
		unsigned char* head = m_pVertexData;
		for (int j = 0; j < numVerts; j++)
		{
			CPUVertex cur;

			memcpy(&cur.Position, head, 4 * 3);
			head += 4 * 3;

			memcpy(&cur.TexCoords, head, 4 * 2);
			head += 4 * 2;

			memcpy(&cur.Normal, head, 4 * 3); //NORMAL
			head += 4 * 3;

			memcpy(&cur.Tangent, head, 4 * 3); //TANGENT
			head += 4 * 3;

			memcpy(&cur.Bitangent, head, 4 * 3); //BITANGENT
			head += 4 * 3;

			output[j] = cur;
		}	
	}
};

struct CPUFace {
	CPUFace(CPUVertex v0, CPUVertex v1, CPUVertex v2) : v0(v0), v1(v1), v2(v2) {};
	CPUVertex v0, v1, v2;

	glm::vec2 getuv(float u, float v) //u, v are barycentric coordinates 
	{
		return (1 - u - v) * v0.TexCoords + u * v1.TexCoords + v * v2.TexCoords;
	}

	glm::vec3 getNormal(float u, float v) //u, v are barycentric coordinates 
	{
		return (1 - u - v) * v0.Normal + u * v1.Normal + v * v2.Normal;
	}

	glm::vec3 getBitangent(float u, float v) //u, v are barycentric coordinates 
	{
		return (1 - u - v) * v0.Bitangent + u * v1.Bitangent + v * v2.Bitangent;
	}

	glm::vec3 getTangent(float u, float v) //u, v are barycentric coordinates 
	{
		return (1 - u - v) * v0.Tangent + u * v1.Tangent + v * v2.Tangent;
	}

	glm::vec3 getPosition(float u, float v) //u, v are barycentric coordinates 
	{
		return (1 - u - v) * v0.Position + u * v1.Position + v * v2.Position;
	}
};

class CPUTexture {
public:
	int width, height, nrComponents;
	std::string path;
	std::string type;

	CPUTexture() {};
	CPUTexture(unsigned char* data, int width, int height, int nrComponents) : data(data), width(width), height(height), nrComponents(nrComponents) {};

	CPUTexture(std::string filename, const std::string &directory, bool ignoreGamma = false, const std::string& type = "") : type(type)
	{
		filename = directory + "/" + filename;
		path = filename;
		ImageIO::ReadImageFile(filename.c_str(), &data, &width, &height, &nrComponents, ignoreGamma, true);
	}
	glm::vec2 TileClamp(const glm::vec2 &uv)
	{
		glm::vec2 u;
		u.x = uv.x - (int)uv.x;
		u.y = uv.y - (int)uv.y;
		if (u.x < 0) u.x += 1;
		if (u.y < 0) u.y += 1;
		u.y = 1 - u.y;
		return u;
	}

	CPUColor4 toColor(int offset)
	{
		offset = nrComponents * offset;
		return CPUColor4(data[offset] / 255.0, data[offset + 1] / 255.0, data[offset + 2] / 255.0, data[offset + 3] / 255.0);
	}

	CPUColor toColor3(int offset)
	{
		offset = nrComponents * offset;
		return CPUColor(data[offset] / 255.0, data[offset + 1] / 255.0, data[offset + 2] / 255.0);
	}

	CPUColor4 Sample(const glm::vec2 &uv)
	{
		if (width + height == 0) return CPUColor4(0, 0, 0, 0);

		glm::vec2 u = TileClamp(uv);
		float x = width * u.x;
		float y = height * u.y;
		int ix = (int)x;
		int iy = (int)y;
		float fx = x - ix;
		float fy = y - iy;

		if (ix < 0) ix -= (ix / width - 1)*width;
		if (ix >= width) ix -= (ix / width)*width;
		int ixp = ix + 1;
		if (ixp >= width) ixp -= width;

		if (iy < 0) iy -= (iy / height - 1)*height;
		if (iy >= height) iy -= (iy / height)*height;
		int iyp = iy + 1;
		if (iyp >= height) iyp -= height;

		//magnification filtering
		return	toColor(iy *width + ix) * ((1 - fx)*(1 - fy)) +
			toColor(iy *width + ixp) * (fx *(1 - fy)) +
			toColor(iyp*width + ix) * ((1 - fx)*   fy) +
			toColor(iyp*width + ixp) * (fx *   fy);
	}

	CPUColor SampleColor3(const glm::vec2 &uv)
	{
		if (width + height == 0) return CPUColor(0, 0, 0);

		glm::vec2 u = TileClamp(uv);
		float x = width * u.x;
		float y = height * u.y;
		int ix = (int)x;
		int iy = (int)y;
		float fx = x - ix;
		float fy = y - iy;

		if (ix < 0) ix -= (ix / width - 1)*width;
		if (ix >= width) ix -= (ix / width)*width;
		int ixp = ix + 1;
		if (ixp >= width) ixp -= width;

		if (iy < 0) iy -= (iy / height - 1)*height;
		if (iy >= height) iy -= (iy / height)*height;
		int iyp = iy + 1;
		if (iyp >= height) iyp -= height;

		//magnification filtering
		return	toColor3(iy *width + ix) * ((1 - fx)*(1 - fy)) +
			toColor3(iy *width + ixp) * (fx *(1 - fy)) +
			toColor3(iyp*width + ix) * ((1 - fx)*   fy) +
			toColor3(iyp*width + ixp) * (fx *   fy);
	}

	unsigned char* data;
};



class CPUMesh {
public:
	std::vector<CPUVertex> vertices;
	std::vector<unsigned int> indices;
	std::vector<CPUTexture> textures;
	CPUColor matDiffuseColor;
	CPUColor matSpecularColor;
	CPUMesh() {}

	CPUMesh(std::vector<CPUVertex> vertices, std::vector<unsigned int> indices, std::vector<CPUTexture> textures, CPUColor matDiffuseColor, CPUColor matSpecularColor)
	{
		this->vertices = vertices;
		this->indices = indices;
		this->textures = textures;
		this->matDiffuseColor = matDiffuseColor;
		this->matSpecularColor = matSpecularColor;
	}

	CPUFace getFace(int primID)
	{
		return CPUFace(vertices[indices[3 * primID]], vertices[indices[3 * primID + 1]], vertices[indices[3 * primID + 2]]);
	}
};

class CPUModel
{
public:

	CPUModel() {};

	std::vector<CPUTexture> textures_loaded;
	std::vector<CPUMesh> meshes;
	std::string directory;
	bool gammaCorrection;
	glm::vec3 scene_sphere_pos;
	float scene_sphere_radius;

	CPUModel(std::string const &path, bool gamma = false) : gammaCorrection(gamma)
	{
		loadModel(path);
		computeRitterBoundingSphere();
	}

	void computeRitterBoundingSphere()
	{
		glm::vec3 x = meshes[0].vertices[0].Position;
		float maxDist = 0, yi = 0, yj = 0, zi = 0, zj = 0;
		for (int i = 0; i < meshes.size(); i++)
		{
			for (int j = 0; j < meshes[i].vertices.size(); j++)
			{
				float dist = length(meshes[i].vertices[j].Position - x);
				if (dist > maxDist)
				{
					maxDist = dist;
					yi = i;
					yj = j;
				}
			}
		}
		maxDist = 0;
		glm::vec3 y = meshes[yi].vertices[yj].Position;
		for (int i = 0; i < meshes.size(); i++)
		{
			for (int j = 0; j < meshes[i].vertices.size(); j++)
			{
				float dist = length(meshes[i].vertices[j].Position - y);
				if (dist > maxDist)
				{
					maxDist = dist;
					zi = i;
					zj = j;
				}
			}
		}

		glm::vec3 z = meshes[zi].vertices[zj].Position;
		glm::vec3 center(0.5f*(y + z));
		float radius = 0.5f * length(y - z);
		for (int i = 0; i < meshes.size(); i++)
		{
			for (int j = 0; j < meshes[i].vertices.size(); j++)
			{
				float dist = length(meshes[i].vertices[j].Position - center);
				if (dist > radius)
				{
					glm::vec3 extra = meshes[i].vertices[j].Position;
					center = center + 0.5f*(dist - radius)*normalize(extra - center);
					radius = 0.5*(dist + radius);
				}
			}
		}
		scene_sphere_pos = center;
		scene_sphere_radius = radius;
	}
	/*  Functions   */
// loads a model with supported ASSIMP extensions from file and stores the resulting meshes in the meshes vector.
	void loadModel(std::string const &path)
	{
		// read file via ASSIMP
		Assimp::Importer importer;
		const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_CalcTangentSpace);
		// check for errors
		if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) // if is Not Zero
		{
			std::cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << std::endl;
			return;
		}
		// retrieve the directory path of the filepath
		directory = path.substr(0, path.find_last_of('/'));

		// process ASSIMP's root node recursively
		processNode(scene->mRootNode, scene);
	}

	// processes a node in a recursive fashion. Processes each individual mesh located at the node and repeats this process on its children nodes (if any).
	void processNode(aiNode *node, const aiScene *scene)
	{
		// process each mesh located at the current node
		int offset = 0;
		for (unsigned int i = 0; i < node->mNumMeshes; i++)
		{
			// the node object only contains indices to index the actual objects in the scene. 
			// the scene contains all the data, node is just to keep stuff organized (like relations between nodes).
			aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
			meshes.push_back(processMesh(mesh, scene, offset));
			offset += meshes[i].indices.size() / 3;
		}
		// after we've processed all of the meshes (if any) we then recursively process each of the children nodes
		for (unsigned int i = 0; i < node->mNumChildren; i++)
		{
			processNode(node->mChildren[i], scene);
		}
	}

	CPUMesh processMesh(aiMesh *mesh, const aiScene *scene, int primOffset)
	{
		// data to fill
		std::vector<CPUVertex> vertices;
		std::vector<unsigned int> indices;
		std::vector<CPUTexture> textures;

		// Walk through each of the mesh's vertices
		for (unsigned int i = 0; i < mesh->mNumVertices; i++)
		{
			CPUVertex vertex;
			glm::vec3 vector; // we declare a placeholder vector since assimp uses its own vector class that doesn't directly convert to glm's vec3 class so we transfer the data to this placeholder glm::vec3 first.
							  // positions
			vector.x = mesh->mVertices[i].x;
			vector.y = mesh->mVertices[i].y;
			vector.z = mesh->mVertices[i].z;
			vertex.Position = vector;
			// normals
			vector.x = mesh->mNormals[i].x;
			vector.y = mesh->mNormals[i].y;
			vector.z = mesh->mNormals[i].z;
			vertex.Normal = vector;
			// texture coordinates
			if (mesh->mTextureCoords[0]) // does the mesh contain texture coordinates?
			{
				glm::vec2 vec;
				// a vertex can contain up to 8 different texture coordinates. We thus make the assumption that we won't 
				// use models where a vertex can have multiple texture coordinates so we always take the first set (0).
				vec.x = mesh->mTextureCoords[0][i].x;
				vec.y = mesh->mTextureCoords[0][i].y;
				vertex.TexCoords = vec;
			}
			else
				vertex.TexCoords = glm::vec2(0.0f, 0.0f);
			// tangent
			if (mesh->mTangents)
			{
				vector.x = mesh->mTangents[i].x;
				vector.y = mesh->mTangents[i].y;
				vector.z = mesh->mTangents[i].z;
				vertex.Tangent = vector;
				// bitangent
				vector.x = mesh->mBitangents[i].x;
				vector.y = mesh->mBitangents[i].y;
				vector.z = mesh->mBitangents[i].z;
				vertex.Bitangent = vector;
			}
			vertices.push_back(vertex);
		}

		for (unsigned int i = 0; i < mesh->mNumFaces; i++)
		{
			aiFace face = mesh->mFaces[i];
			// retrieve all indices of the face and store them in the indices vector
			for (unsigned int j = 0; j < face.mNumIndices; j++)
				indices.push_back(face.mIndices[j]);
		}
		// process materials
		aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];

		// 1. diffuse maps
		std::vector<CPUTexture> diffuseMaps = loadMaterialTextures(material, aiTextureType_DIFFUSE, "texture_diffuse");
		textures.insert(textures.end(), diffuseMaps.begin(), diffuseMaps.end());
		// 2. specular maps
		std::vector<CPUTexture> specularMaps = loadMaterialTextures(material, aiTextureType_SPECULAR, "texture_specular");
		textures.insert(textures.end(), specularMaps.begin(), specularMaps.end());
		// 3. normal maps
		std::vector<CPUTexture> normalMaps = loadMaterialTextures(material, aiTextureType_NORMALS, "texture_normals");
		textures.insert(textures.end(), normalMaps.begin(), normalMaps.end());

		aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];
		aiColor3D diffuse(0.f, 0.f, 0.f), specular(0.f, 0.f, 0.f);
		mat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse);
		mat->Get(AI_MATKEY_COLOR_SPECULAR, specular);
		// return a mesh object created from the extracted mesh data

		return CPUMesh(vertices, indices, textures, CPUColor(diffuse), CPUColor(specular));
	}

	// checks all material textures of a given type and loads the textures if they're not loaded yet.
	// the required info is returned as a Texture struct.
	std::vector<CPUTexture> loadMaterialTextures(aiMaterial *mat, aiTextureType type, std::string typeName)
	{
		std::vector<CPUTexture> textures;
		for (unsigned int i = 0; i < mat->GetTextureCount(type); i++)
		{
			aiString str;
			mat->GetTexture(type, i, &str);
			// check if texture was loaded before and if so, continue to next iteration: skip loading a new texture
			bool skip = false;
			for (unsigned int j = 0; j < textures_loaded.size(); j++)
			{
				if (std::strcmp(textures_loaded[j].path.c_str(), str.C_Str()) == 0)
				{
					textures.push_back(textures_loaded[j]);
					skip = true; // a texture with the same filepath has already been loaded, continue to next one. (optimization)
					break;
				}
			}
			if (!skip)
			{   // if texture hasn't been loaded already, load it
				CPUTexture texture(std::string(str.C_Str()), this->directory, false, typeName);
				textures.push_back(texture);
				textures_loaded.push_back(texture);  // store it as texture loaded for entire model, to ensure we won't unnecesery load duplicate textures.
			}
		}
		return textures;
	}
};
