// Copyright Epic Games, Inc. All Rights Reserved.

#include "Common/ChunkDataSizeProvider.h"
#include "Misc/Paths.h"
#include "BuildPatchUtil.h"

namespace BuildPatchServices
{
	class FChunkDataSizeProvider
		: public IChunkDataSizeProvider
	{
	public:
		// Begin IDataSizeProvider
		virtual uint64 GetDownloadSize(const FString& Identifier) const override
		{
			checkSlow(IsInGameThread());
			uint64 DownloadSize = INDEX_NONE;
			const uint64* DownloadSizePtr = DownloadSizes.Find(Identifier);
			if (DownloadSizePtr != nullptr)
			{
				DownloadSize = *DownloadSizePtr;
			}
			return DownloadSize;
		}
		// End IDataSizeProvider

		// Begin IChunkDataSizeProvider
		virtual void AddManifestData(const FBuildPatchAppManifest* Manifest) override
		{
			check(IsInGameThread());
			if (Manifest != nullptr)
			{
				TSet<FGuid> DataList;
				Manifest->GetDataList(DataList);
				for (const FGuid& DataId : DataList)
				{
					FString CleanFilename = FPaths::GetCleanFilename(FBuildPatchUtils::GetDataFilename(*Manifest, TEXT(""), DataId));
					DownloadSizes.Add(MoveTemp(CleanFilename), Manifest->GetDataSize(DataId));
				}
			}
		}
		// End IChunkDataSizeProvider

	private:
		TMap<FString, uint64> DownloadSizes;
	};
	
	IChunkDataSizeProvider* FChunkDataSizeProviderFactory::Create()
	{
		return new FChunkDataSizeProvider();
	}
}
