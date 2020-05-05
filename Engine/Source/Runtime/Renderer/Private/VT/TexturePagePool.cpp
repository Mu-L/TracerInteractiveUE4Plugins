// Copyright Epic Games, Inc. All Rights Reserved.

#include "TexturePagePool.h"

#include "VirtualTextureSpace.h"
#include "VirtualTextureSystem.h"

FTexturePagePool::FTexturePagePool()
	: PageHash(16u * 1024)
	, NumPages(0u)
	, NumPagesMapped(0u)
{
}

FTexturePagePool::~FTexturePagePool()
{}


void FTexturePagePool::Initialize(uint32 InNumPages)
{

	NumPages = InNumPages;
	Pages.AddZeroed(InNumPages);
	PageHash.Resize(InNumPages);

	FreeHeap.Resize(InNumPages, InNumPages);

	for (uint32 i = 0; i < InNumPages; i++)
	{
		FreeHeap.Add(0, i);
	}

	// Initialize list head for each page, plus one for free list
	PageMapping.AddUninitialized(InNumPages + 1u);
	for (uint32 i = 0; i <= InNumPages; i++)
	{
		FPageMapping& Mapping = PageMapping[i];
		Mapping.PackedValues = 0xffffffff;
		Mapping.PageTableLayerIndex = 0xff;
		Mapping.NextIndex = Mapping.PrevIndex = i;
	}
}

void FTexturePagePool::EvictAllPages(FVirtualTextureSystem* System)
{

	TArray<uint16> PagesToEvict;
	while (FreeHeap.Num() > 0u)
	{
		const uint16 pAddress = FreeHeap.Top();
		FreeHeap.Pop();
		PagesToEvict.Add(pAddress);
	}

	for (int32 i = 0; i < PagesToEvict.Num(); i++)
	{
		UnmapAllPages(System, PagesToEvict[i], false);
		FreeHeap.Add(0, PagesToEvict[i]);
	}
}

void FTexturePagePool::UnmapAllPagesForSpace(FVirtualTextureSystem* System, uint8 SpaceID)
{
	// walk through all of our current mapping entries, and unmap any that belong to the current space
	for (int32 MappingIndex = NumPages + 1; MappingIndex < PageMapping.Num(); ++MappingIndex)
	{
		struct FPageMapping& Mapping = PageMapping[MappingIndex];
		if (Mapping.PageTableLayerIndex != 0xff && Mapping.SpaceID == SpaceID)
		{
			// we're unmapping all pages for space, so don't try to map any ancestor pages...they'll be unmapped as well
			UnmapPageMapping(System, MappingIndex, false);
		}
	}
}

void FTexturePagePool::EvictPages(FVirtualTextureSystem* System, const FVirtualTextureProducerHandle& ProducerHandle)
{
	for (uint32 pAddress = 0u; pAddress < NumPages; ++pAddress)
	{
		const FPageEntry& PageEntry = Pages[pAddress];
		if (PageEntry.PackedProducerHandle == ProducerHandle.PackedValue)
		{
			UnmapAllPages(System, pAddress, false);
			FreeHeap.Update(0, pAddress);
		}
	}
}

void FTexturePagePool::EvictPages(FVirtualTextureSystem* System, FVirtualTextureProducerHandle const& ProducerHandle, FVTProducerDescription const& Desc, FIntRect const& TextureRegion, uint32 MaxLevel, TArray<union FVirtualTextureLocalTile>& OutLocked)
{
	//todo[vt]: 
	// Simple linear iteration of all physical pages here. Can we do better?
	// We should test if it's faster to store a physical page list sorted by Morton code and find upper and lower bounds in that.
	for (uint32 i = 0; i < NumPages; ++i)
	{
		if (Pages[i].PackedProducerHandle == ProducerHandle.PackedValue)
		{
			const uint32 vAddress = Pages[i].Local_vAddress;
			const uint32 vLevel = Pages[i].Local_vLevel;

			if (vLevel <= MaxLevel)
			{
				const int32 TileSize = Desc.TileSize << vLevel;
				const int32 X = FMath::ReverseMortonCode2(vAddress) * TileSize;
				const int32 Y = FMath::ReverseMortonCode2(vAddress >> 1) * TileSize;
				const int32 TileBorderSize = Desc.TileBorderSize << vLevel;
				const FIntRect PageRect(X - TileBorderSize, Y - TileBorderSize, X + TileSize + TileBorderSize, Y + TileSize + TileBorderSize);

				if (!(PageRect.Min.X > TextureRegion.Max.X) && !(TextureRegion.Min.X > PageRect.Max.X) && !(PageRect.Min.Y > TextureRegion.Max.Y) && !(TextureRegion.Min.Y > PageRect.Max.Y))
				{
					if (!FreeHeap.IsPresent(i))
					{
						OutLocked.Add(FVirtualTextureLocalTile(ProducerHandle, vAddress, vLevel));
					}
					else
					{
						UnmapAllPages(System, i, false);
						FreeHeap.Update(0, i);
					}
				}
			}
		}
	}
}

void FTexturePagePool::GetAllLockedPages(FVirtualTextureSystem* System, TSet<FVirtualTextureLocalTile>& OutPages)
{
	OutPages.Reserve(OutPages.Num() + GetNumLockedPages());

	for (uint32 i = 0; i < NumPages; ++i)
	{
		if (!FreeHeap.IsPresent(i))
		{
			OutPages.Add(FVirtualTextureLocalTile(FVirtualTextureProducerHandle(Pages[i].PackedProducerHandle), Pages[i].Local_vAddress, Pages[i].Local_vLevel));
		}
	}
}

FVirtualTextureLocalTile FTexturePagePool::GetLocalTileFromPhysicalAddress(uint16 pAddress)
{
	return FVirtualTextureLocalTile(FVirtualTextureProducerHandle(Pages[pAddress].PackedProducerHandle), Pages[pAddress].Local_vAddress, Pages[pAddress].Local_vLevel);
}

bool FTexturePagePool::AnyFreeAvailable( uint32 Frame ) const
{
	if( FreeHeap.Num() > 0 )
	{
		// Keys include vLevel to help prevent parent before child ordering
		const uint16 pAddress = FreeHeap.Top();
		const uint32 PageFrame = FreeHeap.GetKey(pAddress) >> 4;
		// Don't free any pages that were touched this frame
		return PageFrame != Frame;
	}

	return false;
}

uint16 FTexturePagePool::GetPageHash(const FPageEntry& Entry)
{
	return (uint16)MurmurFinalize64(Entry.PackedValue);
}

uint32 FTexturePagePool::FindPageAddress(const FVirtualTextureProducerHandle& ProducerHandle, uint8 GroupIndex, uint32 Local_vAddress, uint8 Local_vLevel) const
{
	FPageEntry CheckPage;
	CheckPage.PackedProducerHandle = ProducerHandle.PackedValue;
	CheckPage.Local_vAddress = Local_vAddress;
	CheckPage.Local_vLevel = Local_vLevel;
	CheckPage.GroupIndex = GroupIndex;

	const uint16 Hash = GetPageHash(CheckPage);
	for (uint32 PageIndex = PageHash.First(Hash); PageHash.IsValid(PageIndex); PageIndex = PageHash.Next(PageIndex))
	{
		const FPageEntry& PageEntry = Pages[PageIndex];
		if (PageEntry.PackedValue == CheckPage.PackedValue)
		{
			return PageIndex;
		}
	}

	return ~0u;
}

uint32 FTexturePagePool::FindNearestPageAddress(const FVirtualTextureProducerHandle& ProducerHandle, uint8 GroupIndex, uint32 Local_vAddress, uint8 Local_vLevel, uint8 MaxLevel) const
{
	while (Local_vLevel <= MaxLevel)
	{
		const uint32 pAddress = FindPageAddress(ProducerHandle, GroupIndex, Local_vAddress, Local_vLevel);
		if (pAddress != ~0u)
		{
			return pAddress;
		}

		++Local_vLevel;
		Local_vAddress >>= 2;
	}
	return ~0u;
}

uint32 FTexturePagePool::FindNearestPageLevel(const FVirtualTextureProducerHandle& ProducerHandle, uint8 GroupIndex, uint32 Local_vAddress, uint8 Local_vLevel) const
{
	while (Local_vLevel < 16u)
	{
		const uint32 pAddress = FindPageAddress(ProducerHandle, GroupIndex, Local_vAddress, Local_vLevel);
		if (pAddress != ~0u)
		{
			return Pages[pAddress].Local_vLevel;
		}

		++Local_vLevel;
		Local_vAddress >>= 2;
	}
	return ~0u;
}

uint32 FTexturePagePool::Alloc(FVirtualTextureSystem* System, uint32 Frame, const FVirtualTextureProducerHandle& ProducerHandle, uint8 GroupIndex, uint32 Local_vAddress, uint8 Local_vLevel, bool bLock)
{
	check(ProducerHandle.PackedValue != 0u);
	check(AnyFreeAvailable(Frame));
	checkSlow(FindPageAddress(ProducerHandle, GroupIndex, Local_vAddress, Local_vLevel) == ~0u);

	// Grab the LRU free page
	const uint16 pAddress = FreeHeap.Top();

	// Unmap any previous usage
	UnmapAllPages(System, pAddress, true);

	// Mark the page as used for the given producer
	FPageEntry& PageEntry = Pages[pAddress];
	PageEntry.PackedProducerHandle = ProducerHandle.PackedValue;
	PageEntry.Local_vAddress = Local_vAddress;
	PageEntry.Local_vLevel = Local_vLevel;
	PageEntry.GroupIndex = GroupIndex;
	PageHash.Add(GetPageHash(PageEntry), pAddress);

	if (bLock)
	{
		FreeHeap.Pop();
	}
	else
	{
		FreeHeap.Update((Frame << 4) + (Local_vLevel & 0xf), pAddress);
	}

	return pAddress;
}

void FTexturePagePool::Free(FVirtualTextureSystem* System, uint16 pAddress)
{
	UnmapAllPages(System, pAddress, true);

	if (FreeHeap.IsPresent(pAddress))
	{
		FreeHeap.Update(0u, pAddress);
	}
	else
	{
		FreeHeap.Add(0u, pAddress);
	}
}

void FTexturePagePool::Unlock(uint32 Frame, uint16 pAddress)
{
	const FPageEntry& PageEntry = Pages[pAddress];
	FreeHeap.Add((Frame << 4) + PageEntry.Local_vLevel, pAddress);
}

void FTexturePagePool::Lock(uint16 pAddress)
{
	// 'Remove' checks IsPresent(), so this will be a nop if address is already locked
	FreeHeap.Remove(pAddress);
}

void FTexturePagePool::UpdateUsage(uint32 Frame, uint16 pAddress)
{
	if (FreeHeap.IsPresent(pAddress))
	{
		const FPageEntry& PageEntry = Pages[pAddress];
		FreeHeap.Update((Frame << 4) + PageEntry.Local_vLevel, pAddress);
	}
}

uint32 FTexturePagePool::GetNumVisiblePages(uint32 Frame) const
{
	uint32 Count = 0;
	for (uint32 i = 0; i < NumPages; ++i)
	{
		if (FreeHeap.IsPresent(i))
		{
			uint32 Key = FreeHeap.GetKey(i);
			if ((Key >> 4) > Frame)
			{
				Count ++;
			}
		}
		else
		{
			// Consider all locked pages as visible
			Count++;
		}
	}

	return Count;
}

void FTexturePagePool::MapPage(FVirtualTextureSpace* Space, FVirtualTexturePhysicalSpace* PhysicalSpace, uint8 PageTableLayerIndex, uint8 vLogSize, uint32 vAddress, uint8 vLevel, uint16 pAddress)
{
	check(pAddress < NumPages);
	FTexturePageMap& PageMap = Space->GetPageMapForPageTableLayer(PageTableLayerIndex);
	PageMap.MapPage(Space, PhysicalSpace, vLogSize, vAddress, vLevel, pAddress);

	++NumPagesMapped;

	const uint32 MappingIndex = AcquireMapping();
	AddMappingToList(pAddress, MappingIndex);
	FPageMapping& Mapping = PageMapping[MappingIndex];
	Mapping.SpaceID = Space->GetID();
	Mapping.vAddress = vAddress;
	Mapping.vLogSize = vLogSize;
	Mapping.PageTableLayerIndex = PageTableLayerIndex;
}

void FTexturePagePool::UnmapPageMapping(FVirtualTextureSystem* System, uint32 MappingIndex, bool bMapAncestorPage)
{
	FPageMapping& Mapping = PageMapping[MappingIndex];
	FVirtualTextureSpace* Space = System->GetSpace(Mapping.SpaceID);
	FTexturePageMap& PageMap = Space->GetPageMapForPageTableLayer(Mapping.PageTableLayerIndex);

	PageMap.UnmapPage(System, Space, Mapping.vLogSize, Mapping.vAddress, bMapAncestorPage);

	check(NumPagesMapped > 0u);
	--NumPagesMapped;

	Mapping.PackedValues = 0xffffffff;
	Mapping.PageTableLayerIndex = 0xff;
	ReleaseMapping(MappingIndex);
}

void FTexturePagePool::UnmapAllPages(FVirtualTextureSystem* System, uint16 pAddress, bool bMapAncestorPages)
{
	FPageEntry& PageEntry = Pages[pAddress];
	if (PageEntry.PackedProducerHandle != 0u)
	{
		PageHash.Remove(GetPageHash(PageEntry), pAddress);
		PageEntry.PackedValue = 0u;
	}

	// unmap the page from all of its current mappings
	uint32 MappingIndex = PageMapping[pAddress].NextIndex;
	while (MappingIndex != pAddress)
	{
		FPageMapping& Mapping = PageMapping[MappingIndex];
		const uint32 NextIndex = Mapping.NextIndex;
		UnmapPageMapping(System, MappingIndex, bMapAncestorPages);

		MappingIndex = NextIndex;
	}

	check(PageMapping[pAddress].NextIndex == pAddress); // verify the list is properly empty
}
