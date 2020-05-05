// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
StaticMeshUpdate.h: Helpers to stream in and out static mesh LODs.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Engine/StaticMesh.h"
#include "Async/AsyncFileHandle.h"

/**
* A context used to update or proceed with the next update step.
* The mesh and render data references could be stored in the update object
* but are currently kept outside to avoid lifetime management within the object.
*/
struct FStaticMeshUpdateContext
{
	typedef int32 EThreadType;

	FStaticMeshUpdateContext(UStaticMesh* InMesh, EThreadType InCurrentThread);

	FStaticMeshUpdateContext(UStreamableRenderAsset* InMesh, EThreadType InCurrentThread);

	UStreamableRenderAsset* GetRenderAsset() const
	{
		return Mesh;
	}

	EThreadType GetCurrentThread() const
	{
		return CurrentThread;
	}

	/** The mesh to update, this must be the same one as the one used when creating the FStaticMeshUpdate object. */
	UStaticMesh* Mesh;
	/** The current render data of this mesh. */
	FStaticMeshRenderData* RenderData;
	/** The thread on which the context was created. */
	EThreadType CurrentThread;
};

// Declare that TRenderAssetUpdate is instantiated for FStaticMeshUpdateContext
extern template class TRenderAssetUpdate<FStaticMeshUpdateContext>;

/**
* This class provides a framework for loading and unloading the LODs of static meshes.
* Each thread essentially calls Tick() until the job is done.
* The object can be safely deleted when IsCompleted() returns true.
*/
class FStaticMeshUpdate : public TRenderAssetUpdate<FStaticMeshUpdateContext>
{
public:
	FStaticMeshUpdate(UStaticMesh* InMesh, int32 InRequestedMips);

	virtual void Abort()
	{
		TRenderAssetUpdate<FStaticMeshUpdateContext>::Abort();
	}

protected:

	virtual ~FStaticMeshUpdate() {}

	/** Cached index of current first LOD that will be replaced by PendingFirstMip */
	int32 CurrentFirstLODIdx;
};

class FStaticMeshStreamIn : public FStaticMeshUpdate
{
public:
	FStaticMeshStreamIn(UStaticMesh* InMesh, int32 InRequestedMips);

	virtual ~FStaticMeshStreamIn();

protected:
	/** Correspond to the buffers in FStaticMeshLODResources */
	struct FIntermediateBuffers
	{
		FVertexBufferRHIRef TangentsVertexBuffer;
		FVertexBufferRHIRef TexCoordVertexBuffer;
		FVertexBufferRHIRef PositionVertexBuffer;
		FVertexBufferRHIRef ColorVertexBuffer;
		FIndexBufferRHIRef IndexBuffer;
		FIndexBufferRHIRef ReversedIndexBuffer;
		FIndexBufferRHIRef DepthOnlyIndexBuffer;
		FIndexBufferRHIRef ReversedDepthOnlyIndexBuffer;
		FIndexBufferRHIRef WireframeIndexBuffer;
		FIndexBufferRHIRef AdjacencyIndexBuffer;

		void CreateFromCPUData_RenderThread(UStaticMesh* Mesh, FStaticMeshLODResources& LODResource);
		void CreateFromCPUData_Async(UStaticMesh* Mesh, FStaticMeshLODResources& LODResource);

		void SafeRelease();

		/** Transfer ownership of buffers to a LOD resource */
		template <uint32 MaxNumUpdates>
		void TransferBuffers(FStaticMeshLODResources& LODResource, TRHIResourceUpdateBatcher<MaxNumUpdates>& Batcher);

		void CheckIsNull() const;
	};

	/** Create buffers with new LOD data on render or pooled thread */
	void CreateBuffers_RenderThread(const FContext& Context);
	void CreateBuffers_Async(const FContext& Context);

	/** Discard newly streamed-in CPU data */
	void DiscardNewLODs(const FContext& Context);

	/** Apply the new buffers (if not cancelled) and finish the update process. When cancelled, the intermediate buffers simply gets discarded. */
	void DoFinishUpdate(const FContext& Context);

	/** Discard streamed-in CPU data and intermediate RHI buffers */
	void DoCancel(const FContext& Context);

	/** The intermediate buffers created in the update process. */
	FIntermediateBuffers IntermediateBuffersArray[MAX_MESH_LOD_COUNT];

private:
	template <bool bRenderThread>
	void CreateBuffers_Internal(const FContext& Context);
};

/** A streamout that doesn't actually touches the CPU data. Required because DDC stream in doesn't reset. */
class FStaticMeshStreamOut : public FStaticMeshUpdate
{
public:
	FStaticMeshStreamOut(UStaticMesh* InMesh, int32 InRequestedMips, bool InDiscardCPUData);

private:

	void CheckReferencesAndDiscardCPUData(const FContext& Context);
	void ReleaseRHIBuffers(const FContext& Context);
	/** Restore */
	void Cancel(const FContext& Context);

	uint8 InitialFirstLOD = 0;
	bool bDiscardCPUData = false;
	int32 NumReferenceChecks = 0;
	uint32 PreviousNumberOfExternalReferences = 0;
};

class FStaticMeshStreamIn_IO : public FStaticMeshStreamIn
{
public:
	FStaticMeshStreamIn_IO(UStaticMesh* InMesh, int32 InRequestedMips, bool bHighPrio);

	virtual ~FStaticMeshStreamIn_IO() {}

	virtual void Abort() override;

protected:
	class FCancelIORequestsTask : public FNonAbandonableTask
	{
	public:
		FCancelIORequestsTask(FStaticMeshStreamIn_IO* InPendingUpdate)
			: PendingUpdate(InPendingUpdate)
		{}

		void DoWork();

		FORCEINLINE TStatId GetStatId() const
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(FCancelIORequestsTask_StaticMesh, STATGROUP_ThreadPoolAsyncTasks);
		}

	private:
		TRefCountPtr<FStaticMeshStreamIn_IO> PendingUpdate;
	};

	typedef FAutoDeleteAsyncTask<FCancelIORequestsTask> FAsyncCancelIORequestsTask;
	friend class FCancelIORequestsTask;

	/** Figure out the full name of the .bulk file */
	FString GetIOFilename(const FContext& Context);

	/** Set a callback called when IORequest is completed or cancelled */
	void SetAsyncFileCallback(const FContext& Context);

	/** Create a new async IO request to read in LOD data */
	void SetIORequest(const FContext& Context, const FString& IOFilename);

	/** Release IORequest and IOFileHandle. IORequest will be cancelled if still inflight */
	void ClearIORequest(const FContext& Context);

	/** Serialize data of new LODs to corresponding FStaticMeshLODResources */
	void SerializeLODData(const FContext& Context);

	/** Called by FAsyncCancelIORequestsTask to cancel inflight IO request if any */
	void CancelIORequest();

	class IBulkDataIORequest* IORequest;
	FBulkDataIORequestCallBack AsyncFileCallback;
	bool bHighPrioIORequest;
};

template <bool bRenderThread>
class TStaticMeshStreamIn_IO : public FStaticMeshStreamIn_IO
{
public:
	TStaticMeshStreamIn_IO(UStaticMesh* InMesh, int32 InRequestedMips, bool bHighPrio);

	virtual ~TStaticMeshStreamIn_IO() {}

protected:
	void DoInitiateIO(const FContext& Context);

	void DoSerializeLODData(const FContext& Context);

	void DoCreateBuffers(const FContext& Context);

	void DoCancelIO(const FContext& Context);
};

typedef TStaticMeshStreamIn_IO<true> FStaticMeshStreamIn_IO_RenderThread;
typedef TStaticMeshStreamIn_IO<false> FStaticMeshStreamIn_IO_Async;

#if WITH_EDITOR
class FStaticMeshStreamIn_DDC : public FStaticMeshStreamIn
{
public:
	FStaticMeshStreamIn_DDC(UStaticMesh* InMesh, int32 InRequestedMips);

	virtual ~FStaticMeshStreamIn_DDC() {}

	bool DDCIsInvalid() const override { return bDerivedDataInvalid; }

protected:
	void LoadNewLODsFromDDC(const FContext& Context);

	bool bDerivedDataInvalid;
};

template <bool bRenderThread>
class TStaticMeshStreamIn_DDC : public FStaticMeshStreamIn_DDC
{
public:
	TStaticMeshStreamIn_DDC(UStaticMesh* InMesh, int32 InRequestedMips);

	virtual ~TStaticMeshStreamIn_DDC() {}

private:
	/** Load new LOD buffers from DDC and queue a task to create RHI buffers on RT */
	void DoLoadNewLODsFromDDC(const FContext& Context);

	/** Create RHI buffers for newly streamed-in LODs and queue a task to rename references on RT */
	void DoCreateBuffers(const FContext& Context);
};

typedef TStaticMeshStreamIn_DDC<true> FStaticMeshStreamIn_DDC_RenderThread;
typedef TStaticMeshStreamIn_DDC<false> FStaticMeshStreamIn_DDC_Async;
#endif