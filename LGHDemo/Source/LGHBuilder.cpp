#include "LGHBuilder.h"

const char* LGHBuilder::buildSourceOptionsText[2] = { "From S1", "From VPLs" };
EnumVar LGHBuilder::m_BuildSource("Application/LGH/Build Source", 0, 2, buildSourceOptionsText);

void LGHBuilder::FindBoundingBox(ComputeContext & cptContext)
{
	ScopedTimer _p0(L"Find BBox", cptContext);

	Vector3 bbox_min;
	Vector3 bbox_max;

	int numGroups = (numVPLs + 2047) / 2048;

	cptContext.TransitionResource(bboxReductionBuffer[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(bboxReductionBuffer[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLs[POSITION], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	__declspec(align(16)) struct {
		int n;
	} CSConstants = { numVPLs };

	cptContext.SetRootSignature(RootSig);
	cptContext.SetDynamicConstantBufferView(0, sizeof(CSConstants), &CSConstants);
	cptContext.SetDynamicDescriptor(1, 0, bboxReductionBuffer[1].GetUAV());
	cptContext.SetDynamicDescriptor(2, 0, vplAttribs[POSITION]);
	cptContext.SetPipelineState(m_FindVPLBboxMaxPSO);
	cptContext.Dispatch1D(numVPLs, 1024);
	cptContext.Flush(true);
	CSConstants.n = numGroups;
	cptContext.SetDynamicConstantBufferView(0, sizeof(CSConstants), &CSConstants);
	cptContext.TransitionResource(bboxReductionBuffer[1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.SetDynamicDescriptor(1, 0, bboxReductionBuffer[0].GetUAV());
	cptContext.SetDynamicDescriptor(2, 0, bboxReductionBuffer[1].GetSRV());
	cptContext.Dispatch1D(numGroups, 1024);
	cptContext.Flush(true);

	bool largeNumVPLs = numGroups > 2048;

	if (largeNumVPLs) // > 4M VPLs
	{
		int numGroups_2 = (numGroups + 2047) / 2048;
		CSConstants.n = numGroups_2;
		cptContext.SetDynamicConstantBufferView(0, sizeof(CSConstants), &CSConstants);
		cptContext.TransitionResource(bboxReductionBuffer[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cptContext.TransitionResource(bboxReductionBuffer[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.SetDynamicDescriptor(1, 0, bboxReductionBuffer[1].GetUAV());
		cptContext.SetDynamicDescriptor(2, 0, bboxReductionBuffer[0].GetSRV());
		cptContext.Dispatch1D(numGroups_2, 1024);
		cptContext.Flush(true);
	}

	ReadbackBuffer readback;
	readback.Create(L"ReadBackMinMaxBuffer", 1, sizeof(Vector3));
	cptContext.TransitionResource(bboxReductionBuffer[largeNumVPLs], D3D12_RESOURCE_STATE_COPY_SOURCE);
	cptContext.TransitionResource(readback, D3D12_RESOURCE_STATE_COPY_DEST);
	cptContext.CopyBufferRegion(readback, 0, bboxReductionBuffer[largeNumVPLs], 0, sizeof(Vector3));
	cptContext.Flush(true);
	Vector3* temp = (Vector3*)readback.Map();
	bbox_max = *temp;
	readback.Unmap();

	//// do the same for min
	CSConstants.n = numVPLs;
	cptContext.SetDynamicConstantBufferView(0, sizeof(CSConstants), &CSConstants);
	cptContext.TransitionResource(bboxReductionBuffer[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(bboxReductionBuffer[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.SetDynamicDescriptor(1, 0, bboxReductionBuffer[1].GetUAV());
	cptContext.SetDynamicDescriptor(2, 0, vplAttribs[POSITION]);
	cptContext.SetPipelineState(m_FindVPLBboxMinPSO);
	cptContext.Dispatch1D(numVPLs, 1024);
	cptContext.Flush(true);
	CSConstants.n = numGroups;
	cptContext.SetDynamicConstantBufferView(0, sizeof(CSConstants), &CSConstants);
	cptContext.TransitionResource(bboxReductionBuffer[1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.SetDynamicDescriptor(1, 0, bboxReductionBuffer[0].GetUAV());
	cptContext.SetDynamicDescriptor(2, 0, bboxReductionBuffer[1].GetSRV());
	cptContext.Dispatch1D(numGroups, 1024);
	cptContext.Flush(true);

	if (largeNumVPLs) // > 4M VPLs
	{
		int numGroups_2 = (numGroups + 2047) / 2048;
		CSConstants.n = numGroups_2;
		cptContext.SetDynamicConstantBufferView(0, sizeof(CSConstants), &CSConstants);
		cptContext.TransitionResource(bboxReductionBuffer[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cptContext.TransitionResource(bboxReductionBuffer[0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.SetDynamicDescriptor(1, 0, bboxReductionBuffer[1].GetUAV());
		cptContext.SetDynamicDescriptor(2, 0, bboxReductionBuffer[0].GetSRV());
		cptContext.Dispatch1D(numGroups_2, 1024);
		cptContext.Flush(true);
	}

	cptContext.TransitionResource(bboxReductionBuffer[largeNumVPLs], D3D12_RESOURCE_STATE_COPY_SOURCE);
	cptContext.TransitionResource(readback, D3D12_RESOURCE_STATE_COPY_DEST);
	cptContext.CopyBufferRegion(readback, 0, bboxReductionBuffer[largeNumVPLs], 0, sizeof(Vector3));
	cptContext.Flush(true);
	temp = (Vector3*)readback.Map();
	bbox_min = *temp;
	readback.Unmap();
	readback.Destroy();

	Vector3 bbox_dim = bbox_max - bbox_min;
	highestCellSize = fmaxf(bbox_dim.GetX(), fmaxf(bbox_dim.GetY(), bbox_dim.GetZ())) * 1.1f;
	Vector3 center = (bbox_max + bbox_min) / 2;
	lgh_corner = center - Vector3(highestCellSize / 2);

	baseRadius = highestCellSize / (1 << highestLevel);
}

void LGHBuilder::MergeLevelsInterleave(ComputeContext & cptContext, int interleavedRate, bool includeLevelZero, bool isRemerge)
{
	numInstances = includeLevelZero ? numVPLs : 0;

	for (int i = 1; i <= highestLevel; i++)
	{
		numInstances += levelSizes[i];
	}

	int numTiles = interleavedRate * interleavedRate;
	std::vector<std::vector<int>> levelSizeOfTileAtLevel(numTiles, std::vector<int>(highestLevel + 1));
	std::vector<int> levelOffsetOfTileAtLevel(numTiles * (highestLevel + 1));

	numInstanceOfTile.clear();
	numInstanceOfTile.resize(numTiles);
	offsetOfTile.clear();
	offsetOfTile.resize(numTiles);

	for (int level = (includeLevelZero ? 0 : 1); level <= highestLevel; level++)
	{
		int tileBaseSize = levelSizes[level] / numTiles;
		for (int tileId = 0; tileId < numTiles; tileId++)
		{
			int remainder = levelSizes[level] - tileBaseSize * numTiles;
			levelSizeOfTileAtLevel[tileId][level] = tileBaseSize + (remainder > tileId);
			levelOffsetOfTileAtLevel[tileId*(highestLevel + 1) + level] = numInstanceOfTile[tileId];
			numInstanceOfTile[tileId] += levelSizeOfTileAtLevel[tileId][level];
		}
	}

	int cdf = 0;
	for (int tileId = 0; tileId < numTiles; tileId++)
	{
		offsetOfTile[tileId] = cdf;
		for (int level = (includeLevelZero ? 0 : 1); level <= highestLevel; level++)
		{
			levelOffsetOfTileAtLevel[tileId*(highestLevel + 1) + level] += cdf;
		}
		cdf += numInstanceOfTile[tileId];
	}

	D3D12_CPU_DESCRIPTOR_HANDLE InstanceUAVs[4];

	//"remerge" might be unnecessary
	if (isRemerge || numInstances > lastNumInstances || numInstances < 0.5*lastNumInstances)
	{
		InstanceBuffers.resize(4);
		InstanceBuffers[POSITION].Create(L"POSITION instance buffer", 1.1*numInstances, sizeof(Vector4));
		InstanceBuffers[NORMAL].Create(L"NORMAL instance buffer", 1.1*numInstances, sizeof(Vector3));
		InstanceBuffers[COLOR].Create(L"COLOR instance buffer", 1.1*numInstances, sizeof(Vector3));
		InstanceBuffers[STDEV].Create(L"STDEV instance buffer", 1.1*numInstances, sizeof(Vector4));
		lastNumInstances = numInstances;
	}

	cptContext.TransitionResource(InstanceBuffers[POSITION], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(InstanceBuffers[NORMAL], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(InstanceBuffers[COLOR], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(InstanceBuffers[STDEV], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	InstanceUAVs[0] = InstanceBuffers[POSITION].GetUAV();
	InstanceUAVs[1] = InstanceBuffers[NORMAL].GetUAV();
	InstanceUAVs[2] = InstanceBuffers[COLOR].GetUAV();
	InstanceUAVs[3] = InstanceBuffers[STDEV].GetUAV();

	StructuredBuffer levelOffsetOfTileBuffer;
	levelOffsetOfTileBuffer.Create(L"LevelOffsetOfTileBuffer",
		numTiles * (highestLevel + 1), sizeof(int), levelOffsetOfTileAtLevel.data());
	cptContext.TransitionResource(levelOffsetOfTileBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	cptContext.SetRootSignature(RootSig);
	cptContext.SetPipelineState(m_MergeLevelsInterleavePSO);


	__declspec(align(16)) struct {
		int baseTileLevelSize;
		int baseOffset;
		int level;
		int levelSize;
		int numLevels;
		int numTiles;
	} mergeConstants;

	mergeConstants.numLevels = highestLevel + 1;
	mergeConstants.numTiles = numTiles;

	if (includeLevelZero)
	{
		mergeConstants.baseTileLevelSize = numVPLs / numTiles;
		int remainder = numVPLs - mergeConstants.baseTileLevelSize * numTiles;
		mergeConstants.baseOffset = (mergeConstants.baseTileLevelSize + 1) * remainder;
		mergeConstants.level = 0;
		mergeConstants.levelSize = numVPLs;

		cptContext.TransitionResource(VPLs[POSITION], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(VPLs[NORMAL], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(VPLs[COLOR], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		D3D12_CPU_DESCRIPTOR_HANDLE LevelAttribs[3] = { VPLs[POSITION].GetSRV(),
														VPLs[NORMAL].GetSRV(),
														VPLs[COLOR].GetSRV() };

		cptContext.SetDynamicConstantBufferView(0, sizeof(mergeConstants), &mergeConstants);
		cptContext.SetDynamicDescriptors(1, 0, _countof(InstanceUAVs), InstanceUAVs);
		cptContext.SetDynamicDescriptors(2, 0, _countof(LevelAttribs), LevelAttribs);
		cptContext.SetDynamicDescriptor(2, 4, levelOffsetOfTileBuffer.GetSRV());
		cptContext.Dispatch1D(numVPLs, 1024);
	}

	for (int level = 1; level <= highestLevel; level++)
	{
		mergeConstants.baseTileLevelSize = levelSizes[level] / numTiles;
		int remainder = levelSizes[level] - mergeConstants.baseTileLevelSize * numTiles;
		mergeConstants.baseOffset = (mergeConstants.baseTileLevelSize + 1) * remainder;
		mergeConstants.level = level;
		mergeConstants.levelSize = levelSizes[level];

		cptContext.TransitionResource(VPLBuffersAtLevel[level][POSITION], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(VPLBuffersAtLevel[level][NORMAL], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(VPLBuffersAtLevel[level][COLOR], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(VPLBuffersAtLevel[level][STDEV], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		D3D12_CPU_DESCRIPTOR_HANDLE LevelAttribs[4] = { VPLBuffersAtLevel[level][POSITION].GetSRV(),
			VPLBuffersAtLevel[level][NORMAL].GetSRV(),
			VPLBuffersAtLevel[level][COLOR].GetSRV(),
			VPLBuffersAtLevel[level][STDEV].GetSRV() };

		cptContext.SetDynamicConstantBufferView(0, sizeof(mergeConstants), &mergeConstants);
		cptContext.SetDynamicDescriptors(1, 0, _countof(InstanceUAVs), InstanceUAVs);
		cptContext.SetDynamicDescriptors(2, 0, _countof(LevelAttribs), LevelAttribs);
		cptContext.SetDynamicDescriptor(2, 4, levelOffsetOfTileBuffer.GetSRV());
		cptContext.Dispatch1D(levelSizes[level], 1024);
	}
	cptContext.Flush(true);

}

void LGHBuilder::MergeLevels(ComputeContext & cptContext, bool includeLevelZero, bool isRemerge)
{
	ScopedTimer _p0(L"Merge levels", cptContext);

	numInstances = includeLevelZero ? numVPLs : 0;
	std::vector<int> levelOffsets(highestLevel + 1);
	for (int i = 1; i <= highestLevel; i++)
	{
		levelOffsets[i] = numInstances;
		numInstances += levelSizes[i];
	}

	if (isRemerge || numInstances > lastNumInstances || numInstances < 0.5*lastNumInstances)
	{
		InstanceBuffers.resize(4);
		InstanceBuffers[POSITION].Create(L"POSITION instance buffer", int(1.1*numInstances), sizeof(Vector4));
		InstanceBuffers[NORMAL].Create(L"NORMAL instance buffer", int(1.1*numInstances), sizeof(Vector3));
		InstanceBuffers[COLOR].Create(L"COLOR instance buffer", int(1.1*numInstances), sizeof(Vector3));
		InstanceBuffers[STDEV].Create(L"STDEV instance buffer", int(1.1*numInstances), sizeof(Vector4));
		lastNumInstances = numInstances;
	}

	cptContext.TransitionResource(InstanceBuffers[POSITION], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(InstanceBuffers[NORMAL], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(InstanceBuffers[COLOR], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(InstanceBuffers[STDEV], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	D3D12_CPU_DESCRIPTOR_HANDLE InstanceUAVs[4] = { InstanceBuffers[POSITION].GetUAV(),
		InstanceBuffers[NORMAL].GetUAV(),
		InstanceBuffers[COLOR].GetUAV(),
		InstanceBuffers[STDEV].GetUAV() };
	cptContext.SetRootSignature(RootSig);
	cptContext.SetPipelineState(m_MergeLevelsPSO);

	if (includeLevelZero)
	{
		__declspec(align(16)) struct {
			int levelOffset;
			int levelSize;
			int isLevelZero;
		} mergeConstants;
		mergeConstants.levelOffset = 0;
		mergeConstants.levelSize = numVPLs;
		mergeConstants.isLevelZero = 1;
		cptContext.TransitionResource(VPLs[POSITION], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(VPLs[NORMAL], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(VPLs[COLOR], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		D3D12_CPU_DESCRIPTOR_HANDLE LevelAttribs[3] = { VPLs[POSITION].GetSRV(),
			VPLs[NORMAL].GetSRV(),
			VPLs[COLOR].GetSRV() };
		cptContext.SetDynamicConstantBufferView(0, sizeof(mergeConstants), &mergeConstants);
		cptContext.SetDynamicDescriptors(1, 0, _countof(InstanceUAVs), InstanceUAVs);
		cptContext.SetDynamicDescriptors(2, 0, _countof(LevelAttribs), LevelAttribs);
		cptContext.Dispatch1D(numVPLs, 1024);
	}


	for (int i = 1; i <= highestLevel; i++)
	{
		__declspec(align(16)) struct {
			int levelOffset;
			int levelSize;
			int isLevelZero;
		} mergeConstants;
		mergeConstants.levelOffset = levelOffsets[i];
		mergeConstants.levelSize = levelSizes[i];
		mergeConstants.isLevelZero = 0;
		cptContext.TransitionResource(VPLBuffersAtLevel[i][POSITION], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(VPLBuffersAtLevel[i][NORMAL], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(VPLBuffersAtLevel[i][COLOR], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.TransitionResource(VPLBuffersAtLevel[i][STDEV], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		D3D12_CPU_DESCRIPTOR_HANDLE LevelAttribs[4] = { VPLBuffersAtLevel[i][POSITION].GetSRV(),
			VPLBuffersAtLevel[i][NORMAL].GetSRV(),
			VPLBuffersAtLevel[i][COLOR].GetSRV(),
			VPLBuffersAtLevel[i][STDEV].GetSRV() };

		cptContext.SetDynamicConstantBufferView(0, sizeof(mergeConstants), &mergeConstants);
		cptContext.SetDynamicDescriptors(1, 0, _countof(InstanceUAVs), InstanceUAVs);
		cptContext.SetDynamicDescriptors(2, 0, _countof(LevelAttribs), LevelAttribs);
		cptContext.Dispatch1D(levelSizes[i], 1024);
	}
	cptContext.Flush(true);
}

void LGHBuilder::GatherForHighLevel(int level, ComputeContext & cptContext)
{
	ScopedTimer _p0(L"Gather level " + std::to_wstring(level), cptContext);

	int levelRes = (1 << (highestLevel - level)) + 1;
	int numVerts = levelRes * levelRes * levelRes;

	int taskDivRate = 8;// >> (highestLevel - level);
	if (level == highestLevel) taskDivRate = 16;
	int numTasksPerVertex = taskDivRate * taskDivRate * taskDivRate;

	__declspec(align(16)) struct {
		int level;
		int highestLevel;
		int leveloneRes;
		float cellSize;
		Vector3 corner;
		int taskdivRate;
		int numTasksPerVertex;
		int numVerts;
	} gatherConstants;

	gatherConstants.level = level;
	gatherConstants.highestLevel = highestLevel;
	gatherConstants.leveloneRes = (1 << (highestLevel - 1)) + 1;
	gatherConstants.cellSize = highestCellSize / (1 << (highestLevel - level));
	gatherConstants.corner = lgh_corner;
	gatherConstants.taskdivRate = taskDivRate;
	gatherConstants.numTasksPerVertex = numTasksPerVertex;
	gatherConstants.numVerts = numVerts;

	// a thread gathers part of the vpls(verts)

	cptContext.TransitionResource(VPLScratchBuffersAtLevel[1][POSITION], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[1][NORMAL], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[1][COLOR], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[1][STDEV], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[1][WEIGHT], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLTaskBuffersAtHighLevel[level - firstHighLevel][POSITION], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLTaskBuffersAtHighLevel[level - firstHighLevel][NORMAL], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLTaskBuffersAtHighLevel[level - firstHighLevel][COLOR], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLTaskBuffersAtHighLevel[level - firstHighLevel][STDEV], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLTaskBuffersAtHighLevel[level - firstHighLevel][WEIGHT], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	D3D12_CPU_DESCRIPTOR_HANDLE LeveloneAttribs[5] = { VPLScratchBuffersAtLevel[1][POSITION].GetSRV(),
		VPLScratchBuffersAtLevel[1][NORMAL].GetSRV(),
		VPLScratchBuffersAtLevel[1][COLOR].GetSRV(),
		VPLScratchBuffersAtLevel[1][STDEV].GetSRV(),
		VPLScratchBuffersAtLevel[1][WEIGHT].GetSRV() };
	D3D12_CPU_DESCRIPTOR_HANDLE LevelTaskAttribs[5] = { VPLTaskBuffersAtHighLevel[level - firstHighLevel][POSITION].GetUAV(),
		VPLTaskBuffersAtHighLevel[level - firstHighLevel][NORMAL].GetUAV(),
		VPLTaskBuffersAtHighLevel[level - firstHighLevel][COLOR].GetUAV(),
		VPLTaskBuffersAtHighLevel[level - firstHighLevel][STDEV].GetUAV(),
		VPLTaskBuffersAtHighLevel[level - firstHighLevel][WEIGHT].GetUAV() };

	cptContext.SetRootSignature(RootSig);
	cptContext.SetDynamicConstantBufferView(0, sizeof(gatherConstants), &gatherConstants);
	cptContext.SetDynamicDescriptors(1, 0, _countof(LevelTaskAttribs), LevelTaskAttribs);
	cptContext.SetDynamicDescriptors(2, 0, _countof(LeveloneAttribs), LeveloneAttribs);
	cptContext.SetPipelineState(m_VertGatherHigh_1PSO);
	cptContext.Dispatch3D(taskDivRate * levelRes, taskDivRate * levelRes, taskDivRate * levelRes, 4, 4, 4);
	cptContext.Flush(true);

	// final gather
	cptContext.TransitionResource(VPLTaskBuffersAtHighLevel[level - firstHighLevel][POSITION], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLTaskBuffersAtHighLevel[level - firstHighLevel][NORMAL], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLTaskBuffersAtHighLevel[level - firstHighLevel][COLOR], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLTaskBuffersAtHighLevel[level - firstHighLevel][STDEV], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLTaskBuffersAtHighLevel[level - firstHighLevel][WEIGHT], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][POSITION], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][NORMAL], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][COLOR], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][STDEV], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][WEIGHT], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	D3D12_CPU_DESCRIPTOR_HANDLE LevelAttribs[5] = { VPLScratchBuffersAtLevel[level][POSITION].GetUAV(),
		VPLScratchBuffersAtLevel[level][NORMAL].GetUAV(),
		VPLScratchBuffersAtLevel[level][COLOR].GetUAV(),
		VPLScratchBuffersAtLevel[level][STDEV].GetUAV(),
		VPLScratchBuffersAtLevel[level][WEIGHT].GetUAV() };
	D3D12_CPU_DESCRIPTOR_HANDLE LevelTaskAttribsSrvs[5] = { VPLTaskBuffersAtHighLevel[level - firstHighLevel][POSITION].GetSRV(),
		VPLTaskBuffersAtHighLevel[level - firstHighLevel][NORMAL].GetSRV(),
		VPLTaskBuffersAtHighLevel[level - firstHighLevel][COLOR].GetSRV(),
		VPLTaskBuffersAtHighLevel[level - firstHighLevel][STDEV].GetSRV(),
		VPLTaskBuffersAtHighLevel[level - firstHighLevel][WEIGHT].GetSRV() };
	cptContext.SetDynamicConstantBufferView(0, sizeof(gatherConstants), &gatherConstants);
	cptContext.SetDynamicDescriptors(1, 0, _countof(LevelAttribs), LevelAttribs);
	cptContext.SetDynamicDescriptors(2, 0, _countof(LevelTaskAttribsSrvs), LevelTaskAttribsSrvs);
	cptContext.SetPipelineState(m_VertGatherHigh_2PSO);
	cptContext.Dispatch1D(numVerts, MAX_BLOCK_SIZE);
	cptContext.Flush(true);


	// compaction

	// stage 1 (pre-scan)

	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][WEIGHT], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLAddressBuffersAtLevel[level][0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.SetDynamicDescriptor(1, 0, VPLAddressBuffersAtLevel[level][0].GetUAV());
	cptContext.SetDynamicDescriptor(2, 0, VPLScratchBuffersAtLevel[level][WEIGHT].GetSRV());
	cptContext.SetPipelineState(m_VplPreScanPSO);
	cptContext.Dispatch1D(numVerts, MAX_BLOCK_SIZE);

	// stage 2

	// scan 1 (scan for each thread group)

	cptContext.TransitionResource(VPLAddressBuffersAtLevel[level][1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLAddressBuffersAtLevel[level][0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.SetDynamicDescriptor(1, 0, VPLAddressBuffersAtLevel[level][1].GetUAV());
	cptContext.SetDynamicDescriptor(2, 0, VPLAddressBuffersAtLevel[level][0].GetSRV());
	cptContext.SetPipelineState(m_VplScan1PSO);
	cptContext.Dispatch1D(numVerts, MAX_BLOCK_SIZE);

	int numGroups = numVerts;
	__declspec(align(16)) struct {
		unsigned int stride;
	} scanConstants;
	scanConstants.stride = 1;
	while (numGroups > MAX_BLOCK_SIZE)
	{
		// scan 2 (scan sum of each thread group)
		cptContext.TransitionResource(VPLAddressBuffersAtLevel[level][0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cptContext.TransitionResource(VPLAddressBuffersAtLevel[level][1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		scanConstants.stride *= MAX_BLOCK_SIZE;
		cptContext.SetDynamicConstantBufferView(0, sizeof(scanConstants), &scanConstants);
		cptContext.SetDynamicDescriptor(1, 0, VPLAddressBuffersAtLevel[level][0].GetUAV());
		cptContext.SetDynamicDescriptor(2, 0, VPLAddressBuffersAtLevel[level][1].GetSRV());
		cptContext.SetPipelineState(m_VplScan2PSO);
		cptContext.Dispatch1D((numGroups + MAX_BLOCK_SIZE - 1) / MAX_BLOCK_SIZE, MAX_BLOCK_SIZE);

		// scan 3 (add back scanned sum to each group)
		cptContext.TransitionResource(VPLAddressBuffersAtLevel[level][1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cptContext.TransitionResource(VPLAddressBuffersAtLevel[level][0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.SetDynamicDescriptor(1, 0, VPLAddressBuffersAtLevel[level][1].GetUAV());
		cptContext.SetDynamicDescriptor(2, 0, VPLAddressBuffersAtLevel[level][0].GetSRV());
		cptContext.SetPipelineState(m_VplScan3PSO);
		cptContext.Dispatch1D(numVerts, MAX_BLOCK_SIZE);
		numGroups = (numGroups + MAX_BLOCK_SIZE - 1) / MAX_BLOCK_SIZE;
	}

	ReadbackBuffer readback;
	readback.Create(L"ReadBackVplsBuffer", 1, sizeof(int));
	cptContext.TransitionResource(VPLAddressBuffersAtLevel[level][1], D3D12_RESOURCE_STATE_COPY_SOURCE);
	cptContext.CopyBufferRegion(readback, 0, VPLAddressBuffersAtLevel[level][1], (numVerts - 1) * sizeof(int), 4);
	cptContext.Flush(true);
	unsigned int* temp = (unsigned int*)readback.Map();
	unsigned int numNonEmptyVerts = *temp;
	readback.Unmap();
	readback.Destroy();

	levelSizes[level] = numNonEmptyVerts;

	__declspec(align(16)) struct {
		int level;
	} compactConstants;
	compactConstants.level = level;

	// stage 3 scatter to new address and normalize
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][POSITION], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][NORMAL], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][COLOR], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][STDEV], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][WEIGHT], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLAddressBuffersAtLevel[level][1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLBuffersAtLevel[level][POSITION], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLBuffersAtLevel[level][NORMAL], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLBuffersAtLevel[level][COLOR], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLBuffersAtLevel[level][STDEV], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.SetDynamicConstantBufferView(0, sizeof(compactConstants), &compactConstants);
	cptContext.SetDynamicDescriptor(1, 0, VPLBuffersAtLevel[level][POSITION].GetUAV());
	cptContext.SetDynamicDescriptor(1, 1, VPLBuffersAtLevel[level][NORMAL].GetUAV());
	cptContext.SetDynamicDescriptor(1, 2, VPLBuffersAtLevel[level][COLOR].GetUAV());
	cptContext.SetDynamicDescriptor(1, 3, VPLBuffersAtLevel[level][STDEV].GetUAV());
	cptContext.SetDynamicDescriptor(2, 0, VPLScratchBuffersAtLevel[level][POSITION].GetSRV());
	cptContext.SetDynamicDescriptor(2, 1, VPLScratchBuffersAtLevel[level][NORMAL].GetSRV());
	cptContext.SetDynamicDescriptor(2, 2, VPLScratchBuffersAtLevel[level][COLOR].GetSRV());
	cptContext.SetDynamicDescriptor(2, 3, VPLScratchBuffersAtLevel[level][STDEV].GetSRV());
	cptContext.SetDynamicDescriptor(2, 4, VPLScratchBuffersAtLevel[level][WEIGHT].GetSRV());
	cptContext.SetDynamicDescriptor(2, 5, VPLAddressBuffersAtLevel[level][1].GetSRV());
	cptContext.SetPipelineState(m_VplCompactionPSO);
	cptContext.Dispatch1D(numVerts, MAX_BLOCK_SIZE);
	cptContext.Flush(true);

}

void LGHBuilder::GatherForLevel(int level, ComputeContext & cptContext)
{
	ScopedTimer _p0(L"Gather level " + std::to_wstring(level), cptContext);

	int levelRes = (1 << (highestLevel - level)) + 1;
	int numVerts = levelRes * levelRes * levelRes;

	__declspec(align(16)) struct {
		//int type;
		int level;
		int highestLevel;
		int leveloneRes;
		float cellSize;
		Vector3 corner;
	} gatherConstants;

	gatherConstants.level = level;
	gatherConstants.highestLevel = highestLevel;
	gatherConstants.leveloneRes = (1 << (highestLevel - 1)) + 1;
	gatherConstants.cellSize = highestCellSize / (1 << (highestLevel - level));
	gatherConstants.corner = lgh_corner;

	cptContext.TransitionResource(VPLScratchBuffersAtLevel[1][POSITION], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[1][NORMAL], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[1][COLOR], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[1][STDEV], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[1][WEIGHT], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][POSITION], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][NORMAL], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][COLOR], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][STDEV], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][WEIGHT], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	D3D12_CPU_DESCRIPTOR_HANDLE LeveloneAttribs[5] = { VPLScratchBuffersAtLevel[1][POSITION].GetSRV(),
		VPLScratchBuffersAtLevel[1][NORMAL].GetSRV(),
		VPLScratchBuffersAtLevel[1][COLOR].GetSRV(),
		VPLScratchBuffersAtLevel[1][STDEV].GetSRV(),
		VPLScratchBuffersAtLevel[1][WEIGHT].GetSRV() };
	D3D12_CPU_DESCRIPTOR_HANDLE LevelAttribs[5] = { VPLScratchBuffersAtLevel[level][POSITION].GetUAV(),
		VPLScratchBuffersAtLevel[level][NORMAL].GetUAV(),
		VPLScratchBuffersAtLevel[level][COLOR].GetUAV(),
		VPLScratchBuffersAtLevel[level][STDEV].GetUAV(),
		VPLScratchBuffersAtLevel[level][WEIGHT].GetUAV() };
	cptContext.SetRootSignature(RootSig);
	cptContext.SetDynamicConstantBufferView(0, sizeof(gatherConstants), &gatherConstants);
	cptContext.SetDynamicDescriptors(1, 0, _countof(LevelAttribs), LevelAttribs);
	cptContext.SetDynamicDescriptors(2, 0, _countof(LeveloneAttribs), LeveloneAttribs);
	cptContext.SetPipelineState(m_VertGatherPSO);
	cptContext.Dispatch3D(levelRes, levelRes, levelRes, 4, 4, 4);
	cptContext.Flush(true);

	// compaction

	// stage 1 (pre-scan)

	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][WEIGHT], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLAddressBuffersAtLevel[level][0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.SetDynamicDescriptor(1, 0, VPLAddressBuffersAtLevel[level][0].GetUAV());
	cptContext.SetDynamicDescriptor(2, 0, VPLScratchBuffersAtLevel[level][WEIGHT].GetSRV());
	cptContext.SetPipelineState(m_VplPreScanPSO);
	cptContext.Dispatch1D(numVerts, MAX_BLOCK_SIZE);

	// stage 2

	// scan 1 (scan for each thread group)

	cptContext.TransitionResource(VPLAddressBuffersAtLevel[level][1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLAddressBuffersAtLevel[level][0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.SetDynamicDescriptor(1, 0, VPLAddressBuffersAtLevel[level][1].GetUAV());
	cptContext.SetDynamicDescriptor(2, 0, VPLAddressBuffersAtLevel[level][0].GetSRV());
	cptContext.SetPipelineState(m_VplScan1PSO);
	cptContext.Dispatch1D(numVerts, MAX_BLOCK_SIZE);

	int numGroups = numVerts;
	__declspec(align(16)) struct {
		unsigned int stride;
	} scanConstants;
	scanConstants.stride = 1;
	while (numGroups > MAX_BLOCK_SIZE)
	{
		// scan 2 (scan sum of each thread group)
		cptContext.TransitionResource(VPLAddressBuffersAtLevel[level][0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cptContext.TransitionResource(VPLAddressBuffersAtLevel[level][1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		scanConstants.stride *= MAX_BLOCK_SIZE;
		cptContext.SetDynamicConstantBufferView(0, sizeof(scanConstants), &scanConstants);
		cptContext.SetDynamicDescriptor(1, 0, VPLAddressBuffersAtLevel[level][0].GetUAV());
		cptContext.SetDynamicDescriptor(2, 0, VPLAddressBuffersAtLevel[level][1].GetSRV());
		cptContext.SetPipelineState(m_VplScan2PSO);
		cptContext.Dispatch1D((numGroups + MAX_BLOCK_SIZE - 1) / MAX_BLOCK_SIZE, MAX_BLOCK_SIZE);

		// scan 3 (add back scanned sum to each group)
		cptContext.TransitionResource(VPLAddressBuffersAtLevel[level][1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cptContext.TransitionResource(VPLAddressBuffersAtLevel[level][0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.SetDynamicDescriptor(1, 0, VPLAddressBuffersAtLevel[level][1].GetUAV());
		cptContext.SetDynamicDescriptor(2, 0, VPLAddressBuffersAtLevel[level][0].GetSRV());
		cptContext.SetPipelineState(m_VplScan3PSO);
		cptContext.Dispatch1D(numVerts, MAX_BLOCK_SIZE);
		numGroups = (numGroups + MAX_BLOCK_SIZE - 1) / MAX_BLOCK_SIZE;
	}

	ReadbackBuffer readback;
	readback.Create(L"ReadBackVplsBuffer", 1, sizeof(int));
	cptContext.TransitionResource(VPLAddressBuffersAtLevel[level][1], D3D12_RESOURCE_STATE_COPY_SOURCE);
	cptContext.CopyBufferRegion(readback, 0, VPLAddressBuffersAtLevel[level][1], (numVerts - 1) * sizeof(int), 4);
	cptContext.Flush(true);
	unsigned int* temp = (unsigned int*)readback.Map();
	unsigned int numNonEmptyVerts = *temp;
	readback.Unmap();
	readback.Destroy();


	__declspec(align(16)) struct {
		int level;
	} compactConstants;
	compactConstants.level = level;

	levelSizes[level] = numNonEmptyVerts;

	// stage 3 scatter to new address and normalize
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][POSITION], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][NORMAL], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][COLOR], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][STDEV], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][WEIGHT], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLAddressBuffersAtLevel[level][1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLBuffersAtLevel[level][POSITION], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLBuffersAtLevel[level][NORMAL], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLBuffersAtLevel[level][COLOR], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLBuffersAtLevel[level][STDEV], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.SetDynamicConstantBufferView(0, sizeof(compactConstants), &compactConstants);
	cptContext.SetDynamicDescriptor(1, 0, VPLBuffersAtLevel[level][POSITION].GetUAV());
	cptContext.SetDynamicDescriptor(1, 1, VPLBuffersAtLevel[level][NORMAL].GetUAV());
	cptContext.SetDynamicDescriptor(1, 2, VPLBuffersAtLevel[level][COLOR].GetUAV());
	cptContext.SetDynamicDescriptor(1, 3, VPLBuffersAtLevel[level][STDEV].GetUAV());
	cptContext.SetDynamicDescriptor(2, 0, VPLScratchBuffersAtLevel[level][POSITION].GetSRV());
	cptContext.SetDynamicDescriptor(2, 1, VPLScratchBuffersAtLevel[level][NORMAL].GetSRV());
	cptContext.SetDynamicDescriptor(2, 2, VPLScratchBuffersAtLevel[level][COLOR].GetSRV());
	cptContext.SetDynamicDescriptor(2, 3, VPLScratchBuffersAtLevel[level][STDEV].GetSRV());
	cptContext.SetDynamicDescriptor(2, 4, VPLScratchBuffersAtLevel[level][WEIGHT].GetSRV());
	cptContext.SetDynamicDescriptor(2, 5, VPLAddressBuffersAtLevel[level][1].GetSRV());
	cptContext.SetPipelineState(m_VplCompactionPSO);
	cptContext.Dispatch1D(numVerts, MAX_BLOCK_SIZE);
	cptContext.Flush(true);

	//std::vector<std::vector<Vector4>> VPLread(4, std::vector<Vector4>(numNonEmptyVerts));
	//ReadbackBuffer readbackVplsBuffer;
	//readbackVplsBuffer.Create(L"ReadBackVplsBuffer", numNonEmptyVerts, sizeof(Vector4));
	//for (int i = 0; i < 4; i++)
	//{
	//	cptContext.TransitionResource(VPLBuffersAtLevel[level][i], D3D12_RESOURCE_STATE_COPY_SOURCE, true);
	//	cptContext.TransitionResource(readbackVplsBuffer, D3D12_RESOURCE_STATE_COPY_DEST, true);
	//	cptContext.CopyBuffer(readbackVplsBuffer, VPLBuffersAtLevel[level][i]);
	//	cptContext.Flush(true);
	//	Vector4* tempLevelAttrib = (Vector4*)readbackVplsBuffer.Map();
	//	memcpy(VPLread[i].data(), tempLevelAttrib, sizeof(Vector4)*numNonEmptyVerts);
	//}
	//int a = 0;
}

//consider using different store order (or splat order) to minimize race

void LGHBuilder::SplatForLevel(int level, ComputeContext & cptContext, bool isBuildFromS1)
{
	ScopedTimer _p0(L"Splat level " + std::to_wstring(level), cptContext);

	int numVerts = (1 << (highestLevel - level)) + 1;
	numVerts = numVerts * numVerts * numVerts;

	__declspec(align(16)) struct {
		int numVerts;
	} clearConstants;
	clearConstants.numVerts = numVerts;

	D3D12_CPU_DESCRIPTOR_HANDLE LevelAttribs[5] = { VPLScratchBuffersAtLevel[level][POSITION].GetUAV(),
		VPLScratchBuffersAtLevel[level][NORMAL].GetUAV(),
		VPLScratchBuffersAtLevel[level][COLOR].GetUAV(),
		VPLScratchBuffersAtLevel[level][STDEV].GetUAV(),
		VPLScratchBuffersAtLevel[level][WEIGHT].GetUAV() };

	cptContext.SetRootSignature(RootSig);
	//clear buffers
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][POSITION], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][NORMAL], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][COLOR], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][STDEV], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][WEIGHT], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	cptContext.SetDynamicDescriptors(1, 0, _countof(LevelAttribs), LevelAttribs);

	cptContext.SetDynamicConstantBufferView(0, sizeof(clearConstants), &clearConstants);
	cptContext.SetPipelineState(m_ClearVPLBufferPSO);
	cptContext.Dispatch1D(numVerts, MAX_BLOCK_SIZE);
	cptContext.InsertUAVBarrier(VPLScratchBuffersAtLevel[level][POSITION]);
	cptContext.InsertUAVBarrier(VPLScratchBuffersAtLevel[level][NORMAL]);
	cptContext.InsertUAVBarrier(VPLScratchBuffersAtLevel[level][COLOR]);
	cptContext.InsertUAVBarrier(VPLScratchBuffersAtLevel[level][STDEV]);
	cptContext.InsertUAVBarrier(VPLScratchBuffersAtLevel[level][WEIGHT]);
	cptContext.Flush(true);


	// splat
	__declspec(align(16)) struct {
		//int type;
		int numVpls;
		int level;
		int highestLevel;
		float cellSize;
		Vector3 corner;
		int round;
		//int numCells1D;
		//int numCells;
	} splatConstants;

	//check alignment
	assert((int*)&splatConstants.corner - (int*)&splatConstants.numVpls == 4);

	splatConstants.numVpls = numVPLs;
	splatConstants.level = level;
	splatConstants.highestLevel = highestLevel;
	splatConstants.cellSize = highestCellSize / (1 << (highestLevel - level));
	splatConstants.corner = lgh_corner;

	cptContext.TransitionResource(VPLs[POSITION], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLs[NORMAL], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLs[COLOR], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	cptContext.SetDynamicDescriptors(1, 0, _countof(LevelAttribs), LevelAttribs);
	cptContext.SetDynamicDescriptor(2, 0, VPLs[POSITION].GetSRV());
	cptContext.SetDynamicDescriptor(2, 1, VPLs[NORMAL].GetSRV());
	cptContext.SetDynamicDescriptor(2, 2, VPLs[COLOR].GetSRV());

	cptContext.SetPipelineState(m_VplSplatToVertexPSO);
	cptContext.SetDynamicConstantBufferView(0, sizeof(splatConstants), &splatConstants);
	cptContext.Dispatch1D(numVPLs * 8, 1024);


	// compaction

	// stage 1 (pre-scan)

	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][WEIGHT], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLAddressBuffersAtLevel[level][0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.SetDynamicDescriptor(1, 0, VPLAddressBuffersAtLevel[level][0].GetUAV());
	cptContext.SetDynamicDescriptor(2, 0, VPLScratchBuffersAtLevel[level][WEIGHT].GetSRV());
	cptContext.SetPipelineState(m_VplPreScanPSO);
	cptContext.Dispatch1D(numVerts, MAX_BLOCK_SIZE);

	// stage 2

	// scan 1 (scan for each thread group)

	cptContext.TransitionResource(VPLAddressBuffersAtLevel[level][1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLAddressBuffersAtLevel[level][0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.SetDynamicDescriptor(1, 0, VPLAddressBuffersAtLevel[level][1].GetUAV());
	cptContext.SetDynamicDescriptor(2, 0, VPLAddressBuffersAtLevel[level][0].GetSRV());
	cptContext.SetPipelineState(m_VplScan1PSO);
	cptContext.Dispatch1D(numVerts, MAX_BLOCK_SIZE);

	int numGroups = numVerts;
	__declspec(align(16)) struct {
		unsigned int stride;
	} scanConstants;
	scanConstants.stride = 1;
	while (numGroups > MAX_BLOCK_SIZE)
	{
		// scan 2 (scan sum of each thread group)
		cptContext.TransitionResource(VPLAddressBuffersAtLevel[level][0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cptContext.TransitionResource(VPLAddressBuffersAtLevel[level][1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		scanConstants.stride *= MAX_BLOCK_SIZE;
		cptContext.SetDynamicConstantBufferView(0, sizeof(scanConstants), &scanConstants);
		cptContext.SetDynamicDescriptor(1, 0, VPLAddressBuffersAtLevel[level][0].GetUAV());
		cptContext.SetDynamicDescriptor(2, 0, VPLAddressBuffersAtLevel[level][1].GetSRV());
		cptContext.SetPipelineState(m_VplScan2PSO);
		cptContext.Dispatch1D((numGroups + MAX_BLOCK_SIZE - 1) / MAX_BLOCK_SIZE, MAX_BLOCK_SIZE);

		// scan 3 (add back scanned sum to each group)
		cptContext.TransitionResource(VPLAddressBuffersAtLevel[level][1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cptContext.TransitionResource(VPLAddressBuffersAtLevel[level][0], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.SetDynamicDescriptor(1, 0, VPLAddressBuffersAtLevel[level][1].GetUAV());
		cptContext.SetDynamicDescriptor(2, 0, VPLAddressBuffersAtLevel[level][0].GetSRV());
		cptContext.SetPipelineState(m_VplScan3PSO);
		cptContext.Dispatch1D(numVerts, MAX_BLOCK_SIZE);
		numGroups = (numGroups + MAX_BLOCK_SIZE - 1) / MAX_BLOCK_SIZE;
	}

	ReadbackBuffer readback;
	readback.Create(L"ReadBackVplsBuffer", 1, sizeof(int));
	cptContext.TransitionResource(VPLAddressBuffersAtLevel[level][1], D3D12_RESOURCE_STATE_COPY_SOURCE);
	cptContext.CopyBufferRegion(readback, 0, VPLAddressBuffersAtLevel[level][1], (numVerts - 1) * sizeof(int), 4);
	cptContext.Flush(true);
	unsigned int* temp = (unsigned int*)readback.Map();
	unsigned int numNonEmptyVerts = *temp;
	readback.Unmap();
	readback.Destroy();


	levelSizes[level] = numNonEmptyVerts;

	__declspec(align(16)) struct {
		int level;
	} compactConstants;
	compactConstants.level = level;

	// stage 3 scatter to new address and normalize
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][POSITION], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][NORMAL], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][COLOR], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][STDEV], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][WEIGHT], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLAddressBuffersAtLevel[level][1], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	cptContext.TransitionResource(VPLBuffersAtLevel[level][POSITION], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLBuffersAtLevel[level][NORMAL], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLBuffersAtLevel[level][COLOR], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.TransitionResource(VPLBuffersAtLevel[level][STDEV], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cptContext.SetDynamicConstantBufferView(0, sizeof(compactConstants), &compactConstants);
	cptContext.SetDynamicDescriptor(1, 0, VPLBuffersAtLevel[level][POSITION].GetUAV());
	cptContext.SetDynamicDescriptor(1, 1, VPLBuffersAtLevel[level][NORMAL].GetUAV());
	cptContext.SetDynamicDescriptor(1, 2, VPLBuffersAtLevel[level][COLOR].GetUAV());
	cptContext.SetDynamicDescriptor(1, 3, VPLBuffersAtLevel[level][STDEV].GetUAV());
	cptContext.SetDynamicDescriptor(2, 0, VPLScratchBuffersAtLevel[level][POSITION].GetSRV());
	cptContext.SetDynamicDescriptor(2, 1, VPLScratchBuffersAtLevel[level][NORMAL].GetSRV());
	cptContext.SetDynamicDescriptor(2, 2, VPLScratchBuffersAtLevel[level][COLOR].GetSRV());
	cptContext.SetDynamicDescriptor(2, 3, VPLScratchBuffersAtLevel[level][STDEV].GetSRV());
	cptContext.SetDynamicDescriptor(2, 4, VPLScratchBuffersAtLevel[level][WEIGHT].GetSRV());
	cptContext.SetDynamicDescriptor(2, 5, VPLAddressBuffersAtLevel[level][1].GetSRV());
	cptContext.SetPipelineState(m_VplCompactionPSO);
	cptContext.Dispatch1D(numVerts, MAX_BLOCK_SIZE);
	cptContext.Flush(true);

	if (isBuildFromS1)
	{
		cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][POSITION], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][NORMAL], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][STDEV], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cptContext.TransitionResource(VPLScratchBuffersAtLevel[level][WEIGHT], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		cptContext.SetDynamicDescriptor(1, 0, VPLScratchBuffersAtLevel[level][POSITION].GetUAV());
		cptContext.SetDynamicDescriptor(1, 1, VPLScratchBuffersAtLevel[level][NORMAL].GetUAV());
		cptContext.SetDynamicDescriptor(1, 2, VPLScratchBuffersAtLevel[level][STDEV].GetUAV());
		cptContext.SetDynamicDescriptor(2, 0, VPLScratchBuffersAtLevel[level][WEIGHT].GetSRV());
		cptContext.SetPipelineState(m_CellNormalizePSO);
		cptContext.Dispatch1D(numVerts, MAX_BLOCK_SIZE);
	}

	cptContext.Flush(true);

}