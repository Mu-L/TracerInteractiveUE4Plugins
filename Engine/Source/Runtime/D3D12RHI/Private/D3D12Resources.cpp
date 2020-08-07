// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
D3D12Resources.cpp: D3D RHI utility implementation.
=============================================================================*/

#include "D3D12RHIPrivate.h"
#include "EngineModule.h"
#include "HAL/LowLevelMemTracker.h"

D3D12RHI_API int32 GD3D12AsyncDeferredDeletion = ASYNC_DEFERRED_DELETION;

static FAutoConsoleVariableRef CVarAsyncDeferredDeletion(
	TEXT("D3D12.AsyncDeferredDeletion"),
	GD3D12AsyncDeferredDeletion,
	TEXT("Controls whether D3D12 resources will be released on a separate thread (default = ")
#if ASYNC_DEFERRED_DELETION
	TEXT("on")
#else
	TEXT("off")
#endif
	TEXT(")."),
	ECVF_ReadOnly
);

/////////////////////////////////////////////////////////////////////
//	FD3D12 Deferred Deletion Queue
/////////////////////////////////////////////////////////////////////

FD3D12DeferredDeletionQueue::FD3D12DeferredDeletionQueue(FD3D12Adapter* InParent) :
	FD3D12AdapterChild(InParent) {}

FD3D12DeferredDeletionQueue::~FD3D12DeferredDeletionQueue()
{
	FAsyncTask<FD3D12AsyncDeletionWorker>* DeleteTask = nullptr;
	while (DeleteTasks.Peek(DeleteTask))
	{
		DeleteTasks.Dequeue(DeleteTask);
		DeleteTask->EnsureCompletion(true);
		delete(DeleteTask);
	}
}

void FD3D12DeferredDeletionQueue::EnqueueResource(FD3D12Resource* pResource, FD3D12Fence* Fence)
{
	check(pResource->ShouldDeferDelete());

	// Useful message for identifying when resources are released on the rendering thread.
	//UE_CLOG(IsInActualRenderingThread(), LogD3D12RHI, Display, TEXT("Rendering Thread: Deleting %#016llx when done with frame fence %llu"), pResource, Fence->GetCurrentFence());

	FencedObjectType FencedObject;
	FencedObject.RHIObject  = pResource;
	FencedObject.Fence      = Fence;
	FencedObject.FenceValue = Fence->GetCurrentFence();
	FencedObject.Type       = EObjectType::RHI;
	DeferredReleaseQueue.Enqueue(FencedObject);
}

void FD3D12DeferredDeletionQueue::EnqueueResource(ID3D12Object* pResource, FD3D12Fence* Fence)
{
	// Useful message for identifying when resources are released on the rendering thread.
	//UE_CLOG(IsInActualRenderingThread(), LogD3D12RHI, Display, TEXT("Rendering Thread: Deleting %#016llx when done with frame fence %llu"), pResource, Fence->GetCurrentFence());

	FencedObjectType FencedObject;
	FencedObject.D3DObject  = pResource;
	FencedObject.Fence      = Fence;
	FencedObject.FenceValue = Fence->GetCurrentFence();
	FencedObject.Type       = EObjectType::D3D;
	DeferredReleaseQueue.Enqueue(FencedObject);
}

bool FD3D12DeferredDeletionQueue::ReleaseResources(bool bDeleteImmediately, bool bIsShutDown)
{
	FD3D12Adapter* Adapter = GetParentAdapter();

	if (GD3D12AsyncDeferredDeletion)
	{
		if (bDeleteImmediately)
		{
			FAsyncTask<FD3D12AsyncDeletionWorker>* DeleteTask = nullptr;
			// Call back all threads
			while (DeleteTasks.Peek(DeleteTask))
			{
				DeleteTasks.Dequeue(DeleteTask);
				DeleteTask->EnsureCompletion(true);
				delete(DeleteTask);
			}
		}
		else
		{
			FAsyncTask<FD3D12AsyncDeletionWorker>* DeleteTask = nullptr;
			while (DeleteTasks.Peek(DeleteTask) && DeleteTask->IsDone())
			{
				DeleteTasks.Dequeue(DeleteTask);
				delete(DeleteTask);
			}

			DeleteTask = new FAsyncTask<FD3D12AsyncDeletionWorker>(Adapter, &DeferredReleaseQueue);

			DeleteTask->StartBackgroundTask();
			DeleteTasks.Enqueue(DeleteTask);

			return false;
		}
	}

	FencedObjectType FenceObject;

	if (bIsShutDown)
	{
		// FORT-236194 - Output what we are releasing on exit to catch a crash on Release()
		UE_LOG(LogD3D12RHI, Display, TEXT("D3D12 ReleaseResources: %u items to release"), DeferredReleaseQueue.GetSize());

		while (DeferredReleaseQueue.Dequeue(FenceObject))
		{
			if (FenceObject.Type == EObjectType::RHI)
			{
				D3D12_RESOURCE_DESC Desc = FenceObject.RHIObject->GetDesc();
				FString Name = FenceObject.RHIObject->GetName().ToString();
				UE_LOG(LogD3D12RHI, Display, TEXT("D3D12 ReleaseResources: \"%s\", %llu x %u x %u, Mips: %u, Format: 0x%X, Flags: 0x%X"), *Name, Desc.Width, Desc.Height, Desc.DepthOrArraySize, Desc.MipLevels, Desc.Format, Desc.Flags);

				uint32 RefCount = FenceObject.RHIObject->Release();
				if (RefCount)
				{
					UE_LOG(LogD3D12RHI, Display, TEXT("RefCount was %u"), RefCount);
				}
			}
			else
			{
				UE_LOG(LogD3D12RHI, Display, TEXT("D3D12 ReleaseResources: 0x%llX"), FenceObject.D3DObject);

				uint32 RefCount = FenceObject.D3DObject->Release();
				if (RefCount)
				{
					UE_LOG(LogD3D12RHI, Display, TEXT("RefCount was %u"), RefCount);
				}
			}
		}
	}
	else
	{
		struct FDequeueFenceObject
		{
			bool operator() (FencedObjectType FenceObject) const
			{
				return FenceObject.Fence->IsFenceComplete(FenceObject.FenceValue);
			}
		};

		while (DeferredReleaseQueue.Dequeue(FenceObject, FDequeueFenceObject()))
		{
			if (FenceObject.Type == EObjectType::RHI)
			{
				FenceObject.RHIObject->Release();
			}
			else
			{
				FenceObject.D3DObject->Release();
			}
		}
	}

	return DeferredReleaseQueue.IsEmpty();
}

FD3D12DeferredDeletionQueue::FD3D12AsyncDeletionWorker::FD3D12AsyncDeletionWorker(FD3D12Adapter* Adapter, FThreadsafeQueue<FencedObjectType>* DeletionQueue)
	: FD3D12AdapterChild(Adapter)
{
	struct FDequeueFenceObject
	{
		bool operator() (FencedObjectType FenceObject) const
		{
			return FenceObject.Fence->IsFenceComplete(FenceObject.FenceValue);
		}
	};

	DeletionQueue->BatchDequeue(&Queue, FDequeueFenceObject(), 4096);
}

void FD3D12DeferredDeletionQueue::FD3D12AsyncDeletionWorker::DoWork()
{
	FencedObjectType ResourceToDelete;

	while (Queue.Dequeue(ResourceToDelete))
	{
		if (ResourceToDelete.Type == EObjectType::RHI)
		{
			// This should be a final release.
			check(ResourceToDelete.RHIObject->GetRefCount() == 1);
			ResourceToDelete.RHIObject->Release();
		}
		else
		{
			ResourceToDelete.D3DObject->Release();
		}
	}
}

/////////////////////////////////////////////////////////////////////
//	FD3D12 Resource
/////////////////////////////////////////////////////////////////////

#if UE_BUILD_DEBUG
int64 FD3D12Resource::TotalResourceCount = 0;
int64 FD3D12Resource::NoStateTrackingResourceCount = 0;
#endif

FD3D12Resource::FD3D12Resource(FD3D12Device* ParentDevice,
	FRHIGPUMask VisibleNodes,
	ID3D12Resource* InResource,
	D3D12_RESOURCE_STATES InitialState,
	D3D12_RESOURCE_DESC const& InDesc,
	FD3D12Heap* InHeap,
	D3D12_HEAP_TYPE InHeapType)
	: FD3D12DeviceChild(ParentDevice)
	, FD3D12MultiNodeGPUObject(ParentDevice->GetGPUMask(), VisibleNodes)
	, Resource(InResource)
	, Heap(InHeap)
	, ResidencyHandle()
	, Desc(InDesc)
	, PlaneCount(::GetPlaneCount(Desc.Format))
	, SubresourceCount(0)
	, DefaultResourceState(D3D12_RESOURCE_STATE_TBD)
	, bRequiresResourceStateTracking(true)
	, bDepthStencil(false)
	, bDeferDelete(true)
	, HeapType(InHeapType)
	, GPUVirtualAddress(0)
	, ResourceBaseAddress(nullptr)
{
#if UE_BUILD_DEBUG
	FPlatformAtomics::InterlockedIncrement(&TotalResourceCount);
#endif

	if (Resource
#if PLATFORM_WINDOWS
		&& Desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER
#endif
		)
	{
		GPUVirtualAddress = Resource->GetGPUVirtualAddress();
	}

	InitalizeResourceState(InitialState);
}

FD3D12Resource::~FD3D12Resource()
{
	if (D3DX12Residency::IsInitialized(ResidencyHandle))
	{
		D3DX12Residency::EndTrackingObject(GetParentDevice()->GetResidencyManager(), ResidencyHandle);
	}
}

void FD3D12Resource::StartTrackingForResidency()
{
#if ENABLE_RESIDENCY_MANAGEMENT
	check(IsCPUInaccessible(HeapType));	// This is checked at a higher level before calling this function.
	check(D3DX12Residency::IsInitialized(ResidencyHandle) == false);
	const D3D12_RESOURCE_DESC ResourceDesc = Resource->GetDesc();
	const D3D12_RESOURCE_ALLOCATION_INFO Info = GetParentDevice()->GetDevice()->GetResourceAllocationInfo(0, 1, &ResourceDesc);

	D3DX12Residency::Initialize(ResidencyHandle, Resource.GetReference(), Info.SizeInBytes);
	D3DX12Residency::BeginTrackingObject(GetParentDevice()->GetResidencyManager(), ResidencyHandle);
#endif
}

void FD3D12Resource::UpdateResidency(FD3D12CommandListHandle& CommandList)
{
#if ENABLE_RESIDENCY_MANAGEMENT
	if (IsPlacedResource())
	{
		Heap->UpdateResidency(CommandList);
	}
	else if (D3DX12Residency::IsInitialized(ResidencyHandle))
	{
		check(Heap == nullptr);
		D3DX12Residency::Insert(CommandList.GetResidencySet(), ResidencyHandle);
	}
#endif
}

void FD3D12Resource::DeferDelete()
{
	GetParentDevice()->GetParentAdapter()->GetDeferredDeletionQueue().EnqueueResource(this, &GetParentDevice()->GetCommandListManager().GetFence());
}

/////////////////////////////////////////////////////////////////////
//	FD3D12 Heap
/////////////////////////////////////////////////////////////////////

FD3D12Heap::FD3D12Heap(FD3D12Device* Parent, FRHIGPUMask VisibleNodes) :
	FD3D12DeviceChild(Parent),
	FD3D12MultiNodeGPUObject(Parent->GetGPUMask(), VisibleNodes),
	ResidencyHandle()
{
}

FD3D12Heap::~FD3D12Heap()
{
	Destroy();
}

void FD3D12Heap::UpdateResidency(FD3D12CommandListHandle& CommandList)
{
#if ENABLE_RESIDENCY_MANAGEMENT
	if (D3DX12Residency::IsInitialized(ResidencyHandle))
	{
		D3DX12Residency::Insert(CommandList.GetResidencySet(), ResidencyHandle);
	}
#endif
}

void FD3D12Heap::Destroy()
{
	//TODO: Check ref counts?
	if (D3DX12Residency::IsInitialized(ResidencyHandle))
	{
		D3DX12Residency::EndTrackingObject(GetParentDevice()->GetResidencyManager(), ResidencyHandle);
		ResidencyHandle = {};
	}
}

void FD3D12Heap::BeginTrackingResidency(uint64 Size)
{
#if ENABLE_RESIDENCY_MANAGEMENT
	D3DX12Residency::Initialize(ResidencyHandle, Heap.GetReference(), Size);
	D3DX12Residency::BeginTrackingObject(GetParentDevice()->GetResidencyManager(), ResidencyHandle);
#endif
}

/////////////////////////////////////////////////////////////////////
//	FD3D12 Adapter
/////////////////////////////////////////////////////////////////////

HRESULT FD3D12Adapter::CreateCommittedResource(const D3D12_RESOURCE_DESC& InDesc, FRHIGPUMask CreationNode, const D3D12_HEAP_PROPERTIES& HeapProps, const D3D12_RESOURCE_STATES& InitialUsage, const D3D12_CLEAR_VALUE* ClearValue, FD3D12Resource** ppOutResource, const TCHAR* Name, bool bVerifyHResult)
{
	if (!ppOutResource)
	{
		return E_POINTER;
	}

	LLM_PLATFORM_SCOPE(ELLMTag::GraphicsPlatform);

	TRefCountPtr<ID3D12Resource> pResource;
	D3D12_HEAP_FLAGS HeapFlags = D3D12_HEAP_FLAG_NONE;
	if (InDesc.Flags & D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS)
	{
		HeapFlags |= D3D12_HEAP_FLAG_SHARED;
	}

	const HRESULT hr = RootDevice->CreateCommittedResource(&HeapProps, HeapFlags, &InDesc, InitialUsage, ClearValue, IID_PPV_ARGS(pResource.GetInitReference()));
	if (bVerifyHResult)
	{
		VERIFYD3D12RESULT_EX(hr, RootDevice);
	}

	if (SUCCEEDED(hr))
	{
		// Set a default name (can override later).
		SetName(pResource, Name);

		// Set the output pointer
		*ppOutResource = new FD3D12Resource(GetDevice(CreationNode.ToIndex()), CreationNode, pResource, InitialUsage, InDesc, nullptr, HeapProps.Type);
		(*ppOutResource)->AddRef();

		// Only track resources that cannot be accessed on the CPU.
		if (IsCPUInaccessible(HeapProps.Type))
		{
			(*ppOutResource)->StartTrackingForResidency();
		}
	}

	return hr;
}

HRESULT FD3D12Adapter::CreatePlacedResource(const D3D12_RESOURCE_DESC& InDesc, FD3D12Heap* BackingHeap, uint64 HeapOffset, const D3D12_RESOURCE_STATES& InitialUsage, const D3D12_CLEAR_VALUE* ClearValue, FD3D12Resource** ppOutResource, const TCHAR* Name, bool bVerifyHResult)
{
	if (!ppOutResource)
	{
		return E_POINTER;
	}

	ID3D12Heap* Heap = BackingHeap->GetHeap();

	TRefCountPtr<ID3D12Resource> pResource;
	const HRESULT hr = RootDevice->CreatePlacedResource(Heap, HeapOffset, &InDesc, InitialUsage, nullptr, IID_PPV_ARGS(pResource.GetInitReference()));

	if (bVerifyHResult)
	{
		VERIFYD3D12RESULT_EX(hr, RootDevice);
	}

	if (SUCCEEDED(hr))
	{
		// Set a default name (can override later).
		SetName(pResource, Name);

		FD3D12Device* Device = BackingHeap->GetParentDevice();
		const D3D12_HEAP_DESC HeapDesc = Heap->GetDesc();

		// Set the output pointer
		*ppOutResource = new FD3D12Resource(Device,
			Device->GetVisibilityMask(),
			pResource,
			InitialUsage,
			InDesc,
			BackingHeap,
			HeapDesc.Properties.Type);

		(*ppOutResource)->AddRef();
	}

	return hr;
}

HRESULT FD3D12Adapter::CreateBuffer(D3D12_HEAP_TYPE HeapType, FRHIGPUMask CreationNode, FRHIGPUMask VisibleNodes, uint64 HeapSize, FD3D12Resource** ppOutResource, const TCHAR* Name, D3D12_RESOURCE_FLAGS Flags)
{
	const D3D12_HEAP_PROPERTIES HeapProps = CD3DX12_HEAP_PROPERTIES(HeapType, CreationNode.GetNative(), VisibleNodes.GetNative());
	const D3D12_RESOURCE_STATES InitialState = DetermineInitialResourceState(HeapProps.Type, &HeapProps);
	return CreateBuffer(HeapProps, CreationNode, InitialState, HeapSize, ppOutResource, Name, Flags);
}

HRESULT FD3D12Adapter::CreateBuffer(D3D12_HEAP_TYPE HeapType, FRHIGPUMask CreationNode, FRHIGPUMask VisibleNodes, D3D12_RESOURCE_STATES InitialState, uint64 HeapSize, FD3D12Resource** ppOutResource, const TCHAR* Name, D3D12_RESOURCE_FLAGS Flags)
{
	const D3D12_HEAP_PROPERTIES HeapProps = CD3DX12_HEAP_PROPERTIES(HeapType, CreationNode.GetNative(), VisibleNodes.GetNative());
	return CreateBuffer(HeapProps, CreationNode, InitialState, HeapSize, ppOutResource, Name, Flags);
}

HRESULT FD3D12Adapter::CreateBuffer(const D3D12_HEAP_PROPERTIES& HeapProps,
	FRHIGPUMask CreationNode,
	D3D12_RESOURCE_STATES InitialState,
	uint64 HeapSize,
	FD3D12Resource** ppOutResource,
	const TCHAR* Name,
	D3D12_RESOURCE_FLAGS Flags)
{
	if (!ppOutResource)
	{
		return E_POINTER;
	}

	const D3D12_RESOURCE_DESC BufDesc = CD3DX12_RESOURCE_DESC::Buffer(HeapSize, Flags);
	return CreateCommittedResource(BufDesc,
		CreationNode,
		HeapProps,
		InitialState,
		nullptr,
		ppOutResource, Name);
}

/////////////////////////////////////////////////////////////////////
//	FD3D12 Resource Location
/////////////////////////////////////////////////////////////////////

FD3D12ResourceLocation::FD3D12ResourceLocation(FD3D12Device* Parent)
	: FD3D12DeviceChild(Parent)
	, Type(ResourceLocationType::eUndefined)
	, UnderlyingResource(nullptr)
	, ResidencyHandle(nullptr)
	, Allocator(nullptr)
	, MappedBaseAddress(nullptr)
	, GPUVirtualAddress(0)
	, OffsetFromBaseOfResource(0)
	, Size(0)
	, bTransient(false)
	, AllocatorType(AT_Unknown)
{
	FMemory::Memzero(AllocatorData);
}

FD3D12ResourceLocation::~FD3D12ResourceLocation()
{
	ReleaseResource();
}

void FD3D12ResourceLocation::Clear()
{
	InternalClear<true>();
}

template void FD3D12ResourceLocation::InternalClear<false>();
template void FD3D12ResourceLocation::InternalClear<true>();

template<bool bReleaseResource>
void FD3D12ResourceLocation::InternalClear()
{
	if (bReleaseResource)
	{
		ReleaseResource();
	}

	// Reset members
	Type = ResourceLocationType::eUndefined;
	UnderlyingResource = nullptr;
	MappedBaseAddress = nullptr;
	GPUVirtualAddress = 0;
	ResidencyHandle = nullptr;
	Size = 0;
	OffsetFromBaseOfResource = 0;
	FMemory::Memzero(AllocatorData);

	Allocator = nullptr;
	AllocatorType = AT_Unknown;
}

void FD3D12ResourceLocation::TransferOwnership(FD3D12ResourceLocation& Destination, FD3D12ResourceLocation& Source)
{
	// Clear out the destination
	Destination.Clear();

	FMemory::Memmove(&Destination, &Source, sizeof(FD3D12ResourceLocation));

	// update tracked allocation
#if !PLATFORM_WINDOWS && ENABLE_LOW_LEVEL_MEM_TRACKER
	if (Source.GetType() == ResourceLocationType::eSubAllocation && Source.AllocatorType != AT_SegList)
	{
		FLowLevelMemTracker::Get().OnLowLevelAllocMoved( ELLMTracker::Default, &Destination, &Source );
	}
#endif

	// Destroy the source but don't invoke any resource destruction
	Source.InternalClear<false>();
}

void FD3D12ResourceLocation::Swap(FD3D12ResourceLocation& Other)
{
	// TODO: Probably shouldn't manually track suballocations. It's error-prone and inaccurate
#if !PLATFORM_WINDOWS && ENABLE_LOW_LEVEL_MEM_TRACKER
	const bool bRequiresManualTracking = GetType() == ResourceLocationType::eSubAllocation && AllocatorType != AT_SegList;
	const bool bOtherRequiresManualTracking = Other.GetType() == ResourceLocationType::eSubAllocation && Other.AllocatorType != AT_SegList;

	if (bRequiresManualTracking)
	{
		FLowLevelMemTracker::Get().OnLowLevelFree(ELLMTracker::Default, this);
	}
	if (bOtherRequiresManualTracking)
	{
		FLowLevelMemTracker::Get().OnLowLevelAllocMoved(ELLMTracker::Default, this, &Other);
	}
	if (bRequiresManualTracking)
	{
		FLowLevelMemTracker::Get().OnLowLevelAlloc(ELLMTracker::Default, &Other, GetSize());
	}
#endif

	::Swap(*this, Other);
}

void FD3D12ResourceLocation::Alias(FD3D12ResourceLocation & Destination, FD3D12ResourceLocation & Source)
{
	check(Source.GetResource() != nullptr);
	Destination.Clear();

	FMemory::Memmove(&Destination, &Source, sizeof(FD3D12ResourceLocation));
	Destination.SetType(ResourceLocationType::eAliased);
	Source.SetType(ResourceLocationType::eAliased);

	// Addref the source as another resource location references it
	Source.GetResource()->AddRef();
}

void FD3D12ResourceLocation::ReferenceNode(FD3D12Device* DestinationDevice, FD3D12ResourceLocation& Destination, FD3D12ResourceLocation& Source)
{
	check(Source.GetResource() != nullptr);
	Destination.Clear();

	FMemory::Memmove(&Destination, &Source, sizeof(FD3D12ResourceLocation));
	Destination.SetType(ResourceLocationType::eNodeReference);

	Destination.Parent = DestinationDevice;

	// Addref the source as another resource location references it
	Source.GetResource()->AddRef();
}

void FD3D12ResourceLocation::ReleaseResource()
{
	switch (Type)
	{
	case ResourceLocationType::eStandAlone:
	{
		// Multi-GPU support : because of references, several GPU nodes can refrence the same stand-alone resource.
		check(UnderlyingResource->GetRefCount() == 1 || GNumExplicitGPUsForRendering > 1);
		
		if (UnderlyingResource->ShouldDeferDelete())
		{
			UnderlyingResource->DeferDelete();
		}
		else
		{
			UnderlyingResource->Release();
		}
		break;
	}
	case ResourceLocationType::eSubAllocation:
	{
		check(Allocator != nullptr);
		if (AllocatorType == AT_SegList)
		{
			SegListAllocator->Deallocate(
				GetResource(),
				GetSegListAllocatorPrivateData().Offset,
				GetSize());
		}
		else
		{
			Allocator->Deallocate(*this);
		}
		break;
	}
	case ResourceLocationType::eNodeReference:
	case ResourceLocationType::eAliased:
	{
		if (UnderlyingResource->ShouldDeferDelete() && UnderlyingResource->GetRefCount() == 1)
		{
			UnderlyingResource->DeferDelete();
		}
		else
		{
			UnderlyingResource->Release();
		}
		break;
	}
	case ResourceLocationType::eHeapAliased:
	{
		check(UnderlyingResource->GetRefCount() == 1);
		if (UnderlyingResource->ShouldDeferDelete())
		{
			UnderlyingResource->DeferDelete();
		}
		else
		{
			UnderlyingResource->Release();
		}
		break;
	}
	case ResourceLocationType::eFastAllocation:
	case ResourceLocationType::eUndefined:
	default:
		// Fast allocations are volatile by default so no work needs to be done.
		break;
	}
}

void FD3D12ResourceLocation::SetResource(FD3D12Resource* Value)
{
	check(UnderlyingResource == nullptr);
	check(ResidencyHandle == nullptr);

	GPUVirtualAddress = Value->GetGPUVirtualAddress();

	UnderlyingResource = Value;
	ResidencyHandle = UnderlyingResource->GetResidencyHandle();
}
