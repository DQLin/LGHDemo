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

#pragma once
#include "GpuBuffer.h"
#include "CommandContext.h"
#include "ReadbackBuffer.h"
#include "BitonicSort.h"
#include "CellNormalizeCS.h"
#include "ClearVPLBufferCS.h"
#include "VplSplatToVertexCS.h"
#include "VplPreScanCS.h"
#include "VplScan1CS.h"
#include "VplScan2CS.h"
#include "VplScan3CS.h"
#include "VplCompactionCS.h"
#include "GenVPLIndexKeyListCS.h"
#include "ReorderVplsCS.h"
#include "VertGatherCS.h"
#include "VertGatherHigh_1CS.h"
#include "VertGatherHigh_2CS.h"
#include "MergeLevelsCS.h"
#include "FindVPLBboxMaxCS.h"
#include "FindVPLBboxMinCS.h"
#include "MergeLevelsInterleaveCS.h"
#include <iostream>

//change this if you want to generate more than 10M vpls
constexpr unsigned int MAXIMUM_NUM_VPLS = 11000000; 

#define MAX_BLOCK_SIZE 512
#define MAX_ITER 4


using namespace Math;
class LGHBuilder
{
public:

	LGHBuilder() {};

	int CalculateNumLevels(int numVPLs)
	{
		int numEstDenseVPLs = numVPLs * 10;
		int initEst = 8;
		for (int i = 2; i <= 10; i++)
		{
			int dimLen = 1 << (i - 2) + 1;
			if (dimLen*dimLen*dimLen > numEstDenseVPLs)
			{
				return i;
			}
		}
		std::cout << "number of VPLs exceeds LGH capacity!\n" << std::endl;
		exit(1);
	}

	void Init(ComputeContext& cptContext, int _numVPLs, std::vector<StructuredBuffer>& _VPLs, 
										bool _isLevelZeroIncluded = false, bool isReinit = false, int _firstHighLevel = 5)
	{
		lastBuildSourceOption = (BuildSourceOptions)((int)m_BuildSource);
		lastNumInstances = 0;
		assert(_VPLs.size() == 3);
		VPLs = _VPLs;
		numVPLs = _numVPLs;
		isLevelZeroIncluded = _isLevelZeroIncluded;
		int numLevels = CalculateNumLevels(numVPLs);

		highestLevel = numLevels - 1;
		firstHighLevel = _firstHighLevel;
		levelSizes.resize(numLevels);
		levelSizes[0] = numVPLs;
		vplAttribs[0] = VPLs[0].GetSRV();
		vplAttribs[1] = VPLs[1].GetSRV();
		vplAttribs[2] = VPLs[2].GetSRV();

		VPLBuffersAtLevel.resize(numLevels);
		VPLScratchBuffersAtLevel.resize(numLevels);
		VPLTaskBuffersAtHighLevel.resize(numLevels - firstHighLevel);
		VPLAddressBuffersAtLevel.resize(numLevels);
		CellStartIdAtLevel.resize(numLevels);
		CellEndIdAtLevel.resize(numLevels);

		SortedVPLs.resize(3);
		SortedVPLs[0].Create(L"Sorted VPL Position List", 1.5 * numVPLs, sizeof(Vector3));
		SortedVPLs[1].Create(L"Sorted VPL Normal List", 1.5 * numVPLs, sizeof(Vector3));
		SortedVPLs[2].Create(L"Sorted VPL Color List", 1.5 * numVPLs, sizeof(Vector3));
		IndexKeyList.Create(L"GPU Sort List", 1.2*numVPLs, sizeof(uint64_t));
		for (int level = 1; level < numLevels; level++)
		{
			VPLBuffersAtLevel[level].resize(4);
			VPLScratchBuffersAtLevel[level].resize(5); //additionally with weight
			int numVerts = (1 << (highestLevel - level)) + 1;
			numVerts = numVerts * numVerts * numVerts;
			VPLScratchBuffersAtLevel[level][POSITION].Create(L"A VPLScratchBuffersAtLevel", numVerts, sizeof(Vector3));
			VPLScratchBuffersAtLevel[level][NORMAL].Create(L"A VPLScratchBuffersAtLevel", numVerts, sizeof(Vector3));
			VPLScratchBuffersAtLevel[level][COLOR].Create(L"A VPLScratchBuffersAtLevel", numVerts, sizeof(Vector3));
			VPLScratchBuffersAtLevel[level][STDEV].Create(L"A VPLScratchBuffersAtLevel", numVerts, sizeof(Vector3));
			VPLScratchBuffersAtLevel[level][WEIGHT].Create(L"A VPLScratchBuffersAtLevel", numVerts, sizeof(float));

			VPLBuffersAtLevel[level][POSITION].Create(L"A VPLBuffersAtLevel", numVerts, sizeof(Vector3));
			VPLBuffersAtLevel[level][NORMAL].Create(L"A VPLBuffersAtLevel", numVerts, sizeof(Vector3));
			VPLBuffersAtLevel[level][COLOR].Create(L"A VPLBuffersAtLevel", numVerts, sizeof(Vector3));
			VPLBuffersAtLevel[level][STDEV].Create(L"A VPLBuffersAtLevel", numVerts, sizeof(Vector3));

			VPLAddressBuffersAtLevel[level].resize(2);
			VPLAddressBuffersAtLevel[level][0].Create(L"A VPLAddressBufferAtLevel", numVerts, sizeof(int));
			VPLAddressBuffersAtLevel[level][1].Create(L"A VPLAddressBufferAtLevel", numVerts, sizeof(int));

			int numCells = (1 << (highestLevel - level));
			numCells = numCells * numCells * numCells;
			CellStartIdAtLevel[level].Create(L"A CellStartIdAtLevel", numCells, sizeof(int));
			CellEndIdAtLevel[level].Create(L"A CellEndIdAtLevel", numCells, sizeof(int));
			if (level >= firstHighLevel)
			{
				int taskDivRate = 8;
				if (level == highestLevel) taskDivRate = 16;
				int numTasksPerVertex = taskDivRate * taskDivRate * taskDivRate;

				VPLTaskBuffersAtHighLevel[level - firstHighLevel].resize(5);
				VPLTaskBuffersAtHighLevel[level - firstHighLevel][POSITION].Create(L"task buffer position", numTasksPerVertex * numVerts, sizeof(Vector3));
				VPLTaskBuffersAtHighLevel[level - firstHighLevel][NORMAL].Create(L"task buffer normal", numTasksPerVertex * numVerts, sizeof(Vector3));
				VPLTaskBuffersAtHighLevel[level - firstHighLevel][COLOR].Create(L"task buffer color", numTasksPerVertex * numVerts, sizeof(Vector3));
				VPLTaskBuffersAtHighLevel[level - firstHighLevel][STDEV].Create(L"task buffer stdev", numTasksPerVertex * numVerts, sizeof(Vector3));
				VPLTaskBuffersAtHighLevel[level - firstHighLevel][WEIGHT].Create(L"task buffer weight", numTasksPerVertex * numVerts, sizeof(float));
			}
		}

		int numLevelOneVerts = (1 << (highestLevel - 1)) + 1;
		VertLockBuffers.resize(5);
		VertLockBuffers[0].Create(L"vert lock buffer", numLevelOneVerts, sizeof(UINT));
		VertLockBuffers[1].Create(L"vert lock buffer", numLevelOneVerts, sizeof(UINT));
		VertLockBuffers[2].Create(L"vert lock buffer", numLevelOneVerts, sizeof(UINT));
		VertLockBuffers[3].Create(L"vert lock buffer", numLevelOneVerts, sizeof(UINT));
		VertLockBuffers[4].Create(L"vert lock buffer", numLevelOneVerts, sizeof(UINT));

		int numBboxGroups = (MAXIMUM_NUM_VPLS + 1023) / 1024;
		bboxReductionBuffer[0].Create(L"Reductionbuffer", numBboxGroups, sizeof(Vector3));
		bboxReductionBuffer[1].Create(L"Reductionbuffer", numBboxGroups, sizeof(Vector3));

		if (!isReinit)
		{
			RootSig.Reset(3);
			RootSig[0].InitAsConstantBuffer(0);
			RootSig[1].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 10);
			RootSig[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 7);
			RootSig.Finalize(L"GPU Lighting Grid");

	#define CreatePSO( ObjName, ShaderByteCode ) \
			ObjName.SetRootSignature(RootSig); \
			ObjName.SetComputeShader(ShaderByteCode, sizeof(ShaderByteCode) ); \
			ObjName.Finalize();

			CreatePSO(m_CellNormalizePSO, g_pCellNormalizeCS);
			CreatePSO(m_ClearVPLBufferPSO, g_pClearVPLBufferCS);
			CreatePSO(m_VplSplatToVertexPSO, g_pVplSplatToVertexCS);
			CreatePSO(m_VplCompactionPSO, g_pVplCompactionCS);
			CreatePSO(m_VplScan1PSO, g_pVplScan1CS);
			CreatePSO(m_VplScan2PSO, g_pVplScan2CS);
			CreatePSO(m_VplScan3PSO, g_pVplScan3CS);
			CreatePSO(m_VplPreScanPSO, g_pVplPreScanCS);
			CreatePSO(m_GenVPLIndexKeyListPSO, g_pGenVPLIndexKeyListCS);
			CreatePSO(m_ReorderVplsPSO, g_pReorderVplsCS);
			CreatePSO(m_VertGatherPSO, g_pVertGatherCS);
			CreatePSO(m_VertGatherHigh_1PSO, g_pVertGatherHigh_1CS);
			CreatePSO(m_VertGatherHigh_2PSO, g_pVertGatherHigh_2CS);
			CreatePSO(m_MergeLevelsPSO, g_pMergeLevelsCS);
			CreatePSO(m_MergeLevelsInterleavePSO, g_pMergeLevelsInterleaveCS);
			CreatePSO(m_FindVPLBboxMinPSO, g_pFindVPLBboxMinCS);
			CreatePSO(m_FindVPLBboxMaxPSO, g_pFindVPLBboxMaxCS);
		}
	};

	bool CheckUpdate(ComputeContext& cptContext, int interleaveRate, bool vplsUpdated, bool drawLevelsChanged)
	{
		if (vplsUpdated || m_BuildSource != lastBuildSourceOption)
		{
			lastBuildSourceOption = (BuildSourceOptions)(int)m_BuildSource;
			Build(cptContext, interleaveRate);
			return true;
		}
		else if (drawLevelsChanged || interleaveRate != lastInterleaveRate) //re-merge
		{
			if (drawLevelsChanged) isLevelZeroIncluded = !isLevelZeroIncluded;
			if (interleaveRate != lastInterleaveRate) { lastInterleaveRate = interleaveRate; };

			if (interleaveRate > 1) MergeLevelsInterleave(cptContext, interleaveRate, isLevelZeroIncluded, true); //TODO: add buffer size dynamic adaption
			else MergeLevels(cptContext, isLevelZeroIncluded, true);
			return true;
		}
		else return false;
	}

	void Build(ComputeContext& cptContext, int interleavedRate = 1)
	{
		ScopedTimer _p0(L"Build LGH", cptContext);

		FindBoundingBox(cptContext);

		SplatForLevel(1, cptContext, m_BuildSource == buildFromS1);

		for (int i = 2; i <= firstHighLevel - 1; i++)
		{
			if (m_BuildSource == buildFromVPLs)
				SplatForLevel(i, cptContext);
			else
				GatherForLevel(i, cptContext);
		}

		for (int i = firstHighLevel; i <= highestLevel; i++)
		{
			if (m_BuildSource == buildFromVPLs)
				SplatForLevel(i, cptContext);
			else
				GatherForHighLevel(i, cptContext);
		}

		if (interleavedRate > 1) MergeLevelsInterleave(cptContext, interleavedRate, isLevelZeroIncluded);
		else MergeLevels(cptContext, isLevelZeroIncluded);

		lastInterleaveRate = interleavedRate;
	}

	// create lighting grid
	enum VPLAttributes
	{
		POSITION = 0,
		NORMAL,
		COLOR,
		STDEV,
		WEIGHT
	};

	int numVPLs;
	int highestLevel;
	int firstHighLevel;
	float highestCellSize;
	float baseRadius;
	Vector3 lgh_corner;

	std::vector<StructuredBuffer> InstanceBuffers;
	std::vector<int> levelSizes;
	int numInstances;
	int lastNumInstances;

	// for interleaved rendering
	std::vector<int> offsetOfTile;
	std::vector<int> numInstanceOfTile;

private:

	enum BuildSourceOptions { buildFromS1 = 0, buildFromVPLs };
	static const char* buildSourceOptionsText[2];
	static EnumVar m_BuildSource;

	bool isLevelZeroIncluded;

	BuildSourceOptions lastBuildSourceOption;

	int lastInterleaveRate;

	void VerifySort(uint64_t* List, uint32_t ListLength, bool bAscending)
	{
		const uint64_t IndexMask = Math::AlignPowerOfTwo(ListLength) - 1;

		for (uint32_t i = 0; i < ListLength - 1; ++i)
		{
			ASSERT((List[i] & IndexMask) < ListLength, "Corrupted list index detected");

			if (bAscending)
			{
				printf("%d %d\n", List[i] >> 32, List[i] & 0xffffffff);

				ASSERT((List[i] >> 32) <= (List[i + 1] >> 32), "Invalid sort order:  non-ascending");
			}
			else
			{
				ASSERT(List[i] >= List[i + 1], "Invalid sort order:  non-descending");
			}
		}

		ASSERT((List[ListLength - 1] & IndexMask) < ListLength, "Corrupted list index detected");

		printf("success!\n");
	}

	void FindBoundingBox(ComputeContext& cptContext);
	void MergeLevelsInterleave(ComputeContext& cptContext, int interleavedRate, bool includeLevelZero = false, bool isRemerge = false);
	void MergeLevels(ComputeContext& cptContext, bool includeLevelZero = false, bool isRemerge = false);
	void GatherForHighLevel(int level, ComputeContext& cptContext);
	void GatherForLevel(int level, ComputeContext& cptContext);
	void SplatForLevel(int level, ComputeContext& cptContext, bool isBuildFromS1 = false);

	std::vector<std::vector<StructuredBuffer>> VPLScratchBuffersAtLevel; //before compaction
	std::vector<std::vector<StructuredBuffer>> VPLBuffersAtLevel;
	std::vector<std::vector<StructuredBuffer>> VPLTaskBuffersAtHighLevel;
	std::vector<std::vector<StructuredBuffer>> VPLAddressBuffersAtLevel;
	std::vector<StructuredBuffer> CellStartIdAtLevel;
	std::vector<StructuredBuffer> CellEndIdAtLevel;

	std::vector<StructuredBuffer> VertLockBuffers; //for level 1

	ByteAddressBuffer IndexKeyList;

	StructuredBuffer bboxReductionBuffer[2];

	std::vector<StructuredBuffer> VPLs;

	std::vector<StructuredBuffer> SortedVPLs;

	RootSignature RootSig;

	ComputePSO m_CellNormalizePSO;

	ComputePSO m_ClearVPLBufferPSO;
	ComputePSO m_VplSplatToVertexPSO;
	ComputePSO m_VplCompactionPSO;
	ComputePSO m_VplScan1PSO;
	ComputePSO m_VplScan2PSO;
	ComputePSO m_VplScan3PSO;
	ComputePSO m_VplPreScanPSO;
	ComputePSO m_GenVPLIndexKeyListPSO;
	ComputePSO m_ReorderVplsPSO;
	ComputePSO m_MergeLevelsPSO;
	ComputePSO m_MergeLevelsInterleavePSO;
	ComputePSO m_VertGatherPSO;
	ComputePSO m_VertGatherHigh_1PSO;
	ComputePSO m_VertGatherHigh_2PSO;
	ComputePSO m_FindVPLBboxMaxPSO;
	ComputePSO m_FindVPLBboxMinPSO;

	D3D12_CPU_DESCRIPTOR_HANDLE vplAttribs[3];

};