#pragma once
#include "VectorMath.h"
#include "GpuBuffer.h"

using namespace Math;

class Cube
{
public:
	Cube() {};
	~Cube() {};

	void Init()
	{
		uint32_t m_VertexStride = 16;
		indicesPerInstance = 14;
		float vertices[] = {
			-1,-1,1,1,
			1,-1,1,1,
			-1,-1,-1,1,
			1,-1,-1,1,
			-1,1,1,1,
			1,1,1,1,
			-1,1,-1,1,
			1,1,-1,1
		};
		__declspec(align(16)) unsigned int indices[] = { 3,2,7,6,4,2,0,3,1,7,5,4,1,0 };

		m_VertexBuffer.Create(L"CubeVertexBuffer", sizeof(vertices) / m_VertexStride, m_VertexStride, vertices);
		m_IndexBuffer.Create(L"CubeIndexBuffer", sizeof(indices) / sizeof(unsigned int), sizeof(unsigned int), indices);
	}

	StructuredBuffer m_VertexBuffer;
	ByteAddressBuffer m_IndexBuffer;
	unsigned indicesPerInstance;

};
