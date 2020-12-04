// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "D3D12RHIPrivate.h"

#if D3D12_RHI_RAYTRACING

#include "RayTracingBuiltInResources.h"

class FD3D12RayTracingPipelineState;
class FD3D12RayTracingShaderTable;

typedef FD3D12VertexBuffer FD3D12MemBuffer; // Generic GPU memory buffer

// Built-in local root parameters that are always bound to all hit shaders
struct FHitGroupSystemParameters
{
	D3D12_GPU_VIRTUAL_ADDRESS IndexBuffer;
	D3D12_GPU_VIRTUAL_ADDRESS VertexBuffer;
	FHitGroupSystemRootConstants RootConstants;
};

class FD3D12RayTracingGeometry : public FRHIRayTracingGeometry
{
public:

	FD3D12RayTracingGeometry(const FRayTracingGeometryInitializer& Initializer);
	~FD3D12RayTracingGeometry();

	void TransitionBuffers(FD3D12CommandContext& CommandContext);
	void BuildAccelerationStructure(FD3D12CommandContext& CommandContext, EAccelerationStructureBuildMode BuildMode);
	void ConditionalCompactAccelerationStructure(FD3D12CommandContext& CommandContext);

	bool bIsAccelerationStructureDirty[MAX_NUM_GPUS] = {};
	void SetDirty(FRHIGPUMask GPUMask, bool bState)
	{
		for (uint32 GPUIndex : GPUMask)
		{
			bIsAccelerationStructureDirty[GPUIndex] = bState;
		}
	}
	bool IsDirty(uint32 GPUIndex) const
	{
		return bIsAccelerationStructureDirty[GPUIndex];
	}

	uint32 IndexStride = 0; // 0 for non-indexed / implicit triangle list, 2 for uint16, 4 for uint32
	uint32 IndexOffsetInBytes = 0;
	uint32 TotalPrimitiveCount = 0; // Combined number of primitives in all mesh segments

	D3D12_RAYTRACING_GEOMETRY_TYPE GeometryType = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;

	TArray<FRayTracingGeometrySegment> Segments; // Defines addressable parts of the mesh that can be used for material assignment (one segment = one SBT record)
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS BuildFlags;

	FIndexBufferRHIRef  RHIIndexBuffer;
	static FVertexBufferRHIRef NullTransformBuffer; // Null transform for hidden sections

	TRefCountPtr<FD3D12MemBuffer> AccelerationStructureBuffers[MAX_NUM_GPUS];
	TRefCountPtr<FD3D12MemBuffer> ScratchBuffers[MAX_NUM_GPUS];

	uint64 PostBuildInfoBufferReadbackFences[MAX_NUM_GPUS];
	TRefCountPtr<FD3D12MemBuffer> PostBuildInfoBuffers[MAX_NUM_GPUS];
	FStagingBufferRHIRef PostBuildInfoStagingBuffers[MAX_NUM_GPUS];

	// Hit shader parameters per geometry segment
	TArray<FHitGroupSystemParameters> HitGroupSystemParameters[MAX_NUM_GPUS];

	FName DebugName;
};

class FD3D12RayTracingScene : public FRHIRayTracingScene, public FD3D12AdapterChild
{
public:

	// Ray tracing shader bindings can be processed in parallel.
	// Each concurrent worker gets its own dedicated descriptor cache instance to avoid contention or locking.
	// Scaling beyond 5 total threads does not yield any speedup in practice.
	static constexpr uint32 MaxBindingWorkers = 5; // RHI thread + 4 parallel workers.

	FD3D12RayTracingScene(FD3D12Adapter* Adapter, const FRayTracingSceneInitializer& Initializer);
	~FD3D12RayTracingScene();

	void BuildAccelerationStructure(FD3D12CommandContext& CommandContext, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS BuildFlags);

	TRefCountPtr<FD3D12MemBuffer> AccelerationStructureBuffers[MAX_NUM_GPUS];
	bool bAccelerationStructureViewInitialized[MAX_NUM_GPUS] = {};

	TArray<FRayTracingGeometryInstance> Instances;

	// Unique list of geometries referenced by all instances in this scene.
	// Any referenced geometry is kept alive while the scene is alive.
	TArray<TRefCountPtr<FD3D12RayTracingGeometry>> Geometries;

	// Scene keeps track of child acceleration structure buffers to ensure
	// they are resident when any ray tracing work is dispatched.
	TArray<FD3D12ResidencyHandle*> GeometryResidencyHandles[MAX_NUM_GPUS];

	void UpdateResidency(FD3D12CommandContext& CommandContext);

	uint32 ShaderSlotsPerGeometrySegment = 1;

	// Exclusive prefix sum of `Instance.NumTransforms` for all instances in this scene. Used to emulate SV_InstanceID in hit shaders.
	TArray<uint32> BaseInstancePrefixSum;

	// Exclusive prefix sum of instance geometry segments is used to calculate SBT record address from instance and segment indices.
	TArray<uint32> SegmentPrefixSum;
	uint32 NumTotalSegments = 0;
	uint32 GetHitRecordBaseIndex(uint32 InstanceIndex, uint32 SegmentIndex) const { return (SegmentPrefixSum[InstanceIndex] + SegmentIndex) * ShaderSlotsPerGeometrySegment; }

	uint64 TotalPrimitiveCount = 0; // Combined number of primitives in all geometry instances

	uint32 NumCallableShaderSlots = 0;
	uint32 NumMissShaderSlots = 1; // always at least the default

	TArray<FHitGroupSystemParameters> HitGroupSystemParametersCache;

	// #dxr_todo UE-68230: shader tables should be explicitly registered and unregistered with the scene
	FD3D12RayTracingShaderTable* FindOrCreateShaderTable(const FD3D12RayTracingPipelineState* Pipeline, FD3D12Device* Device);
	FD3D12RayTracingShaderTable* FindExistingShaderTable(const FD3D12RayTracingPipelineState* Pipeline, FD3D12Device* Device) const;

	TMap<const FD3D12RayTracingPipelineState*, FD3D12RayTracingShaderTable*> ShaderTables[MAX_NUM_GPUS];

	ERayTracingSceneLifetime Lifetime = RTSL_SingleFrame;
	uint64 CreatedFrameFenceValue = 0;

	uint64 LastCommandListID = 0;

	FName DebugName;
};

#endif // D3D12_RHI_RAYTRACING
