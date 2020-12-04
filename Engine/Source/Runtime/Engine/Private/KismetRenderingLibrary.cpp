// Copyright Epic Games, Inc. All Rights Reserved.

#include "Kismet/KismetRenderingLibrary.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Serialization/BufferArchive.h"
#include "EngineGlobals.h"
#include "RenderingThread.h"
#include "Engine/Engine.h"
#include "CanvasTypes.h"
#include "Engine/Canvas.h"
#include "Misc/App.h"
#include "TextureResource.h"
#include "SceneUtils.h"
#include "Logging/MessageLog.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTarget2DArray.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "ImageUtils.h"
#include "OneColorShader.h"
#include "PipelineStateCache.h"
#include "ClearQuad.h"
#include "Engine/Texture2D.h"
#include "RHI.h"

#if WITH_EDITOR
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "IContentBrowserSingleton.h"
#include "PackageTools.h"
#endif

//////////////////////////////////////////////////////////////////////////
// UKismetRenderingLibrary

#define LOCTEXT_NAMESPACE "KismetRenderingLibrary"

UKismetRenderingLibrary::UKismetRenderingLibrary(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UKismetRenderingLibrary::ClearRenderTarget2D(UObject* WorldContextObject, UTextureRenderTarget2D* TextureRenderTarget, FLinearColor ClearColor)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);

	if (TextureRenderTarget
		&& TextureRenderTarget->Resource
		&& World)
	{
		FTextureRenderTargetResource* RenderTargetResource = TextureRenderTarget->GameThread_GetRenderTargetResource();
		ENQUEUE_RENDER_COMMAND(ClearRTCommand)(
			[RenderTargetResource, ClearColor](FRHICommandList& RHICmdList)
		{
			FRHIRenderPassInfo RPInfo(RenderTargetResource->GetRenderTargetTexture(), ERenderTargetActions::DontLoad_Store);
			TransitionRenderPassTargets(RHICmdList, RPInfo);
			RHICmdList.BeginRenderPass(RPInfo, TEXT("ClearRT"));
			DrawClearQuad(RHICmdList, ClearColor);
			RHICmdList.EndRenderPass();

			RHICmdList.Transition(FRHITransitionInfo(RenderTargetResource->GetRenderTargetTexture(), ERHIAccess::RTV, ERHIAccess::SRVMask));
		});
	}
}

UTextureRenderTarget2D* UKismetRenderingLibrary::CreateRenderTarget2D(UObject* WorldContextObject, int32 Width, int32 Height, ETextureRenderTargetFormat Format, FLinearColor ClearColor, bool bAutoGenerateMipMaps)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);

	if (Width > 0 && Height > 0 && World)
	{
		UTextureRenderTarget2D* NewRenderTarget2D = NewObject<UTextureRenderTarget2D>(WorldContextObject);
		check(NewRenderTarget2D);
		NewRenderTarget2D->RenderTargetFormat = Format;
		NewRenderTarget2D->ClearColor = ClearColor;
		NewRenderTarget2D->bAutoGenerateMips = bAutoGenerateMipMaps;
		NewRenderTarget2D->InitAutoFormat(Width, Height);	
		NewRenderTarget2D->UpdateResourceImmediate(true);

		return NewRenderTarget2D; 
	}

	return nullptr;
}

UTextureRenderTarget2DArray* UKismetRenderingLibrary::CreateRenderTarget2DArray(UObject* WorldContextObject, int32 Width, int32 Height, int32 Slices, ETextureRenderTargetFormat Format, FLinearColor ClearColor, bool bAutoGenerateMipMaps)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);

	if (Width > 0 && Height > 0 && Slices > 0 && World)
	{
		UTextureRenderTarget2DArray* NewRenderTarget = NewObject<UTextureRenderTarget2DArray>(WorldContextObject);
		check(NewRenderTarget);
		NewRenderTarget->ClearColor = ClearColor;
		NewRenderTarget->Init(Width, Height, Slices, GetPixelFormatFromRenderTargetFormat(Format));
		NewRenderTarget->UpdateResourceImmediate(true);
		return NewRenderTarget;
	}

	return nullptr;
}

UTextureRenderTargetVolume* UKismetRenderingLibrary::CreateRenderTargetVolume(UObject* WorldContextObject, int32 Width, int32 Height, int32 Depth, ETextureRenderTargetFormat Format, FLinearColor ClearColor, bool bAutoGenerateMipMaps)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);

	if (Width > 0 && Height > 0 && Depth > 0 && World)
	{
		UTextureRenderTargetVolume* NewRenderTarget = NewObject<UTextureRenderTargetVolume>(WorldContextObject);
		check(NewRenderTarget);
		NewRenderTarget->ClearColor = ClearColor;
		NewRenderTarget->Init(Width, Height, Depth, GetPixelFormatFromRenderTargetFormat(Format));
		NewRenderTarget->UpdateResourceImmediate(true);
		return NewRenderTarget;
	}

	return nullptr;
}

// static 
void UKismetRenderingLibrary::ReleaseRenderTarget2D(UTextureRenderTarget2D* TextureRenderTarget)
{
	if (!TextureRenderTarget)
	{
		return;
	}

	TextureRenderTarget->ReleaseResource();
}

void UKismetRenderingLibrary::DrawMaterialToRenderTarget(UObject* WorldContextObject, UTextureRenderTarget2D* TextureRenderTarget, UMaterialInterface* Material)
{
	if (!FApp::CanEverRender())
	{
		// Returning early to avoid warnings about missing resources that are expected when CanEverRender is false.
		return;
	}

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);

	if (!World)
	{
		FMessageLog("Blueprint").Warning(LOCTEXT("DrawMaterialToRenderTarget_InvalidWorldContextObject", "DrawMaterialToRenderTarget: WorldContextObject is not valid."));
	}
	else if (!Material)
	{
		FMessageLog("Blueprint").Warning(FText::Format(LOCTEXT("DrawMaterialToRenderTarget_InvalidMaterial", "DrawMaterialToRenderTarget[{0}]: Material must be non-null."), FText::FromString(GetPathNameSafe(WorldContextObject))));
	}
	else if (!TextureRenderTarget)
	{
		FMessageLog("Blueprint").Warning(FText::Format(LOCTEXT("DrawMaterialToRenderTarget_InvalidTextureRenderTarget", "DrawMaterialToRenderTarget[{0}]: TextureRenderTarget must be non-null."), FText::FromString(GetPathNameSafe(WorldContextObject))));
	}
	else if (!TextureRenderTarget->Resource)
	{
		FMessageLog("Blueprint").Warning(FText::Format(LOCTEXT("DrawMaterialToRenderTarget_ReleasedTextureRenderTarget", "DrawMaterialToRenderTarget[{0}]: render target has been released."), FText::FromString(GetPathNameSafe(WorldContextObject))));
	}
	else
	{
		World->FlushDeferredParameterCollectionInstanceUpdates();

		FTextureRenderTargetResource* RenderTargetResource = TextureRenderTarget->GameThread_GetRenderTargetResource();

		UCanvas* Canvas = World->GetCanvasForDrawMaterialToRenderTarget();

		FCanvas RenderCanvas(
			RenderTargetResource, 
			nullptr, 
			World,
			World->FeatureLevel);

		Canvas->Init(TextureRenderTarget->SizeX, TextureRenderTarget->SizeY, nullptr, &RenderCanvas);
		Canvas->Update();

		FDrawEvent* DrawMaterialToTargetEvent = new FDrawEvent();

		FName RTName = TextureRenderTarget->GetFName();
		ENQUEUE_RENDER_COMMAND(BeginDrawEventCommand)(
			[RTName, DrawMaterialToTargetEvent, RenderTargetResource](FRHICommandListImmediate& RHICmdList)
			{
				RenderTargetResource->FlushDeferredResourceUpdate(RHICmdList);

				BEGIN_DRAW_EVENTF(
					RHICmdList, 
					DrawCanvasToTarget, 
					(*DrawMaterialToTargetEvent), 
					*RTName.ToString());
			});

		Canvas->K2_DrawMaterial(Material, FVector2D(0, 0), FVector2D(TextureRenderTarget->SizeX, TextureRenderTarget->SizeY), FVector2D(0, 0));

		RenderCanvas.Flush_GameThread();
		Canvas->Canvas = NULL;

		//UpdateResourceImmediate must be called here to ensure mips are generated.
		TextureRenderTarget->UpdateResourceImmediate(false);
		ENQUEUE_RENDER_COMMAND(CanvasRenderTargetResolveCommand)(
			[DrawMaterialToTargetEvent](FRHICommandList& RHICmdList)
			{
				STOP_DRAW_EVENT((*DrawMaterialToTargetEvent));
				delete DrawMaterialToTargetEvent;
			}
		);
	}
}

void UKismetRenderingLibrary::ExportRenderTarget(UObject* WorldContextObject, UTextureRenderTarget2D* TextureRenderTarget, const FString& FilePath, const FString& FileName)
{
	FString TotalFileName = FPaths::Combine(*FilePath, *FileName);
	FText PathError;
	FPaths::ValidatePath(TotalFileName, &PathError);


	if (!TextureRenderTarget)
	{
		FMessageLog("Blueprint").Warning(FText::Format(LOCTEXT("ExportRenderTarget_InvalidTextureRenderTarget", "ExportRenderTarget[{0}]: TextureRenderTarget must be non-null."), FText::FromString(GetPathNameSafe(WorldContextObject))));
	}
	else if (!TextureRenderTarget->Resource)
	{
		FMessageLog("Blueprint").Warning(FText::Format(LOCTEXT("ExportRenderTarget_ReleasedTextureRenderTarget", "ExportRenderTarget[{0}]: render target has been released."), FText::FromString(GetPathNameSafe(WorldContextObject))));
	}
	else if (!PathError.IsEmpty())
	{
		FMessageLog("Blueprint").Warning(FText::Format(LOCTEXT("ExportRenderTarget_InvalidFilePath", "ExportRenderTarget[{0}]: Invalid file path provided: '{1}'"), FText::FromString(GetPathNameSafe(WorldContextObject)), PathError));
	}
	else if (FileName.IsEmpty())
	{
		FMessageLog("Blueprint").Warning(FText::Format(LOCTEXT("ExportRenderTarget_InvalidFileName", "ExportRenderTarget[{0}]: FileName must be non-empty."), FText::FromString(GetPathNameSafe(WorldContextObject))));
	}
	else
	{
		FArchive* Ar = IFileManager::Get().CreateFileWriter(*TotalFileName);

		if (Ar)
		{
			FBufferArchive Buffer;

			bool bSuccess = false;
			if (TextureRenderTarget->RenderTargetFormat == RTF_RGBA16f)
			{
				// Note == is case insensitive
				if (FPaths::GetExtension(TotalFileName) == TEXT("HDR"))
				{
					bSuccess = FImageUtils::ExportRenderTarget2DAsHDR(TextureRenderTarget, Buffer);
				}
				else
				{
					bSuccess = FImageUtils::ExportRenderTarget2DAsEXR(TextureRenderTarget, Buffer);
				}
				
			}
			else
			{
				bSuccess = FImageUtils::ExportRenderTarget2DAsPNG(TextureRenderTarget, Buffer);
			}

			if (bSuccess)
			{
				Ar->Serialize(const_cast<uint8*>(Buffer.GetData()), Buffer.Num());
			}

			delete Ar;
		}
		else
		{
			FMessageLog("Blueprint").Warning(LOCTEXT("ExportRenderTarget_FileWriterFailedToCreate", "ExportRenderTarget: FileWrite failed to create."));
		}
	}
}

EPixelFormat ReadRenderTargetHelper(
	TArray<FColor>& OutLDRValues,
	TArray<FLinearColor>& OutHDRValues,
	UObject* WorldContextObject,
	UTextureRenderTarget2D* TextureRenderTarget,
	int32 X,
	int32 Y,
	int32 Width,
	int32 Height)
{
	EPixelFormat OutFormat = PF_Unknown;

	if (!TextureRenderTarget)
	{
		return OutFormat;
	}

	FTextureRenderTarget2DResource* RTResource = (FTextureRenderTarget2DResource*)TextureRenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource)
	{
		return OutFormat;
	}

	X = FMath::Clamp(X, 0, TextureRenderTarget->SizeX - 1);
	Y = FMath::Clamp(Y, 0, TextureRenderTarget->SizeY - 1);
	Width = FMath::Clamp(Width, 1, TextureRenderTarget->SizeX);
	Height = FMath::Clamp(Height, 1, TextureRenderTarget->SizeY);
	Width = Width - FMath::Max(X + Width - TextureRenderTarget->SizeX, 0);
	Height = Height - FMath::Max(Y + Height - TextureRenderTarget->SizeY, 0);

	FIntRect SampleRect(X, Y, X + Width, Y + Height);
	FReadSurfaceDataFlags ReadSurfaceDataFlags;

	FRenderTarget* RenderTarget = TextureRenderTarget->GameThread_GetRenderTargetResource();
	OutFormat = TextureRenderTarget->GetFormat();

	const int32 NumPixelsToRead = Width * Height;

	switch (OutFormat)
	{
	case PF_B8G8R8A8:
		OutLDRValues.SetNumUninitialized(NumPixelsToRead);
		if (!RenderTarget->ReadPixelsPtr(OutLDRValues.GetData(), ReadSurfaceDataFlags, SampleRect))
		{
			OutFormat = PF_Unknown;
		}
		break;
	case PF_FloatRGBA:
		OutHDRValues.SetNumUninitialized(NumPixelsToRead);
		if (!RenderTarget->ReadLinearColorPixelsPtr(OutHDRValues.GetData(), ReadSurfaceDataFlags, SampleRect))
		{
			OutFormat = PF_Unknown;
		}
		break;
	default:
		OutFormat = PF_Unknown;
		break;
	}

	return OutFormat;
}

FColor UKismetRenderingLibrary::ReadRenderTargetUV(UObject* WorldContextObject, UTextureRenderTarget2D* TextureRenderTarget, float U, float V)
{
	if (!TextureRenderTarget)
	{
		return FColor::Red;
	}

	U = FMath::Clamp(U, 0.0f, 1.0f);
	V = FMath::Clamp(V, 0.0f, 1.0f);
	int32 XPos = U * (float)TextureRenderTarget->SizeX;
	int32 YPos = V * (float)TextureRenderTarget->SizeY;

	return ReadRenderTargetPixel(WorldContextObject, TextureRenderTarget, XPos, YPos);
}

FColor UKismetRenderingLibrary::ReadRenderTargetPixel(UObject* WorldContextObject, UTextureRenderTarget2D* TextureRenderTarget, int32 X, int32 Y)
{	
	TArray<FColor> Samples;
	TArray<FLinearColor> LinearSamples;

	switch (ReadRenderTargetHelper(Samples, LinearSamples, WorldContextObject, TextureRenderTarget, X, Y, 1, 1))
	{
	case PF_B8G8R8A8:
		check(Samples.Num() == 1 && LinearSamples.Num() == 0);
		return Samples[0];
	case PF_FloatRGBA:
		check(Samples.Num() == 0 && LinearSamples.Num() == 1);
		return LinearSamples[0].ToFColor(true);
	case PF_Unknown:
	default:
		return FColor::Red;
	}
}

FLinearColor UKismetRenderingLibrary::ReadRenderTargetRawPixel(UObject * WorldContextObject, UTextureRenderTarget2D * TextureRenderTarget, int32 X, int32 Y)
{
	TArray<FColor> Samples;
	TArray<FLinearColor> LinearSamples;

	switch (ReadRenderTargetHelper(Samples, LinearSamples, WorldContextObject, TextureRenderTarget, X, Y, 1, 1))
	{
	case PF_B8G8R8A8:
		check(Samples.Num() == 1 && LinearSamples.Num() == 0);
		return FLinearColor(float(Samples[0].R), float(Samples[0].G), float(Samples[0].B), float(Samples[0].A));
	case PF_FloatRGBA:
		check(Samples.Num() == 0 && LinearSamples.Num() == 1);
		return LinearSamples[0];
	case PF_Unknown:
	default:
		return FLinearColor::Red;
	}
}

FLinearColor UKismetRenderingLibrary::ReadRenderTargetRawUV(UObject * WorldContextObject, UTextureRenderTarget2D * TextureRenderTarget, float U, float V)
{
	if (!TextureRenderTarget)
	{
		return FLinearColor::Red;
	}

	U = FMath::Clamp(U, 0.0f, 1.0f);
	V = FMath::Clamp(V, 0.0f, 1.0f);
	int32 XPos = U * (float)TextureRenderTarget->SizeX;
	int32 YPos = V * (float)TextureRenderTarget->SizeY;

	return ReadRenderTargetRawPixel(WorldContextObject, TextureRenderTarget, XPos, YPos);
}

/*

void UKismetRenderingLibrary::CreateTexture2DFromRenderTarget(UObject* WorldContextObject, UTextureRenderTarget2D* RenderTarget, const FString &TextureAssetName)
{
	if (RenderTarget && Texture)
	{

		//FImageUtils::CreateTexture2D

		UTexture2D* NewTexture = RenderTarget->ConstructTexture2D(Texture->GetOuter(), Texture->GetName(), RenderTarget->GetMaskedFlags(), CTF_Default, NULL);

		check(NewTexture == Texture);
		NewTexture->UpdateResource();
	}
	else if (!RenderTarget)
	{
		FMessageLog("Blueprint").Warning(LOCTEXT("ConvertRenderTargetToTexture2D_InvalidRenderTarget", "ExportRenderTarget: RenderTarget must be non-null."));
	}
	else if (!Texture)
	{
		FMessageLog("Blueprint").Warning(LOCTEXT("ConvertRenderTargetToTexture2D_InvalidTexture", "ExportRenderTarget: Texture must be non-null."));
	}

}*/

UTexture2D* UKismetRenderingLibrary::RenderTargetCreateStaticTexture2DEditorOnly(UTextureRenderTarget2D* RenderTarget, FString InName, enum TextureCompressionSettings CompressionSettings, enum TextureMipGenSettings MipSettings)
{
#if WITH_EDITOR
	if (!RenderTarget)
	{
		FMessageLog("Blueprint").Warning(LOCTEXT("RenderTargetCreateStaticTexture2D_InvalidRenderTarget", "RenderTargetCreateStaticTexture2DEditorOnly: RenderTarget must be non-null."));
		return nullptr;
	}
	else if (!RenderTarget->Resource)
	{
		FMessageLog("Blueprint").Warning(LOCTEXT("RenderTargetCreateStaticTexture2D_ReleasedRenderTarget", "RenderTargetCreateStaticTexture2DEditorOnly: RenderTarget has been released."));
		return nullptr;
	}
	else
	{
		FString Name;
		FString PackageName;
		IAssetTools& AssetTools = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

		//Use asset name only if directories are specified, otherwise full path
		if (!InName.Contains(TEXT("/")))
		{
			FString AssetName = RenderTarget->GetOutermost()->GetName();
			const FString SanitizedBasePackageName = UPackageTools::SanitizePackageName(AssetName);
			const FString PackagePath = FPackageName::GetLongPackagePath(SanitizedBasePackageName) + TEXT("/");
			AssetTools.CreateUniqueAssetName(PackagePath, InName, PackageName, Name);
		}
		else
		{
			InName.RemoveFromStart(TEXT("/"));
			InName.RemoveFromStart(TEXT("Content/"));
			InName.StartsWith(TEXT("Game/")) == true ? InName.InsertAt(0, TEXT("/")) : InName.InsertAt(0, TEXT("/Game/"));
			AssetTools.CreateUniqueAssetName(InName, TEXT(""), PackageName, Name);
		}

		UObject* NewObj = nullptr;

		// create a static 2d texture
		NewObj = RenderTarget->ConstructTexture2D(CreatePackage( *PackageName), Name, RenderTarget->GetMaskedFlags() | RF_Public | RF_Standalone, CTF_Default | CTF_AllowMips, NULL);
		UTexture2D* NewTex = Cast<UTexture2D>(NewObj);

		if (NewTex != nullptr)
		{
			// package needs saving
			NewObj->MarkPackageDirty();

			// Notify the asset registry
			FAssetRegistryModule::AssetCreated(NewObj);

			// Update Compression and Mip settings
			NewTex->CompressionSettings = CompressionSettings;
			NewTex->MipGenSettings = MipSettings;
			NewTex->PostEditChange();

			return NewTex;
		}
		FMessageLog("Blueprint").Warning(LOCTEXT("RenderTargetCreateStaticTexture2D_FailedToCreateTexture", "RenderTargetCreateStaticTexture2DEditorOnly: Failed to create a new texture."));
	}
#else
	FMessageLog("Blueprint").Error(LOCTEXT("Texture2D's cannot be created at runtime.", "RenderTargetCreateStaticTexture2DEditorOnly: Can't create Texture2D at run time. "));
#endif
	return nullptr;
}


void UKismetRenderingLibrary::ConvertRenderTargetToTexture2DEditorOnly( UObject* WorldContextObject, UTextureRenderTarget2D* RenderTarget, UTexture2D* Texture )
{
#if WITH_EDITOR
	if (!RenderTarget)
	{
		FMessageLog("Blueprint").Warning(FText::Format(LOCTEXT("ConvertRenderTargetToTexture2D_InvalidRenderTarget", "ConvertRenderTargetToTexture2DEditorOnly[{0}]: RenderTarget must be non-null."), FText::FromString(GetPathNameSafe(WorldContextObject))));
	}
	else if (!RenderTarget->Resource)
	{
		FMessageLog("Blueprint").Warning(FText::Format(LOCTEXT("ConvertRenderTargetToTexture2D_ReleasedTextureRenderTarget", "ConvertRenderTargetToTexture2DEditorOnly[{0}]: render target has been released."), FText::FromString(GetPathNameSafe(WorldContextObject))));
	}
	else if (!Texture)
	{
		FMessageLog("Blueprint").Warning(FText::Format(LOCTEXT("ConvertRenderTargetToTexture2D_InvalidTexture", "ConvertRenderTargetToTexture2DEditorOnly[{0}]: Texture must be non-null."), FText::FromString(GetPathNameSafe(WorldContextObject))));
	}
	else
	{
		UTexture2D* NewTexture = RenderTarget->ConstructTexture2D(Texture->GetOuter(), Texture->GetName(), RenderTarget->GetMaskedFlags() | RF_Public | RF_Standalone, CTF_Default, NULL);

		check(NewTexture == Texture);

		NewTexture->Modify();
		NewTexture->MarkPackageDirty();
		NewTexture->PostEditChange();
		NewTexture->UpdateResource();
	}
#else
	FMessageLog("Blueprint").Error(LOCTEXT("Convert to render target can't be used at run time.", "ConvertRenderTarget: Can't convert render target to texture2d at run time. "));
#endif

}

void UKismetRenderingLibrary::ExportTexture2D(UObject* WorldContextObject, UTexture2D* Texture, const FString& FilePath, const FString& FileName)
{
	FString TotalFileName = FPaths::Combine(*FilePath, *FileName);
	FText PathError;
	FPaths::ValidatePath(TotalFileName, &PathError);

	if (Texture && !FileName.IsEmpty() && PathError.IsEmpty())
	{
		FArchive* Ar = IFileManager::Get().CreateFileWriter(*TotalFileName);

		if (Ar)
		{
			FBufferArchive Buffer;
			bool bSuccess = FImageUtils::ExportTexture2DAsHDR(Texture, Buffer);

			if (bSuccess)
			{
				Ar->Serialize(const_cast<uint8*>(Buffer.GetData()), Buffer.Num());
			}

			delete Ar;
		}
		else
		{
			FMessageLog("Blueprint").Warning(LOCTEXT("ExportTexture2D_FileWriterFailedToCreate", "ExportTexture2D: FileWrite failed to create."));
		}
	}

	else if (!Texture)
	{
		FMessageLog("Blueprint").Warning(LOCTEXT("ExportTexture2D_InvalidTextureRenderTarget", "ExportTexture2D: TextureRenderTarget must be non-null."));
	}
	if (!PathError.IsEmpty())
	{
		FMessageLog("Blueprint").Warning(FText::Format(LOCTEXT("ExportTexture2D_InvalidFilePath", "ExportTexture2D: Invalid file path provided: '{0}'"), PathError));
	}
	if (FileName.IsEmpty())
	{
		FMessageLog("Blueprint").Warning(LOCTEXT("ExportTexture2D_InvalidFileName", "ExportTexture2D: FileName must be non-empty."));
	}
}

UTexture2D* UKismetRenderingLibrary::ImportFileAsTexture2D(UObject* WorldContextObject, const FString& Filename)
{
	return FImageUtils::ImportFileAsTexture2D(Filename);
}

UTexture2D* UKismetRenderingLibrary::ImportBufferAsTexture2D(UObject* WorldContextObject, const TArray<uint8>& Buffer)
{
	return FImageUtils::ImportBufferAsTexture2D(Buffer);
}

void UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(UObject* WorldContextObject, UTextureRenderTarget2D* TextureRenderTarget, UCanvas*& Canvas, FVector2D& Size, FDrawToRenderTargetContext& Context)
{
	Canvas = NULL;
	Size = FVector2D(0, 0);
	Context = FDrawToRenderTargetContext();
	
	if (!FApp::CanEverRender())
	{
		// Returning early to avoid warnings about missing resources that are expected when CanEverRender is false.
		return;
	}

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);

	if (!World)
	{
		FMessageLog("Blueprint").Warning(LOCTEXT("BeginDrawCanvasToRenderTarget_InvalidWorldContextObject", "BeginDrawCanvasToRenderTarget: WorldContextObject is not valid."));
	}
	else if (!TextureRenderTarget)
	{
		FMessageLog("Blueprint").Warning(FText::Format(LOCTEXT("BeginDrawCanvasToRenderTarget_InvalidTextureRenderTarget", "BeginDrawCanvasToRenderTarget[{0}]: TextureRenderTarget must be non-null."), FText::FromString(GetPathNameSafe(WorldContextObject))));
	}
	else if (!TextureRenderTarget->Resource)
	{
		FMessageLog("Blueprint").Warning(FText::Format(LOCTEXT("BeginDrawCanvasToRenderTarget_ReleasedTextureRenderTarget", "BeginDrawCanvasToRenderTarget[{0}]: render target has been released."), FText::FromString(GetPathNameSafe(WorldContextObject))));
	}
	else
	{
		World->FlushDeferredParameterCollectionInstanceUpdates();

		Context.RenderTarget = TextureRenderTarget;

		Canvas = World->GetCanvasForRenderingToTarget();

		Size = FVector2D(TextureRenderTarget->SizeX, TextureRenderTarget->SizeY);

		FTextureRenderTargetResource* RenderTargetResource = TextureRenderTarget->GameThread_GetRenderTargetResource();
		FCanvas* NewCanvas = new FCanvas(
			RenderTargetResource,
			nullptr, 
			World,
			World->FeatureLevel, 
			// Draw immediately so that interleaved SetVectorParameter (etc) function calls work as expected
			FCanvas::CDM_ImmediateDrawing);
		Canvas->Init(TextureRenderTarget->SizeX, TextureRenderTarget->SizeY, nullptr, NewCanvas);
		Canvas->Update();

		Context.DrawEvent = new FDrawEvent();

		FName RTName = TextureRenderTarget->GetFName();
		FDrawEvent* DrawEvent = Context.DrawEvent;
		ENQUEUE_RENDER_COMMAND(BeginDrawEventCommand)(
			[RTName, DrawEvent, RenderTargetResource](FRHICommandListImmediate& RHICmdList)
			{
				RenderTargetResource->FlushDeferredResourceUpdate(RHICmdList);

				BEGIN_DRAW_EVENTF(
					RHICmdList, 
					DrawCanvasToTarget, 
					(*DrawEvent), 
					*RTName.ToString());
			});
	}
}

void UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(UObject* WorldContextObject, const FDrawToRenderTargetContext& Context)
{
	if (!FApp::CanEverRender())
	{
		// Returning early to avoid warnings about missing resources that are expected when CanEverRender is false.
		return;
	}

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);

	if (World)
	{
		UCanvas* WorldCanvas = World->GetCanvasForRenderingToTarget();

		if (WorldCanvas->Canvas)
		{
			WorldCanvas->Canvas->Flush_GameThread();
			delete WorldCanvas->Canvas;
			WorldCanvas->Canvas = nullptr;
		}
		
		if (Context.RenderTarget)
		{
			FTextureRenderTargetResource* RenderTargetResource = Context.RenderTarget->GameThread_GetRenderTargetResource();
			FDrawEvent* DrawEvent = Context.DrawEvent;
			ENQUEUE_RENDER_COMMAND(CanvasRenderTargetResolveCommand)(
				[RenderTargetResource, DrawEvent](FRHICommandList& RHICmdList)
				{
					RHICmdList.CopyToResolveTarget(RenderTargetResource->GetRenderTargetTexture(), RenderTargetResource->TextureRHI, FResolveParams());
					STOP_DRAW_EVENT((*DrawEvent));
					delete DrawEvent;
				}
			);

			// Remove references to the context now that we've resolved it, to avoid a crash when EndDrawCanvasToRenderTarget is called multiple times with the same context
			// const cast required, as BP will treat Context as an output without the const
			const_cast<FDrawToRenderTargetContext&>(Context) = FDrawToRenderTargetContext();
		}
		else
		{
			FMessageLog("Blueprint").Warning(LOCTEXT("EndDrawCanvasToRenderTarget_InvalidContext", "EndDrawCanvasToRenderTarget: Context must be valid."));
		}
	}
	else
	{
		FMessageLog("Blueprint").Warning(LOCTEXT("EndDrawCanvasToRenderTarget_InvalidWorldContextObject", "EndDrawCanvasToRenderTarget: WorldContextObject is not valid."));
	}
}

FSkelMeshSkinWeightInfo UKismetRenderingLibrary::MakeSkinWeightInfo(int32 Bone0, uint8 Weight0, int32 Bone1, uint8 Weight1, int32 Bone2, uint8 Weight2, int32 Bone3, uint8 Weight3)
{
	FSkelMeshSkinWeightInfo Info;
	FMemory::Memzero(&Info, sizeof(FSkelMeshSkinWeightInfo));
	Info.Bones[0] = Bone0;
	Info.Weights[0] = Weight0;
	Info.Bones[1] = Bone1;
	Info.Weights[1] = Weight1;
	Info.Bones[2] = Bone2;
	Info.Weights[2] = Weight2;
	Info.Bones[3] = Bone3;
	Info.Weights[3] = Weight3;
	return Info;
}


void UKismetRenderingLibrary::BreakSkinWeightInfo(FSkelMeshSkinWeightInfo InWeight, int32& Bone0, uint8& Weight0, int32& Bone1, uint8& Weight1, int32& Bone2, uint8& Weight2, int32& Bone3, uint8& Weight3)
{
	FMemory::Memzero(&InWeight, sizeof(FSkelMeshSkinWeightInfo));
	Bone0 = InWeight.Bones[0];
	Weight0 = InWeight.Weights[0];
	Bone1 = InWeight.Bones[1];
	Weight1 = InWeight.Weights[1];
	Bone2 = InWeight.Bones[2];
	Weight2 = InWeight.Weights[2];
	Bone3 = InWeight.Bones[3];
	Weight3 = InWeight.Weights[3];
}

void UKismetRenderingLibrary::SetCastInsetShadowForAllAttachments(UPrimitiveComponent* PrimitiveComponent, bool bCastInsetShadow, bool bLightAttachmentsAsGroup)
{
	if (PrimitiveComponent)
	{
		// Update this primitive
		PrimitiveComponent->SetCastInsetShadow(bCastInsetShadow);
		PrimitiveComponent->SetLightAttachmentsAsGroup(bLightAttachmentsAsGroup);

		// Go through all potential children and update them 
		TArray<USceneComponent*, TInlineAllocator<8>> ProcessStack;
		ProcessStack.Append(PrimitiveComponent->GetAttachChildren());

		// Walk down the tree updating
		while (ProcessStack.Num() > 0)
		{
			USceneComponent* Current = ProcessStack.Pop(/*bAllowShrinking=*/ false);
			UPrimitiveComponent* CurrentPrimitive = Cast<UPrimitiveComponent>(Current);

			if (CurrentPrimitive && CurrentPrimitive->ShouldComponentAddToScene())
			{
				if (bLightAttachmentsAsGroup)
				{
					// Clear all the children if the root primitive wants to light attachments as group
					// This is to make sure no child attachment in the chain overrides its parent
					CurrentPrimitive->SetLightAttachmentsAsGroup(false);
				}

				CurrentPrimitive->SetCastInsetShadow(bCastInsetShadow);
			}

			ProcessStack.Append(Current->GetAttachChildren());
		}
	}
	else
	{
		FMessageLog("Blueprint").Warning(LOCTEXT("SetCastInsetShadowForAllAttachments_InvalidPrimitiveComponent", "SetCastInsetShadowForAllAttachments: PrimitiveComponent must be non-null."));
	}
}
#undef LOCTEXT_NAMESPACE
