// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "Sound/SoundNodeAssetReferencer.h"
#include "Sound/SoundNodeQualityLevel.h"
#include "Sound/SoundCue.h"
#include "UObject/FrameworkObjectVersion.h"

bool USoundNodeAssetReferencer::ShouldHardReferenceAsset() const
{
	bool bShouldHardReference = true;

	if (USoundCue* Cue = Cast<USoundCue>(GetOuter()))
	{
		TArray<USoundNodeQualityLevel*> QualityNodes;
		TArray<USoundNodeAssetReferencer*> WavePlayers;
		Cue->RecursiveFindNode(Cue->FirstNode, QualityNodes);

		for (USoundNodeQualityLevel* QualityNode : QualityNodes)
		{
			WavePlayers.Reset();
			Cue->RecursiveFindNode(QualityNode, WavePlayers);
			if (WavePlayers.Contains(this))
			{
				bShouldHardReference = false;
				break;
			}
		}
	}

	return bShouldHardReference;
}

#if WITH_EDITOR
void USoundNodeAssetReferencer::PostEditImport()
{
	Super::PostEditImport();

	LoadAsset();
}
#endif
