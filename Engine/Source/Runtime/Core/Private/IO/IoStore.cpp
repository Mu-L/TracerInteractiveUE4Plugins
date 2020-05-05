// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/IoStore.h"
#include "Containers/Map.h"
#include "HAL/FileManager.h"
#include "Templates/UniquePtr.h"
#include "Misc/Paths.h"

//////////////////////////////////////////////////////////////////////////

constexpr char FIoStoreTocHeader::TocMagicImg[];
constexpr uint32 IoChunkAlignment = 16;

//////////////////////////////////////////////////////////////////////////

FIoStoreEnvironment::FIoStoreEnvironment()
{
}

FIoStoreEnvironment::~FIoStoreEnvironment()
{
}

void FIoStoreEnvironment::InitializeFileEnvironment(FStringView InPath)
{
	Path = InPath;
}

//////////////////////////////////////////////////////////////////////////

class FIoStoreWriterImpl
{
public:
	FIoStoreWriterImpl(FIoStoreEnvironment& InEnvironment)
	:	Environment(InEnvironment)
	{
	}

	FIoStatus Initialize()
	{
		IPlatformFile& Ipf = IPlatformFile::GetPlatformPhysical();

		FString TocFilePath = Environment.GetPath() + TEXT(".utoc");
		FString ContainerFilePath = Environment.GetPath() + TEXT(".ucas");

		Ipf.CreateDirectoryTree(*FPaths::GetPath(ContainerFilePath));

		ContainerFileHandle.Reset(Ipf.OpenWrite(*ContainerFilePath, /* append */ false, /* allowread */ true));

		if (!ContainerFileHandle)
		{ 
			return FIoStatusBuilder(EIoErrorCode::FileOpenFailed) << TEXT("Failed to open IoStore container file '") << *ContainerFilePath << TEXT("'");
		}

		TocFileHandle.Reset(Ipf.OpenWrite(*TocFilePath, /* append */ false, /* allowread */ true));

		if (!TocFileHandle)
		{
			return FIoStatusBuilder(EIoErrorCode::FileOpenFailed) << TEXT("Failed to open IoStore TOC file '") << *TocFilePath << TEXT("'");
		}

		return FIoStatus::Ok;
	}

	FIoStatus EnableCsvOutput()
	{
		FString CsvFilePath = Environment.GetPath() + TEXT(".csv");
		CsvArchive.Reset(IFileManager::Get().CreateFileWriter(*CsvFilePath));
		if (!CsvArchive)
		{
			return FIoStatusBuilder(EIoErrorCode::FileOpenFailed) << TEXT("Failed to open IoStore CSV file '") << *CsvFilePath << TEXT("'");
		}
		ANSICHAR Header[] = "Name,Offset,Size\n";
		CsvArchive->Serialize(Header, sizeof(Header) - 1);

		return FIoStatus::Ok;
	}

	UE_NODISCARD FIoStatus Append(FIoChunkId ChunkId, FIoBuffer Chunk, const TCHAR* Name)
	{
		if (!ContainerFileHandle)
		{
			return FIoStatus(EIoErrorCode::FileNotOpen, TEXT("No container file to append to"));
		}

		if (!ChunkId.IsValid())
		{
			return FIoStatus(EIoErrorCode::InvalidParameter, TEXT("ChunkId is not valid!"));
		}

		if (Toc.Find(ChunkId) != nullptr)
		{
			return FIoStatus(EIoErrorCode::InvalidParameter, TEXT("ChunkId is already mapped"));
		}

		FIoStoreTocEntry TocEntry;

		check(ContainerFileHandle->Tell() % IoChunkAlignment == 0);

		TocEntry.SetOffset(ContainerFileHandle->Tell());
		TocEntry.SetLength(Chunk.DataSize());
		TocEntry.ChunkId = ChunkId;

		IsMetadataDirty = true;

		bool Success = ContainerFileHandle->Write(Chunk.Data(), Chunk.DataSize());

		if (uint32 UnpaddedBytes = Chunk.DataSize() % IoChunkAlignment)
		{
			static constexpr uint8 Zeroes[IoChunkAlignment] = {};
			uint32 Padding = (IoChunkAlignment - UnpaddedBytes) % IoChunkAlignment;
			Success &= ContainerFileHandle->Write(Zeroes, Padding);
		}

		if (Success)
		{
			Toc.Add(ChunkId, TocEntry);

			if (CsvArchive)
			{
				ANSICHAR Line[MAX_SPRINTF];
				FCStringAnsi::Sprintf(Line, "%s,%lld,%lld\n", TCHAR_TO_ANSI(Name), TocEntry.GetOffset(), TocEntry.GetLength());
				CsvArchive->Serialize(Line, FCStringAnsi::Strlen(Line));
			}

			return FIoStatus::Ok;
		}
		else
		{
			return FIoStatus(EIoErrorCode::WriteError, TEXT("Append failed"));
		}
	}

	UE_NODISCARD FIoStatus MapPartialRange(FIoChunkId OriginalChunkId, uint64 Offset, uint64 Length, FIoChunkId ChunkIdPartialRange)
	{
		//TODO: Does RelativeOffset + Length overflow?

		const FIoStoreTocEntry* Entry = Toc.Find(OriginalChunkId);
		if (Entry == nullptr)
		{
			return FIoStatus(EIoErrorCode::UnknownChunkID, TEXT("OriginalChunkId does not exist in the container"));
		}

		if (!ChunkIdPartialRange.IsValid())
		{
			return FIoStatus(EIoErrorCode::InvalidParameter, TEXT("ChunkIdPartialRange is not valid!"));
		}

		if (Toc.Find(ChunkIdPartialRange) != nullptr)
		{
			return FIoStatus(EIoErrorCode::InvalidParameter, TEXT("ChunkIdPartialRange is already mapped"));
		}

		if (Offset + Length > Entry->GetLength())
		{
			return FIoStatus(EIoErrorCode::InvalidParameter, TEXT("The given range (Offset/Length) is not within the bounds of OriginalChunkId's data"));
		}

		FIoStoreTocEntry TocEntry;

		TocEntry.SetOffset(Entry->GetOffset() + Offset);
		TocEntry.SetLength(Length);
		TocEntry.ChunkId = ChunkIdPartialRange;

		Toc.Add(ChunkIdPartialRange, TocEntry);

		IsMetadataDirty = true;

		return FIoStatus::Ok;
	}

	UE_NODISCARD FIoStatus FlushMetadata()
	{
		TocFileHandle->Seek(0);

		FIoStoreTocHeader TocHeader;
		FMemory::Memset(TocHeader, 0);

		TocHeader.MakeMagic();
		TocHeader.TocHeaderSize = sizeof TocHeader;
		TocHeader.TocEntryCount = Toc.Num();
		TocHeader.TocEntrySize = sizeof(FIoStoreTocEntry);

		const bool Success = TocFileHandle->Write(reinterpret_cast<const uint8*>(&TocHeader), sizeof TocHeader);

		if (!Success)
		{
			return FIoStatus(EIoErrorCode::WriteError, TEXT("TOC write failed"));
		}

		for (auto& _: Toc)
		{
			FIoStoreTocEntry& TocEntry = _.Value;
			
			TocFileHandle->Write(reinterpret_cast<const uint8*>(&TocEntry), sizeof TocEntry);
		}

		return FIoStatus::Ok;
	}

private:
	FIoStoreEnvironment&				Environment;
	TMap<FIoChunkId, FIoStoreTocEntry>	Toc;
	TUniquePtr<IFileHandle>				ContainerFileHandle;
	TUniquePtr<IFileHandle>				TocFileHandle;
	TUniquePtr<FArchive>				CsvArchive;
	bool								IsMetadataDirty = true;
};

FIoStoreWriter::FIoStoreWriter(FIoStoreEnvironment& InEnvironment)
:	Impl(new FIoStoreWriterImpl(InEnvironment))
{
}

FIoStoreWriter::~FIoStoreWriter()
{
	(void)Impl->FlushMetadata();
}

FIoStatus FIoStoreWriter::Initialize()
{
	return Impl->Initialize();
}

FIoStatus FIoStoreWriter::EnableCsvOutput()
{
	return Impl->EnableCsvOutput();
}

FIoStatus FIoStoreWriter::Append(FIoChunkId ChunkId, FIoBuffer Chunk, const TCHAR* Name)
{
	return Impl->Append(ChunkId, Chunk, Name);
}

FIoStatus FIoStoreWriter::MapPartialRange(FIoChunkId OriginalChunkId, uint64 Offset, uint64 Length, FIoChunkId ChunkIdPartialRange)
{
	return Impl->MapPartialRange(OriginalChunkId, Offset, Length, ChunkIdPartialRange);
}

FIoStatus FIoStoreWriter::FlushMetadata()
{
	return Impl->FlushMetadata();
}
