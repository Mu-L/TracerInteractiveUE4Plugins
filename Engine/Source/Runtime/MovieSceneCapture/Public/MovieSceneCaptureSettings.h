// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "Engine/EngineTypes.h"
#include "Misc/FrameRate.h"
#include "MovieSceneCaptureSettings.generated.h"


/** Structure representing a capture resolution */
USTRUCT(BlueprintType)
struct MOVIESCENECAPTURE_API FCaptureResolution
{
	FCaptureResolution(int32 InX = 0, int32 InY = 0) : ResX(InX), ResY(InY) {}
	
	GENERATED_BODY()

	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category=Resolution, meta=(ClampMin = 16, ClampMax=7680))
	int32 ResX;

	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category=Resolution, meta=(ClampMin = 16, ClampMax=7680))
	int32 ResY;
};

/** Common movie-scene capture settings */
USTRUCT(BlueprintType)
struct MOVIESCENECAPTURE_API FMovieSceneCaptureSettings
{
	FMovieSceneCaptureSettings();

	GENERATED_BODY()

	/** The directory to output the captured file(s) in */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category=General, meta=(RelativePath))
	FDirectoryPath OutputDirectory;

	/** Optional game mode to override the map's default game mode with.  This can be useful if the game's normal mode displays UI elements or loading screens that you don't want captured. */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category=General, AdvancedDisplay)
	TSubclassOf<class AGameModeBase> GameModeOverride;

	/** The format to use for the resulting filename. Extension will be added automatically. Any tokens of the form {token} will be replaced with the corresponding value:
	 * {fps}		- The captured framerate
	 * {frame}		- The current frame number (only relevant for image sequences)
	 * {width}		- The width of the captured frames
	 * {height}		- The height of the captured frames
	 * {world}		- The name of the current world
	 * {quality}	- The image compression quality setting
	 * {material}   - The material/render pass
	 * {shot}       - The name of the level sequence asset shot being played
	 * {camera}     - The name of the current camera
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category=General, DisplayName="Filename Format")
	FString OutputFormat;

	/** Whether to overwrite existing files or not */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category=General, AdvancedDisplay )
	bool bOverwriteExisting;

	/** True if frame numbers in the output files should be relative to zero, rather than the actual frame numbers in the originating animation content. */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category=General, AdvancedDisplay)
	bool bUseRelativeFrameNumbers;

	/** Number of frame handles to include for each shot */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category=Sequence, AdvancedDisplay, meta=(ClampMin=0, UIMin=0))
	int32 HandleFrames;

	/** How much to zero-pad frame numbers on filenames */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category=General, AdvancedDisplay, meta=(ClampMin=0, UIMin=0))
	uint8 ZeroPadFrameNumbers;

	/** The frame rate at which to capture */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category=CaptureSettings, meta=(ClampMin=1, UIMin=1, ClampMax=200, UIMax=200))
	FFrameRate FrameRate;

	/** The resolution at which to capture */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category=CaptureSettings, meta=(ShowOnlyInnerProperties))
	FCaptureResolution Resolution;
	
	/** Whether to texture streaming should be enabled while capturing.  Turning off texture streaming may cause much more memory to be used, but also reduces the chance of blurry textures in your captured video. */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category=CaptureSettings, AdvancedDisplay)
	bool bEnableTextureStreaming;

	/** Whether to enable cinematic engine scalability settings */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category=Cinematic)
	bool bCinematicEngineScalability;

	/** Whether to enable cinematic mode whilst capturing */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category=Cinematic)
	bool bCinematicMode;

	/** Whether to allow player movement whilst capturing */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category=Cinematic, AdvancedDisplay, meta=(EditCondition="bCinematicMode"))
	bool bAllowMovement;

	/** Whether to allow player rotation whilst capturing */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category=Cinematic, AdvancedDisplay, meta=(EditCondition="bCinematicMode"))
	bool bAllowTurning;

	/** Whether to show the local player whilst capturing */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category=Cinematic, AdvancedDisplay, meta=(EditCondition="bCinematicMode"))
	bool bShowPlayer;

	/** Whether to show the in-game HUD whilst capturing */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category=Cinematic, AdvancedDisplay, meta=(EditCondition="bCinematicMode"))
	bool bShowHUD;
};
