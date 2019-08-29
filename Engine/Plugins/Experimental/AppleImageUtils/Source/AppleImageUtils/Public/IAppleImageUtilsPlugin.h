// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "HAL/ThreadSafeBool.h"

#include "AppleImageUtilsAvailability.h"

#if SUPPORTS_IMAGE_UTILS_1_0
	#import <CoreGraphics/CoreGraphics.h>
#endif

class UTexture;
class UTexture2D;

/**
 * Interface to access the async compression request
 */
class APPLEIMAGEUTILS_API IAppleImageUtilsConversionTask
{
public:
	/** @return whether the task succeeded or not */
	virtual bool HadError() const = 0;
	/** @return information about the error if there was one */
	virtual FString GetErrorReason() const = 0;
	/** @return whether the task has completed or not */
	virtual bool IsDone() const = 0;
	/** @return the data once the task has completed or empty array if still in progress. NOTE: this should use MoveTemp() to avoid duplication so call once :) */
	virtual TArray<uint8> GetData() = 0;
};

/**
 * Base class for implementing IAppleImageUtilsCompressionTasks
 */
class APPLEIMAGEUTILS_API FAppleImageUtilsConversionTaskBase :
	public IAppleImageUtilsConversionTask
{
protected:
	FAppleImageUtilsConversionTaskBase() {}
	virtual ~FAppleImageUtilsConversionTaskBase() {}

public:
	virtual bool IsDone() const override { return bIsDone; }
	virtual bool HadError() const override { return bHadError; }
	virtual FString GetErrorReason() const override { return Error; }

protected:
	FThreadSafeBool bIsDone;
	FThreadSafeBool bHadError;
	FString Error;
};

class APPLEIMAGEUTILS_API IAppleImageUtilsPlugin :
	public IModuleInterface
{
public:
	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline IAppleImageUtilsPlugin& Get()
	{
		return FModuleManager::LoadModuleChecked<IAppleImageUtilsPlugin>("AppleImageUtils");
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("AppleImageUtils");
	}

	/**
	 * Converts a image to an array of JPEG data in a background task
	 *
	 * @param SourceImage the image to compress (NOTE: must support UAppleImageInterface)
	 * @param Quality the quality level to compress to
	 * @param bWantColor whether the JPEG is color (true) or monochrome (false)
	 * @param bUseGpu whether to use the GPU (true) or the CPU (false) to compress
	 *
	 * @return the async task that is doing the conversion
	 */
	virtual TSharedPtr<FAppleImageUtilsConversionTaskBase, ESPMode::ThreadSafe> ConvertToJPEG(UTexture* SourceImage, int32 Quality = 85, bool bWantColor = true, bool bUseGpu = true) = 0;

	/**
	 * Converts a image to an array of HEIF data in a background task
	 *
	 * @param SourceImage the image to compress (NOTE: must support UAppleImageInterface)
	 * @param Quality the quality level to compress to
	 * @param bWantColor whether the HEIF is color (true) or monochrome (false)
	 * @param bUseGpu whether to use the GPU (true) or the CPU (false) to compress
	 *
	 * @return the async task that is doing the conversion
	 */
	virtual TSharedPtr<FAppleImageUtilsConversionTaskBase, ESPMode::ThreadSafe> ConvertToHEIF(UTexture* SourceImage, int32 Quality = 85,  bool bWantColor = true, bool bUseGpu = true) = 0;

	/**
	 * Converts a image to an array of PNG data in a background task
	 *
	 * @param SourceImage the image to compress (NOTE: must support UAppleImageInterface)
	 * @param Quality the quality level to compress to
	 * @param bWantColor whether the PNG is color (true) or monochrome (false)
	 * @param bUseGpu whether to use the GPU (true) or the CPU (false) to compress
	 *
	 * @return the async task that is doing the conversion
	 */
	virtual TSharedPtr<FAppleImageUtilsConversionTaskBase, ESPMode::ThreadSafe> ConvertToPNG(UTexture* SourceImage, bool bWantColor = true, bool bUseGpu = true) = 0;

	/**
	 * Converts a image to an array of TIFF data in a background task
	 *
	 * @param SourceImage the image to compress (NOTE: must support UAppleImageInterface)
	 * @param Quality the quality level to compress to
	 * @param bUseGpu whether to use the GPU (true) or the CPU (false) to compress
	 *
	 * @return the async task that is doing the conversion
	 */
	virtual TSharedPtr<FAppleImageUtilsConversionTaskBase, ESPMode::ThreadSafe> ConvertToTIFF(UTexture* SourceImage, bool bWantColor = true, bool bUseGpu = true) = 0;

#if SUPPORTS_IMAGE_UTILS_1_0
	/**
	 * Copies the contents of a UTexture2D to a CGImage object
	 *
	 * @param SourceImage the image to create a CGImage from
	 *
	 * @return the CGImage if conversion was possible
	 */
	virtual CGImageRef UTexture2DToCGImage(UTexture2D* Source) = 0;
#endif
};

