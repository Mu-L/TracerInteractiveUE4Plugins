// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "MovieSceneToolsUserSettings.generated.h"

UENUM()
enum class EThumbnailQuality : uint8
{
	Draft,
	Normal,
	Best,
};

UCLASS(config=EditorSettings)
class MOVIESCENETOOLS_API UMovieSceneUserThumbnailSettings : public UObject
{
public:
	UMovieSceneUserThumbnailSettings(const FObjectInitializer& Initializer);
	
	GENERATED_BODY()

	/** Whether to draw thumbnails or not */
	UPROPERTY(EditAnywhere, config, Category=General)
	bool bDrawThumbnails;

	/** Whether to draw a single thumbnail for this section or as many as can fit */
	UPROPERTY(EditAnywhere, config, Category=General, meta=(EditCondition=bDrawThumbnails))
	bool bDrawSingleThumbnails;

	/** Size at which to draw thumbnails on thumbnail sections */
	UPROPERTY(EditAnywhere, config, Category=General, meta=(ClampMin=1, ClampMax=1024, EditCondition=bDrawThumbnails))
	FIntPoint ThumbnailSize;

	/** Quality to render the thumbnails with */
	UPROPERTY(EditAnywhere, config, Category=General, meta=(EditCondition=bDrawThumbnails))
	EThumbnailQuality Quality;

	DECLARE_EVENT(UMovieSceneUserThumbnailSettings, FOnForceRedraw)
	FOnForceRedraw& OnForceRedraw() { return OnForceRedrawEvent; }
	void BroadcastRedrawThumbnails() const { OnForceRedrawEvent.Broadcast(); }

	virtual void PostEditChangeProperty( struct FPropertyChangedEvent& PropertyChangedEvent) override;

private:
	FOnForceRedraw OnForceRedrawEvent;
};

UCLASS(config=EditorSettings)
class MOVIESCENETOOLS_API UMovieSceneUserImportFBXSettings : public UObject
{
public:
	UMovieSceneUserImportFBXSettings(const FObjectInitializer& Initializer);
	
	GENERATED_BODY()

	/** Whether to match fbx node names to sequencer node names. */
	UPROPERTY(EditAnywhere, config, Category=Import, meta= (ToolTip = "Match fbx node names to sequencer node names"))
	bool bMatchByNameOnly;

	/** Whether to force the front axis to be align with X instead of -Y. */
	UPROPERTY(EditAnywhere, config, Category=Import, meta= (ToolTip = "Convert the scene from FBX coordinate system to UE4 coordinate system with front X axis instead of -Y"))
	bool bForceFrontXAxis;

	/** Whether to create cameras if they don't already exist in the level. */
	UPROPERTY(EditAnywhere, config, Category=Import)
	bool bCreateCameras;

	/** Whether to remove keyframes within a tolerance from the imported tracks */
	UPROPERTY(EditAnywhere, config, Category=Import)
	bool bReduceKeys;

	/** The tolerance for reduce keys */
	UPROPERTY(EditAnywhere, config, Category=Import, meta=(EditCondition=bReduceKeys))
	float ReduceKeysTolerance;
};