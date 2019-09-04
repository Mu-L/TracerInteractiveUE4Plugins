// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	EditorFactories.cpp: Editor class factories.
=============================================================================*/

#include "CoreMinimal.h"
#include "EngineDefines.h"
#include "Misc/MessageDialog.h"
#include "HAL/FileManager.h"
#include "Misc/CoreMisc.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"
#include "UObject/Interface.h"
#include "Misc/PackageName.h"
#include "Fonts/FontBulkData.h"
#include "Fonts/CompositeFont.h"
#include "Misc/Attribute.h"
#include "Input/Reply.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "EditorStyleSet.h"
#include "Engine/EngineTypes.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Model.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Curves/KeyHandle.h"
#include "MaterialExpressionIO.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialFunctionMaterialLayer.h"
#include "Materials/MaterialFunctionMaterialLayerBlend.h"
#include "Materials/MaterialFunctionInstance.h"
#include "Materials/Material.h"
#include "Animation/AnimSequence.h"
#include "Curves/CurveBase.h"
#include "Curves/CurveFloat.h"
#include "Engine/Font.h"
#include "Animation/AnimInstance.h"
#include "Engine/Brush.h"
#include "Editor/EditorEngine.h"
#include "Engine/Selection.h"
#include "Factories/Factory.h"
#include "Factories/BlendSpaceFactory1D.h"
#include "Factories/AimOffsetBlendSpaceFactory1D.h"
#include "Factories/BlendSpaceFactoryNew.h"
#include "Factories/AimOffsetBlendSpaceFactoryNew.h"
#include "Factories/BlueprintFactory.h"
#include "Factories/BlueprintFunctionLibraryFactory.h"
#include "Factories/BlueprintMacroFactory.h"
#include "Factories/BlueprintInterfaceFactory.h"
#include "Factories/CameraAnimFactory.h"
#include "Factories/CurveFactory.h"
#include "Factories/CurveImportFactory.h"
#include "Factories/DataAssetFactory.h"
#include "Factories/DialogueVoiceFactory.h"
#include "Factories/DialogueWaveFactory.h"
#include "Factories/EnumFactory.h"
#include "Factories/ReimportFbxAnimSequenceFactory.h"
#include "Factories/ReimportFbxSkeletalMeshFactory.h"
#include "Factories/ReimportFbxStaticMeshFactory.h"
#include "Factories/FontFactory.h"
#include "Factories/FontFileImportFactory.h"
#include "Factories/ForceFeedbackEffectFactory.h"
#include "Factories/HapticFeedbackEffectCurveFactory.h"
#include "Factories/HapticFeedbackEffectBufferFactory.h"
#include "Factories/HapticFeedbackEffectSoundWaveFactory.h"
#include "Factories/InterpDataFactoryNew.h"
#include "Factories/LevelFactory.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialFunctionFactoryNew.h"
#include "Factories/MaterialFunctionMaterialLayerFactory.h"
#include "Factories/MaterialFunctionMaterialLayerBlendFactory.h"
#include "Factories/MaterialFunctionInstanceFactory.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Factories/MaterialParameterCollectionFactoryNew.h"
#include "Factories/ModelFactory.h"
#include "Factories/ObjectLibraryFactory.h"
#include "Factories/PackageFactory.h"
#include "Factories/ParticleSystemFactoryNew.h"
#include "Factories/PhysicalMaterialFactoryNew.h"
#include "Factories/PolysFactory.h"
#include "Factories/ReverbEffectFactory.h"
#include "Factories/SoundAttenuationFactory.h"
#include "Factories/SoundConcurrencyFactory.h"
#include "Factories/SoundClassFactory.h"
#include "Factories/SoundCueFactoryNew.h"
#include "Factories/ReimportSoundFactory.h"
#include "Factories/SoundMixFactory.h"
#include "Factories/ReimportSoundSurroundFactory.h"
#include "Factories/StructureFactory.h"
#include "Factories/StringTableFactory.h"
#include "Factories/SubsurfaceProfileFactory.h"
#include "Factories/Texture2dFactoryNew.h"
#include "Engine/Texture.h"
#include "Factories/TextureFactory.h"
#include "Factories/ReimportTextureFactory.h"
#include "Factories/TextureRenderTargetCubeFactoryNew.h"
#include "Factories/TextureRenderTargetFactoryNew.h"
#include "Factories/TouchInterfaceFactory.h"
#include "Factories/FbxAssetImportData.h"
#include "Factories/FbxAnimSequenceImportData.h"
#include "Factories/FbxSkeletalMeshImportData.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "Factories/FbxImportUI.h"
#include "Editor/GroupActor.h"
#include "Particles/ParticleSystem.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureLightProfile.h"
#include "SoundCueGraph/SoundCueGraphNode.h"
#include "Exporters/TextureCubeExporterHDR.h"
#include "Exporters/TextureExporterBMP.h"
#include "Exporters/TextureExporterHDR.h"
#include "Exporters/RenderTargetExporterHDR.h"
#include "Exporters/TextureExporterPCX.h"
#include "Exporters/TextureExporterTGA.h"
#include "EngineGlobals.h"
#include "GameFramework/ForceFeedbackEffect.h"
#include "Engine/StaticMesh.h"
#include "Sound/SoundWave.h"
#include "GameFramework/DefaultPhysicsVolume.h"
#include "Engine/SubsurfaceProfile.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FeedbackContext.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/DataAsset.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Camera/CameraAnim.h"
#include "Curves/CurveLinearColor.h"
#include "Curves/CurveVector.h"
#include "Engine/DataTable.h"
#include "Sound/DialogueVoice.h"
#include "Sound/DialogueWave.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialParameterCollection.h"
#include "Engine/ObjectLibrary.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Engine/Polys.h"
#include "Sound/ReverbEffect.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundMix.h"
#include "Engine/TextureCube.h"
#include "Engine/VolumeTexture.h"
#include "Engine/TextureRenderTarget.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "Engine/TextureRenderTargetCube.h"
#include "GameFramework/TouchInterface.h"
#include "Engine/UserDefinedEnum.h"
#include "Engine/UserDefinedStruct.h"
#include "Internationalization/StringTable.h"
#include "Editor.h"
#include "Matinee/InterpData.h"
#include "Matinee/InterpGroupCamera.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "Sound/SoundNodeAttenuation.h"
#include "Sound/SoundNodeModulator.h"
#include "Factories.h"
#include "NormalMapIdentification.h"
#include "AudioDeviceManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "BmpImageSupport.h"
#include "ScopedTransaction.h"
#include "BSPOps.h"
#include "LevelUtils.h"
#include "PackageTools.h"
#include "SSkeletonWidget.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"

#include "DDSLoader.h"
#include "HDRLoader.h"
#include "Factories/IESLoader.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"

#include "FbxImporter.h"
#include "Misc/FbxErrors.h"

#include "AssetRegistryModule.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "ClassViewerModule.h"
#include "ClassViewerFilter.h"
#include "Kismet2/SClassPickerDialog.h"
#include "Logging/MessageLog.h"
#include "Kismet2/EnumEditorUtils.h"
#include "Kismet2/StructureEditorUtils.h"

#include "InstancedFoliageActor.h"

#if PLATFORM_WINDOWS
	// Needed for DDS support.
	#include "Windows/WindowsHWrapper.h"
	#include "Windows/AllowWindowsPlatformTypes.h"
		#include <ddraw.h>
	#include "Windows/HideWindowsPlatformTypes.h"
#endif

#if WITH_EDITOR
	#include "CubemapUnwrapUtils.h"
#endif

#include "Components/BrushComponent.h"
#include "EngineUtils.h"
#include "Engine/AssetUserData.h"
#include "Animation/BlendSpace1D.h"
#include "Engine/FontFace.h"
#include "Components/AudioComponent.h"
#include "AI/Navigation/NavCollisionBase.h"
#include "Animation/BlendSpace.h"
#include "Animation/AimOffsetBlendSpace.h"
#include "Animation/AimOffsetBlendSpace1D.h"
#include "GameFramework/ForceFeedbackAttenuation.h"
#include "Haptics/HapticFeedbackEffect_Curve.h"
#include "Haptics/HapticFeedbackEffect_Buffer.h"
#include "Haptics/HapticFeedbackEffect_SoundWave.h"
#include "DataTableEditorUtils.h"
#include "KismetCompilerModule.h"
#include "Factories/SubUVAnimationFactory.h"
#include "Particles/SubUVAnimation.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Factories/CanvasRenderTarget2DFactoryNew.h"
#include "ImageUtils.h"
#include "Engine/PreviewMeshCollection.h"
#include "Factories/PreviewMeshCollectionFactory.h"
#include "Factories/ForceFeedbackAttenuationFactory.h"
#include "Misc/FileHelper.h"
#include "ActorGroupingUtils.h"

#include "Editor/EditorPerProjectUserSettings.h"
#include "JsonObjectConverter.h"
#include "MaterialEditorModule.h"
#include "Factories/CurveLinearColorAtlasFactory.h"
#include "Curves/CurveLinearColorAtlas.h"
#include "Rendering/SkeletalMeshModel.h"

#include "Misc/App.h"
#include "Subsystems/ImportSubsystem.h"

#include "IDesktopPlatform.h"
#include "DesktopPlatformModule.h"
#include "Interfaces/IMainFrameModule.h"
#include "Factories/TextureImportSettings.h"

#include "LODUtilities.h"

DEFINE_LOG_CATEGORY(LogEditorFactories);

#define LOCTEXT_NAMESPACE "EditorFactories"

/*------------------------------------------------------------------------------
	Shared - used by multiple factories
------------------------------------------------------------------------------*/

void GetReimportPathFromUser(const FText& TitleLabel, TArray<FString>& InOutFilenames)
{
	FString FileTypes;
	FString AllExtensions;
	// Determine whether we will allow multi select and clear old filenames
	bool bAllowMultiSelect = false;
	InOutFilenames.Empty();

	FileTypes = TEXT("FBX Files (*.fbx)|*.fbx");

	FString DefaultFolder;
	FString DefaultFile;

	// Prompt the user for the filenames
	TArray<FString> OpenFilenames;
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	bool bOpened = false;
	if (DesktopPlatform)
	{
		void* ParentWindowWindowHandle = NULL;

		IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
		const TSharedPtr<SWindow>& MainFrameParentWindow = MainFrameModule.GetParentWindow();
		if (MainFrameParentWindow.IsValid() && MainFrameParentWindow->GetNativeWindow().IsValid())
		{
			ParentWindowWindowHandle = MainFrameParentWindow->GetNativeWindow()->GetOSWindowHandle();
		}

		const FString Title = FString::Printf(TEXT("%s %s"), *NSLOCTEXT("FBXReimport", "ImportContentTypeDialogTitle", "Add import source file for").ToString(), *TitleLabel.ToString());
		bOpened = DesktopPlatform->OpenFileDialog(
			ParentWindowWindowHandle,
			Title,
			*DefaultFolder,
			*DefaultFile,
			FileTypes,
			bAllowMultiSelect ? EFileDialogFlags::Multiple : EFileDialogFlags::None,
			OpenFilenames
		);
	}

	if (bOpened)
	{
		for (int32 FileIndex = 0; FileIndex < OpenFilenames.Num(); ++FileIndex)
		{
			InOutFilenames.Add(OpenFilenames[FileIndex]);
		}
	}
}


class FAssetClassParentFilter : public IClassViewerFilter
{
public:
	FAssetClassParentFilter()
		: DisallowedClassFlags(CLASS_None), bDisallowBlueprintBase(false)
	{}

	/** All children of these classes will be included unless filtered out by another setting. */
	TSet< const UClass* > AllowedChildrenOfClasses;

	/** Disallowed class flags. */
	EClassFlags DisallowedClassFlags;

	/** Disallow blueprint base classes. */
	bool bDisallowBlueprintBase;

	virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
	{
		bool bAllowed= !InClass->HasAnyClassFlags(DisallowedClassFlags)
			&& InFilterFuncs->IfInChildOfClassesSet(AllowedChildrenOfClasses, InClass) != EFilterReturn::Failed;

		if (bAllowed && bDisallowBlueprintBase)
		{
			if (FKismetEditorUtilities::CanCreateBlueprintOfClass(InClass))
			{
				return false;
			}
		}

		return bAllowed;
	}

	virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
	{
		if (bDisallowBlueprintBase)
		{
			return false;
		}

		return !InUnloadedClassData->HasAnyClassFlags(DisallowedClassFlags)
			&& InFilterFuncs->IfInChildOfClassesSet(AllowedChildrenOfClasses, InUnloadedClassData) != EFilterReturn::Failed;
	}
};

/*------------------------------------------------------------------------------
	UTexture2DFactoryNew implementation.
------------------------------------------------------------------------------*/
UTexture2DFactoryNew::UTexture2DFactoryNew(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass		= UTexture2D::StaticClass();
	bCreateNew			= true;
	bEditAfterNew		= true;

	Width = 256;
	Height = 256;
}

bool UTexture2DFactoryNew::ShouldShowInNewMenu() const
{
	// You may not create texture2d assets in the content browser
	return false;
}

UObject* UTexture2DFactoryNew::FactoryCreateNew( UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn )
{
	// Do not create a texture with bad dimensions.
	if((Width & (Width - 1)) || (Height & (Height - 1)))
	{
		return nullptr;
	}

	UTexture2D* Object = NewObject<UTexture2D>(InParent, InClass, InName, Flags);

	Object->Source.Init2DWithMipChain(Width, Height, TSF_BGRA8);

	//Set the source art to be white as default.
	if( Object->Source.IsValid() )
	{
		TArray64<uint8> TexturePixels;
		Object->Source.GetMipData( TexturePixels, 0 );

		uint8* DestData = Object->Source.LockMip(0);
		FMemory::Memset(DestData, 255, TexturePixels.Num() * sizeof(uint8));
		Object->Source.UnlockMip(0);

		Object->PostEditChange();
	}
	return Object;
}

/*------------------------------------------------------------------------------
	UMaterialInstanceConstantFactoryNew implementation.
------------------------------------------------------------------------------*/
UMaterialInstanceConstantFactoryNew::UMaterialInstanceConstantFactoryNew(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	SupportedClass = UMaterialInstanceConstant::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UMaterialInstanceConstantFactoryNew::FactoryCreateNew(UClass* Class,UObject* InParent,FName Name,EObjectFlags Flags,UObject* Context,FFeedbackContext* Warn)
{
	auto MIC = NewObject<UMaterialInstanceConstant>(InParent, Class, Name, Flags);
	
	if ( MIC )
	{
		MIC->InitResources();

		if ( InitialParent )
		{
			MIC->SetParentEditorOnly(InitialParent);
		}
	}

	return MIC;
}

/*------------------------------------------------------------------------------
	UMaterialFactoryNew implementation.
------------------------------------------------------------------------------*/
UMaterialFactoryNew::UMaterialFactoryNew(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	SupportedClass = UMaterial::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}


UObject* UMaterialFactoryNew::FactoryCreateNew(UClass* Class,UObject* InParent,FName Name,EObjectFlags Flags,UObject* Context,FFeedbackContext* Warn)
{
	UMaterial* NewMaterial = NewObject<UMaterial>(InParent, Class, Name, Flags);

	if ( InitialTexture != nullptr )
	{
		// An initial texture was specified, add it and assign it to the BaseColor
		UMaterialExpressionTextureSample* TextureSampler = NewObject<UMaterialExpressionTextureSample>(NewMaterial);
		{
			TextureSampler->MaterialExpressionEditorX = -250;
			TextureSampler->Texture = InitialTexture;
			TextureSampler->AutoSetSampleType();
		}

		NewMaterial->Expressions.Add(TextureSampler);

		FExpressionOutput& Output = TextureSampler->GetOutputs()[0];
		FExpressionInput& Input = (TextureSampler->SamplerType == SAMPLERTYPE_Normal)
			? (FExpressionInput&)NewMaterial->Normal
			: (FExpressionInput&)NewMaterial->BaseColor;

		Input.Expression = TextureSampler;
		Input.Mask = Output.Mask;
		Input.MaskR = Output.MaskR;
		Input.MaskG = Output.MaskG;
		Input.MaskB = Output.MaskB;
		Input.MaskA = Output.MaskA;

		NewMaterial->PostEditChange();
	}

	return NewMaterial;
}


/*------------------------------------------------------------------------------
	UMaterialFunctionFactoryNew implementation.
------------------------------------------------------------------------------*/
UMaterialFunctionFactoryNew::UMaterialFunctionFactoryNew(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UMaterialFunction::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UMaterialFunctionFactoryNew::FactoryCreateNew(UClass* Class,UObject* InParent,FName Name,EObjectFlags Flags,UObject* Context,FFeedbackContext* Warn)
{
	return NewObject<UObject>(InParent, Class, Name, Flags);
}

/*------------------------------------------------------------------------------
	UMaterialFunctionMaterialLayerFactory implementation.
------------------------------------------------------------------------------*/
UMaterialFunctionMaterialLayerFactory::UMaterialFunctionMaterialLayerFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UMaterialFunctionMaterialLayer::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

bool UMaterialFunctionMaterialLayerFactory::CanCreateNew() const
{
	IMaterialEditorModule& MaterialEditorModule = FModuleManager::LoadModuleChecked<IMaterialEditorModule>( "MaterialEditor" );
	return MaterialEditorModule.MaterialLayersEnabled();
}

UObject* UMaterialFunctionMaterialLayerFactory::FactoryCreateNew(UClass* Class,UObject* InParent,FName Name,EObjectFlags Flags,UObject* Context,FFeedbackContext* Warn)
{
	UMaterialFunctionMaterialLayer* Function = NewObject<UMaterialFunctionMaterialLayer>(InParent, UMaterialFunctionMaterialLayer::StaticClass(), Name, Flags);
	if (Function)
	{
		Function->SetMaterialFunctionUsage(EMaterialFunctionUsage::MaterialLayer);
	}
	return Function;
}

/*------------------------------------------------------------------------------
	UMaterialFunctionMaterialLayerBlendFactory implementation.
------------------------------------------------------------------------------*/
UMaterialFunctionMaterialLayerBlendFactory::UMaterialFunctionMaterialLayerBlendFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UMaterialFunctionMaterialLayerBlend::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

bool UMaterialFunctionMaterialLayerBlendFactory::CanCreateNew() const
{
	IMaterialEditorModule& MaterialEditorModule = FModuleManager::LoadModuleChecked<IMaterialEditorModule>( "MaterialEditor" );
	return MaterialEditorModule.MaterialLayersEnabled();
}

UObject* UMaterialFunctionMaterialLayerBlendFactory::FactoryCreateNew(UClass* Class,UObject* InParent,FName Name,EObjectFlags Flags,UObject* Context,FFeedbackContext* Warn)
{
	UMaterialFunctionMaterialLayerBlend* Function = NewObject<UMaterialFunctionMaterialLayerBlend>(InParent, UMaterialFunctionMaterialLayerBlend::StaticClass(), Name, Flags);
	if (Function)
	{
		Function->SetMaterialFunctionUsage(EMaterialFunctionUsage::MaterialLayerBlend);
	}
	return Function;
}


/*------------------------------------------------------------------------------
	UMaterialFunctionInstanceFactory implementation.
------------------------------------------------------------------------------*/
UMaterialFunctionInstanceFactory::UMaterialFunctionInstanceFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UMaterialFunctionInstance::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UMaterialFunctionInstanceFactory::FactoryCreateNew(UClass* Class,UObject* InParent,FName Name,EObjectFlags Flags,UObject* Context,FFeedbackContext* Warn)
{
	auto MFI = NewObject<UMaterialFunctionInstance>(InParent, Class, Name, Flags);

	if (MFI)
	{
		MFI->SetParent(InitialParent);
	}

	return MFI;
}

/*------------------------------------------------------------------------------
UMaterialFunctionMaterialLayerInstanceFactory implementation.
------------------------------------------------------------------------------*/
UMaterialFunctionMaterialLayerInstanceFactory::UMaterialFunctionMaterialLayerInstanceFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UMaterialFunctionMaterialLayerInstance::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UMaterialFunctionMaterialLayerInstanceFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	auto MFI = NewObject<UMaterialFunctionMaterialLayerInstance>(InParent, Class, Name, Flags);

	if (MFI)
	{
		MFI->SetParent(InitialParent);
	}

	return MFI;
}

/*------------------------------------------------------------------------------
UMaterialFunctionMaterialLayerBlendInstanceFactory implementation.
------------------------------------------------------------------------------*/
UMaterialFunctionMaterialLayerBlendInstanceFactory::UMaterialFunctionMaterialLayerBlendInstanceFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UMaterialFunctionMaterialLayerBlendInstance::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UMaterialFunctionMaterialLayerBlendInstanceFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	auto MFI = NewObject<UMaterialFunctionMaterialLayerBlendInstance>(InParent, Class, Name, Flags);

	if (MFI)
	{
		MFI->SetParent(InitialParent);
	}

	return MFI;
}

/*------------------------------------------------------------------------------
	UMaterialParameterCollectionFactoryNew implementation.
------------------------------------------------------------------------------*/
UMaterialParameterCollectionFactoryNew::UMaterialParameterCollectionFactoryNew(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	SupportedClass = UMaterialParameterCollection::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UMaterialParameterCollectionFactoryNew::FactoryCreateNew(UClass* Class,UObject* InParent,FName Name,EObjectFlags Flags,UObject* Context,FFeedbackContext* Warn)
{
	UMaterialParameterCollection* MaterialParameterCollection = NewObject<UMaterialParameterCollection>(InParent, Class, Name, Flags);
	
	if (MaterialParameterCollection)
	{
		for (TObjectIterator<UWorld> It; It; ++It)
		{
			UWorld* CurrentWorld = *It;
			CurrentWorld->AddParameterCollectionInstance(MaterialParameterCollection, true);
		}
	}

	return MaterialParameterCollection;
}

/*------------------------------------------------------------------------------
	ULevelFactory.
------------------------------------------------------------------------------*/

ULevelFactory::ULevelFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UWorld::StaticClass();
	Formats.Add(TEXT("t3d;Unreal World"));

	bCreateNew = false;
	bText = true;
	bEditorImport = false;
}

UObject* ULevelFactory::FactoryCreateText
(
	UClass*				Class,
	UObject*			InParent,
	FName				Name,
	EObjectFlags		Flags,
	UObject*			Context,
	const TCHAR*		Type,
	const TCHAR*&		Buffer,
	const TCHAR*		BufferEnd,
	FFeedbackContext*	Warn
)
{
	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPreImport(this, Class, InParent, Name, Type);

	UWorld* World = GWorld;
	//@todo locked levels - if lock state is persistent, do we need to check for whether the level is locked?
#ifdef MULTI_LEVEL_IMPORT
	// this level is the current level for pasting. If we get a named level, not for pasting, we will look up the level, and overwrite this
	ULevel*				OldCurrentLevel = World->GetCurrentLevel();
	check(OldCurrentLevel);
#endif

	UPackage* RootMapPackage = Cast<UPackage>(InParent);
	TMap<FString, UPackage*> MapPackages;
	TMap<AActor*, AActor*> MapActors;
	// Assumes data is being imported over top of a new, valid map.
	FParse::Next( &Buffer );
	if (GetBEGIN(&Buffer, TEXT("MAP")))
	{
		if (ULevel* Level = Cast<ULevel>(InParent))
		{
			World = Level->GetWorld();
		}

		if (RootMapPackage)
		{
			FString MapName;
			if (FParse::Value(Buffer, TEXT("Name="), MapName))
			{
				// Advance the buffer
				Buffer += FCString::Strlen(TEXT("Name="));
				Buffer += MapName.Len();
				// Check to make sure that there are no naming conflicts
				if( RootMapPackage->Rename(*MapName, nullptr, REN_Test | REN_ForceNoResetLoaders) )
				{
					// Rename it!
					RootMapPackage->Rename(*MapName, nullptr, REN_ForceNoResetLoaders);
				}
				else
				{
					Warn->Logf(ELogVerbosity::Warning, TEXT("The Root map package name : '%s', conflicts with the existing object : '%s'"), *RootMapPackage->GetFullName(), *MapName);
					GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, nullptr );
					return nullptr;
				}
				
				// Stick it in the package map
				MapPackages.Add(MapName, RootMapPackage);
			}
		}
	}
	else
	{
		return World;
	}

	bool bIsExpectingNewMapTag = false;

	// Unselect all actors.
	if (GWorld == World)
	{
		GEditor->SelectNone( false, false );

		// Mark us importing a T3D (only from a file, not from copy/paste).
		GEditor->IsImportingT3D = (FCString::Stricmp(Type,TEXT("paste")) != 0) && (FCString::Stricmp(Type,TEXT("move")) != 0);
		GIsImportingT3D = GEditor->IsImportingT3D;
	}

	// We need to detect if the .t3d file is the entire level or just selected actors, because we
	// don't want to replace the WorldSettings and BuildBrush if they already exist. To know if we
	// can skip the WorldSettings and BuilderBrush (which will always be the first two actors if the entire
	// level was exported), we make sure the first actor is a WorldSettings, if it is, and we already had
	// a WorldSettings, then we skip the builder brush
	// In other words, if we are importing a full level into a full level, we don't want to import
	// the WorldSettings and BuildBrush
	bool bShouldSkipImportSpecialActors = false;
	bool bHitLevelToken = false;

	FString MapPackageText;

	int32 ActorIndex = 0;

	//@todo locked levels - what needs to happen here?


	// Maintain a list of a new actors and the text they were created from.
	TMap<AActor*,FString> NewActorMap;
	TMap< FString, AGroupActor* > NewGroups; // Key=The orig actor's group's name, Value=The new actor's group.
	
	// Maintain a lookup for the new actors, keyed by their source FName.
	TMap<FName, AActor*> NewActorsFNames;

	// Maintain a lookup from existing to new actors, used when replacing internal references when copy+pasting / duplicating
	TMap<AActor*, AActor*> ExistingToNewMap;

	// Maintain a lookup of the new actors to their parent and socket attachment if provided.
	struct FAttachmentDetail
	{
		const FName ParentName;
		const FName SocketName;
		FAttachmentDetail(const FName InParentName, const FName InSocketName) : ParentName(InParentName), SocketName(InSocketName) {}
	};
	TMap<AActor*,FAttachmentDetail> NewActorsAttachmentMap;

	FString StrLine;
	while( FParse::Line(&Buffer,StrLine) )
	{
		const TCHAR* Str = *StrLine;

		// If we're still waiting to see a 'MAP' tag, then check for that
		if( bIsExpectingNewMapTag )
		{
			if( GetBEGIN( &Str, TEXT("MAP")) )
			{
				bIsExpectingNewMapTag = false;
			}
			else
			{
				// Not a new map tag, so continue on
			}
		}
		else if( GetEND(&Str,TEXT("MAP")) )
		{
			// End of brush polys.
			bIsExpectingNewMapTag = true;
		}
		else if( GetBEGIN(&Str,TEXT("LEVEL")) )
		{
			bHitLevelToken = true;
#ifdef MULTI_LEVEL_IMPORT
			// try to look up the named level. if this fails, we will need to create a new level
			if (ParseObject<ULevel>(Str, TEXT("NAME="), World->GetCurrentLevel(), World->GetOuter()) == false)
			{
				// get the name
				FString LevelName;
				// if there is no name, that means we are pasting, so just put this guy into the CurrentLevel - don't make a new one
				if (FParse::Value(Str, TEXT("NAME="), LevelName))
				{
					// create a new named level
					World->SetCurrentLevel( new(World->GetOuter(), *LevelName)ULevel(FObjectInitializer(),FURL(nullptr)) );
				}
			}
#endif
		}
		else if( GetEND(&Str,TEXT("LEVEL")) )
		{
#ifdef MULTI_LEVEL_IMPORT
			// any actors outside of a level block go into the current level
			World->SetCurrentLevel( OldCurrentLevel );
#endif
		}
		else if( GetBEGIN(&Str,TEXT("ACTOR")) )
		{
			UClass* TempClass;
			if( ParseObject<UClass>( Str, TEXT("CLASS="), TempClass, ANY_PACKAGE ) )
			{
				// Get actor name.
				FName ActorUniqueName(NAME_None);
				FName ActorSourceName(NAME_None);
				FParse::Value( Str, TEXT("NAME="), ActorSourceName );
				ActorUniqueName = ActorSourceName;
				// Make sure this name is unique.
				AActor* Found=nullptr;
				if( ActorUniqueName!=NAME_None )
				{
					// look in the current level for the same named actor
					Found = FindObject<AActor>( World->GetCurrentLevel(), *ActorUniqueName.ToString() );
				}
				if( Found )
				{
					ActorUniqueName = MakeUniqueObjectName( World->GetCurrentLevel(), TempClass, ActorUniqueName );
				}

				// Get parent name for attachment.
				FName ActorParentName(NAME_None);
				FParse::Value( Str, TEXT("ParentActor="), ActorParentName );

				// Get socket name for attachment.
				FName ActorParentSocket(NAME_None);
				FParse::Value( Str, TEXT("SocketName="), ActorParentSocket );

				// if an archetype was specified in the Begin Object block, use that as the template for the ConstructObject call.
				FString ArchetypeName;
				AActor* Archetype = nullptr;
				if (FParse::Value(Str, TEXT("Archetype="), ArchetypeName))
				{
					// if given a name, break it up along the ' so separate the class from the name
					FString ObjectClass;
					FString ObjectPath;
					if ( FPackageName::ParseExportTextPath(ArchetypeName, &ObjectClass, &ObjectPath) )
					{
						// find the class
						UClass* ArchetypeClass = (UClass*)StaticFindObject(UClass::StaticClass(), ANY_PACKAGE, *ObjectClass);
						if ( ArchetypeClass )
						{
							if ( ArchetypeClass->IsChildOf(AActor::StaticClass()) )
							{
								// if we had the class, find the archetype
								Archetype = Cast<AActor>(StaticFindObject(ArchetypeClass, ANY_PACKAGE, *ObjectPath));
							}
							else
							{
								Warn->Logf(ELogVerbosity::Warning, TEXT("Invalid archetype specified in subobject definition '%s': %s is not a child of Actor"),
									Str, *ObjectClass);
							}
						}
					}
				}

				// If we're pasting from a class that belongs to a map we need to duplicate the class and use that instead
				if (FBlueprintEditorUtils::IsAnonymousBlueprintClass(TempClass))
				{
					UBlueprint* NewBP = DuplicateObject(CastChecked<UBlueprint>(TempClass->ClassGeneratedBy), World->GetCurrentLevel(), *FString::Printf(TEXT("%s_BPClass"), *ActorUniqueName.ToString()));
					if (NewBP)
					{
						NewBP->ClearFlags(RF_Standalone);

						FKismetEditorUtilities::CompileBlueprint(NewBP, EBlueprintCompileOptions::SkipGarbageCollection);

						TempClass = NewBP->GeneratedClass;

						// Since we changed the class we can't use an Archetype,
						// however that is fine since we will have been editing the CDO anyways
						Archetype = nullptr;
					}
				}

				if (TempClass->IsChildOf(AWorldSettings::StaticClass()))
				{
					// if we see a WorldSettings, then we are importing an entire level, so if we
					// are importing into an existing level, then we should not import the next actor
					// which will be the builder brush
					check(ActorIndex == 0);

					// if we have any actors, then we are importing into an existing level
					if (World->GetCurrentLevel()->Actors.Num())
					{
						check(World->GetCurrentLevel()->Actors[0]->IsA(AWorldSettings::StaticClass()));

						// full level into full level, skip the first two actors
						bShouldSkipImportSpecialActors = true;
					}
				}

				// Get property text.
				FString PropText, PropertyLine;
				while
				(	GetEND( &Buffer, TEXT("ACTOR") )==0
				&&	FParse::Line( &Buffer, PropertyLine ) )
				{
					PropText += *PropertyLine;
					PropText += TEXT("\r\n");
				}

				// If we need to skip the WorldSettings and BuilderBrush, skip the first two actors.  Note that
				// at this point, we already know that we have a WorldSettings and BuilderBrush in the .t3d.
				if ( FLevelUtils::IsLevelLocked(World->GetCurrentLevel()) )
				{
					UE_LOG(LogEditorFactories, Warning, TEXT("Import actor: The requested operation could not be completed because the level is locked."));
					GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, nullptr );
					return nullptr;
				}
				else if ( !(bShouldSkipImportSpecialActors && ActorIndex < 2) )
				{
					// Don't import the default physics volume, as it doesn't have a UModel associated with it
					// and thus will not import properly.
					if ( !TempClass->IsChildOf(ADefaultPhysicsVolume::StaticClass()) )
					{
						// Create a new actor.
						FActorSpawnParameters SpawnInfo;
						SpawnInfo.Name = ActorUniqueName;
						SpawnInfo.Template = Archetype;
						SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
						AActor* NewActor = World->SpawnActor( TempClass, nullptr, nullptr, SpawnInfo );
						
						if( NewActor )
						{
							if( UActorGroupingUtils::IsGroupingActive() && !Cast<AGroupActor>(NewActor) )
							{
								bool bGrouped = false;

								AGroupActor** tmpNewGroup = nullptr;
								// We need to add all the objects we selected into groups with new objects that were in their group before.
								FString GroupName;
								if(FParse::Value(Str, TEXT("GroupActor="), GroupName))
								{
									tmpNewGroup = NewGroups.Find(GroupName);
									bGrouped = true;
								}

								// Does the group exist?
								if(tmpNewGroup)
								{
									AGroupActor* NewActorGroup = *tmpNewGroup;

									// Add it to the group.
									NewActorGroup->Add(*NewActor);
								}
								else if(bGrouped)
								{
									// Create a new group and add the actor.
									AGroupActor* SpawnedGroupActor = NewActor->GetWorld()->SpawnActor<AGroupActor>();
									SpawnedGroupActor->Add(*NewActor);

									// Place the group in the map so we can find it later.
									NewGroups.Add( GroupName, SpawnedGroupActor);
									FActorLabelUtilities::SetActorLabelUnique(SpawnedGroupActor, GroupName);
								}

								// If we're copying a sub-group, add add duplicated group to original parent
								// If we're just copying an actor, only append it to the original parent group if unlocked
								if (Found)
								{
									AGroupActor* FoundParent = AGroupActor::GetParentForActor(Found);
									if(FoundParent && (Found->IsA(AGroupActor::StaticClass()) || !FoundParent->IsLocked()) )
									{
										FoundParent->Add(*NewActor);
									}
								}
							}

							// Store the new actor and the text it should be initialized with.
							NewActorMap.Add( NewActor, *PropText );

							// Store the copy to original actor mapping
							MapActors.Add(NewActor, Found);

							// Store the new actor against its source actor name (not the one that may have been made unique)
							if( ActorSourceName!=NAME_None )
							{
								NewActorsFNames.Add( ActorSourceName, NewActor );
								if (Found)
								{
									ExistingToNewMap.Add(Found, NewActor);
								}
							}

							// Store the new actor with its parent's FName, and socket FName if applicable
							if( ActorParentName!=NAME_None )
							{
								NewActorsAttachmentMap.Add( NewActor, FAttachmentDetail( ActorParentName, ActorParentSocket ) );
							}
						}
					}
				}

				// increment the number of actors we imported
				ActorIndex++;
			}
		}
		else if( GetBEGIN(&Str,TEXT("SURFACE")) )
		{
			UMaterialInterface* SrcMaterial = nullptr;
			FVector SrcBase, SrcTextureU, SrcTextureV, SrcNormal;
			uint32 SrcPolyFlags = PF_DefaultFlags;
			int32 SurfacePropertiesParsed = 0;

			SrcBase = FVector::ZeroVector;
			SrcTextureU = FVector::ZeroVector;
			SrcTextureV = FVector::ZeroVector;
			SrcNormal = FVector::ZeroVector;

			bool bJustParsedTextureName = false;
			bool bFoundSurfaceEnd = false;
			bool bParsedLineSuccessfully = false;

			do
			{
				if( GetEND( &Buffer, TEXT("SURFACE") ) )
				{
					bFoundSurfaceEnd = true;
					bParsedLineSuccessfully = true;
				}
				else if( FParse::Command(&Buffer,TEXT("TEXTURE")) )
				{
					Buffer++;	// Move past the '=' sign

					FString TextureName;
					bParsedLineSuccessfully = FParse::Line(&Buffer, TextureName, true);
					if ( TextureName != TEXT("None") )
					{
						SrcMaterial = Cast<UMaterialInterface>(StaticLoadObject( UMaterialInterface::StaticClass(), nullptr, *TextureName, nullptr, LOAD_NoWarn, nullptr ));
					}
					bJustParsedTextureName = true;
					SurfacePropertiesParsed++;
				}
				else if( FParse::Command(&Buffer,TEXT("BASE")) )
				{
					GetFVECTOR( Buffer, SrcBase );
					SurfacePropertiesParsed++;
				}
				else if( FParse::Command(&Buffer,TEXT("TEXTUREU")) )
				{
					GetFVECTOR( Buffer, SrcTextureU );
					SurfacePropertiesParsed++;
				}
				else if( FParse::Command(&Buffer,TEXT("TEXTUREV")) )
				{
					GetFVECTOR( Buffer, SrcTextureV );
					SurfacePropertiesParsed++;
				}
				else if( FParse::Command(&Buffer,TEXT("NORMAL")) )
				{
					GetFVECTOR( Buffer, SrcNormal );
					SurfacePropertiesParsed++;
				}
				else if( FParse::Command(&Buffer,TEXT("POLYFLAGS")) )
				{
					FParse::Value( Buffer, TEXT("="), SrcPolyFlags );
					SurfacePropertiesParsed++;
				}

				// Parse to the next line only if the texture name wasn't just parsed or if the 
				// end of surface isn't parsed. Don't parse to the next line for the texture 
				// name because a FParse::Line() is called when retrieving the texture name. 
				// Doing another FParse::Line() would skip past a necessary surface property.
				if( !bJustParsedTextureName && !bFoundSurfaceEnd )
				{
					FString DummyLine;
					bParsedLineSuccessfully = FParse::Line( &Buffer, DummyLine );
				}

				// Reset this bool so that we can parse lines starting during next iteration.
				bJustParsedTextureName = false;
			}
			while( !bFoundSurfaceEnd && bParsedLineSuccessfully );

			// There are 6 BSP surface properties exported via T3D. If there wasn't 6 properties 
			// successfully parsed, the parsing failed. This surface isn't valid then.
			if( SurfacePropertiesParsed == 6 )
			{
				const FScopedTransaction Transaction( NSLOCTEXT("UnrealEd", "PasteTextureToSurface", "Paste Texture to Surface") );

				for( int32 j = 0; j < World->GetNumLevels(); ++j )
				{
					ULevel* CurrentLevel = World->GetLevel(j);
					for( int32 i = 0 ; i < CurrentLevel->Model->Surfs.Num() ; i++ )
					{
						FBspSurf* DstSurf = &CurrentLevel->Model->Surfs[i];

						if( DstSurf->PolyFlags & PF_Selected )
						{
							CurrentLevel->Model->ModifySurf( i, 1 );

							const FVector DstNormal = CurrentLevel->Model->Vectors[ DstSurf->vNormal ];

							// Need to compensate for changes in the polygon normal.
							const FRotator SrcRot = SrcNormal.Rotation();
							const FRotator DstRot = DstNormal.Rotation();
							const FRotationMatrix RotMatrix( DstRot - SrcRot );

							FVector NewBase	= RotMatrix.TransformPosition( SrcBase );
							FVector NewTextureU = RotMatrix.TransformVector( SrcTextureU );
							FVector NewTextureV = RotMatrix.TransformVector( SrcTextureV );

							DstSurf->Material = SrcMaterial;
							DstSurf->pBase = FBSPOps::bspAddPoint( CurrentLevel->Model, &NewBase, 1 );
							DstSurf->vTextureU = FBSPOps::bspAddVector( CurrentLevel->Model, &NewTextureU, 0 );
							DstSurf->vTextureV = FBSPOps::bspAddVector( CurrentLevel->Model, &NewTextureV, 0 );
							DstSurf->PolyFlags = SrcPolyFlags;

							DstSurf->PolyFlags &= ~PF_Selected;

							CurrentLevel->MarkPackageDirty();

							const bool bUpdateTexCoords = true;
							const bool bOnlyRefreshSurfaceMaterials = false;
							if (GWorld == World)
							{
								GEditor->polyUpdateMaster(CurrentLevel->Model, i, bUpdateTexCoords, bOnlyRefreshSurfaceMaterials);
							}
						}
					}
				}
			}
		}
		else if (GetBEGIN(&Str,TEXT("MAPPACKAGE")))
		{
			// Get all the text.
			while ((GetEND(&Buffer, TEXT("MAPPACKAGE") )==0) && FParse::Line(&Buffer, StrLine))
			{
				MapPackageText += *StrLine;
				MapPackageText += TEXT("\r\n");
			}
		}
	}

	// Import actor properties.
	// We do this after creating all actors so that actor references can be matched up.
	AWorldSettings* WorldSettings = World->GetWorldSettings();

	if (GIsImportingT3D && (MapPackageText.Len() > 0))
	{
		UPackageFactory* PackageFactory = NewObject<UPackageFactory>();
		check(PackageFactory);

		FName NewPackageName(*(RootMapPackage->GetName()));

		const TCHAR* MapPkg_BufferStart = *MapPackageText;
		const TCHAR* MapPkg_BufferEnd = MapPkg_BufferStart + MapPackageText.Len();
		PackageFactory->FactoryCreateText(UPackage::StaticClass(), nullptr, NewPackageName, RF_NoFlags, 0, TEXT("T3D"), MapPkg_BufferStart, MapPkg_BufferEnd, Warn);
	}

	// Pass 1: Sort out all the properties on the individual actors
	bool bIsMoveToStreamingLevel =(FCString::Stricmp(Type, TEXT("move")) == 0);
	for (auto& ActorMapElement : NewActorMap)
	{
		AActor* Actor = ActorMapElement.Key;

		// Import properties if the new actor is 
		bool		bActorChanged = false;
		FString&	PropText = ActorMapElement.Value;
		if ( Actor->ShouldImport(&PropText, bIsMoveToStreamingLevel) )
		{
			Actor->PreEditChange(nullptr);
			ImportObjectProperties( (uint8*)Actor, *PropText, Actor->GetClass(), Actor, Actor, Warn, 0, INDEX_NONE, NULL, &ExistingToNewMap );
			bActorChanged = true;

			if (GWorld == World)
			{
				GEditor->SelectActor( Actor, true, false, true );
			}
		}
		else // This actor is new, but rejected to import its properties, so just delete...
		{
			Actor->Destroy();
		}

		// If this is a newly imported brush, validate it.  If it's a newly imported dynamic brush, rebuild it first.
		// Previously, this just called bspValidateBrush.  However, that caused the dynamic brushes which require a valid BSP tree
		// to be built to break after being duplicated.  Calling RebuildBrush will rebuild the BSP tree from the imported polygons.
		ABrush* Brush = Cast<ABrush>(Actor);
		if( bActorChanged && Brush && Brush->Brush )
		{
			const bool bIsStaticBrush = Brush->IsStaticBrush();
			if( !bIsStaticBrush )
			{
				FBSPOps::RebuildBrush( Brush->Brush );
			}

			FBSPOps::bspValidateBrush(Brush->Brush, true, false);
		}

		// Copy brushes' model pointers over to their BrushComponent, to keep compatibility with old T3Ds.
		if( Brush && bActorChanged )
		{
			if( Brush->GetBrushComponent() ) // Should always be the case, but not asserting so that old broken content won't crash.
			{
				Brush->GetBrushComponent()->Brush = Brush->Brush;

				// We need to avoid duplicating default/ builder brushes. This is done by destroying all brushes that are CSG_Active and are not
				// the default brush in their respective levels.
				if( Brush->IsStaticBrush() && Brush->BrushType==Brush_Default )
				{
					bool bIsDefaultBrush = false;
					
					// Iterate over all levels and compare current actor to the level's default brush.
					for( int32 LevelIndex=0; LevelIndex<World->GetNumLevels(); LevelIndex++ )
					{
						ULevel* Level = World->GetLevel(LevelIndex);
						if(Level->GetDefaultBrush() == Brush)
						{
							bIsDefaultBrush = true;
							break;
						}
					}

					// Destroy actor if it's a builder brush but not the default brush in any of the currently loaded levels.
					if( !bIsDefaultBrush )
					{
						World->DestroyActor( Brush );

						// Since the actor has been destroyed, skip the rest of this iteration of the loop.
						continue;
					}
				}
			}
		}
		
		// If the actor was imported . . .
		if( bActorChanged )
		{
			// Let the actor deal with having been imported, if desired.
			Actor->PostEditImport();

			// Notify actor its properties have changed.
			Actor->PostEditChange();
		}
	}

	// Pass 2: Sort out any attachment parenting on the new actors now that all actors have the correct properties set
	for( auto It = MapActors.CreateIterator(); It; ++It )
	{
		AActor* const Actor = It.Key();

		// Fixup parenting
		FAttachmentDetail* ActorAttachmentDetail = NewActorsAttachmentMap.Find( Actor );
		if( ActorAttachmentDetail != nullptr )
		{
			AActor* ActorParent = nullptr;
			// Try to find the new copy of the parent
			AActor** NewActorParent = NewActorsFNames.Find( ActorAttachmentDetail->ParentName );
			if ( NewActorParent != nullptr )
			{
				ActorParent = *NewActorParent;
			}
			// Try to find an already existing parent
			if( ActorParent == nullptr )
			{
				ActorParent = FindObject<AActor>( World->GetCurrentLevel(), *ActorAttachmentDetail->ParentName.ToString() );
			}
			// Parent the actors
			if( GWorld == World && ActorParent != nullptr )
			{
				// Make sure our parent isn't selected (would cause GEditor->ParentActors to fail)
				const bool bParentWasSelected = ActorParent->IsSelected();
				if(bParentWasSelected)
				{
					GEditor->SelectActor(ActorParent, false, false, true);
				}

				GEditor->ParentActors( ActorParent, Actor, ActorAttachmentDetail->SocketName );

				if(bParentWasSelected)
				{
					GEditor->SelectActor(ActorParent, true, false, true);
				}
			}
		}
	}

	// Go through all the groups we added and finalize them.
	for(TMap< FString, AGroupActor* >::TIterator It(NewGroups); It; ++It)
	{
		It.Value()->CenterGroupLocation();
		It.Value()->Lock();
	}

	// Mark us as no longer importing a T3D.
	if (GWorld == World)
	{
		GEditor->IsImportingT3D = 0;
		GIsImportingT3D = false;

		GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, World );
	}

	return World;
}

/*-----------------------------------------------------------------------------
	UPackageFactory.
-----------------------------------------------------------------------------*/
UPackageFactory::UPackageFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UPackage::StaticClass();
	Formats.Add(TEXT("T3DPKG;Unreal Package"));

	bCreateNew = false;
	bText = true;
	bEditorImport = false;
}

UObject* UPackageFactory::FactoryCreateText( UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, const TCHAR* Type, const TCHAR*& Buffer, const TCHAR* BufferEnd, FFeedbackContext* Warn )
{
	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPreImport(this, Class, InParent, Name, Type);

	bool bSavedImportingT3D = GIsImportingT3D;
	// Mark us as importing a T3D.
	GEditor->IsImportingT3D = true;
	GIsImportingT3D = true;

	if (InParent != nullptr)
	{
		return nullptr;
	}

	TMap<FString, UPackage*> MapPackages;
	bool bImportingMapPackage = false;

	UPackage* TopLevelPackage = nullptr;
	UPackage* RootMapPackage = nullptr;
	UWorld* World = GWorld;
	if (World)
	{
		RootMapPackage = World->GetOutermost();
	}

	if (RootMapPackage)
	{
		if (RootMapPackage->GetName() == Name.ToString())
		{
			// Loading into the Map package!
			MapPackages.Add(RootMapPackage->GetName(), RootMapPackage);
			TopLevelPackage = RootMapPackage;
			bImportingMapPackage = true;
		}
	}

	// Unselect all actors.
	GEditor->SelectNone( false, false );

	// Mark us importing a T3D (only from a file, not from copy/paste).
	GEditor->IsImportingT3D = FCString::Stricmp(Type,TEXT("paste")) != 0;
	GIsImportingT3D = GEditor->IsImportingT3D;

	// Maintain a list of a new package objects and the text they were created from.
	TMap<UObject*,FString> NewPackageObjectMap;

	FString StrLine;
	while( FParse::Line(&Buffer,StrLine) )
	{
		const TCHAR* Str = *StrLine;

		if (GetBEGIN(&Str, TEXT("TOPLEVELPACKAGE")) && !bImportingMapPackage)
		{
			//Begin TopLevelPackage Class=Package Name=ExportTest_ORIG Archetype=Package'Core.Default__Package'
			UClass* TempClass;
			if( ParseObject<UClass>( Str, TEXT("CLASS="), TempClass, ANY_PACKAGE ) )
			{
				// Get actor name.
				FName PackageName(NAME_None);
				FParse::Value( Str, TEXT("NAME="), PackageName );

				if (FindObject<UPackage>(ANY_PACKAGE, *(PackageName.ToString())))
				{
					UE_LOG(LogEditorFactories, Warning, TEXT("Package factory can only handle the map package or new packages!"));
					return nullptr;
				}
				TopLevelPackage = CreatePackage(nullptr, *(PackageName.ToString()));
				TopLevelPackage->SetFlags(RF_Standalone|RF_Public);
				MapPackages.Add(TopLevelPackage->GetName(), TopLevelPackage);

				// if an archetype was specified in the Begin Object block, use that as the template for the ConstructObject call.
				FString ArchetypeName;
				AActor* Archetype = nullptr;
				if (FParse::Value(Str, TEXT("Archetype="), ArchetypeName))
				{
				}
			}
		}
		else if (GetBEGIN(&Str,TEXT("PACKAGE")))
		{
			FString ParentPackageName;
			FParse::Value(Str, TEXT("PARENTPACKAGE="), ParentPackageName);
			UClass* PkgClass;
			if (ParseObject<UClass>(Str, TEXT("CLASS="), PkgClass, ANY_PACKAGE))
			{
				// Get the name of the object.
				FName NewPackageName(NAME_None);
				FParse::Value(Str, TEXT("NAME="), NewPackageName);

				// if an archetype was specified in the Begin Object block, use that as the template for the ConstructObject call.
				FString ArchetypeName;
				UPackage* Archetype = nullptr;
				if (FParse::Value(Str, TEXT("Archetype="), ArchetypeName))
				{
					// if given a name, break it up along the ' so separate the class from the name
					FString ObjectClass;
					FString ObjectPath;
					if ( FPackageName::ParseExportTextPath(ArchetypeName, &ObjectClass, &ObjectPath) )
					{
						// find the class
						UClass* ArchetypeClass = (UClass*)StaticFindObject(UClass::StaticClass(), ANY_PACKAGE, *ObjectClass);
						if (ArchetypeClass)
						{
							if (ArchetypeClass->IsChildOf(UPackage::StaticClass()))
							{
								// if we had the class, find the archetype
								Archetype = Cast<UPackage>(StaticFindObject(ArchetypeClass, ANY_PACKAGE, *ObjectPath));
							}
							else
							{
								Warn->Logf(ELogVerbosity::Warning, TEXT("Invalid archetype specified in subobject definition '%s': %s is not a child of Package"),
									Str, *ObjectClass);
							}
						}
					}

					UPackage* ParentPkg = nullptr;
					UPackage** ppParentPkg = MapPackages.Find(ParentPackageName);
					if (ppParentPkg)
					{
						ParentPkg = *ppParentPkg;
					}
					check(ParentPkg);

					auto NewPackage = NewObject<UPackage>(ParentPkg, NewPackageName, RF_NoFlags, Archetype);
					check(NewPackage);
					NewPackage->SetFlags(RF_Standalone|RF_Public);
					MapPackages.Add(NewPackageName.ToString(), NewPackage);
				}
			}
		}
	}

	for (FObjectIterator ObjIt; ObjIt; ++ObjIt)
	{
		UObject* LoadObject = *ObjIt;

		if (LoadObject)
		{
			bool bModifiedObject = false;

			FString* PropText = NewPackageObjectMap.Find(LoadObject);
			if (PropText)
			{
				LoadObject->PreEditChange(nullptr);
				ImportObjectProperties((uint8*)LoadObject, **PropText, LoadObject->GetClass(), LoadObject, LoadObject, Warn, 0 );
				bModifiedObject = true;
			}

			if (bModifiedObject)
			{
				// Let the actor deal with having been imported, if desired.
				LoadObject->PostEditImport();
				// Notify actor its properties have changed.
				LoadObject->PostEditChange();
				LoadObject->SetFlags(RF_Standalone | RF_Public);
				LoadObject->MarkPackageDirty();
			}
		}
	}

	// Mark us as no longer importing a T3D.
	GEditor->IsImportingT3D = bSavedImportingT3D;
	GIsImportingT3D = bSavedImportingT3D;

	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, TopLevelPackage );

	return TopLevelPackage;
}

/*-----------------------------------------------------------------------------
	UPolysFactory.
-----------------------------------------------------------------------------*/

UPolysFactory::UPolysFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	SupportedClass = UPolys::StaticClass();
	Formats.Add(TEXT("t3d;Unreal brush text"));
	bCreateNew = false;
	bText = true;
}

UObject* UPolysFactory::FactoryCreateText
(
	UClass*				Class,
	UObject*			InParent,
	FName				Name,
	EObjectFlags		Flags,
	UObject*			Context,
	const TCHAR*		Type,
	const TCHAR*&		Buffer,
	const TCHAR*		BufferEnd,
	FFeedbackContext*	Warn
)
{
	FVector PointPool[4096];
	int32 NumPoints = 0;

	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPreImport(this, Class, InParent, Name, Type);

	// Create polys.	
	UPolys* Polys = Context ? CastChecked<UPolys>(Context) : NewObject<UPolys>(InParent, Name, Flags);

	// Eat up if present.
	GetBEGIN( &Buffer, TEXT("POLYLIST") );

	// Parse all stuff.
	int32 First=1, GotBase=0;
	FString StrLine, ExtraLine;
	FPoly Poly;
	while( FParse::Line( &Buffer, StrLine ) )
	{
		const TCHAR* Str = *StrLine;
		if( GetEND(&Str,TEXT("POLYLIST")) )
		{
			// End of brush polys.
			break;
		}
		//
		//
		// AutoCad - DXF File
		//
		//
		else if( FCString::Strstr(Str,TEXT("ENTITIES")) && First )
		{
			UE_LOG(LogEditorFactories, Log, TEXT("Reading Autocad DXF file"));
			int32 Started=0, IsFace=0;
			FPoly NewPoly; NewPoly.Init();
			NumPoints = 0;

			while
			(	FParse::Line( &Buffer, StrLine, 1 )
			&&	FParse::Line( &Buffer, ExtraLine, 1 ) )
			{
				// Handle the line.
				Str = *ExtraLine;
				int32 Code = FCString::Atoi(*StrLine);
				if( Code==0 )
				{
					// Finish up current poly.
					if( Started )
					{
						if( NewPoly.Vertices.Num() == 0 )
						{
							// Got a vertex definition.
							NumPoints++;
						}
						else if( NewPoly.Vertices.Num()>=3 )
						{
							// Got a poly definition.
							if( IsFace ) NewPoly.Reverse();
							NewPoly.Base = NewPoly.Vertices[0];
							NewPoly.Finalize(nullptr,0);
							new(Polys->Element)FPoly( NewPoly );
						}
						else
						{
							// Bad.
							Warn->Logf( TEXT("DXF: Bad vertex count %i"), NewPoly.Vertices.Num() );
						}
						
						// Prepare for next.
						NewPoly.Init();
					}
					Started=0;

					if( FParse::Command(&Str,TEXT("VERTEX")) )
					{
						// Start of new vertex.
						PointPool[NumPoints] = FVector::ZeroVector;
						Started = 1;
						IsFace  = 0;
					}
					else if( FParse::Command(&Str,TEXT("3DFACE")) )
					{
						// Start of 3d face definition.
						Started = 1;
						IsFace  = 1;
					}
					else if( FParse::Command(&Str,TEXT("SEQEND")) )
					{
						// End of sequence.
						NumPoints=0;
					}
					else if( FParse::Command(&Str,TEXT("EOF")) )
					{
						// End of file.
						break;
					}
				}
				else if( Started )
				{
					// Replace commas with periods to handle european dxf's.
					//for( TCHAR* Stupid = FCString::Strchr(*ExtraLine,','); Stupid; Stupid=FCString::Strchr(Stupid,',') )
					//	*Stupid = '.';

					// Handle codes.
					if( Code>=10 && Code<=19 )
					{
						// X coordinate.
						int32 VertexIndex = Code-10;
						if( IsFace && VertexIndex >= NewPoly.Vertices.Num() )
						{
							NewPoly.Vertices.AddZeroed(VertexIndex - NewPoly.Vertices.Num() + 1);
						}
						NewPoly.Vertices[VertexIndex].X = PointPool[NumPoints].X = FCString::Atof(*ExtraLine);
					}
					else if( Code>=20 && Code<=29 )
					{
						// Y coordinate.
						int32 VertexIndex = Code-20;
						NewPoly.Vertices[VertexIndex].Y = PointPool[NumPoints].Y = FCString::Atof(*ExtraLine);
					}
					else if( Code>=30 && Code<=39 )
					{
						// Z coordinate.
						int32 VertexIndex = Code-30;
						NewPoly.Vertices[VertexIndex].Z = PointPool[NumPoints].Z = FCString::Atof(*ExtraLine);
					}
					else if( Code>=71 && Code<=79 && (Code-71)==NewPoly.Vertices.Num() )
					{
						int32 iPoint = FMath::Abs(FCString::Atoi(*ExtraLine));
						if( iPoint>0 && iPoint<=NumPoints )
							new(NewPoly.Vertices) FVector(PointPool[iPoint-1]);
						else UE_LOG(LogEditorFactories, Warning, TEXT("DXF: Invalid point index %i/%i"), iPoint, NumPoints );
					}
				}
			}
		}
		//
		//
		// 3D Studio MAX - ASC File
		//
		//
		else if( FCString::Strstr(Str,TEXT("Tri-mesh,")) && First )
		{
			UE_LOG(LogEditorFactories, Log,  TEXT("Reading 3D Studio ASC file") );
			NumPoints = 0;

			AscReloop:
			int32 TempNumPolys=0, TempVerts=0;
			while( FParse::Line( &Buffer, StrLine ) )
			{
				Str = *StrLine;

				FString VertText = FString::Printf( TEXT("Vertex %i:"), NumPoints );
				FString FaceText = FString::Printf( TEXT("Face %i:"), TempNumPolys );
				if( FCString::Strstr(Str,*VertText) )
				{
					PointPool[NumPoints].X = FCString::Atof(FCString::Strstr(Str,TEXT("X:"))+2);
					PointPool[NumPoints].Y = FCString::Atof(FCString::Strstr(Str,TEXT("Y:"))+2);
					PointPool[NumPoints].Z = FCString::Atof(FCString::Strstr(Str,TEXT("Z:"))+2);
					NumPoints++;
					TempVerts++;
				}
				else if( FCString::Strstr(Str,*FaceText) )
				{
					Poly.Init();
					new(Poly.Vertices)FVector(PointPool[FCString::Atoi(FCString::Strstr(Str,TEXT("A:"))+2)]);
					new(Poly.Vertices)FVector(PointPool[FCString::Atoi(FCString::Strstr(Str,TEXT("B:"))+2)]);
					new(Poly.Vertices)FVector(PointPool[FCString::Atoi(FCString::Strstr(Str,TEXT("C:"))+2)]);
					Poly.Base = Poly.Vertices[0];
					Poly.Finalize(nullptr,0);
					new(Polys->Element)FPoly(Poly);
					TempNumPolys++;
				}
				else if( FCString::Strstr(Str,TEXT("Tri-mesh,")) )
					goto AscReloop;
			}
			UE_LOG(LogEditorFactories, Log,  TEXT("Imported %i vertices, %i faces"), TempVerts, Polys->Element.Num() );
		}
		//
		//
		// T3D FORMAT
		//
		//
		else if( GetBEGIN(&Str,TEXT("POLYGON")) )
		{
			// Init to defaults and get group/item and texture.
			Poly.Init();
			FParse::Value( Str, TEXT("LINK="), Poly.iLink );
			FParse::Value( Str, TEXT("ITEM="), Poly.ItemName );
			FParse::Value( Str, TEXT("FLAGS="), Poly.PolyFlags );
			FParse::Value( Str, TEXT("LightMapScale="), Poly.LightMapScale );
			Poly.PolyFlags &= ~PF_NoImport;

			FString TextureName;
			// only load the texture if it was present
			if (FParse::Value( Str, TEXT("TEXTURE="), TextureName ))
			{
				Poly.Material = Cast<UMaterialInterface>(StaticFindObject( UMaterialInterface::StaticClass(), ANY_PACKAGE, *TextureName ) );
/***
				if (Poly.Material == nullptr)
				{
					Poly.Material = Cast<UMaterialInterface>(StaticLoadObject( UMaterialInterface::StaticClass(), nullptr, *TextureName, nullptr,  LOAD_NoWarn, nullptr ) );
				}
***/
			}
		}
		else if( FParse::Command(&Str,TEXT("PAN")) )
		{
			int32	PanU = 0,
				PanV = 0;

			FParse::Value( Str, TEXT("U="), PanU );
			FParse::Value( Str, TEXT("V="), PanV );

			Poly.Base += Poly.TextureU * PanU;
			Poly.Base += Poly.TextureV * PanV;
		}
		else if( FParse::Command(&Str,TEXT("ORIGIN")) )
		{
			GotBase=1;
			GetFVECTOR( Str, Poly.Base );
		}
		else if( FParse::Command(&Str,TEXT("VERTEX")) )
		{
			FVector TempVertex;
			GetFVECTOR( Str, TempVertex );
			new(Poly.Vertices) FVector(TempVertex);
		}
		else if( FParse::Command(&Str,TEXT("TEXTUREU")) )
		{
			GetFVECTOR( Str, Poly.TextureU );
		}
		else if( FParse::Command(&Str,TEXT("TEXTUREV")) )
		{
			GetFVECTOR( Str, Poly.TextureV );
		}
		else if( GetEND(&Str,TEXT("POLYGON")) )
		{
			if( !GotBase )
				Poly.Base = Poly.Vertices[0];
			if( Poly.Finalize(nullptr,1)==0 )
				new(Polys->Element)FPoly(Poly);
			GotBase=0;
		}
	}

	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, Polys );

	// Success.
	return Polys;
}

/*-----------------------------------------------------------------------------
	UModelFactory.
-----------------------------------------------------------------------------*/
UModelFactory::UModelFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UModel::StaticClass();
	Formats.Add(TEXT("t3d;Unreal model text"));
	bCreateNew = false;
	bText = true;
}

UObject* UModelFactory::FactoryCreateText
(
	UClass*				Class,
	UObject*			InParent,
	FName				Name,
	EObjectFlags		Flags,
	UObject*			Context,
	const TCHAR*		Type,
	const TCHAR*&		Buffer,
	const TCHAR*		BufferEnd,
	FFeedbackContext*	Warn
)
{
	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPreImport(this, Class, InParent, Name, Type);

	ABrush* TempOwner = (ABrush*)Context;
	UModel* Model = NewObject<UModel>(InParent, Name, Flags);
	Model->Initialize(TempOwner, true);

	const TCHAR* StrPtr;
	FString StrLine;
	if( TempOwner )
	{
		TempOwner->InitPosRotScale();
		GEditor->GetSelectedActors()->Deselect( TempOwner );
	}
	while( FParse::Line( &Buffer, StrLine ) )
	{
		StrPtr = *StrLine;
		if( GetEND(&StrPtr,TEXT("BRUSH")) )
		{
			break;
		}
		else if( GetBEGIN (&StrPtr,TEXT("POLYLIST")) )
		{
			UPolysFactory* PolysFactory = NewObject<UPolysFactory>();
			Model->Polys = (UPolys*)PolysFactory->FactoryCreateText(UPolys::StaticClass(),Model,NAME_None,RF_Transactional,nullptr,Type,Buffer,BufferEnd,Warn);
			check(Model->Polys);
		}
		if( TempOwner )
		{
			if(FParse::Command(&StrPtr,TEXT("PREPIVOT"	))) 
			{
				FVector TempPrePivot(0.f);
				GetFVECTOR 	(StrPtr,TempPrePivot);
				TempOwner->SetPivotOffset(TempPrePivot);
			}
			else if (FParse::Command(&StrPtr,TEXT("LOCATION"	))) 
			{
				FVector NewLocation(0.f);
				GetFVECTOR	(StrPtr,NewLocation);
				TempOwner->SetActorLocation(NewLocation, false);
			}
			else if (FParse::Command(&StrPtr,TEXT("ROTATION"	))) 
			{
				FRotator NewRotation;
				GetFROTATOR  (StrPtr,NewRotation,1);
				TempOwner->SetActorRotation(NewRotation);
			}
			if( FParse::Command(&StrPtr,TEXT("SETTINGS")) )
			{
				uint8 BrushType = (uint8)TempOwner->BrushType;
				FParse::Value( StrPtr, TEXT("BRUSHTYPE="), BrushType);
				TempOwner->BrushType = EBrushType(BrushType);
				FParse::Value( StrPtr, TEXT("POLYFLAGS="), TempOwner->PolyFlags );
			}
		}
	}

	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, Model );

	return Model;
}

/*------------------------------------------------------------------------------
	UParticleSystemFactoryNew.
------------------------------------------------------------------------------*/
UParticleSystemFactoryNew::UParticleSystemFactoryNew(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	SupportedClass = UParticleSystem::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UParticleSystemFactoryNew::FactoryCreateNew(UClass* Class,UObject* InParent,FName Name,EObjectFlags Flags,UObject* Context,FFeedbackContext* Warn)
{
	return NewObject<UObject>(InParent, Class, Name, Flags);
}

USubUVAnimationFactory::USubUVAnimationFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = USubUVAnimation::StaticClass();
}

UObject* USubUVAnimationFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	USubUVAnimation* NewAsset = NewObject<USubUVAnimation>(InParent, Class, Name, Flags | RF_Transactional);

	if ( InitialTexture != nullptr )
	{
		//@todo - auto-detect SubImages_Horizontal and SubImages_Vertical from texture contents
		NewAsset->SubUVTexture = InitialTexture;
		NewAsset->PostEditChange();
	}

	return NewAsset;
}

uint32 USubUVAnimationFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Misc;
}

/*------------------------------------------------------------------------------
	UPhysicalMaterialFactoryNew.
------------------------------------------------------------------------------*/
UPhysicalMaterialFactoryNew::UPhysicalMaterialFactoryNew(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	SupportedClass = UPhysicalMaterial::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

bool UPhysicalMaterialFactoryNew::ConfigureProperties()
{
	// nullptr the DataAssetClass so we can check for selection
	PhysicalMaterialClass = nullptr;

	// Load the classviewer module to display a class picker
	FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");

	// Fill in options
	FClassViewerInitializationOptions Options;
	Options.Mode = EClassViewerMode::ClassPicker;

	TSharedPtr<FAssetClassParentFilter> Filter = MakeShareable(new FAssetClassParentFilter);
	Options.ClassFilter = Filter;

	Filter->DisallowedClassFlags = CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists;
	Filter->AllowedChildrenOfClasses.Add(UPhysicalMaterial::StaticClass());

	const FText TitleText = LOCTEXT("CreatePhysicalMaterial", "Pick Physical Material Class");
	UClass* ChosenClass = nullptr;
	const bool bPressedOk = SClassPickerDialog::PickClass(TitleText, Options, ChosenClass, UPhysicalMaterial::StaticClass());

	if (bPressedOk)
	{
		PhysicalMaterialClass = ChosenClass;
	}

	return bPressedOk;
}
UObject* UPhysicalMaterialFactoryNew::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	if (PhysicalMaterialClass != nullptr)
	{
		return NewObject<UPhysicalMaterial>(InParent, PhysicalMaterialClass, Name, Flags | RF_Transactional);
	}
	else
	{
		// if we have no data asset class, use the passed-in class instead
		check(Class->IsChildOf(UPhysicalMaterial::StaticClass()));
		return NewObject<UPhysicalMaterial>(InParent, Class, Name, Flags);
	}
}

/*------------------------------------------------------------------------------
	UInterpDataFactoryNew.
------------------------------------------------------------------------------*/
UInterpDataFactoryNew::UInterpDataFactoryNew(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	SupportedClass = UInterpData::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UInterpDataFactoryNew::FactoryCreateNew(UClass* Class,UObject* InParent,FName Name,EObjectFlags Flags,UObject* Context,FFeedbackContext* Warn)
{
	return NewObject<UObject>(InParent, Class, Name, Flags);
}

/*-----------------------------------------------------------------------------
	UTextureRenderTargetFactoryNew
-----------------------------------------------------------------------------*/

UTextureRenderTargetFactoryNew::UTextureRenderTargetFactoryNew(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UTextureRenderTarget2D::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
	bEditorImport = false;

	Width = 256;
	Height = 256;
	Format = 0;
}


UObject* UTextureRenderTargetFactoryNew::FactoryCreateNew(UClass* Class,UObject* InParent,FName Name,EObjectFlags Flags,UObject* Context,FFeedbackContext* Warn)
{
	// create the new object
	UTextureRenderTarget2D* Result = NewObject<UTextureRenderTarget2D>(InParent, Class, Name, Flags);
	// initialize the resource
	Result->InitAutoFormat( Width, Height );
	return( Result );
}

/*-----------------------------------------------------------------------------
	UCanvasRenderTargetFactoryNew
-----------------------------------------------------------------------------*/

UCanvasRenderTarget2DFactoryNew::UCanvasRenderTarget2DFactoryNew(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UCanvasRenderTarget2D::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
	bEditorImport = false;

	Width = 256;
	Height = 256;
	Format = 0;
}


UObject* UCanvasRenderTarget2DFactoryNew::FactoryCreateNew(UClass* Class,UObject* InParent,FName Name,EObjectFlags Flags,UObject* Context,FFeedbackContext* Warn)
{
	// create the new object
	UCanvasRenderTarget2D* Result = NewObject<UCanvasRenderTarget2D>(InParent, Class, Name, Flags);
	check(Result);
	// initialize the resource
	Result->InitAutoFormat( Width, Height );
	return( Result );
}

/*-----------------------------------------------------------------------------
UCurveLinearColorAtlasFactory
-----------------------------------------------------------------------------*/

UCurveLinearColorAtlasFactory::UCurveLinearColorAtlasFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UCurveLinearColorAtlas::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
	bEditorImport = false;

	Width = 256;
	Height = 256;
	Format = 0;
}

FText UCurveLinearColorAtlasFactory::GetDisplayName() const
{
	return LOCTEXT("CurveAtlas", "Curve Atlas");
}

uint32 UCurveLinearColorAtlasFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Misc;
}

UObject* UCurveLinearColorAtlasFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	// Do not create a texture with bad dimensions.
	if ((Width & (Width - 1)) || (Height & (Height - 1)))
	{
		return nullptr;
	}

	UCurveLinearColorAtlas* Object = NewObject<UCurveLinearColorAtlas>(InParent, Class, Name, Flags);
	Object->Source.Init(Width, Height, 1, 1, TSF_RGBA16F);
	const int32 TextureDataSize = Object->Source.CalcMipSize(0);
	Object->SrcData.AddUninitialized(TextureDataSize);
	uint32* TextureData = (uint32*)Object->Source.LockMip(0);
	for (uint32 y = 0; y < Object->TextureSize; y++)
	{
		// Create base mip for the texture we created.
		for (uint32 x = 0; x < Object->TextureSize; x++)
		{
			Object->SrcData[x*Object->TextureSize + y] = FLinearColor::White;
		}
	}
	FMemory::Memcpy(TextureData, Object->SrcData.GetData(), TextureDataSize);
	Object->Source.UnlockMip(0);

	Object->UpdateResource();
	return Object;
}


/*-----------------------------------------------------------------------------
	UTextureRenderTargetCubeFactoryNew
-----------------------------------------------------------------------------*/
UTextureRenderTargetCubeFactoryNew::UTextureRenderTargetCubeFactoryNew(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	SupportedClass = UTextureRenderTargetCube::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
	bEditorImport = false;

	Width = 256;
	Format = 0;
}

UObject* UTextureRenderTargetCubeFactoryNew::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	// create the new object
	UTextureRenderTargetCube* Result = NewObject<UTextureRenderTargetCube>(InParent, Class, Name, Flags);

	// initialize the resource
	Result->InitAutoFormat(Width);

	return (Result);
}


/*-----------------------------------------------------------------------------
	UTextureFactory.
-----------------------------------------------------------------------------*/

// .PCX file header.
#pragma pack(push,1)
class FPCXFileHeader
{
public:
	uint8	Manufacturer;		// Always 10.
	uint8	Version;			// PCX file version.
	uint8	Encoding;			// 1=run-length, 0=none.
	uint8	BitsPerPixel;		// 1,2,4, or 8.
	uint16	XMin;				// Dimensions of the image.
	uint16	YMin;				// Dimensions of the image.
	uint16	XMax;				// Dimensions of the image.
	uint16	YMax;				// Dimensions of the image.
	uint16	XDotsPerInch;		// Horizontal printer resolution.
	uint16	YDotsPerInch;		// Vertical printer resolution.
	uint8	OldColorMap[48];	// Old colormap info data.
	uint8	Reserved1;			// Must be 0.
	uint8	NumPlanes;			// Number of color planes (1, 3, 4, etc).
	uint16	BytesPerLine;		// Number of bytes per scanline.
	uint16	PaletteType;		// How to interpret palette: 1=color, 2=gray.
	uint16	HScreenSize;		// Horizontal monitor size.
	uint16	VScreenSize;		// Vertical monitor size.
	uint8	Reserved2[54];		// Must be 0.
	friend FArchive& operator<<( FArchive& Ar, FPCXFileHeader& H )
	{
		Ar << H.Manufacturer << H.Version << H.Encoding << H.BitsPerPixel;
		Ar << H.XMin << H.YMin << H.XMax << H.YMax << H.XDotsPerInch << H.YDotsPerInch;
		for( int32 i=0; i<ARRAY_COUNT(H.OldColorMap); i++ )
			Ar << H.OldColorMap[i];
		Ar << H.Reserved1 << H.NumPlanes;
		Ar << H.BytesPerLine << H.PaletteType << H.HScreenSize << H.VScreenSize;
		for( int32 i=0; i<ARRAY_COUNT(H.Reserved2); i++ )
			Ar << H.Reserved2[i];
		return Ar;
	}
};

struct FTGAFileFooter
{
	uint32 ExtensionAreaOffset;
	uint32 DeveloperDirectoryOffset;
	uint8 Signature[16];
	uint8 TrailingPeriod;
	uint8 NullTerminator;
};

struct FPSDFileHeader
{                                                           
	int32     Signature;      // 8BPS
	int16   Version;        // Version
	int16   nChannels;      // Number of Channels (3=RGB) (4=RGBA)
	int32     Height;         // Number of Image Rows
	int32     Width;          // Number of Image Columns
	int16   Depth;          // Number of Bits per Channel
	int16   Mode;           // Image Mode (0=Bitmap)(1=Grayscale)(2=Indexed)(3=RGB)(4=CYMK)(7=Multichannel)
	uint8    Pad[6];         // Padding

	/**
	 * @return Whether file has a valid signature
	 */
	bool IsValid( void )
	{
		// Fail on bad signature
		if (Signature != 0x38425053)
			return false;

		return true;
	}

	/**
	 * @return Whether file has a supported version
	 */
	bool IsSupported( void )
	{
		// Fail on bad version
		if( Version != 1 )
			return false;   
		// Fail on anything other than 1, 3 or 4 channels
		if ((nChannels!=1) && (nChannels!=3) && (nChannels!=4))
			return false;
		// Fail on anything other than 8 Bits/channel or 16 Bits/channel  
		if ((Depth != 8) && (Depth != 16))
			return false;
		// Fail on anything other than Grayscale and RGB
		// We can add support for indexed later if needed.
		if (Mode!=1 && Mode!=3)
			return false;

		return true;
	}
};

#pragma pack(pop)


static bool psd_ReadData( uint8* pOut, const uint8*& pBuffer, FPSDFileHeader& Info )
{
	const uint8* pPlane = nullptr;
	const uint8* pRowTable = nullptr;
	int32         iPlane;
	int16       CompressionType;
	int32         iPixel;
	int32         iRow;
	int32         CompressedBytes;
	int32         iByte;
	int32         Count;
	uint8        Value;

	// Double check to make sure this is a valid request
	if (!Info.IsValid() || !Info.IsSupported())
	{
		return false;
	}

	const uint8* pCur = pBuffer + sizeof(FPSDFileHeader);
	int32         NPixels = Info.Width * Info.Height;

	int32  ClutSize =  ((int32)pCur[ 0] << 24) +
		((int32)pCur[ 1] << 16) +
		((int32)pCur[ 2] <<  8) +
		((int32)pCur[ 3] <<  0);
	pCur+=4;
	pCur += ClutSize;    

	// Skip Image Resource Section
	int32 ImageResourceSize = ((int32)pCur[ 0] << 24) +
		((int32)pCur[ 1] << 16) +
		((int32)pCur[ 2] <<  8) +
		((int32)pCur[ 3] <<  0);
	pCur += 4+ImageResourceSize;

	// Skip Layer and Mask Section
	int32 LayerAndMaskSize =  ((int32)pCur[ 0] << 24) +
		((int32)pCur[ 1] << 16) +
		((int32)pCur[ 2] <<  8) +
		((int32)pCur[ 3] <<  0);
	pCur += 4+LayerAndMaskSize;

	// Determine number of bytes per pixel
	int32 BytesPerPixel = 3;
	const int32 BytesPerChannel = (Info.Depth / 8);
	switch( Info.Mode )
	{
	case 1: // 'GrayScale'
		BytesPerPixel = BytesPerChannel;
		break;
	case 2:        
		BytesPerPixel = 1;        
		return false;  // until we support indexed...
		break;
	case 3: // 'RGBColor'
		if (Info.nChannels == 3)
			BytesPerPixel = 3 * BytesPerChannel;
		else                   
			BytesPerPixel = 4 * BytesPerChannel;       
		break;
	default:
		return false;
		break;
	}

	// Get Compression Type
	CompressionType = ((int32)pCur[0] <<  8) + ((int32)pCur[1] <<  0);    
	pCur += 2;

	// Fail on 16 Bits/channel with RLE. This can occur when the file is not saved with 'Maximize Compatibility'. Compression doesn't appear to be standard.
	if(CompressionType == 1 && Info.Depth == 16)
	{
		return false;
	}

	// If no alpha channel, set alpha to opaque (255 or 65536).
	if( Info.nChannels != 4)
	{
		if(Info.Depth == 8)
		{
			const uint32 Channels = 4;
			const uint32 BufferSize = Info.Width * Info.Height * Channels * sizeof(uint8);
			FMemory::Memset(pOut, 0xff, BufferSize);
		}
		else if(Info.Depth == 16)
		{
			const uint32 Channels = 4;
			const uint32 BufferSize = Info.Width * Info.Height * Channels * sizeof(uint16);
			FMemory::Memset(pOut, 0xff, BufferSize);
		}
	}

	// Uncompressed?
	if( CompressionType == 0 )
	{
		if(Info.Depth == 8)
		{
			FColor* Dest = (FColor*)pOut;
			for(int32 Pixel=0; Pixel < NPixels; Pixel++ )
			{
				if (Info.nChannels == 1)
				{
					Dest[Pixel].R = pCur[NPixels + Pixel];
					Dest[Pixel].G = pCur[NPixels + Pixel];
					Dest[Pixel].B = pCur[NPixels + Pixel];
				}
				else
				{
					Dest[Pixel].R = pCur[NPixels * 0 + Pixel];
					Dest[Pixel].G = pCur[NPixels * 1 + Pixel];
					Dest[Pixel].B = pCur[NPixels * 2 + Pixel];
					if (Info.nChannels == 4)
					{
						Dest[Pixel].A = pCur[NPixels * 3 + Pixel];
					}
				}
			}
		}
		else if (Info.Depth == 16)
		{
			uint32 SrcOffset = 0;
			
			if (Info.nChannels == 1)
			{
				uint16* Dest = (uint16*)pOut;
				uint32 ChannelOffset = 0;

				for (int32 Pixel = 0; Pixel < NPixels; Pixel++)
				{
					Dest[ChannelOffset + 0] = ((pCur[SrcOffset] << 8) + (pCur[SrcOffset + 1] << 0));
					Dest[ChannelOffset + 1] = ((pCur[SrcOffset] << 8) + (pCur[SrcOffset + 1] << 0));
					Dest[ChannelOffset + 2] = ((pCur[SrcOffset] << 8) + (pCur[SrcOffset + 1] << 0));

					//Increment offsets
					ChannelOffset += 4;
					SrcOffset += BytesPerChannel;
				}
			}
			else
			{
				// Loop through the planes	
				for (iPlane = 0; iPlane < Info.nChannels; iPlane++)
				{
					uint16* Dest = (uint16*)pOut;
					uint32 ChannelOffset = iPlane;

					for (int32 Pixel = 0; Pixel < NPixels; Pixel++)
					{
						Dest[ChannelOffset] = ((pCur[SrcOffset] << 8) + (pCur[SrcOffset + 1] << 0));

						//Increment offsets
						ChannelOffset += 4;
						SrcOffset += BytesPerChannel;
					}
				}
			}
		}
	}
	// RLE?
	else if( CompressionType == 1 )
	{
		// Setup RowTable
		pRowTable = pCur;
		pCur += Info.nChannels*Info.Height*2;

		FColor* Dest = (FColor*)pOut;

		// Loop through the planes
		for( iPlane=0 ; iPlane<Info.nChannels ; iPlane++ )
		{
			int32 iWritePlane = iPlane;
			if( iWritePlane > BytesPerPixel-1 ) iWritePlane = BytesPerPixel-1;

			// Loop through the rows
			for( iRow=0 ; iRow<Info.Height ; iRow++ )
			{
				// Load a row
				CompressedBytes = (pRowTable[(iPlane*Info.Height+iRow)*2  ] << 8) +
					(pRowTable[(iPlane*Info.Height+iRow)*2+1] << 0);

				// Setup Plane
				pPlane = pCur;
				pCur += CompressedBytes;

				// Decompress Row
				iPixel = 0;
				iByte = 0;
				while( (iPixel < Info.Width) && (iByte < CompressedBytes) )
				{
					int8 code = (int8)pPlane[iByte++];

					// Is it a repeat?
					if( code < 0 )
					{
						Count = -(int32)code + 1;
						Value = pPlane[iByte++];
						while( Count-- > 0 )
						{
							int32 idx = (iPixel) + (iRow*Info.Width);
							if (Info.nChannels == 1)
							{
								Dest[idx].R = Value;
								Dest[idx].G = Value;
								Dest[idx].B = Value;
							}
							else
							{
								switch (iWritePlane)
								{
								case 0: Dest[idx].R = Value; break;
								case 1: Dest[idx].G = Value; break;
								case 2: Dest[idx].B = Value; break;
								case 3: Dest[idx].A = Value; break;
								}
							}
							iPixel++;
						}
					}
					// Must be a literal then
					else
					{
						Count = (int32)code + 1;
						while( Count-- > 0 )
						{
							Value = pPlane[iByte++];
							int32 idx = (iPixel) + (iRow*Info.Width);

							if (Info.nChannels == 1)
							{
								Dest[idx].R = Value;
								Dest[idx].G = Value;
								Dest[idx].B = Value;
							}
							else
							{
								switch (iWritePlane)
								{
								case 0: Dest[idx].R = Value; break;
								case 1: Dest[idx].G = Value; break;
								case 2: Dest[idx].B = Value; break;
								case 3: Dest[idx].A = Value; break;
								}
							}
							iPixel++;
						}
					}
				}

				// Confirm that we decoded the right number of bytes
				check( iByte  == CompressedBytes );
				check( iPixel == Info.Width );
			}
		}
	}
	else
		return false;

	// Success!
	return( true );
}

static void psd_GetPSDHeader( const uint8* Buffer, FPSDFileHeader& Info )
{
	Info.Signature      =   ((int32)Buffer[ 0] << 24) +
		((int32)Buffer[ 1] << 16) +
		((int32)Buffer[ 2] <<  8) +
		((int32)Buffer[ 3] <<  0);
	Info.Version        =   ((int32)Buffer[ 4] <<  8) +
		((int32)Buffer[ 5] <<  0);
	Info.nChannels      =   ((int32)Buffer[12] <<  8) +
		((int32)Buffer[13] <<  0);
	Info.Height         =   ((int32)Buffer[14] << 24) +
		((int32)Buffer[15] << 16) +
		((int32)Buffer[16] <<  8) +
		((int32)Buffer[17] <<  0);
	Info.Width          =   ((int32)Buffer[18] << 24) +
		((int32)Buffer[19] << 16) +
		((int32)Buffer[20] <<  8) +
		((int32)Buffer[21] <<  0);
	Info.Depth          =   ((int32)Buffer[22] <<  8) +
		((int32)Buffer[23] <<  0);
	Info.Mode           =   ((int32)Buffer[24] <<  8) +
		((int32)Buffer[25] <<  0);
}


void DecompressTGA_RLE_32bpp( const FTGAFileHeader* TGA, uint32* TextureData )
{
	uint8*	IdData		= (uint8*)TGA + sizeof(FTGAFileHeader); 
	uint8*	ColorMap	= IdData + TGA->IdFieldLength;
	uint8*	ImageData	= (uint8*) (ColorMap + (TGA->ColorMapEntrySize + 4) / 8 * TGA->ColorMapLength);				
	uint32	Pixel		= 0;
	int32     RLERun		= 0;
	int32     RAWRun		= 0;

	for(int32 Y = TGA->Height-1; Y >=0; Y--) // Y-flipped.
	{					
		for(int32 X = 0;X < TGA->Width;X++)
		{						
			if( RLERun > 0 )
			{
				RLERun--;  // reuse current Pixel data.
			}
			else if( RAWRun == 0 ) // new raw pixel or RLE-run.
			{
				uint8 RLEChunk = *(ImageData++);							
				if( RLEChunk & 0x80 )
				{
					RLERun = ( RLEChunk & 0x7F ) + 1;
					RAWRun = 1;
				}
				else
				{
					RAWRun = ( RLEChunk & 0x7F ) + 1;
				}
			}							
			// Retrieve new pixel data - raw run or single pixel for RLE stretch.
			if( RAWRun > 0 )
			{
				Pixel = *(uint32*)ImageData; // RGBA 32-bit dword.
				ImageData += 4;
				RAWRun--;
				RLERun--;
			}
			// Store.
			*( (TextureData + Y*TGA->Width)+X ) = Pixel;
		}
	}
}

void DecompressTGA_RLE_24bpp( const FTGAFileHeader* TGA, uint32* TextureData )
{
	uint8*	IdData = (uint8*)TGA + sizeof(FTGAFileHeader); 
	uint8*	ColorMap = IdData + TGA->IdFieldLength;
	uint8*	ImageData = (uint8*) (ColorMap + (TGA->ColorMapEntrySize + 4) / 8 * TGA->ColorMapLength);
	uint8    Pixel[4] = {};
	int32     RLERun = 0;
	int32     RAWRun = 0;

	for(int32 Y = TGA->Height-1; Y >=0; Y--) // Y-flipped.
	{					
		for(int32 X = 0;X < TGA->Width;X++)
		{						
			if( RLERun > 0 )
				RLERun--;  // reuse current Pixel data.
			else if( RAWRun == 0 ) // new raw pixel or RLE-run.
			{
				uint8 RLEChunk = *(ImageData++);
				if( RLEChunk & 0x80 )
				{
					RLERun = ( RLEChunk & 0x7F ) + 1;
					RAWRun = 1;
				}
				else
				{
					RAWRun = ( RLEChunk & 0x7F ) + 1;
				}
			}							
			// Retrieve new pixel data - raw run or single pixel for RLE stretch.
			if( RAWRun > 0 )
			{
				Pixel[0] = *(ImageData++);
				Pixel[1] = *(ImageData++);
				Pixel[2] = *(ImageData++);
				Pixel[3] = 255;
				RAWRun--;
				RLERun--;
			}
			// Store.
			*( (TextureData + Y*TGA->Width)+X ) = *(uint32*)&Pixel;
		}
	}
}

void DecompressTGA_RLE_16bpp( const FTGAFileHeader* TGA, uint32* TextureData )
{
	uint8*	IdData = (uint8*)TGA + sizeof(FTGAFileHeader);
	uint8*	ColorMap = IdData + TGA->IdFieldLength;				
	uint16*	ImageData = (uint16*) (ColorMap + (TGA->ColorMapEntrySize + 4) / 8 * TGA->ColorMapLength);
	uint16  FilePixel = 0;
	uint32	TexturePixel = 0;
	int32     RLERun = 0;
	int32     RAWRun = 0;

	for(int32 Y = TGA->Height-1; Y >=0; Y--) // Y-flipped.
	{					
		for( int32 X=0;X<TGA->Width;X++ )
		{						
			if( RLERun > 0 )
				RLERun--;  // reuse current Pixel data.
			else if( RAWRun == 0 ) // new raw pixel or RLE-run.
			{
				uint8 RLEChunk =  *((uint8*)ImageData);
				ImageData = (uint16*)(((uint8*)ImageData)+1);
				if( RLEChunk & 0x80 )
				{
					RLERun = ( RLEChunk & 0x7F ) + 1;
					RAWRun = 1;
				}
				else
				{
					RAWRun = ( RLEChunk & 0x7F ) + 1;
				}
			}							
			// Retrieve new pixel data - raw run or single pixel for RLE stretch.
			if( RAWRun > 0 )
			{ 
				FilePixel = *(ImageData++);
				RAWRun--;
				RLERun--;
			}
			// Convert file format A1R5G5B5 into pixel format B8G8R8B8
			TexturePixel = (FilePixel & 0x001F) << 3;
			TexturePixel |= (FilePixel & 0x03E0) << 6;
			TexturePixel |= (FilePixel & 0x7C00) << 9;
			TexturePixel |= (FilePixel & 0x8000) << 16;
			// Store.
			*( (TextureData + Y*TGA->Width)+X ) = TexturePixel;
		}
	}
}

void DecompressTGA_32bpp( const FTGAFileHeader* TGA, uint32* TextureData )
{

	uint8*	IdData = (uint8*)TGA + sizeof(FTGAFileHeader);
	uint8*	ColorMap = IdData + TGA->IdFieldLength;
	uint32*	ImageData = (uint32*) (ColorMap + (TGA->ColorMapEntrySize + 4) / 8 * TGA->ColorMapLength);

	for(int32 Y = 0;Y < TGA->Height;Y++)
	{
		FMemory::Memcpy(TextureData + Y * TGA->Width,ImageData + (TGA->Height - Y - 1) * TGA->Width,TGA->Width * 4);
	}
}

void DecompressTGA_16bpp( const FTGAFileHeader* TGA, uint32* TextureData )
{
	uint8*	IdData = (uint8*)TGA + sizeof(FTGAFileHeader);
	uint8*	ColorMap = IdData + TGA->IdFieldLength;
	uint16*	ImageData = (uint16*) (ColorMap + (TGA->ColorMapEntrySize + 4) / 8 * TGA->ColorMapLength);
	uint16    FilePixel = 0;
	uint32	TexturePixel = 0;

	for (int32 Y = TGA->Height - 1; Y >= 0; Y--)
	{					
		for (int32 X = 0; X<TGA->Width; X++)
		{
			FilePixel = *ImageData++;
			// Convert file format A1R5G5B5 into pixel format B8G8R8A8
			TexturePixel = (FilePixel & 0x001F) << 3;
			TexturePixel |= (FilePixel & 0x03E0) << 6;
			TexturePixel |= (FilePixel & 0x7C00) << 9;
			TexturePixel |= (FilePixel & 0x8000) << 16;
			// Store.
			*((TextureData + Y*TGA->Width) + X) = TexturePixel;						
		}
	}
}

void DecompressTGA_24bpp( const FTGAFileHeader* TGA, uint32* TextureData )
{
	uint8*	IdData = (uint8*)TGA + sizeof(FTGAFileHeader);
	uint8*	ColorMap = IdData + TGA->IdFieldLength;
	uint8*	ImageData = (uint8*) (ColorMap + (TGA->ColorMapEntrySize + 4) / 8 * TGA->ColorMapLength);
	uint8    Pixel[4];

	for(int32 Y = 0;Y < TGA->Height;Y++)
	{
		for(int32 X = 0;X < TGA->Width;X++)
		{
			Pixel[0] = *(( ImageData+( TGA->Height-Y-1 )*TGA->Width*3 )+X*3+0);
			Pixel[1] = *(( ImageData+( TGA->Height-Y-1 )*TGA->Width*3 )+X*3+1);
			Pixel[2] = *(( ImageData+( TGA->Height-Y-1 )*TGA->Width*3 )+X*3+2);
			Pixel[3] = 255;
			*((TextureData+Y*TGA->Width)+X) = *(uint32*)&Pixel;
		}
	}
}

void DecompressTGA_8bpp( const FTGAFileHeader* TGA, uint8* TextureData )
{
	const uint8*  const IdData = (uint8*)TGA + sizeof(FTGAFileHeader);
	const uint8*  const ColorMap = IdData + TGA->IdFieldLength;
	const uint8*  const ImageData = (uint8*) (ColorMap + (TGA->ColorMapEntrySize + 4) / 8 * TGA->ColorMapLength);

	int32 RevY = 0;
	for (int32 Y = TGA->Height-1; Y >= 0; --Y)
	{
		const uint8* ImageCol = ImageData + (Y * TGA->Width); 
		uint8* TextureCol = TextureData + (RevY++ * TGA->Width);
		FMemory::Memcpy(TextureCol, ImageCol, TGA->Width);
	}
}

bool DecompressTGA_helper(
	const FTGAFileHeader* TGA,
	uint32*& TextureData,
	const int32 TextureDataSize,
	FFeedbackContext* Warn )
{
	if(TGA->ImageTypeCode == 10) // 10 = RLE compressed 
	{
		// RLE compression: CHUNKS: 1 -byte header, high bit 0 = raw, 1 = compressed
		// bits 0-6 are a 7-bit count; count+1 = number of raw pixels following, or rle pixels to be expanded. 
		if(TGA->BitsPerPixel == 32)
		{
			DecompressTGA_RLE_32bpp(TGA, TextureData);
		}
		else if( TGA->BitsPerPixel == 24 )
		{	
			DecompressTGA_RLE_24bpp(TGA, TextureData);
		}
		else if( TGA->BitsPerPixel == 16 )
		{
			DecompressTGA_RLE_16bpp(TGA, TextureData);
		}
		else
		{
			Warn->Logf(ELogVerbosity::Error, TEXT("TGA uses an unsupported rle-compressed bit-depth: %u"),TGA->BitsPerPixel);
			return false;
		}
	}
	else if(TGA->ImageTypeCode == 2) // 2 = Uncompressed RGB
	{
		if(TGA->BitsPerPixel == 32)
		{
			DecompressTGA_32bpp(TGA, TextureData);
		}
		else if(TGA->BitsPerPixel == 16)
		{
			DecompressTGA_16bpp(TGA, TextureData);
		}            
		else if(TGA->BitsPerPixel == 24)
		{
			DecompressTGA_24bpp(TGA, TextureData);
		}
		else
		{
			Warn->Logf(ELogVerbosity::Error, TEXT("TGA uses an unsupported bit-depth: %u"),TGA->BitsPerPixel);
			return false;
		}
	}
	// Support for alpha stored as pseudo-color 8-bit TGA
	else if(TGA->ColorMapType == 1 && TGA->ImageTypeCode == 1 && TGA->BitsPerPixel == 8)
	{
		DecompressTGA_8bpp(TGA, (uint8*)TextureData);
	}
	// standard grayscale
	else if(TGA->ColorMapType == 0 && TGA->ImageTypeCode == 3 && TGA->BitsPerPixel == 8)
	{
		DecompressTGA_8bpp(TGA, (uint8*)TextureData);
	}
	else
	{
		Warn->Logf(ELogVerbosity::Error, TEXT("TGA is an unsupported type: %u"),TGA->ImageTypeCode);
		return false;
	}

	// Flip the image data if the flip bits are set in the TGA header.
	bool FlipX = (TGA->ImageDescriptor & 0x10) ? 1 : 0;
	bool FlipY = (TGA->ImageDescriptor & 0x20) ? 1 : 0;
	if(FlipY || FlipX)
	{
		TArray<uint8> FlippedData;
		FlippedData.AddUninitialized(TextureDataSize);

		int32 NumBlocksX = TGA->Width;
		int32 NumBlocksY = TGA->Height;
		int32 BlockBytes = TGA->BitsPerPixel == 8 ? 1 : 4;

		uint8* MipData = (uint8*)TextureData;

		for(int32 Y = 0;Y < NumBlocksY;Y++)
		{
			for(int32 X  = 0;X < NumBlocksX;X++)
			{
				int32 DestX = FlipX ? (NumBlocksX - X - 1) : X;
				int32 DestY = FlipY ? (NumBlocksY - Y - 1) : Y;
				FMemory::Memcpy(
					&FlippedData[(DestX + DestY * NumBlocksX) * BlockBytes],
					&MipData[(X + Y * NumBlocksX) * BlockBytes],
					BlockBytes
					);
			}
		}
		FMemory::Memcpy(MipData,FlippedData.GetData(),FlippedData.Num());
	}

	return true;
}

bool DecompressTGA(
	const FTGAFileHeader*	TGA,
	FImportImage&			OutImage,
	FFeedbackContext*		Warn)
{
	if (TGA->ColorMapType == 1 && TGA->ImageTypeCode == 1 && TGA->BitsPerPixel == 8)
	{
		// Notes: The Scaleform GFx exporter (dll) strips all font glyphs into a single 8-bit texture.
		// The targa format uses this for a palette index; GFx uses a palette of (i,i,i,i) so the index
		// is also the alpha value.
		//
		// We store the image as PF_G8, where it will be used as alpha in the Glyph shader.
		OutImage.Init2D(
			TGA->Width,
			TGA->Height,
			TSF_G8);
		OutImage.CompressionSettings = TC_Grayscale;
	}
	else if(TGA->ColorMapType == 0 && TGA->ImageTypeCode == 3 && TGA->BitsPerPixel == 8)
	{
		// standard grayscale images
		OutImage.Init2D(
			TGA->Width,
			TGA->Height,
			TSF_G8);
		OutImage.CompressionSettings = TC_Grayscale;
	}
	else
	{
		if(TGA->ImageTypeCode == 10) // 10 = RLE compressed 
		{
			if( TGA->BitsPerPixel != 32 &&
				TGA->BitsPerPixel != 24 &&
			    TGA->BitsPerPixel != 16 )
			{
				Warn->Logf(ELogVerbosity::Error, TEXT("TGA uses an unsupported rle-compressed bit-depth: %u"),TGA->BitsPerPixel);
				return false;
			}
		}
		else
		{
			if( TGA->BitsPerPixel != 32 &&
				TGA->BitsPerPixel != 16 &&
				TGA->BitsPerPixel != 24)
			{
				Warn->Logf(ELogVerbosity::Error, TEXT("TGA uses an unsupported bit-depth: %u"),TGA->BitsPerPixel);
				return false;
			}
		}

		OutImage.Init2D(
			TGA->Width,
			TGA->Height,
			TSF_BGRA8);
	}

	int32 TextureDataSize = OutImage.RawData.Num();
	uint32* TextureData = (uint32*)OutImage.RawData.GetData();

	return DecompressTGA_helper(TGA, TextureData, TextureDataSize, Warn);
}

bool UTextureFactory::bSuppressImportOverwriteDialog = false;
bool UTextureFactory::bForceOverwriteExistingSettings = false;

UTextureFactory::UTextureFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UTexture::StaticClass();

	Formats.Add( TEXT( "bmp;Texture" ) );
	Formats.Add( TEXT( "pcx;Texture" ) );
	Formats.Add( TEXT( "tga;Texture" ) );
	Formats.Add( TEXT( "float;Texture" ) );
	Formats.Add( TEXT( "psd;Texture" ) );
	Formats.Add( TEXT( "dds;Texture (Cubemap or 2D)" ) );
	Formats.Add( TEXT( "hdr;Cubemap Texture (LongLat unwrap)" ) );
	Formats.Add( TEXT( "ies;IES Texture (Standard light profiles)" ) );
	Formats.Add( TEXT( "png;Texture" ) );
	Formats.Add( TEXT( "jpg;Texture" ) );
	Formats.Add( TEXT( "jpeg;Texture" ) );
	Formats.Add( TEXT( "exr;Texture (HDR)" ) );

	bCreateNew = false;
	bEditorImport = true;
}

bool UTextureFactory::FactoryCanImport(const FString& Filename)
{
	FString Extension = FPaths::GetExtension(Filename);

	return (Formats.ContainsByPredicate(
		[&Extension](const FString& Format)
		{
			return Format.StartsWith(Extension);
		}));
}



void UTextureFactory::PostInitProperties()
{
	Super::PostInitProperties();
	MipGenSettings = TextureMipGenSettings(0);
	bool bFlipNormalMapGreenChannelSetting = false;
	GConfig->GetBool(TEXT("/Script/UnrealEd.EditorEngine"), TEXT("FlipNormalMapGreenChannel"), bFlipNormalMapGreenChannelSetting, GEngineIni);
	bFlipNormalMapGreenChannel = bFlipNormalMapGreenChannelSetting;
}

UTexture2D* UTextureFactory::CreateTexture2D( UObject* InParent, FName Name, EObjectFlags Flags )
{
	UObject* NewObject = CreateOrOverwriteAsset(UTexture2D::StaticClass(), InParent, Name, Flags);
	UTexture2D* NewTexture = nullptr;
	if(NewObject)
	{
		NewTexture = CastChecked<UTexture2D>(NewObject);
	}
	
	return NewTexture;
}

UTextureCube* UTextureFactory::CreateTextureCube( UObject* InParent, FName Name, EObjectFlags Flags )
{
	// CreateOrOverwriteAsset could fail if this cubemap replaces an asset that still has references.
	UObject* NewObject = CreateOrOverwriteAsset(UTextureCube::StaticClass(), InParent, Name, Flags);
	return NewObject ? CastChecked<UTextureCube>(NewObject) : nullptr;
}

void UTextureFactory::SuppressImportOverwriteDialog(bool bOverwriteExistingSettings)
{
	bSuppressImportOverwriteDialog = true;
    bForceOverwriteExistingSettings = bOverwriteExistingSettings;
}

/**
 * This fills any pixels of a texture with have an alpha value of zero,
 * with an RGB from the nearest neighboring pixel which has non-zero alpha.
*/
template<typename PixelDataType, typename ColorDataType, int32 RIdx, int32 GIdx, int32 BIdx, int32 AIdx> class PNGDataFill
{
public:

	PNGDataFill( int32 SizeX, int32 SizeY, uint8* SourceTextureData )
		: SourceData( reinterpret_cast<PixelDataType*>(SourceTextureData) )
		, TextureWidth(SizeX)
		, TextureHeight(SizeY)
	{
	}

	void ProcessData()
	{
		int32 NumZeroedTopRowsToProcess = 0;
		int32 FillColorRow = -1;
		for (int32 Y = 0; Y < TextureHeight; ++Y)
		{
			if (!ProcessHorizontalRow(Y))
			{
				if (FillColorRow != -1)
				{
					FillRowColorPixels(FillColorRow, Y);
				}
				else
				{
					NumZeroedTopRowsToProcess = Y;
				}
			}
			else
			{
				FillColorRow = Y;
			}
		}

		// Can only fill upwards if image not fully zeroed
		if (NumZeroedTopRowsToProcess > 0 && NumZeroedTopRowsToProcess + 1 < TextureHeight)
		{
			for (int32 Y = 0; Y <= NumZeroedTopRowsToProcess; ++Y)
			{
				FillRowColorPixels(NumZeroedTopRowsToProcess + 1, Y);
			}
		}
	}

	/* returns False if requires further processing because entire row is filled with zeroed alpha values */
	bool ProcessHorizontalRow(int32 Y)
	{
		// only wipe out colors that are affected by png turning valid colors white if alpha = 0
		const uint32 WhiteWithZeroAlpha = FColor(255, 255, 255, 0).DWColor();

		// Left -> Right
		int32 NumLeftmostZerosToProcess = 0;
		const PixelDataType* FillColor = nullptr;
		for (int32 X = 0; X < TextureWidth; ++X)
		{
			PixelDataType* PixelData = SourceData + (Y * TextureWidth + X) * 4;
			ColorDataType* ColorData = reinterpret_cast<ColorDataType*>(PixelData);

			if (*ColorData == WhiteWithZeroAlpha)
			{
				if (FillColor)
				{
					PixelData[RIdx] = FillColor[RIdx];
					PixelData[GIdx] = FillColor[GIdx];
					PixelData[BIdx] = FillColor[BIdx];
				}
				else
				{
					// Mark pixel as needing fill
					*ColorData = 0;

					// Keep track of how many pixels to fill starting at beginning of row
					NumLeftmostZerosToProcess = X;
				}
			}
			else
			{
				FillColor = PixelData;
			}
		}

		if (NumLeftmostZerosToProcess == 0)
		{
			// No pixels left that are zero
			return true;
		}

		if (NumLeftmostZerosToProcess + 1 >= TextureWidth)
		{
			// All pixels in this row are zero and must be filled using rows above or below
			return false;
		}

		// Fill using non zero pixel immediately to the right of the beginning series of zeros
		FillColor = SourceData + (Y * TextureWidth + NumLeftmostZerosToProcess + 1) * 4;

		// Fill zero pixels found at beginning of row that could not be filled during the Left to Right pass
		for (int32 X = 0; X <= NumLeftmostZerosToProcess; ++X)
		{
			PixelDataType* PixelData = SourceData + (Y * TextureWidth + X) * 4;
			PixelData[RIdx] = FillColor[RIdx];
			PixelData[GIdx] = FillColor[GIdx];
			PixelData[BIdx] = FillColor[BIdx];
		}

		return true;
	}

	void FillRowColorPixels(int32 FillColorRow, int32 Y)
	{
		for (int32 X = 0; X < TextureWidth; ++X)
		{
			const PixelDataType* FillColor = SourceData + (FillColorRow * TextureWidth + X) * 4;
			PixelDataType* PixelData = SourceData + (Y * TextureWidth + X) * 4;
			PixelData[RIdx] = FillColor[RIdx];
			PixelData[GIdx] = FillColor[GIdx];
			PixelData[BIdx] = FillColor[BIdx];
		}
	}

	PixelDataType* SourceData;
	int32 TextureWidth;
	int32 TextureHeight;
};

/**
 * For PNG texture importing, this ensures that any pixels with an alpha value of zero have an RGB
 * assigned to them from a neighboring pixel which has non-zero alpha.
 * This is needed as PNG exporters tend to turn pixels that are RGBA = (x,x,x,0) to (1,1,1,0)
 * and this produces artifacts when drawing the texture with bilinear filtering. 
 *
 * @param TextureSource - The source texture
 * @param SourceData - The source texture data
*/
void FillZeroAlphaPNGData( int32 SizeX, int32 SizeY, ETextureSourceFormat SourceFormat, uint8* SourceData )
{
	switch( SourceFormat )
	{
		case TSF_BGRA8:
		{
			PNGDataFill<uint8, uint32, 2, 1, 0, 3> PNGFill(SizeX, SizeY, SourceData );
			PNGFill.ProcessData();
			break;
		}

		case TSF_RGBA16:
		{
			PNGDataFill<uint16, uint64, 0, 1, 2, 3> PNGFill(SizeX, SizeY, SourceData );
			PNGFill.ProcessData();
			break;
		}
	}
}

extern ENGINE_API bool GUseBilinearLightmaps;

void FImportImage::Init2D(int32 InSizeX, int32 InSizeY, ETextureSourceFormat InFormat, const void* InData)
{
	SizeX = InSizeX;
	SizeY = InSizeY;
	NumMips = 1;
	Format = InFormat;
	RawData.AddUninitialized(SizeX * SizeY * FTextureSource::GetBytesPerPixel(Format));
	if (InData)
	{
		FMemory::Memcpy(RawData.GetData(), InData, RawData.Num());
	}
}

void FImportImage::Init2DWithMips(int32 InSizeX, int32 InSizeY, int32 InNumMips, ETextureSourceFormat InFormat, const void* InData)
{
	SizeX = InSizeX;
	SizeY = InSizeY;
	NumMips = InNumMips;
	Format = InFormat;

	int32 TotalSize = 0;
	for (int32 MipIndex = 0; MipIndex < InNumMips; ++MipIndex)
	{
		TotalSize += GetMipSize(MipIndex);
	}
	RawData.AddUninitialized(TotalSize);

	if (InData)
	{
		FMemory::Memcpy(RawData.GetData(), InData, RawData.Num());
	}
}

int32 FImportImage::GetMipSize(int32 InMipIndex) const
{
	check(InMipIndex >= 0);
	check(InMipIndex < NumMips);
	const int32 MipSizeX = FMath::Max(SizeX >> InMipIndex, 1);
	const int32 MipSizeY = FMath::Max(SizeY >> InMipIndex, 1);
	return MipSizeX * MipSizeY * FTextureSource::GetBytesPerPixel(Format);
}

void* FImportImage::GetMipData(int32 InMipIndex)
{
	int32 Offset = 0;
	for (int32 MipIndex = 0; MipIndex < InMipIndex; ++MipIndex)
	{
		Offset += GetMipSize(MipIndex);
	}
	return &RawData[Offset];
}

bool UTextureFactory::ImportImage(const uint8* Buffer, uint32 Length, FFeedbackContext* Warn, bool bAllowNonPowerOfTwo, FImportImage& OutImage)
{
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

	//
	// PNG
	//
	TSharedPtr<IImageWrapper> PngImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (PngImageWrapper.IsValid() && PngImageWrapper->SetCompressed(Buffer, Length))
	{
		if (!IsImportResolutionValid(PngImageWrapper->GetWidth(), PngImageWrapper->GetHeight(), bAllowNonPowerOfTwo, Warn))
		{
			return false;
		}

		// Select the texture's source format
		ETextureSourceFormat TextureFormat = TSF_Invalid;
		int32 BitDepth = PngImageWrapper->GetBitDepth();
		ERGBFormat Format = PngImageWrapper->GetFormat();

		if (Format == ERGBFormat::Gray)
		{
			if (BitDepth <= 8)
			{
				TextureFormat = TSF_G8;
				Format = ERGBFormat::Gray;
				BitDepth = 8;
			}
			else if (BitDepth == 16)
			{
				// TODO: TSF_G16?
				TextureFormat = TSF_RGBA16;
				Format = ERGBFormat::RGBA;
				BitDepth = 16;
			}
		}
		else if (Format == ERGBFormat::RGBA || Format == ERGBFormat::BGRA)
		{
			if (BitDepth <= 8)
			{
				TextureFormat = TSF_BGRA8;
				Format = ERGBFormat::BGRA;
				BitDepth = 8;
			}
			else if (BitDepth == 16)
			{
				TextureFormat = TSF_RGBA16;
				Format = ERGBFormat::RGBA;
				BitDepth = 16;
			}
		}

		if (TextureFormat == TSF_Invalid)
		{
			Warn->Logf(ELogVerbosity::Error, TEXT("PNG file contains data in an unsupported format."));
			return false;
		}

		const TArray<uint8>* RawPNG = nullptr;
		if (PngImageWrapper->GetRaw(Format, BitDepth, RawPNG))
		{
			OutImage.Init2D(
				PngImageWrapper->GetWidth(),
				PngImageWrapper->GetHeight(),
				TextureFormat,
				RawPNG->GetData()
			);
			OutImage.SRGB = BitDepth < 16;

			bool bFillPNGZeroAlpha = true;
			GConfig->GetBool(TEXT("TextureImporter"), TEXT("FillPNGZeroAlpha"), bFillPNGZeroAlpha, GEditorIni);

			if (bFillPNGZeroAlpha)
			{
				// Replace the pixels with 0.0 alpha with a color value from the nearest neighboring color which has a non-zero alpha
				FillZeroAlphaPNGData(OutImage.SizeX, OutImage.SizeY, OutImage.Format, OutImage.RawData.GetData());
			}
		}
		else
		{
			Warn->Logf(ELogVerbosity::Error, TEXT("Failed to decode PNG."));
			return false;
		}

		return true;
	}

	//
	// JPEG
	//
	TSharedPtr<IImageWrapper> JpegImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
	if (JpegImageWrapper.IsValid() && JpegImageWrapper->SetCompressed(Buffer, Length))
	{
		if (!IsImportResolutionValid(JpegImageWrapper->GetWidth(), JpegImageWrapper->GetHeight(), bAllowNonPowerOfTwo, Warn))
		{
			return false;
		}

		// Select the texture's source format
		ETextureSourceFormat TextureFormat = TSF_Invalid;
		int32 BitDepth = JpegImageWrapper->GetBitDepth();
		ERGBFormat Format = JpegImageWrapper->GetFormat();

		if (Format == ERGBFormat::Gray)
		{
			if (BitDepth <= 8)
			{
				TextureFormat = TSF_G8;
				Format = ERGBFormat::Gray;
				BitDepth = 8;
			}
		}
		else if (Format == ERGBFormat::RGBA)
		{
			if (BitDepth <= 8)
			{
				TextureFormat = TSF_BGRA8;
				Format = ERGBFormat::BGRA;
				BitDepth = 8;
			}
		}

		if (TextureFormat == TSF_Invalid)
		{
			Warn->Logf(ELogVerbosity::Error, TEXT("JPEG file contains data in an unsupported format."));
			return false;
		}

		const TArray<uint8>* RawJPEG = nullptr;
		if (JpegImageWrapper->GetRaw(Format, BitDepth, RawJPEG))
		{
			OutImage.Init2D(
				JpegImageWrapper->GetWidth(),
				JpegImageWrapper->GetHeight(),
				TextureFormat,
				RawJPEG->GetData()
			);
			OutImage.SRGB = BitDepth < 16;
		}
		else
		{
			Warn->Logf(ELogVerbosity::Error, TEXT("Failed to decode JPEG."));

			return false;
		}

		return true;
	}

	//
	// EXR
	//
	TSharedPtr<IImageWrapper> ExrImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::EXR);
	if (ExrImageWrapper.IsValid() && ExrImageWrapper->SetCompressed(Buffer, Length))
	{
		int32 Width = ExrImageWrapper->GetWidth();
		int32 Height = ExrImageWrapper->GetHeight();

		if (!IsImportResolutionValid(Width, Height, bAllowNonPowerOfTwo, Warn))
		{
			return false;
		}

		// Select the texture's source format
		ETextureSourceFormat TextureFormat = TSF_Invalid;
		int32 BitDepth = ExrImageWrapper->GetBitDepth();
		ERGBFormat Format = ExrImageWrapper->GetFormat();

		if (Format == ERGBFormat::RGBA && BitDepth == 16)
		{
			TextureFormat = TSF_RGBA16F;
			Format = ERGBFormat::BGRA;
		}

		if (TextureFormat == TSF_Invalid)
		{
			Warn->Logf(ELogVerbosity::Error, TEXT("EXR file contains data in an unsupported format."));
			return false;
		}

		const TArray<uint8>* Raw = nullptr;
		if (ExrImageWrapper->GetRaw(Format, BitDepth, Raw))
		{
			OutImage.Init2D(
				Width,
				Height,
				TextureFormat,
				Raw->GetData()
			);
			OutImage.SRGB = false;
			OutImage.CompressionSettings = TC_HDR;
		}
		else
		{
			Warn->Logf(ELogVerbosity::Error, TEXT("Failed to decode EXR."));

			return false;
		}

		return true;
	}

	//
	// BMP
	//
	TSharedPtr<IImageWrapper> BmpImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::BMP);
	if (BmpImageWrapper.IsValid() && BmpImageWrapper->SetCompressed(Buffer, Length))
	{
		// Check the resolution of the imported texture to ensure validity
		if (!IsImportResolutionValid(BmpImageWrapper->GetWidth(), BmpImageWrapper->GetHeight(), bAllowNonPowerOfTwo, Warn))
		{
			return false;
		}

		const TArray<uint8>* RawBMP = nullptr;
		if (BmpImageWrapper->GetRaw(BmpImageWrapper->GetFormat(), BmpImageWrapper->GetBitDepth(), RawBMP))
		{
			// Set texture properties.
			OutImage.Init2D(
				BmpImageWrapper->GetWidth(),
				BmpImageWrapper->GetHeight(),
				TSF_BGRA8,
				RawBMP->GetData()
			);
			return true;
		}

		return false;
	}

	//
	// PCX
	//
	const FPCXFileHeader*    PCX = (FPCXFileHeader *)Buffer;
	if (Length >= sizeof(FPCXFileHeader) && PCX->Manufacturer == 10)
	{
		int32 NewU = PCX->XMax + 1 - PCX->XMin;
		int32 NewV = PCX->YMax + 1 - PCX->YMin;

		// Check the resolution of the imported texture to ensure validity
		if (!IsImportResolutionValid(NewU, NewV, bAllowNonPowerOfTwo, Warn))
		{
			return false;
		}
		else if (PCX->NumPlanes == 1 && PCX->BitsPerPixel == 8)
		{

			// Set texture properties.
			OutImage.Init2D(
				NewU,
				NewV,
				TSF_BGRA8
			);
			FColor* DestPtr = (FColor*)OutImage.RawData.GetData();

			// Import the palette.
			uint8* PCXPalette = (uint8 *)(Buffer + Length - 256 * 3);
			TArray<FColor>	Palette;
			for (uint32 i = 0; i < 256; i++)
			{
				Palette.Add(FColor(PCXPalette[i * 3 + 0], PCXPalette[i * 3 + 1], PCXPalette[i * 3 + 2], i == 0 ? 0 : 255));
			}

			// Import it.
			FColor* DestEnd = DestPtr + NewU * NewV;
			Buffer += 128;
			while (DestPtr < DestEnd)
			{
				uint8 Color = *Buffer++;
				if ((Color & 0xc0) == 0xc0)
				{
					uint32 RunLength = Color & 0x3f;
					Color = *Buffer++;

					for (uint32 Index = 0; Index < RunLength; Index++)
					{
						*DestPtr++ = Palette[Color];
					}
				}
				else *DestPtr++ = Palette[Color];
			}
		}
		else if (PCX->NumPlanes == 3 && PCX->BitsPerPixel == 8)
		{
			// Set texture properties.
			OutImage.Init2D(
				NewU,
				NewV,
				TSF_BGRA8
			);

			uint8* Dest = OutImage.RawData.GetData();

			// Doing a memset to make sure the alpha channel is set to 0xff since we only have 3 color planes.
			FMemory::Memset(Dest, 0xff, NewU * NewV * FTextureSource::GetBytesPerPixel(OutImage.Format));

			// Copy upside-down scanlines.
			Buffer += 128;
			int32 CountU = FMath::Min<int32>(PCX->BytesPerLine, NewU);
			for (int32 i = 0; i < NewV; i++)
			{
				// We need to decode image one line per time building RGB image color plane by color plane.
				int32 RunLength, Overflow = 0;
				uint8 Color = 0;
				for (int32 ColorPlane = 2; ColorPlane >= 0; ColorPlane--)
				{
					for (int32 j = 0; j < CountU; j++)
					{
						if (!Overflow)
						{
							Color = *Buffer++;
							if ((Color & 0xc0) == 0xc0)
							{
								RunLength = FMath::Min((Color & 0x3f), CountU - j);
								Overflow = (Color & 0x3f) - RunLength;
								Color = *Buffer++;
							}
							else
								RunLength = 1;
						}
						else
						{
							RunLength = FMath::Min(Overflow, CountU - j);
							Overflow = Overflow - RunLength;
						}

						//checkf(((i*NewU + RunLength) * 4 + ColorPlane) < (Texture->Source.CalcMipSize(0)),
						//	TEXT("RLE going off the end of buffer"));
						for (int32 k = j; k < j + RunLength; k++)
						{
							Dest[(i*NewU + k) * 4 + ColorPlane] = Color;
						}
						j += RunLength - 1;
					}
				}
			}
		}
		else
		{
			Warn->Logf(ELogVerbosity::Error, TEXT("PCX uses an unsupported format (%i/%i)"), PCX->NumPlanes, PCX->BitsPerPixel);
			return false;
		}

		return true;
	}
	//
	// TGA
	//
	// Support for alpha stored as pseudo-color 8-bit TGA
	const FTGAFileHeader*    TGA = (FTGAFileHeader *)Buffer;
	if (Length >= sizeof(FTGAFileHeader) &&
		((TGA->ColorMapType == 0 && TGA->ImageTypeCode == 2) ||
			// ImageTypeCode 3 is greyscale
		(TGA->ColorMapType == 0 && TGA->ImageTypeCode == 3) ||
			(TGA->ColorMapType == 0 && TGA->ImageTypeCode == 10) ||
			(TGA->ColorMapType == 1 && TGA->ImageTypeCode == 1 && TGA->BitsPerPixel == 8)))
	{
		// Check the resolution of the imported texture to ensure validity
		if (!IsImportResolutionValid(TGA->Width, TGA->Height, bAllowNonPowerOfTwo, Warn))
		{
			return false;
		}

		const bool bResult = DecompressTGA(TGA, OutImage, Warn);
		if (bResult && OutImage.CompressionSettings == TC_Grayscale && TGA->ImageTypeCode == 3)
		{
			// default grayscales to linear as they wont get compression otherwise and are commonly used as masks
			OutImage.SRGB = false;
		}

		return bResult;
	}
	//
	// PSD File
	//
	FPSDFileHeader			 psdhdr;
	if (Length > sizeof(FPSDFileHeader))
	{
		psd_GetPSDHeader(Buffer, psdhdr);
	}
	if (psdhdr.IsValid())
	{
		// Check the resolution of the imported texture to ensure validity
		if (!IsImportResolutionValid(psdhdr.Width, psdhdr.Height, bAllowNonPowerOfTwo, Warn))
		{
			return false;
		}
		if (!psdhdr.IsSupported())
		{
			Warn->Logf(TEXT("Format of this PSD is not supported. Only Grayscale and RGBColor PSD images are currently supported, in 8-bit or 16-bit."));
			return false;
		}

		// Select the texture's source format
		ETextureSourceFormat TextureFormat = TSF_Invalid;
		if (psdhdr.Depth == 8)
		{
			TextureFormat = TSF_BGRA8;
		}
		else if (psdhdr.Depth == 16)
		{
			TextureFormat = TSF_RGBA16;
		}

		if (TextureFormat == TSF_Invalid)
		{
			Warn->Logf(ELogVerbosity::Error, TEXT("PSD file contains data in an unsupported format."));
			return false;
		}

		// The psd is supported. Load it up.        
		OutImage.Init2D(
			psdhdr.Width,
			psdhdr.Height,
			TextureFormat
		);
		uint8* Dst = (uint8*)OutImage.RawData.GetData();

		if (!psd_ReadData(Dst, Buffer, psdhdr))
		{
			Warn->Logf(TEXT("Failed to read this PSD"));
			return false;
		}

		return true;
	}
	
	//
	// DDS Texture
	//
	FDDSLoadHelper  DDSLoadHelper(Buffer, Length);
	if (DDSLoadHelper.IsValid2DTexture())
	{
		// DDS 2d texture
		if (!IsImportResolutionValid(DDSLoadHelper.DDSHeader->dwWidth, DDSLoadHelper.DDSHeader->dwHeight, bAllowNonPowerOfTwo, Warn))
		{
			Warn->Logf(ELogVerbosity::Error, TEXT("DDS has invalid dimensions."));
			return false;
		}

		ETextureSourceFormat SourceFormat = DDSLoadHelper.ComputeSourceFormat();

		// Invalid DDS format
		if (SourceFormat == TSF_Invalid)
		{
			Warn->Logf(ELogVerbosity::Error, TEXT("DDS uses an unsupported format."));
			return false;
		}

		uint32 MipMapCount = DDSLoadHelper.ComputeMipMapCount();
		if (SourceFormat != TSF_Invalid && MipMapCount > 0)
		{
			OutImage.Init2DWithMips(
				DDSLoadHelper.DDSHeader->dwWidth,
				DDSLoadHelper.DDSHeader->dwHeight,
				MipMapCount,
				SourceFormat,
				DDSLoadHelper.GetDDSDataPointer()
			);

			if (MipMapCount > 1)
			{
				// if the source has mips we keep the mips by default, unless the user changes that
				MipGenSettings = TMGS_LeaveExistingMips;
			}

			if (FTextureSource::IsHDR(SourceFormat))
			{
				// the loader can suggest a compression setting
				OutImage.CompressionSettings = TC_HDR;
			}

			return true;
		}
	}
	
	return false;
}

UTexture* UTextureFactory::ImportTextureUDIM(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, const TCHAR* Type, const TMap<int32, FString>& UDIMIndexToFile, FFeedbackContext* Warn)
{
	TArray<uint8> TextureData;
	TArray<FImportImage> SourceImages;
	TArray<FTextureSourceBlock> SourceBlocks;
	TArray<FString> SourceFileNames;
	SourceImages.Reserve(UDIMIndexToFile.Num());
	SourceBlocks.Reserve(UDIMIndexToFile.Num());
	SourceFileNames.Reserve(UDIMIndexToFile.Num());

	ETextureSourceFormat Format = TSF_Invalid;
	TextureCompressionSettings TCSettings = TC_MAX;
	bool bSRGB = false;
	for (const auto& It : UDIMIndexToFile)
	{
		const FString& TexturePath = It.Value;
		if (FFileHelper::LoadFileToArray(TextureData, *TexturePath))
		{
			// UDIM requires each page to be power-of-2
			const bool bAllowNonPowerOfTwo = false;

			FImportImage Image;
			if (ImportImage(TextureData.GetData(), TextureData.Num(), Warn, bAllowNonPowerOfTwo, Image))
			{
				if (Format == TSF_Invalid)
				{
					Format = Image.Format;
					bSRGB = Image.SRGB;
				}

				if (TCSettings == TC_MAX)
				{
					// Should we somehow try to combine different compression settings? Is that ever useful/needed?
					TCSettings = Image.CompressionSettings;
				}

				// Deal with mismatched formats somehow?  convert?
				if (ensure(Format == Image.Format && bSRGB == Image.SRGB))
				{
					const int32 UDIMIndex = It.Key;
					FTextureSourceBlock* Block = new(SourceBlocks) FTextureSourceBlock();
					Block->BlockX = (UDIMIndex - 1001) % 10;
					Block->BlockY = (UDIMIndex - 1001) / 10;
					Block->SizeX = Image.SizeX;
					Block->SizeY = Image.SizeY;
					Block->NumSlices = 1;
					Block->NumMips = Image.NumMips;

					SourceImages.Emplace(MoveTemp(Image));
					SourceFileNames.Add(TexturePath);
				}
				else
				{
					Warn->Logf(ELogVerbosity::Warning, TEXT("Mismatched UDIM image formats, skipping file \"%s\""), *TexturePath);
				}
			}
		}
	}

	if (SourceImages.Num() < 2)
	{
		return nullptr;
	}

	TArray<const uint8*> SourceImageData;
	SourceImageData.Reserve(SourceImages.Num());
	for (const FImportImage& Image : SourceImages)
	{
		SourceImageData.Add(Image.RawData.GetData());
	}

	UTexture2D* Texture = CreateTexture2D(InParent, Name, Flags);
	Texture->Source.InitBlocked(&Format, SourceBlocks.GetData(), 1, SourceBlocks.Num(), SourceImageData.GetData());
	Texture->CompressionSettings = TCSettings;
	Texture->SRGB = bSRGB;

	for (int32 FileIndex = 0; FileIndex < SourceFileNames.Num(); ++FileIndex)
	{
		Texture->AssetImportData->AddFileName(SourceFileNames[FileIndex], FileIndex);
	}

	return Texture;
}

UTexture* UTextureFactory::ImportTexture(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, const TCHAR* Type, const uint8*& Buffer, const uint8* BufferEnd, FFeedbackContext* Warn)
{
	bool bAllowNonPowerOfTwo = false;
	GConfig->GetBool( TEXT("TextureImporter"), TEXT("AllowNonPowerOfTwoTextures"), bAllowNonPowerOfTwo, GEditorIni );

	// Validate it.
	const int32 Length = BufferEnd - Buffer;

	//
	// Generic 2D Image
	//
	FImportImage Image;
	if (ImportImage(Buffer, Length, Warn, bAllowNonPowerOfTwo, Image))
	{
		UTexture2D* Texture = CreateTexture2D(InParent, Name, Flags);
		if (Texture)
		{
			Texture->Source.Init(
				Image.SizeX,
				Image.SizeY,
				/*NumSlices=*/ 1,
				Image.NumMips,
				Image.Format,
				Image.RawData.GetData()
			);
			Texture->CompressionSettings = Image.CompressionSettings;
			Texture->SRGB = Image.SRGB;
		}
		return Texture;
	}

	//
	// DDS Cubemap
	//
	FDDSLoadHelper DDSLoadHelper(Buffer, Length);
	if(DDSLoadHelper.IsValidCubemapTexture())
	{
		if(!IsImportResolutionValid(DDSLoadHelper.DDSHeader->dwWidth, DDSLoadHelper.DDSHeader->dwHeight, bAllowNonPowerOfTwo, Warn))
		{
			Warn->Logf(ELogVerbosity::Error, TEXT("DDS uses an unsupported format"));
			return nullptr;
		}

		int32 NumMips = DDSLoadHelper.ComputeMipMapCount();
		ETextureSourceFormat Format = DDSLoadHelper.ComputeSourceFormat();
		if (Format == TSF_Invalid)
		{
			Warn->Logf(ELogVerbosity::Error, TEXT("DDS file contains data in an unsupported format."));
			return nullptr;
		}

		if (NumMips > MAX_TEXTURE_MIP_COUNT)
		{
			Warn->Logf(ELogVerbosity::Error, TEXT("DDS file contains an unsupported number of mipmap levels."));
			return nullptr;
		}

		// create the cube texture
		UTextureCube* TextureCube = CreateTextureCube( InParent, Name, Flags );

		if ( TextureCube )
		{
			TextureCube->Source.Init(
				DDSLoadHelper.DDSHeader->dwWidth, 
				DDSLoadHelper.DDSHeader->dwHeight,
				/*NumSlices=*/ 6,
				NumMips,
				Format
				);
			if(Format == TSF_RGBA16F)
			{
				TextureCube->CompressionSettings = TC_HDR;
				TextureCube->SRGB = false;
			}

			uint8* DestMipData[MAX_TEXTURE_MIP_COUNT] = {0};
			int32 MipSize[MAX_TEXTURE_MIP_COUNT] = {0};
			for (int32 MipIndex = 0; MipIndex < NumMips; ++MipIndex)
			{
				DestMipData[MipIndex] = TextureCube->Source.LockMip(MipIndex);
				MipSize[MipIndex] = TextureCube->Source.CalcMipSize(MipIndex) / 6;
			}

			for (int32 SliceIndex = 0; SliceIndex < 6; ++SliceIndex)
			{
				const uint8* SrcMipData = DDSLoadHelper.GetDDSDataPointer((ECubeFace)SliceIndex);
				for (int32 MipIndex = 0; MipIndex < NumMips; ++MipIndex)
				{
					FMemory::Memcpy(DestMipData[MipIndex] + MipSize[MipIndex] * SliceIndex, SrcMipData, MipSize[MipIndex]);
					SrcMipData += MipSize[MipIndex];
				}
			}

			for (int32 MipIndex = 0; MipIndex < NumMips; ++MipIndex)
			{
				TextureCube->Source.UnlockMip(MipIndex);
			}

			// for now we don't support mip map generation on cubemaps
			TextureCube->MipGenSettings = TMGS_LeaveExistingMips;
		}

		return TextureCube;
	}
	
	//
	// HDR File
	//
	FHDRLoadHelper           HDRLoadHelper(Buffer, Length);
	if(HDRLoadHelper.IsValid())
	{
		TArray<uint8> DDSFile;
		HDRLoadHelper.ExtractDDSInRGBE(DDSFile);
		FDDSLoadHelper HDRDDSLoadHelper(DDSFile.GetData(), DDSFile.Num());

		// create the cube texture
		UTextureCube* TextureCube = CreateTextureCube(InParent, Name, Flags);
		if ( TextureCube )
		{
			TextureCube->Source.Init(
				HDRDDSLoadHelper.DDSHeader->dwWidth,
				HDRDDSLoadHelper.DDSHeader->dwHeight,
				/*NumSlices=*/ 1,
				/*NumMips=*/ 1,
				TSF_BGRE8,
				HDRDDSLoadHelper.GetDDSDataPointer()
				);
			// the loader can suggest a compression setting
			TextureCube->CompressionSettings = TC_HDR;
		}

		return TextureCube;
	}

	//
	// IES File (usually measured real world light profiles)
	//
	if(!FCString::Stricmp(Type, TEXT("ies")))
	{
		// checks for .IES extension to avoid wasting loading large assets just to reject them during header parsing
		FIESLoadHelper IESLoadHelper(Buffer, Length);

		if(IESLoadHelper.IsValid())
		{
			TArray<uint8> RAWData;
			float Multiplier = IESLoadHelper.ExtractInRGBA16F(RAWData);

			UTextureLightProfile* Texture = Cast<UTextureLightProfile>( CreateOrOverwriteAsset(UTextureLightProfile::StaticClass(), InParent, Name, Flags) );
			if ( Texture )
			{
				Texture->Source.Init(
					IESLoadHelper.GetWidth(),
					IESLoadHelper.GetHeight(),
					/*NumSlices=*/ 1,
					1,
					TSF_RGBA16F,
					RAWData.GetData()
					);

				Texture->AddressX = TA_Clamp;
				Texture->AddressY = TA_Clamp;
				Texture->CompressionSettings = TC_HDR;
				MipGenSettings = TMGS_NoMipmaps;
				Texture->Brightness = IESLoadHelper.GetBrightness();
				Texture->TextureMultiplier = Multiplier;
			}

			return Texture;
		}
	}

	return nullptr;
}

bool UTextureFactory::DoesSupportClass(UClass* Class)
{
	return Class == UTexture2D::StaticClass() || Class == UTextureCube::StaticClass();
}

static int32 ParseUDIMName(const FString& Name, FString& OutRootName)
{
	int32 SeparatorIndex = INDEX_NONE;
	if (!Name.FindLastChar('.', SeparatorIndex))
	{
		// '.' is the standard UDIM separator, but we'll accept '_' as well
		if (!Name.FindLastChar('_', SeparatorIndex))
		{
			return INDEX_NONE;
		}
	}
	if (SeparatorIndex + 5 != Name.Len())
	{
		return INDEX_NONE;
	}
	const TCHAR Digit0 = Name[SeparatorIndex + 4];
	const TCHAR Digit1 = Name[SeparatorIndex + 3];
	const TCHAR Digit2 = Name[SeparatorIndex + 2];
	const TCHAR Digit3 = Name[SeparatorIndex + 1];
	if (Digit0 < '0' || Digit0 > '9') return INDEX_NONE;
	if (Digit1 < '0' || Digit1 > '9') return INDEX_NONE;
	if (Digit2 < '0' || Digit2 > '9') return INDEX_NONE;
	if (Digit3 < '0' || Digit3 > '9') return INDEX_NONE;

	const int32 Value = (int32)(Digit0 - '0') + (int32)(Digit1 - '0') * 10 + (int32)(Digit2 - '0') * 100 + (int32)(Digit3 - '0') * 1000;
	if (Value < 1001)
	{
		// UDIM starts with 1001 as the origin
		return INDEX_NONE;
	}

	OutRootName = Name.Left(SeparatorIndex);
	return Value;
}

UObject* UTextureFactory::FactoryCreateBinary
(
	UClass*				Class,
	UObject*			InParent,
	FName				Name,
	EObjectFlags		Flags,
	UObject*			Context,
	const TCHAR*		Type,
	const uint8*&		Buffer,
	const uint8*			BufferEnd,
	FFeedbackContext*	Warn
)
{
	check(Type);

	FName TextureName = Name;

	// Check to see if we should import a series of textures as UDIM
	// Need to do this first, as this step affects the final name of the created texture asset
	TMap<int32, FString> UDIMIndexToFile;
	{
		const FString FilenameNoExtension = FPaths::GetBaseFilename(CurrentFilename);
		FString BaseUDIMName;
		const int32 BaseUDIMIndex = ParseUDIMName(FilenameNoExtension, BaseUDIMName);
		if (BaseUDIMIndex != INDEX_NONE)
		{
			UDIMIndexToFile.Add(BaseUDIMIndex, CurrentFilename);

			// Filter for other potential UDIM pages, with the same base name and file extension
			const FString Path = FPaths::GetPath(CurrentFilename);
			const FString UDIMFilter = (Path / BaseUDIMName) + TEXT("*") + FPaths::GetExtension(CurrentFilename, true);

			TArray<FString> UDIMFiles;
			IFileManager::Get().FindFiles(UDIMFiles, *UDIMFilter, true, false);

			for (const FString& UDIMFile : UDIMFiles)
			{
				if (!CurrentFilename.EndsWith(UDIMFile) && FactoryCanImport(UDIMFile))
				{
					FString UDIMName;
					const int32 UDIMIndex = ParseUDIMName(FPaths::GetBaseFilename(UDIMFile), UDIMName);
					if (!UDIMIndexToFile.Contains(UDIMIndex) && UDIMName == BaseUDIMName)
					{
						UDIMIndexToFile.Add(UDIMIndex, Path / UDIMFile);
					}
				}
			}
			if (UDIMIndexToFile.Num() > 1)
			{
				// Found multiple UDIM pages, so import as UDIM texture
				// Exclude UDIM number from the name of the UE4 texture asset we create
				TextureName = *BaseUDIMName;

				// Need to rename the package to match the new texture name, since package was already created
				// Package name will be the same as the object name, except will contain additional path information,
				// so we take the existing package name, then extract the UDIM index in order to preserve the path
				FString PackageName;
				InParent->GetName(PackageName);

				FString PackageUDIMName;
				const int32 PackageUDIMIndex = ParseUDIMName(PackageName, PackageUDIMName);
				check(PackageUDIMIndex == BaseUDIMIndex);
				check(PackageUDIMName.EndsWith(BaseUDIMName, ESearchCase::CaseSensitive));

				// In normal case, higher level code would have already checked for duplicate package name
				// But since we're changing package name here, check to see if package with the new name already exists...
				// If it does, code later in this method will prompt user to overwrite the existing asset
				UPackage* ExistingPackage = FindPackage(InParent->GetOuter(), *PackageUDIMName);
				if (ExistingPackage)
				{
					InParent = ExistingPackage;
				}
				else
				{
					verify(InParent->Rename(*PackageUDIMName, nullptr, REN_DontCreateRedirectors));
				}
			}
		}
	}

	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPreImport(this, Class, InParent, TextureName, Type);

	// if the texture already exists, remember the user settings
	UTexture* ExistingTexture = FindObject<UTexture>( InParent, *TextureName.ToString() );
	UTexture2D* ExistingTexture2D = FindObject<UTexture2D>( InParent, *TextureName.ToString() );

	TextureAddress						ExistingAddressX	= TA_Wrap;
	TextureAddress						ExistingAddressY	= TA_Wrap;
	TextureFilter						ExistingFilter		= TF_Default;
	TextureGroup						ExistingLODGroup	= TEXTUREGROUP_World;
	TextureCompressionSettings			ExistingCompressionSettings = TC_Default;
	int32								ExistingLODBias		= 0;
	int32								ExistingNumCinematicMipLevels = 0;
	bool								ExistingNeverStream = false;
	bool								ExistingSRGB		= false;
	bool								ExistingPreserveBorder = false;
	bool								ExistingNoCompression = false;
	bool								ExistingNoAlpha = false;
	bool								ExistingDeferCompression = false;
	bool 								ExistingDitherMipMapAlpha = false;
	bool 								ExistingFlipGreenChannel = false;
	float								ExistingAdjustBrightness = 1.0f;
	float								ExistingAdjustBrightnessCurve = 1.0f;
	float								ExistingAdjustVibrance = 0.0f;
	float								ExistingAdjustSaturation = 1.0f;
	float								ExistingAdjustRGBCurve = 1.0f;
	float								ExistingAdjustHue = 0.0f;
	float								ExistingAdjustMinAlpha = 0.0f;
	float								ExistingAdjustMaxAlpha = 1.0f;
	FVector4							ExistingAlphaCoverageThresholds = FVector4(0, 0, 0, 0);
	TextureMipGenSettings				ExistingMipGenSettings = TextureMipGenSettings(0);
	bool								ExistingVirtualTextureStreaming = false;

	if (bForceOverwriteExistingSettings)
	{
		bUsingExistingSettings = false;
	}
	else
	{
		bUsingExistingSettings = bSuppressImportOverwriteDialog;

		if (ExistingTexture && !bSuppressImportOverwriteDialog)
		{
			DisplayOverwriteOptionsDialog(FText::Format(
				NSLOCTEXT("TextureFactory", "ImportOverwriteWarning", "You are about to import '{0}' over an existing texture."),
				FText::FromName(TextureName)));

			switch (OverwriteYesOrNoToAllState)
			{

			case EAppReturnType::Yes:
			case EAppReturnType::YesAll:
				{
					// Overwrite existing settings
					bUsingExistingSettings = false;
					break;
				}
				case EAppReturnType::No:
				case EAppReturnType::NoAll:
				{
					// Preserve existing settings
					bUsingExistingSettings = true;
					break;
				}
				case EAppReturnType::Cancel:
				default:
				{
					GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, nullptr);
					return nullptr;
				}
			}
		}
	}

	// Don't suppress future textures from checking for overwrites unless the calling code explicitly asks for it
	bSuppressImportOverwriteDialog = false;
	bForceOverwriteExistingSettings = false;

	if (ExistingTexture && bUsingExistingSettings)
	{
		// save settings
		if(ExistingTexture2D)
		{
			ExistingAddressX	= ExistingTexture2D->AddressX;
			ExistingAddressY	= ExistingTexture2D->AddressY;
		}
		ExistingFilter		= ExistingTexture->Filter;
		ExistingLODGroup	= ExistingTexture->LODGroup;
		ExistingCompressionSettings = ExistingTexture->CompressionSettings;
		ExistingLODBias		= ExistingTexture->LODBias;
		ExistingNumCinematicMipLevels = ExistingTexture->NumCinematicMipLevels;
		ExistingNeverStream = ExistingTexture->NeverStream;
		ExistingSRGB		= ExistingTexture->SRGB;
		ExistingPreserveBorder = ExistingTexture->bPreserveBorder;
		ExistingNoCompression = ExistingTexture->CompressionNone;
		ExistingNoAlpha = ExistingTexture->CompressionNoAlpha;
		ExistingDeferCompression = ExistingTexture->DeferCompression;
		ExistingFlipGreenChannel = ExistingTexture->bFlipGreenChannel;
		ExistingDitherMipMapAlpha = ExistingTexture->bDitherMipMapAlpha;
		ExistingAlphaCoverageThresholds = ExistingTexture->AlphaCoverageThresholds;
		ExistingAdjustBrightness = ExistingTexture->AdjustBrightness;
		ExistingAdjustBrightnessCurve = ExistingTexture->AdjustBrightnessCurve;
		ExistingAdjustVibrance = ExistingTexture->AdjustVibrance;
		ExistingAdjustSaturation = ExistingTexture->AdjustSaturation;
		ExistingAdjustRGBCurve = ExistingTexture->AdjustRGBCurve;
		ExistingAdjustHue = ExistingTexture->AdjustHue;
		ExistingAdjustMinAlpha = ExistingTexture->AdjustMinAlpha;
		ExistingAdjustMaxAlpha = ExistingTexture->AdjustMaxAlpha;
		ExistingMipGenSettings = ExistingTexture->MipGenSettings;
		ExistingVirtualTextureStreaming = ExistingTexture->VirtualTextureStreaming;
	}

	if (ExistingTexture2D)
	{
		// Update with new settings, which should disable streaming...
		ExistingTexture2D->UpdateResource();
	}
	
	FTextureReferenceReplacer RefReplacer(ExistingTexture);

	UTexture* Texture = nullptr;
	if (UDIMIndexToFile.Num() > 1)
	{
		// Import UDIM texture
		Texture = ImportTextureUDIM(Class, InParent, TextureName, Flags, Type, UDIMIndexToFile, Warn);
	}
	else
	{
		// Not a UDIM, import a regular texture
		Texture = ImportTexture(Class, InParent, TextureName, Flags, Type, Buffer, BufferEnd, Warn);
		if (Texture)
		{
			Texture->AssetImportData->Update(CurrentFilename, FileHash.IsValid() ? &FileHash : nullptr);
		}
	}

	if(!Texture)
	{
		if (ExistingTexture)
		{
			// We failed to import over the existing texture. Make sure the resource is ready in the existing texture.
			ExistingTexture->UpdateResource();
		}

		Warn->Logf(ELogVerbosity::Error, TEXT("Texture import failed") );
		GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport( this, nullptr );
		return nullptr;
	}

	//Replace the reference for the new texture with the existing one so that all current users still have valid references.
	RefReplacer.Replace(Texture);

	// Start with the value that the loader suggests.
	CompressionSettings = Texture->CompressionSettings;

	// Figure out whether we're using a normal map LOD group.
	bool bIsNormalMapLODGroup = false;
	if( LODGroup == TEXTUREGROUP_WorldNormalMap 
	||	LODGroup == TEXTUREGROUP_CharacterNormalMap
	||	LODGroup == TEXTUREGROUP_VehicleNormalMap
	||	LODGroup == TEXTUREGROUP_WeaponNormalMap )
	{
		// Change from default to normal map.
		if( CompressionSettings == TC_Default )
		{
			CompressionSettings = TC_Normalmap;
		}
		bIsNormalMapLODGroup = true;
	}

	// Propagate options.
	Texture->CompressionSettings	= CompressionSettings;

	// Packed normal map
	if( Texture->IsNormalMap() )
	{
		Texture->SRGB = 0;
		if( !bIsNormalMapLODGroup )
		{
			LODGroup = TEXTUREGROUP_WorldNormalMap;
		}
	}

	if(!FCString::Stricmp(Type, TEXT("ies")))
	{
		LODGroup = TEXTUREGROUP_IESLightProfile;
	}

	Texture->LODGroup				= LODGroup;

	// Revert the LODGroup to the default if it was forcibly set by the texture being a normal map.
	// This handles the case where multiple textures are being imported consecutively and
	// LODGroup unexpectedly changes because some textures were normal maps and others weren't.
	if ( LODGroup == TEXTUREGROUP_WorldNormalMap && !bIsNormalMapLODGroup )
	{
		LODGroup = TEXTUREGROUP_World;
	}

	Texture->CompressionNone		= NoCompression;
	Texture->CompressionNoAlpha		= NoAlpha;
	Texture->DeferCompression		= bDeferCompression;
	Texture->bDitherMipMapAlpha		= bDitherMipMapAlpha;
	Texture->AlphaCoverageThresholds = AlphaCoverageThresholds;
	
	if(Texture->MipGenSettings == TMGS_FromTextureGroup)
	{
		// unless the loader suggest a different setting
		Texture->MipGenSettings = MipGenSettings;
	}
	
	Texture->bPreserveBorder		= bPreserveBorder;

	UTexture2D* Texture2D = Cast<UTexture2D>(Texture);

	// Restore user set options
	if (ExistingTexture && bUsingExistingSettings)
	{
		if(Texture2D)
		{
			Texture2D->AddressX		= ExistingAddressX;
			Texture2D->AddressY		= ExistingAddressY;
		}

		Texture->Filter			= ExistingFilter;
		Texture->LODGroup		= ExistingLODGroup;
		Texture->CompressionSettings = ExistingCompressionSettings;
		Texture->LODBias		= ExistingLODBias;
		Texture->NumCinematicMipLevels = ExistingNumCinematicMipLevels;
		Texture->NeverStream	= ExistingNeverStream;
		Texture->SRGB			= ExistingSRGB;
		Texture->bPreserveBorder = ExistingPreserveBorder;
		Texture->CompressionNone = ExistingNoCompression;
		Texture->CompressionNoAlpha = ExistingNoAlpha;
		Texture->DeferCompression = ExistingDeferCompression;
		Texture->bDitherMipMapAlpha = ExistingDitherMipMapAlpha;
		Texture->AlphaCoverageThresholds = ExistingAlphaCoverageThresholds;
		Texture->bFlipGreenChannel = ExistingFlipGreenChannel;
		Texture->AdjustBrightness = ExistingAdjustBrightness;
		Texture->AdjustBrightnessCurve = ExistingAdjustBrightnessCurve;
		Texture->AdjustVibrance = ExistingAdjustVibrance;
		Texture->AdjustSaturation = ExistingAdjustSaturation;
		Texture->AdjustRGBCurve = ExistingAdjustRGBCurve;
		Texture->AdjustHue = ExistingAdjustHue;
		Texture->AdjustMinAlpha = ExistingAdjustMinAlpha;
		Texture->AdjustMaxAlpha = ExistingAdjustMaxAlpha;
		Texture->MipGenSettings = ExistingMipGenSettings;
		Texture->VirtualTextureStreaming = ExistingVirtualTextureStreaming;
	}
	else
	{
		Texture->bFlipGreenChannel = (bFlipNormalMapGreenChannel && Texture->IsNormalMap());
		// save user option
		GConfig->SetBool( TEXT("/Script/UnrealEd.EditorEngine"), TEXT("FlipNormalMapGreenChannel"), bFlipNormalMapGreenChannel, GEngineIni );
	}

	if(Texture2D)
	{
		// The texture has been imported and has no editor specific changes applied so we clear the painted flag.
		Texture2D->bHasBeenPaintedInEditor = false;

		// If the texture is larger than a certain threshold make it VT. This is explicitly done after the
		// application of the existing settings above, so if a texture gets reimported at a larger size it will
		// still be properly flagged as a VT (note: What about reimporting at a lower resolution?)
		static const auto CVarVirtualTexturesEnabled = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.VirtualTextures")); check(CVarVirtualTexturesEnabled);
		
		if (CVarVirtualTexturesEnabled->GetValueOnAnyThread())
		{
			int virtualTextureAutoEnableThreshold = GetDefault<UTextureImportSettings>()->AutoVTSize;
			int virtualTextureAutoEnableThresholdPixels = virtualTextureAutoEnableThreshold * virtualTextureAutoEnableThreshold;

			// We do this in pixels so a 8192 x 128 texture won't get VT enabled 
			// We use the Source size instead of simple Texture2D->GetSizeX() as this uses the size of the platform data
			// however for a new texture platform data may not be generated yet, and for an reimport of a texture this is the size of the
			// old texture. 
			// Using source size gives one small caveat. It looks at the size before mipmap power of two padding adjustment.
			// Textures with more than 1 block (UDIM textures) must be imported as VT
			if (Texture->Source.GetNumBlocks() > 1 ||
				Texture2D->Source.GetSizeX()*Texture2D->Source.GetSizeY() >= virtualTextureAutoEnableThresholdPixels)
			{
				Texture2D->VirtualTextureStreaming = true;
			}
		}
	}

	// Automatically detect if the texture is a normal map and configure its properties accordingly
	NormalMapIdentification::HandleAssetPostImport(this, Texture);

	if(IsAutomatedImport())
	{
		// Apply Auto import settings 
		// Should be applied before post edit change
		ApplyAutoImportSettings(Texture);
	}

	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, Texture);

	// Invalidate any materials using the newly imported texture. (occurs if you import over an existing texture)
	Texture->PostEditChange();

	// Invalidate any volume texture that was built on this texture.
	if (Texture2D)
	{
		for (TObjectIterator<UVolumeTexture> It; It; ++It)
		{
			UVolumeTexture* VolumeTexture = *It;
			if (VolumeTexture && VolumeTexture->Source2DTexture == Texture2D)
			{
				VolumeTexture->UpdateSourceFromSourceTexture();
				VolumeTexture->UpdateResource();
			}
		}
	}

	// If we are automatically creating a material for this texture...
	if( bCreateMaterial )
	{
		// Create the package for the material
		const FString MaterialName = FString::Printf( TEXT("%s_Mat"), *TextureName.ToString() );
		const FString MaterialPackageName = FPackageName::GetLongPackagePath(InParent->GetName()) + TEXT("/") + MaterialName;
		UPackage* MaterialPackage = CreatePackage(nullptr, *MaterialPackageName);

		// Create the material
		UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
		UMaterial* Material = (UMaterial*)Factory->FactoryCreateNew( UMaterial::StaticClass(), MaterialPackage, *MaterialName, Flags, Context, Warn );

		// Notify the asset registry
		FAssetRegistryModule::AssetCreated(Material);

		// Create a texture reference for the texture we just imported and hook it up to the diffuse channel
		UMaterialExpression* Expression = NewObject<UMaterialExpression>(Material, UMaterialExpressionTextureSample::StaticClass());
		Material->Expressions.Add( Expression );
		TArray<FExpressionOutput> Outputs;

		// If the user hasn't turned on any of the link checkboxes, default "bRGBToBaseColor" to being on.
		if( !bRGBToBaseColor && !bRGBToEmissive && !bAlphaToRoughness && !bAlphaToEmissive && !bAlphaToOpacity && !bAlphaToOpacityMask )
		{
			bRGBToBaseColor = 1;
		}

		// Set up the links the user asked for
		if( bRGBToBaseColor )
		{
			Material->BaseColor.Expression = Expression;
			((UMaterialExpressionTextureSample*)Material->BaseColor.Expression)->Texture = Texture;

			Outputs = Material->BaseColor.Expression->GetOutputs();
			FExpressionOutput* Output = Outputs.GetData();
			Material->BaseColor.Mask = Output->Mask;
			Material->BaseColor.MaskR = Output->MaskR;
			Material->BaseColor.MaskG = Output->MaskG;
			Material->BaseColor.MaskB = Output->MaskB;
			Material->BaseColor.MaskA = Output->MaskA;
		}

		if( bRGBToEmissive )
		{
			Material->EmissiveColor.Expression = Expression;
			((UMaterialExpressionTextureSample*)Material->EmissiveColor.Expression)->Texture = Texture;

			Outputs = Material->EmissiveColor.Expression->GetOutputs();
			FExpressionOutput* Output = Outputs.GetData();
			Material->EmissiveColor.Mask = Output->Mask;
			Material->EmissiveColor.MaskR = Output->MaskR;
			Material->EmissiveColor.MaskG = Output->MaskG;
			Material->EmissiveColor.MaskB = Output->MaskB;
			Material->EmissiveColor.MaskA = Output->MaskA;
		}

		if( bAlphaToRoughness )
		{
			Material->Roughness.Expression = Expression;
			((UMaterialExpressionTextureSample*)Material->Roughness.Expression)->Texture = Texture;

			Outputs = Material->Roughness.Expression->GetOutputs();
			FExpressionOutput* Output = Outputs.GetData();
			Material->Roughness.Mask = Output->Mask;
			Material->Roughness.MaskR = 0;
			Material->Roughness.MaskG = 0;
			Material->Roughness.MaskB = 0;
			Material->Roughness.MaskA = 1;
		}

		if( bAlphaToEmissive )
		{
			Material->EmissiveColor.Expression = Expression;
			((UMaterialExpressionTextureSample*)Material->EmissiveColor.Expression)->Texture = Texture;

			Outputs = Material->EmissiveColor.Expression->GetOutputs();
			FExpressionOutput* Output = Outputs.GetData();
			Material->EmissiveColor.Mask = Output->Mask;
			Material->EmissiveColor.MaskR = 0;
			Material->EmissiveColor.MaskG = 0;
			Material->EmissiveColor.MaskB = 0;
			Material->EmissiveColor.MaskA = 1;
		}

		if( bAlphaToOpacity )
		{
			Material->Opacity.Expression = Expression;
			((UMaterialExpressionTextureSample*)Material->Opacity.Expression)->Texture = Texture;

			Outputs = Material->Opacity.Expression->GetOutputs();
			FExpressionOutput* Output = Outputs.GetData();
			Material->Opacity.Mask = Output->Mask;
			Material->Opacity.MaskR = 0;
			Material->Opacity.MaskG = 0;
			Material->Opacity.MaskB = 0;
			Material->Opacity.MaskA = 1;
		}

		if( bAlphaToOpacityMask )
		{
			Material->OpacityMask.Expression = Expression;
			((UMaterialExpressionTextureSample*)Material->OpacityMask.Expression)->Texture = Texture;

			Outputs = Material->OpacityMask.Expression->GetOutputs();
			FExpressionOutput* Output = Outputs.GetData();
			Material->OpacityMask.Mask = Output->Mask;
			Material->OpacityMask.MaskR = 0;
			Material->OpacityMask.MaskG = 0;
			Material->OpacityMask.MaskB = 0;
			Material->OpacityMask.MaskA = 1;
		}

		Material->TwoSided	= bTwoSided;
		Material->BlendMode = Blending;
		Material->SetShadingModel(ShadingModel);

		Material->PostEditChange();
	}
	return Texture;
}

void UTextureFactory::ApplyAutoImportSettings(UTexture* Texture)
{
	if ( AutomatedImportSettings.IsValid() )
	{
		FJsonObjectConverter::JsonObjectToUStruct(AutomatedImportSettings.ToSharedRef(), Texture->GetClass(), Texture, 0, CPF_InstancedReference);
	}
}

bool UTextureFactory::IsImportResolutionValid(int32 Width, int32 Height, bool bAllowNonPowerOfTwo, FFeedbackContext* Warn)
{
	static const auto CVarVirtualTexturesEnabled = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.VirtualTextures")); check(CVarVirtualTexturesEnabled);

	// In theory this value could be much higher, but various UE4 image code currently uses 32bit size/offset values
	const int32 MaximumSupportedVirtualTextureResolution = 16 * 1024;

	// Calculate the maximum supported resolution utilizing the global max texture mip count
	// (Note, have to subtract 1 because 1x1 is a valid mip-size; this means a GMaxTextureMipCount of 4 means a max resolution of 8x8, not 2^4 = 16x16)
	const int32 MaximumSupportedResolution = CVarVirtualTexturesEnabled->GetValueOnAnyThread() ? MaximumSupportedVirtualTextureResolution : (1 << (GMaxTextureMipCount - 1));

	bool bValid = true;

	// Check if the texture is above the supported resolution and prompt the user if they wish to continue if it is
	if ( Width > MaximumSupportedResolution || Height > MaximumSupportedResolution )
	{
		if ( EAppReturnType::Yes != FMessageDialog::Open( EAppMsgType::YesNo, FText::Format(
				NSLOCTEXT("UnrealEd", "Warning_LargeTextureImport", "Attempting to import {0} x {1} texture, proceed?\nLargest supported texture size: {2} x {3}"),
				FText::AsNumber(Width), FText::AsNumber(Height), FText::AsNumber(MaximumSupportedResolution), FText::AsNumber(MaximumSupportedResolution)) ) )
		{
			bValid = false;
		}

		if (bValid && (Width * Height) > FMath::Square(MaximumSupportedVirtualTextureResolution))
		{
			Warn->Log(ELogVerbosity::Error, *NSLOCTEXT("UnrealEd", "Warning_TextureSizeTooLarge", "Texture is too large to import").ToString());
			bValid = false;
		}
	}

	const bool bIsPowerOfTwo = FMath::IsPowerOfTwo( Width ) && FMath::IsPowerOfTwo( Height );
	// Check if the texture dimensions are powers of two
	if ( !bAllowNonPowerOfTwo && !bIsPowerOfTwo )
	{
		Warn->Log(ELogVerbosity::Error, *NSLOCTEXT("UnrealEd", "Warning_TextureNotAPowerOfTwo", "Cannot import texture with non-power of two dimensions").ToString() );
		bValid = false;
	}
	
	return bValid;
}

IImportSettingsParser* UTextureFactory::GetImportSettingsParser()
{
	return this;
}

void UTextureFactory::ParseFromJson(TSharedRef<class FJsonObject> ImportSettingsJson)
{
	// Store these settings to be applied to the texture later
	AutomatedImportSettings = ImportSettingsJson;

	// Try to apply any import time options now 
	FJsonObjectConverter::JsonObjectToUStruct(ImportSettingsJson, GetClass(), this, 0, CPF_InstancedReference);
}

/*------------------------------------------------------------------------------
	UTextureExporterPCX implementation.
------------------------------------------------------------------------------*/
UTextureExporterPCX::UTextureExporterPCX(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UTexture2D::StaticClass();
	PreferredFormatIndex = 0;
	FormatExtension.Add(TEXT("PCX"));
	FormatDescription.Add(TEXT("PCX File"));

}

bool UTextureExporterPCX::SupportsObject(UObject* Object) const
{
	bool bSupportsObject = false;
	if (Super::SupportsObject(Object))
	{
		UTexture2D* Texture = Cast<UTexture2D>(Object);

		if (Texture)
		{
			bSupportsObject = Texture->Source.GetFormat() == TSF_BGRA8;
		}
	}
	return bSupportsObject;
}

bool UTextureExporterPCX::ExportBinary( UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags )
{
	UTexture2D* Texture = CastChecked<UTexture2D>( Object );

	if( !Texture->Source.IsValid() || Texture->Source.GetFormat() != TSF_BGRA8 )
	{
		return false;
	}

	int32 SizeX = Texture->Source.GetSizeX();
	int32 SizeY = Texture->Source.GetSizeY();
	TArray64<uint8> RawData;
	Texture->Source.GetMipData(RawData, 0);

	// Set all PCX file header properties.
	FPCXFileHeader PCX;
	FMemory::Memzero( &PCX, sizeof(PCX) );
	PCX.Manufacturer	= 10;
	PCX.Version			= 05;
	PCX.Encoding		= 1;
	PCX.BitsPerPixel	= 8;
	PCX.XMin			= 0;
	PCX.YMin			= 0;
	PCX.XMax			= SizeX-1;
	PCX.YMax			= SizeY-1;
	PCX.XDotsPerInch	= SizeX;
	PCX.YDotsPerInch	= SizeY;
	PCX.BytesPerLine	= SizeX;
	PCX.PaletteType		= 0;
	PCX.HScreenSize		= 0;
	PCX.VScreenSize		= 0;

	// Copy all RLE bytes.
	uint8 RleCode=0xc1;

	PCX.NumPlanes = 3;
	Ar << PCX;
	for( int32 Line=0; Line<SizeY; Line++ )
	{
		for( int32 ColorPlane = 2; ColorPlane >= 0; ColorPlane-- )
		{
			uint8* ScreenPtr = RawData.GetData() + (Line * SizeX * 4) + ColorPlane;
			for( int32 Row=0; Row<SizeX; Row++ )
			{
				if( (*ScreenPtr&0xc0)==0xc0 )
					Ar << RleCode;
				Ar << *ScreenPtr;
				ScreenPtr += 4;
			}
		}
	}

	return true;
}

/*------------------------------------------------------------------------------
	UTextureExporterBMP implementation.
------------------------------------------------------------------------------*/
UTextureExporterBMP::UTextureExporterBMP(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UTexture2D::StaticClass();
	PreferredFormatIndex = 0;
	FormatExtension.Add(TEXT("BMP"));
	FormatDescription.Add(TEXT("Windows Bitmap"));

}

bool UTextureExporterBMP::SupportsObject(UObject* Object) const
{
	bool bSupportsObject = false;
	if (Super::SupportsObject(Object))
	{
		UTexture2D* Texture = Cast<UTexture2D>(Object);

		if (Texture)
		{
			bSupportsObject = Texture->Source.GetFormat() == TSF_BGRA8 || Texture->Source.GetFormat() == TSF_RGBA16;
		}
	}
	return bSupportsObject;
}

bool UTextureExporterBMP::ExportBinary( UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags )
{
	UTexture2D* Texture = CastChecked<UTexture2D>( Object );

	if( !Texture->Source.IsValid() || ( Texture->Source.GetFormat() != TSF_BGRA8 && Texture->Source.GetFormat() != TSF_RGBA16 ) )
	{
		return false;
	}

	const bool bIsRGBA16 = Texture->Source.GetFormat() == TSF_RGBA16;
	const int32 SourceBytesPerPixel = bIsRGBA16 ? 8 : 4;

	if (bIsRGBA16)
	{
		FMessageLog ExportWarning("EditorErrors");
		FFormatNamedArguments Arguments;
		Arguments.Add(TEXT("Name"), FText::FromString(Texture->GetName()));
		ExportWarning.Warning(FText::Format(LOCTEXT("BitDepthBMPWarning", "{Name}: Texture is RGBA16 and cannot be represented at such high bit depth in .bmp. Color will be scaled to RGBA8."), Arguments));
		ExportWarning.Open(EMessageSeverity::Warning);
	}

	int32 SizeX = Texture->Source.GetSizeX();
	int32 SizeY = Texture->Source.GetSizeY();
	TArray64<uint8> RawData;
	Texture->Source.GetMipData(RawData, 0);

	FBitmapFileHeader bmf;
	FBitmapInfoHeader bmhdr;

	// File header.
	bmf.bfType      = 'B' + (256*(int32)'M');
	bmf.bfReserved1 = 0;
	bmf.bfReserved2 = 0;
	int32 biSizeImage = SizeX * SizeY * 3;
	bmf.bfOffBits   = sizeof(FBitmapFileHeader) + sizeof(FBitmapInfoHeader);
	bmhdr.biBitCount = 24;

	bmf.bfSize		= bmf.bfOffBits + biSizeImage;
	Ar << bmf;

	// Info header.
	bmhdr.biSize          = sizeof(FBitmapInfoHeader);
	bmhdr.biWidth         = SizeX;
	bmhdr.biHeight        = SizeY;
	bmhdr.biPlanes        = 1;
	bmhdr.biCompression   = BCBI_RGB;
	bmhdr.biSizeImage     = biSizeImage;
	bmhdr.biXPelsPerMeter = 0;
	bmhdr.biYPelsPerMeter = 0;
	bmhdr.biClrUsed       = 0;
	bmhdr.biClrImportant  = 0;
	Ar << bmhdr;


	// Upside-down scanlines.
	for( int32 i=SizeY-1; i>=0; i-- )
	{
		uint8* ScreenPtr = &RawData[i*SizeX*SourceBytesPerPixel];
		for( int32 j=SizeX; j>0; j-- )
		{
			if (bIsRGBA16)
			{
				Ar << ScreenPtr[1];
				Ar << ScreenPtr[3];
				Ar << ScreenPtr[5];
				ScreenPtr += 8;
			}
			else
			{
				Ar << ScreenPtr[0];
				Ar << ScreenPtr[1];
				Ar << ScreenPtr[2];
				ScreenPtr += 4;
			}
		}
	}
	return true;
}

/*------------------------------------------------------------------------------
	URenderTargetExporterHDR implementation.
	Exports render targets.
------------------------------------------------------------------------------*/
URenderTargetExporterHDR::URenderTargetExporterHDR(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UTextureRenderTarget::StaticClass();
	PreferredFormatIndex = 0;
	FormatExtension.Add(TEXT("HDR"));
	FormatDescription.Add(TEXT("HDR"));
}

bool URenderTargetExporterHDR::ExportBinary(UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags)
{
	UTextureRenderTarget2D* TexRT2D = Cast<UTextureRenderTarget2D>(Object);
	UTextureRenderTargetCube* TexRTCube = Cast<UTextureRenderTargetCube>(Object);

	if (TexRT2D != nullptr)
	{
		return FImageUtils::ExportRenderTarget2DAsHDR(TexRT2D, Ar);
	}
	else if (TexRTCube != nullptr)
	{
		return FImageUtils::ExportRenderTargetCubeAsHDR(TexRTCube, Ar);
	}
	return false;
}

/*------------------------------------------------------------------------------
	UTextureCubeExporterHDR implementation.
	Export UTextureCubes as .HDR
------------------------------------------------------------------------------*/
UTextureCubeExporterHDR::UTextureCubeExporterHDR(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UTextureCube::StaticClass();
	PreferredFormatIndex = 0;
	FormatExtension.Add(TEXT("HDR"));
	FormatDescription.Add(TEXT("HDR"));
}

bool UTextureCubeExporterHDR::ExportBinary(UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags)
{
	UTextureCube* TexCube = Cast<UTextureCube>(Object);

	if (TexCube != nullptr)
	{
		return FImageUtils::ExportTextureCubeAsHDR(TexCube, Ar);
	}
	return false;
}

/*------------------------------------------------------------------------------
	UTextureExporterHDR implementation.
	Export UTexture2D as .HDR
------------------------------------------------------------------------------*/
UTextureExporterHDR::UTextureExporterHDR(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UTexture2D::StaticClass();
	PreferredFormatIndex = 0;
	FormatExtension.Add(TEXT("HDR"));
	FormatDescription.Add(TEXT("HDR"));
}

bool UTextureExporterHDR::SupportsObject(UObject* Object) const
{
	bool bSupportsObject = false;
	if (Super::SupportsObject(Object))
	{
		UTexture2D* Texture = Cast<UTexture2D>(Object);

		if (Texture)
		{
			bSupportsObject = Texture->Source.GetFormat() == TSF_BGRA8 || Texture->Source.GetFormat() == TSF_RGBA16F;
		}
	}
	return bSupportsObject;
}

bool UTextureExporterHDR::ExportBinary(UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags)
{
	UTexture2D* Texture = Cast<UTexture2D>(Object);

	if (Texture != nullptr)
	{
		return FImageUtils::ExportTexture2DAsHDR(Texture, Ar);
	}
	return false;
}

/*------------------------------------------------------------------------------
	UTextureExporterTGA implementation.
------------------------------------------------------------------------------*/
UTextureExporterTGA::UTextureExporterTGA(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	SupportedClass = UTexture2D::StaticClass();
	PreferredFormatIndex = 0;
	FormatExtension.Add(TEXT("TGA"));
	FormatDescription.Add(TEXT("Targa"));
}

bool UTextureExporterTGA::SupportsObject(UObject* Object) const
{
	bool bSupportsObject = false;
	if (Super::SupportsObject(Object))
	{
		UTexture2D* Texture = Cast<UTexture2D>(Object);

		if (Texture)
		{
			bSupportsObject = Texture->Source.GetFormat() == TSF_BGRA8 || Texture->Source.GetFormat() == TSF_RGBA16;
		}
	}
	return bSupportsObject;
}

bool UTextureExporterTGA::ExportBinary( UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags )
{
	UTexture2D* Texture = CastChecked<UTexture2D>( Object );

	if (!Texture->Source.IsValid() || (Texture->Source.GetFormat() != TSF_BGRA8 && Texture->Source.GetFormat() != TSF_RGBA16))
	{
		return false;
	}

	const bool bIsRGBA16 = Texture->Source.GetFormat() == TSF_RGBA16;

	if (bIsRGBA16)
	{
		FMessageLog ExportWarning("EditorErrors");
		FFormatNamedArguments Arguments;
		Arguments.Add(TEXT("Name"), FText::FromString(Texture->GetName()));
		ExportWarning.Warning(FText::Format(LOCTEXT("BitDepthTGAWarning", "{Name}: Texture is RGBA16 and cannot be represented at such high bit depth in .tga. Color will be scaled to RGBA8."), Arguments));
		ExportWarning.Open(EMessageSeverity::Warning);
	}

	const int32 BytesPerPixel = bIsRGBA16 ? 8 : 4;

	int32 SizeX = Texture->Source.GetSizeX();
	int32 SizeY = Texture->Source.GetSizeY();
	TArray64<uint8> RawData;
	Texture->Source.GetMipData(RawData, 0);

	// If we should export the file with no alpha info.  
	// If the texture is compressed with no alpha we should definitely not export an alpha channel
	bool bExportWithAlpha = !Texture->CompressionNoAlpha;
	if( bExportWithAlpha )
	{
		// If the texture isn't compressed with no alpha scan the texture to see if the alpha values are all 255 which means we can skip exporting it.
		// This is a relatively slow process but we are just exporting textures 
		bExportWithAlpha = false;
		const int32 AlphaOffset = bIsRGBA16 ? 7 : 3;
		for( int32 Y = SizeY - 1; Y >= 0; --Y )
		{
			uint8* Color = &RawData[Y * SizeX * BytesPerPixel];
			for( int32 X = SizeX; X > 0; --X )
			{
				// Skip color info
				Color += AlphaOffset;
				// Get Alpha value then increment the pointer past it for the next pixel
				uint8 Alpha = *Color++;
				if( Alpha != 255 )
				{
					// When a texture is imported with no alpha, the alpha bits are set to 255
					// So if the texture has non 255 alpha values, the texture is a valid alpha channel
					bExportWithAlpha = true;
					break;
				}
			}
			if( bExportWithAlpha )
			{
				break;
			}
		}
	}

	const int32 OriginalWidth = SizeX;
	const int32 OriginalHeight = SizeY;		

	FTGAFileHeader TGA;
	FMemory::Memzero( &TGA, sizeof(TGA) );
	TGA.ImageTypeCode = 2;
	TGA.BitsPerPixel = bExportWithAlpha ? 32 : 24 ;
	TGA.Height = OriginalHeight;
	TGA.Width = OriginalWidth;
	Ar.Serialize( &TGA, sizeof(TGA) );

	if( bExportWithAlpha && !bIsRGBA16)
	{
		for( int32 Y=0;Y < OriginalHeight;Y++ )
		{
			// If we aren't skipping alpha channels we can serialize each line
			Ar.Serialize( &RawData[ (OriginalHeight - Y - 1) * OriginalWidth * 4 ], OriginalWidth * 4 );
		}
	}
	else
	{
		// Serialize each pixel
		for( int32 Y = OriginalHeight - 1; Y >= 0; --Y )
		{
			uint8* Color = &RawData[Y * OriginalWidth * BytesPerPixel];
			for( int32 X = OriginalWidth; X > 0; --X )
			{
				if (bIsRGBA16)
				{
					Ar << Color[1];
					Ar << Color[3];
					Ar << Color[5];
					if (bExportWithAlpha)
					{
						Ar << Color[7];
					}
					Color += 8;
				}
				else
				{
					Ar << *Color++;
					Ar << *Color++;
					Ar << *Color++;
					// Skip alpha channel since we are exporting with no alpha
					Color++;
				}
			}
		}
	}

	FTGAFileFooter Ftr;
	FMemory::Memzero( &Ftr, sizeof(Ftr) );
	FMemory::Memcpy( Ftr.Signature, "TRUEVISION-XFILE", 16 );
	Ftr.TrailingPeriod = '.';
	Ar.Serialize( &Ftr, sizeof(Ftr) );
	return true;
}

/*------------------------------------------------------------------------------
	UFontFactory.
------------------------------------------------------------------------------*/

UFontFactory::UFontFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UFont::StaticClass();

	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UFontFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, UObject* InContext, FFeedbackContext* InWarn)
{
	UFont* const Font = NewObject<UFont>(InParent, InClass, InName, InFlags);
	if(Font)
	{
		Font->FontCacheType = EFontCacheType::Runtime;
	}
	return Font;
}

/*------------------------------------------------------------------------------
	UFontFileImportFactory.
------------------------------------------------------------------------------*/

UFontFileImportFactory::UFontFileImportFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UFontFace::StaticClass();

	bEditorImport = true;

	Formats.Add(TEXT("ttf;TrueType Font"));
	Formats.Add(TEXT("ttc;TrueType Font"));
	Formats.Add(TEXT("otf;OpenType Font"));
	Formats.Add(TEXT("otc;OpenType Font"));
	
	BatchCreateFontAsset = EBatchCreateFontAsset::Unknown;
}

bool UFontFileImportFactory::ConfigureProperties()
{
	BatchCreateFontAsset = EBatchCreateFontAsset::Unknown;
	return true;
}

UObject* UFontFileImportFactory::FactoryCreateBinary(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, UObject* InContext, const TCHAR* InType, const uint8*& InBuffer, const uint8* InBufferEnd, FFeedbackContext* InWarn)
{
	// Should we create a font asset alongside our font face?
	bool bCreateFontAsset = false;
	{
		const bool bIsAutomated = IsAutomatedImport();
		const bool bShowImportDialog = BatchCreateFontAsset == EBatchCreateFontAsset::Unknown && !bIsAutomated;
		if (bShowImportDialog)
		{
			const FText DlgTitle = LOCTEXT("ImportFont_OptionsDlgTitle", "Font Face Import Options");
			const FText DlgMsg = LOCTEXT("ImportFont_OptionsDlgMsg", "Would you like to create a new Font asset using the imported Font Face as its default font?");
			switch (FMessageDialog::Open(EAppMsgType::YesNoYesAllNoAllCancel, DlgMsg, &DlgTitle))
			{
			case EAppReturnType::Yes:
				bCreateFontAsset = true;
				break;
			case EAppReturnType::YesAll:
				bCreateFontAsset = true;
				BatchCreateFontAsset = EBatchCreateFontAsset::Yes;
				break;
			case EAppReturnType::No:
				break;
			case EAppReturnType::NoAll:
				BatchCreateFontAsset = EBatchCreateFontAsset::No;
				break;
			default:
				BatchCreateFontAsset = EBatchCreateFontAsset::Cancel;
				break;
			}
		}
		else
		{
			bCreateFontAsset = BatchCreateFontAsset == EBatchCreateFontAsset::Yes;
		}
	}

	if (BatchCreateFontAsset == EBatchCreateFontAsset::Cancel)
	{
		return nullptr;
	}

	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPreImport(this, InClass, InParent, InName, InType);

	// Create the font face
	UFontFace* const FontFace = NewObject<UFontFace>(InParent, InClass, InName, InFlags);
	if (FontFace)
	{
		FontFace->SourceFilename = GetCurrentFilename();

		TArray<uint8> FontData;
		FontData.Append(InBuffer, InBufferEnd - InBuffer);
		FontFace->FontFaceData->SetData(MoveTemp(FontData));
		FontFace->CacheSubFaces();
	}

	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, FontFace);
	
	// Create the font (if requested)
	if (FontFace && bCreateFontAsset)
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

		FString FontPackageName;
		FString FontAssetName;
		AssetToolsModule.Get().CreateUniqueAssetName(FString::Printf(TEXT("%s/%s_Font"), *FPackageName::GetLongPackagePath(InParent->GetOutermost()->GetName()), *InName.ToString()), FString(), FontPackageName, FontAssetName);

		UFontFactory* FontFactory = NewObject<UFontFactory>();
		FontFactory->bEditAfterNew = false;

		UPackage* FontPackage = CreatePackage(nullptr, *FontPackageName);
		UFont* Font = Cast<UFont>(FontFactory->FactoryCreateNew(UFont::StaticClass(), FontPackage, *FontAssetName, InFlags, InContext, InWarn));
		if (Font)
		{
			Font->FontCacheType = EFontCacheType::Runtime;

			// Add a default typeface referencing the newly created font face
			FTypefaceEntry& DefaultTypefaceEntry = Font->CompositeFont.DefaultTypeface.Fonts[Font->CompositeFont.DefaultTypeface.Fonts.AddDefaulted()];
			DefaultTypefaceEntry.Name = "Default";
			DefaultTypefaceEntry.Font = FFontData(FontFace);

			FAssetRegistryModule::AssetCreated(Font);
			FontPackage->MarkPackageDirty();
		}
	}

	return FontFace;
}

bool UFontFileImportFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	UFontFace* FontFaceToReimport = Cast<UFontFace>(Obj);
	if (FontFaceToReimport)
	{
		OutFilenames.Add(FontFaceToReimport->SourceFilename);
		return true;
	}
	return false;
}

void UFontFileImportFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{
	UFontFace* FontFaceToReimport = Cast<UFontFace>(Obj);
	if (FontFaceToReimport && ensure(NewReimportPaths.Num() == 1))
	{
		FontFaceToReimport->SourceFilename = NewReimportPaths[0];
	}
}

EReimportResult::Type UFontFileImportFactory::Reimport(UObject* InObject)
{
	UFontFace* FontFaceToReimport = Cast<UFontFace>(InObject);

	if (!FontFaceToReimport)
	{
		return EReimportResult::Failed;
	}

	if (FontFaceToReimport->SourceFilename.IsEmpty() || !FPaths::FileExists(FontFaceToReimport->SourceFilename))
	{
		return EReimportResult::Failed;
	}

	// Never create font assets when reimporting
	BatchCreateFontAsset = EBatchCreateFontAsset::No;

	bool OutCanceled = false;
	if (ImportObject(InObject->GetClass(), InObject->GetOuter(), *InObject->GetName(), RF_Public | RF_Standalone, FontFaceToReimport->SourceFilename, nullptr, OutCanceled))
	{
		return EReimportResult::Succeeded;
	}

	return OutCanceled ? EReimportResult::Cancelled : EReimportResult::Failed;
}

int32 UFontFileImportFactory::GetPriority() const
{
	return ImportPriority;
}

/*------------------------------------------------------------------------------
FCustomizableTextObjectFactory implementation.
------------------------------------------------------------------------------*/

// Util to ensure that InName is a valid name for a new object within InParent. Will rename any existing object within InParent if it is called InName.
void FCustomizableTextObjectFactory::ClearObjectNameUsage(UObject* InParent, FName InName)
{
	// Make sure this name is unique within the scope of InParent.
	UObject* Found=nullptr;
	if( (InName != NAME_None) && (InParent != nullptr) )
	{
		Found = FindObject<UObject>( InParent, *InName.ToString() );
	}

	// If there is already another object in the same scope with this name, rename it.
	while (Found)
	{
		check(Found->GetOuter() == InParent);

		Found->Rename(nullptr, nullptr, REN_DontCreateRedirectors);

		// It's possible after undo for there to be multiple objects with the same name in the way, rename all of them
		Found = FindObject<UObject>(InParent, *InName.ToString());
	}
}

// Constructor for the factory; takes a context for emitting warnings such as GWarn
FCustomizableTextObjectFactory::FCustomizableTextObjectFactory(FFeedbackContext* InWarningContext)
	: WarningContext(InWarningContext)
{
}

// Parses a text buffer and factories objects from it, subject to the restrictions imposed by CanCreateClass()
void FCustomizableTextObjectFactory::ProcessBuffer(UObject* InParent, EObjectFlags Flags, const FString& TextBuffer)
{
	ProcessBuffer(InParent, Flags, *TextBuffer);
}

void FCustomizableTextObjectFactory::ProcessBuffer(UObject* InParent, EObjectFlags Flags, const TCHAR* Buffer)
{
	// We keep a mapping of new, empty sequence objects to their property text.
	// We want to create all new SequenceObjects first before importing their properties (which will create links)
	TArray<UObject*>			NewObjects;
	TMap<UObject*,FString>		PropMap;

	FParse::Next( &Buffer );

	int32 NestedDepth     = 0;
	int32 OmittedOuterObj = 0; // zero signifies "nothing omitted"

	FString StrLine;
	while( FParse::Line(&Buffer,StrLine) )
	{
		const TCHAR* Str = *StrLine;
		if( GetBEGIN(&Str, TEXT("OBJECT")) || (NestedDepth == 0 && GetBEGIN(&Str, TEXT("ACTOR"))) )
		{
			++NestedDepth;
			if (OmittedOuterObj > 0)
			{
				if (NestedDepth > OmittedOuterObj)
				{
					continue;
				}
				ensure(OmittedOuterObj == NestedDepth);
				// clear the omitted outer, we've parsed passed it
				OmittedOuterObj = 0;
			}

			UClass* ObjClass;
			if( ParseObject<UClass>( Str, TEXT("CLASS="), ObjClass, ANY_PACKAGE ) )
			{
				bool bOmitSubObjects = false;
				if (!CanCreateClass(ObjClass, bOmitSubObjects))
				{
					if (bOmitSubObjects)
					{
						OmittedOuterObj = NestedDepth;
					}
					continue;
				}

				FName ObjName(NAME_None);
				FParse::Value( Str, TEXT("NAME="), ObjName );

				// Setup archetype
				UObject* ObjArchetype = nullptr;
				FString ObjArchetypeName;
				if (FParse::Value(Str, TEXT("ARCHETYPE="), ObjArchetypeName))
				{
					ObjArchetype = LoadObject<UObject>(nullptr, *ObjArchetypeName, nullptr, LOAD_None, nullptr);
				}

				UObject* ObjectParent = InParent ? InParent : GetParentForNewObject(ObjClass);

				// Make sure this name is not used by anything else. Will rename other stuff if necessary
				ClearObjectNameUsage(ObjectParent, ObjName);

				// Spawn the object and reset it's archetype
				UObject* CreatedObject = NewObject<UObject>(ObjectParent, ObjClass, ObjName, Flags, ObjArchetype, !!ObjectParent, &InstanceGraph);

				// Get property text for the new object.
				FString PropText, PropLine;
				FString SubObjText;
				int32 ObjDepth = 1;
				while ( FParse::Line( &Buffer, PropLine ) )
				{
					const TCHAR* PropStr = *PropLine;

					// Track how deep we are in contained sets of sub-objects.
					bool bEndLine = false;
					if( GetBEGIN(&PropStr, TEXT("OBJECT")) )
					{
						ObjDepth++;
					}
					else if( GetEND(&PropStr, TEXT("OBJECT")) || (ObjDepth == 1 && GetEND(&PropStr, TEXT("ACTOR"))) )
					{
						bEndLine = true;

						// When close out our initial BEGIN OBJECT, we are done with this object.
						if(ObjDepth == 1)
						{
							break;
						}
					}

					PropText += *PropLine;
					PropText += TEXT("\r\n");

					if(bEndLine)
					{
						ObjDepth--;
					}
				}

				// Save property text and possibly sub-object text.
				PropMap.Add(CreatedObject, *PropText);
				NewObjects.Add(CreatedObject);
			}
		}
		else if (GetEND(&Str, TEXT("OBJECT")) || (NestedDepth == 1 && GetEND(&Str, TEXT("ACTOR"))))
		{
			--NestedDepth;
		}
		else
		{
			ProcessUnidentifiedLine(StrLine);
		}
	}

	// Apply the property text to each of the created objects
	for (int32 i = 0; i < NewObjects.Num(); ++i)
	{
		UObject* CreatedObject = NewObjects[i];
		const FString& PropText = PropMap.FindChecked(CreatedObject);

		// Import the properties and give the derived factory a shot at it
		ImportObjectProperties((uint8*)CreatedObject, *PropText, CreatedObject->GetClass(), CreatedObject, CreatedObject, WarningContext, 0, 0, &InstanceGraph);
		ProcessConstructedObject(CreatedObject);
	}
	PostProcessConstructedObjects();
}


bool FCustomizableTextObjectFactory::CanCreateObjectsFromText( const FString& TextBuffer ) const
{
	bool bCanCreate = false;

	const TCHAR* Buffer = *TextBuffer;
	const TCHAR* BufferEnd = Buffer + FCString::Strlen(Buffer);

	FParse::Next( &Buffer );

	int32 NestedDepth     = 0;
	int32 OmittedOuterObj = 0; // zero signifies "nothing omitted"

	FString StrLine;
	while( FParse::Line(&Buffer,StrLine) )
	{
		const TCHAR* Str = *StrLine;
		if( GetBEGIN(&Str,TEXT("OBJECT")) || (NestedDepth == 0 && GetBEGIN(&Str, TEXT("ACTOR"))) )
		{
			++NestedDepth;
			if (OmittedOuterObj > 0)
			{
				if (NestedDepth > OmittedOuterObj)
				{
					continue;
				}
				ensure(OmittedOuterObj == NestedDepth);
				// clear the omitted outer, we've parsed passed it
				OmittedOuterObj = 0;
			}

			UClass* ObjClass;
			if (ParseObject<UClass>(Str, TEXT("CLASS="), ObjClass, ANY_PACKAGE))
			{
				bool bOmitSubObjects = false;;
				if (CanCreateClass(ObjClass, bOmitSubObjects))
				{
					bCanCreate = true;
					break;
				}
				else if (bOmitSubObjects)
				{
					OmittedOuterObj = NestedDepth;
				}
			}
		}
		else if ( GetEND(&Str, TEXT("OBJECT")) || (NestedDepth == 1 && GetEND(&Str, TEXT("ACTOR"))) )
		{
			--NestedDepth;
		}
	}
	return bCanCreate;
}

/** Return true if the an object of type ObjectClass is allowed to be created; If false is returned, the object and subobjects will be ignored. */
bool FCustomizableTextObjectFactory::CanCreateClass(UClass* ObjectClass, bool& bOmitSubObjs) const
{
	return false;
}
		
/** This is called on each created object after PreEditChange and the property text is imported, but before PostEditChange */
void FCustomizableTextObjectFactory::ProcessConstructedObject(UObject* CreatedObject)
{
}

/*-----------------------------------------------------------------------------
UReimportTextureFactory.
-----------------------------------------------------------------------------*/
UReimportTextureFactory::UReimportTextureFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	SupportedClass = UTexture::StaticClass();

	bCreateNew = false;
}

UTexture2D* UReimportTextureFactory::CreateTexture2D( UObject* InParent, FName Name, EObjectFlags Flags )
{
	UTexture2D* pTex2D = Cast<UTexture2D>(pOriginalTex);

	if (pTex2D)
	{
		// Release the existing resource so the new texture can get a fresh one. Otherwise if the next call to Init changes the format
		// of the texture and UpdateResource is called the editor will crash in RenderThread
		pTex2D->ReleaseResource();
		return pTex2D;
	}
	else
	{
		return Super::CreateTexture2D( InParent, Name, Flags );
	}
}

UTextureCube* UReimportTextureFactory::CreateTextureCube( UObject* InParent, FName Name, EObjectFlags Flags )
{
	UTextureCube* pTexCube = Cast<UTextureCube>(pOriginalTex);
	if (pTexCube)
	{
		// Release the existing resource so the new texture can get a fresh one. Otherwise if the next call to Init changes the format
		// of the texture and UpdateResource is called the editor will crash in RenderThread
		pTexCube->ReleaseResource();
		return pTexCube;
	}
	else
	{
		return Super::CreateTextureCube( InParent, Name, Flags );
	}
}

bool UReimportTextureFactory::CanReimport( UObject* Obj, TArray<FString>& OutFilenames )
{	
	UTexture* pTex = Cast<UTexture>(Obj);
	if( pTex && !pTex->IsA<UTextureRenderTarget>() && !pTex->IsA<UCurveLinearColorAtlas>())
	{
		pTex->AssetImportData->ExtractFilenames(OutFilenames);
		return true;
	}
	return false;
}

void UReimportTextureFactory::SetReimportPaths( UObject* Obj, const TArray<FString>& NewReimportPaths )
{	
	UTexture* pTex = Cast<UTexture>(Obj);
	if(pTex && ensure(NewReimportPaths.Num() == 1))
	{
		pTex->AssetImportData->UpdateFilenameOnly(NewReimportPaths[0]);
	}
}

/**
* Reimports specified texture from its source material, if the meta-data exists
*/
EReimportResult::Type UReimportTextureFactory::Reimport( UObject* Obj )
{
	if(!Obj || !Obj->IsA(UTexture::StaticClass()))
	{
		return EReimportResult::Failed;
	}

	UTexture* pTex = Cast<UTexture>(Obj);
	
	TGuardValue<UTexture*> OriginalTexGuardValue(pOriginalTex, pTex);

	const FString ResolvedSourceFilePath = pTex->AssetImportData->GetFirstFilename();
	if (!ResolvedSourceFilePath.Len())
	{
		// Since this is a new system most textures don't have paths, so logging has been commented out
		//UE_LOG(LogEditorFactories, Warning, TEXT("-- cannot reimport: texture resource does not have path stored."));
		return EReimportResult::Failed;
	}

	UTexture2D* pTex2D = Cast<UTexture2D>(Obj);
	// Check if this texture has been modified by the paint tool.
	// If so, prompt the user to see if they'll continue with reimporting, returning if they decline.
	if( pTex2D && pTex2D->bHasBeenPaintedInEditor && EAppReturnType::Yes != FMessageDialog::Open( EAppMsgType::YesNo,
		FText::Format(NSLOCTEXT("UnrealEd", "Import_TextureHasBeenPaintedInEditor", "The texture '{0}' has been painted on by the Mesh Paint tool.\nReimporting it will override any changes.\nWould you like to continue?"),
			FText::FromString(pTex2D->GetName()))) )
	{
		return EReimportResult::Failed;
	}

	UE_LOG(LogEditorFactories, Log, TEXT("Performing atomic reimport of [%s]"),*ResolvedSourceFilePath);

	// Ensure that the file provided by the path exists
	if (IFileManager::Get().FileSize(*ResolvedSourceFilePath) == INDEX_NONE)
	{
		UE_LOG(LogEditorFactories, Warning, TEXT("-- cannot reimport: source file cannot be found."));
		return EReimportResult::Failed;
	}

	// We use this reimport factory to skip the object creation process
	// which obliterates all of the properties of the texture.
	// Also preset the factory with the settings of the current texture.
	// These will be used during the import and compression process.        
	CompressionSettings   = pTex->CompressionSettings;
	NoCompression         = pTex->CompressionNone;
	NoAlpha               = pTex->CompressionNoAlpha;
	bDeferCompression     = pTex->DeferCompression;
	MipGenSettings		  = pTex->MipGenSettings;

	float Brightness = 0.f;
	float TextureMultiplier = 1.f;

	UTextureLightProfile* pTexLightProfile = Cast<UTextureLightProfile>(Obj);
	if ( pTexLightProfile )
	{
		Brightness = pTexLightProfile->Brightness;
		TextureMultiplier = pTexLightProfile->TextureMultiplier;
	}

	// Suppress the import overwrite dialog because we know that for explicitly re-importing we want to preserve existing settings
	UTextureFactory::SuppressImportOverwriteDialog();

	bool OutCanceled = false;

	if (ImportObject(pTex->GetClass(), pTex->GetOuter(), *pTex->GetName(), RF_Public | RF_Standalone, ResolvedSourceFilePath, nullptr, OutCanceled) != nullptr)
	{
		if ( pTexLightProfile )
		{
			// We don't update the Brightness and TextureMultiplier during reimport.
			// The reason is that the IESLoader has changed and calculates these values differently.
			// Since existing lights have been calibrated, we don't want to screw with those values.
			pTexLightProfile->Brightness = Brightness;
			pTexLightProfile->TextureMultiplier = TextureMultiplier;
		}

		UE_LOG(LogEditorFactories, Log, TEXT("-- imported successfully") );

		pTex->AssetImportData->Update(ResolvedSourceFilePath);

		// Try to find the outer package so we can dirty it up
		if (pTex->GetOuter())
		{
			pTex->GetOuter()->MarkPackageDirty();
		}
		else
		{
			pTex->MarkPackageDirty();
		}
	}
	else if (OutCanceled)
	{
		UE_LOG(LogEditorFactories, Warning, TEXT("-- import canceled"));
		return EReimportResult::Cancelled;
	}
	else
	{
		UE_LOG(LogEditorFactories, Warning, TEXT("-- import failed"));
		return EReimportResult::Failed;
	}
	
	return EReimportResult::Succeeded;
}

int32 UReimportTextureFactory::GetPriority() const
{
	return ImportPriority;
}


/*-----------------------------------------------------------------------------
UReimportFbxStaticMeshFactory.
-----------------------------------------------------------------------------*/
UReimportFbxStaticMeshFactory::UReimportFbxStaticMeshFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	SupportedClass = UStaticMesh::StaticClass();
	Formats.Add(TEXT("fbx;FBX static meshes"));

	bCreateNew = false;
	bText = false;

	// Required to allow other StaticMesh re importers to do their CanReimport checks first, and if they fail the FBX will catch it
	ImportPriority = DefaultImportPriority - 1;
}

bool UReimportFbxStaticMeshFactory::FactoryCanImport(const FString& Filename)
{
	// Return false, we are a reimport only factory
	return false;
}

bool UReimportFbxStaticMeshFactory::CanReimport( UObject* Obj, TArray<FString>& OutFilenames )
{	
	UStaticMesh* Mesh = Cast<UStaticMesh>(Obj);
	if(Mesh)
	{
		if ( Mesh->AssetImportData )
		{
			UFbxAssetImportData *FbxAssetImportData = Cast<UFbxAssetImportData>(Mesh->AssetImportData);
			if (FbxAssetImportData != nullptr && FbxAssetImportData->bImportAsScene)
			{
				//This mesh was import with a scene import, we cannot reimport it
				return false;
			}

			FString FileExtension = FPaths::GetExtension(Mesh->AssetImportData->GetFirstFilename());
			const bool bIsValidFile = FileExtension.Equals(TEXT("fbx"), ESearchCase::IgnoreCase) || FileExtension.Equals("obj", ESearchCase::IgnoreCase);
			if (!bIsValidFile)
			{
				return false;
			}
			OutFilenames.Add(Mesh->AssetImportData->GetFirstFilename());
		}
		else
		{
			OutFilenames.Add(TEXT(""));
		}
		return true;
	}
	return false;
}

void UReimportFbxStaticMeshFactory::SetReimportPaths( UObject* Obj, const TArray<FString>& NewReimportPaths )
{	
	UStaticMesh* Mesh = Cast<UStaticMesh>(Obj);
	if(Mesh && ensure(NewReimportPaths.Num() == 1))
	{
		Mesh->Modify();
		UFbxStaticMeshImportData* ImportData = UFbxStaticMeshImportData::GetImportDataForStaticMesh(Mesh, ImportUI->StaticMeshImportData);

		ImportData->UpdateFilenameOnly(NewReimportPaths[0]);
	}
}

EReimportResult::Type UReimportFbxStaticMeshFactory::Reimport( UObject* Obj )
{
	UStaticMesh* Mesh = Cast<UStaticMesh>(Obj);
	if( !Mesh )
	{
		return EReimportResult::Failed;
	}
	
	UnFbx::FFbxImporter* FFbxImporter = UnFbx::FFbxImporter::GetInstance();
	UnFbx::FBXImportOptions* ImportOptions = FFbxImporter->GetImportOptions();
	
	//Pop the message log in case of error
	UnFbx::FFbxLoggerSetter Logger(FFbxImporter, true);

	//Clean up the options
	UnFbx::FBXImportOptions::ResetOptions(ImportOptions);

	UFbxStaticMeshImportData* ImportData = Cast<UFbxStaticMeshImportData>(Mesh->AssetImportData);
	
	UFbxImportUI* ReimportUI = NewObject<UFbxImportUI>();
	ReimportUI->MeshTypeToImport = FBXIT_StaticMesh;
	ReimportUI->StaticMeshImportData->bCombineMeshes = true;

	if (!ImportUI)
	{
		ImportUI = NewObject<UFbxImportUI>(this, NAME_None, RF_Public);
	}
	//Prevent any UI for automation, unattended and commandlet
	const bool IsUnattended = GIsAutomationTesting || FApp::IsUnattended() || IsRunningCommandlet() || GIsRunningUnattendedScript;
	const bool ShowImportDialogAtReimport = GetDefault<UEditorPerProjectUserSettings>()->bShowImportDialogAtReimport && !IsUnattended;

	if (ImportData == nullptr)
	{
		// An existing import data object was not found, make one here and show the options dialog
		ImportData = UFbxStaticMeshImportData::GetImportDataForStaticMesh(Mesh, ImportUI->StaticMeshImportData);
		Mesh->AssetImportData = ImportData;
	}

	//Get the re-import filename
	const FString Filename = ImportData->GetFirstFilename();
	const FString FileExtension = FPaths::GetExtension(Filename);
	const bool bIsValidFile = FileExtension.Equals(TEXT("fbx"), ESearchCase::IgnoreCase) || FileExtension.Equals("obj", ESearchCase::IgnoreCase);
	if (!bIsValidFile)
	{
		return EReimportResult::Failed;
	}
	if (!(Filename.Len()))
	{
		// Since this is a new system most static meshes don't have paths, so logging has been commented out
		//UE_LOG(LogEditorFactories, Warning, TEXT("-- cannot reimport: static mesh resource does not have path stored."));
		return EReimportResult::Failed;
	}
	// Ensure that the file provided by the path exists
	if (IFileManager::Get().FileSize(*Filename) == INDEX_NONE)
	{
		UE_LOG(LogEditorFactories, Warning, TEXT("-- cannot reimport: source file cannot be found."));
		return EReimportResult::Failed;
	}
	CurrentFilename = Filename;


	if( ImportData  && !ShowImportDialogAtReimport)
	{
		// Import data already exists, apply it to the fbx import options
		ReimportUI->StaticMeshImportData = ImportData;
		ApplyImportUIToImportOptions(ReimportUI, *ImportOptions);
	}
	else
	{
		ReimportUI->bIsReimport = true;
		ReimportUI->ReimportMesh = Mesh;

		//Make sure the outer is the ImportUI, because there is some logic in the meta data needing this outer
		UObject* OriginalOuter = ImportData != nullptr ? ImportData->GetOuter() : nullptr;
		ReimportUI->StaticMeshImportData = ImportData;
		if (ReimportUI->StaticMeshImportData && OriginalOuter)
		{
			ReimportUI->StaticMeshImportData->Rename(nullptr, ReimportUI);
		}
		
		//Force the bAutoGenerateCollision to false if the Mesh Customize collision is true
		bool bOldAutoGenerateCollision = ReimportUI->StaticMeshImportData->bAutoGenerateCollision;
		if (Mesh->bCustomizedCollision)
		{
			ReimportUI->StaticMeshImportData->bAutoGenerateCollision = false;
		}
		
		bool bImportOperationCanceled = false;
		bool bForceImportType = true;
		bool bShowOptionDialog = true;
		bool bOutImportAll = false;
		bool bIsObjFormat = false;
		bool bIsAutomated = false;

		GetImportOptions( FFbxImporter, ReimportUI, bShowOptionDialog, bIsAutomated, Obj->GetPathName(), bOperationCanceled, bOutImportAll, bIsObjFormat, Filename, bForceImportType, FBXIT_StaticMesh);
		
		//Put back the original bAutoGenerateCollision settings since the user cancel the re-import
		if (ReimportUI->StaticMeshImportData && bOperationCanceled && Mesh->bCustomizedCollision)
		{
			ReimportUI->StaticMeshImportData->bAutoGenerateCollision = bOldAutoGenerateCollision;
		}

		//Put back the original SM outer
		if (ReimportUI->StaticMeshImportData && OriginalOuter)
		{
			ReimportUI->StaticMeshImportData->Rename(nullptr, OriginalOuter);
		}
	}
	ImportOptions->bCanShowDialog = !IsUnattended;
	//We do not touch bAutoComputeLodDistances when we re-import, setting it to true will make sure we do not change anything.
	//We set the LODDistance only when the value is false.
	ImportOptions->bAutoComputeLodDistances = true;
	ImportOptions->LodNumber = 0;
	ImportOptions->MinimumLodNumber = 0;
	//Make sure the LODGroup do not change when re-importing a mesh
	ImportOptions->StaticMeshLODGroup = Mesh->LODGroup;

	if( !bOperationCanceled && ensure(ImportData) )
	{
		UE_LOG(LogEditorFactories, Log, TEXT("Performing atomic reimport of [%s]"), *Filename);

		bool bImportSucceed = true;
		if ( FFbxImporter->ImportFromFile( *Filename, FPaths::GetExtension( Filename ), true ) )
		{
			FFbxImporter->ApplyTransformSettingsToFbxNode(FFbxImporter->Scene->GetRootNode(), ImportData);

			// preserve the user data by doing a copy
			const TArray<UAssetUserData*>* UserData = Mesh->GetAssetUserDataArray();
			TMap<UAssetUserData*, bool> UserDataCopy;
			if (UserData)
			{
				for (int32 Idx = 0; Idx < UserData->Num(); Idx++)
				{
					if ((*UserData)[Idx] != nullptr)
					{
						UAssetUserData* DupObject = (UAssetUserData*)StaticDuplicateObject((*UserData)[Idx], GetTransientPackage());
						bool bAddDupToRoot = !(DupObject->IsRooted());
						if (bAddDupToRoot)
						{
							DupObject->AddToRoot();
						}
						UserDataCopy.Add(DupObject, bAddDupToRoot);
					}
				}
			}

			// preserve settings in navcollision subobject
			UNavCollisionBase* NavCollision = Mesh->NavCollision ? 
				(UNavCollisionBase*)StaticDuplicateObject(Mesh->NavCollision, GetTransientPackage()) :
				nullptr;

			bool bAddedNavCollisionDupToRoot = false;
			if (NavCollision && !NavCollision->IsRooted())
			{
				bAddedNavCollisionDupToRoot = true;
				NavCollision->AddToRoot();
			}

			// preserve extended bound settings
			const FVector PositiveBoundsExtension = Mesh->PositiveBoundsExtension;
			const FVector NegativeBoundsExtension = Mesh->NegativeBoundsExtension;

			if (FFbxImporter->ReimportStaticMesh(Mesh, ImportData))
			{
				UE_LOG(LogEditorFactories, Log, TEXT("-- imported successfully") );

				// Copy user data to newly created mesh
				for (auto Kvp : UserDataCopy)
				{
					UAssetUserData* UserDataObject = Kvp.Key;
					if (Kvp.Value)
					{
						//if the duplicated temporary UObject was add to root, we must remove it from the root
						UserDataObject->RemoveFromRoot();
					}
					UserDataObject->Rename(nullptr, Mesh, REN_DontCreateRedirectors | REN_DoNotDirty);
					Mesh->AddAssetUserData(UserDataObject);
				}

				if (NavCollision)
				{
					if (bAddedNavCollisionDupToRoot)
					{
						//if the duplicated temporary UObject was add to root, we must remove it from the root
						NavCollision->RemoveFromRoot();
					}
					Mesh->NavCollision = NavCollision;
					NavCollision->Rename(NULL, Mesh, REN_DontCreateRedirectors | REN_DoNotDirty);
				}

				// Restore bounds extension settings
				Mesh->PositiveBoundsExtension = PositiveBoundsExtension;
				Mesh->NegativeBoundsExtension = NegativeBoundsExtension;

				Mesh->AssetImportData->Update(Filename);

				// Try to find the outer package so we can dirty it up
				if (Mesh->GetOuter())
				{
					Mesh->GetOuter()->MarkPackageDirty();
				}
				else
				{
					Mesh->MarkPackageDirty();
				}

				FFbxImporter->ImportStaticMeshGlobalSockets(Mesh);
			}
			else
			{
				UE_LOG(LogEditorFactories, Warning, TEXT("-- import failed") );
				bImportSucceed = false;
			}
		}
		else
		{
			UE_LOG(LogEditorFactories, Warning, TEXT("-- import failed") );
			bImportSucceed = false;
		}

		FFbxImporter->ReleaseScene(); 

		return bImportSucceed ? EReimportResult::Succeeded : EReimportResult::Failed;
	}
	else
	{
		FFbxImporter->ReleaseScene();
		return EReimportResult::Cancelled;
	}
}

int32 UReimportFbxStaticMeshFactory::GetPriority() const
{
	return ImportPriority;
}



/*-----------------------------------------------------------------------------
UReimportFbxSkeletalMeshFactory
-----------------------------------------------------------------------------*/ 
UReimportFbxSkeletalMeshFactory::UReimportFbxSkeletalMeshFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	SupportedClass = USkeletalMesh::StaticClass();
	Formats.Add(TEXT("fbx;FBX skeletal meshes"));

	bCreateNew = false;
	bText = false;
}

bool UReimportFbxSkeletalMeshFactory::FactoryCanImport(const FString& Filename)
{
	// Return false, we are a reimport only factory
	return false;
}

bool UReimportFbxSkeletalMeshFactory::CanReimport( UObject* Obj, TArray<FString>& OutFilenames )
{
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Obj);
	if (SkeletalMesh && !SkeletalMesh->HasCustomActorReimportFactory())
	{
		if (SkeletalMesh->AssetImportData)
		{
			UFbxAssetImportData *FbxAssetImportData = Cast<UFbxAssetImportData>(SkeletalMesh->AssetImportData);
			if (FbxAssetImportData != nullptr && FbxAssetImportData->bImportAsScene)
			{
				//This skeletal mesh was import with a scene import, we cannot reimport it here
				return false;
			}
			else if (FPaths::GetExtension(SkeletalMesh->AssetImportData->GetFirstFilename()) == TEXT("abc"))
			{
				return false;
			}
			SkeletalMesh->AssetImportData->ExtractFilenames(OutFilenames);
		}
		else
		{
			OutFilenames.Add(TEXT(""));
		}
		return true;
	}
	return false;
}

void UReimportFbxSkeletalMeshFactory::SetReimportPaths( UObject* Obj, const FString& NewReimportPath, const int32 SourceFileIndex )
{	
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Obj);
	if(SkeletalMesh)
	{
		SkeletalMesh->Modify();
		UFbxSkeletalMeshImportData* ImportData = UFbxSkeletalMeshImportData::GetImportDataForSkeletalMesh(SkeletalMesh, ImportUI->SkeletalMeshImportData);
		int32 RealSourceFileIndex = SourceFileIndex == INDEX_NONE ? 0 : SourceFileIndex;
		if (RealSourceFileIndex < ImportData->GetSourceFileCount())
		{
			ImportData->UpdateFilenameOnly(NewReimportPath, SourceFileIndex);
		}
		else
		{
			//Create a source file entry, this case happen when user import a specific content for the first time
			FString SourceIndexLabel = USkeletalMesh::GetSourceFileLabelFromIndex(RealSourceFileIndex).ToString();
			ImportData->AddFileName(NewReimportPath, RealSourceFileIndex, SourceIndexLabel);
		}
	}
}

EReimportResult::Type UReimportFbxSkeletalMeshFactory::Reimport( UObject* Obj, int32 SourceFileIndex)
{
	// Only handle valid skeletal meshes
	if( !Obj || !Obj->IsA( USkeletalMesh::StaticClass() ))
	{
		return EReimportResult::Failed;
	}

	USkeletalMesh* SkeletalMesh = CastChecked<USkeletalMesh>( Obj );

	if(SkeletalMesh->HasCustomActorReimportFactory())
	{
		return EReimportResult::Failed;
	}

	UnFbx::FFbxImporter* FFbxImporter = UnFbx::FFbxImporter::GetInstance();
	UnFbx::FBXImportOptions* ImportOptions = FFbxImporter->GetImportOptions();

	//Pop the message log in case of error
	UnFbx::FFbxLoggerSetter Logger(FFbxImporter, true);

	//Clean up the options
	UnFbx::FBXImportOptions::ResetOptions(ImportOptions);

	UFbxSkeletalMeshImportData* ImportData = Cast<UFbxSkeletalMeshImportData>(SkeletalMesh->AssetImportData);
	
	// Prepare the import options
	UFbxImportUI* ReimportUI = NewObject<UFbxImportUI>();
	ReimportUI->MeshTypeToImport = FBXIT_SkeletalMesh;
	ReimportUI->Skeleton = SkeletalMesh->Skeleton;
	ReimportUI->bCreatePhysicsAsset = false;
	ReimportUI->PhysicsAsset = SkeletalMesh->PhysicsAsset;
	ReimportUI->bImportAnimations = false;
	ReimportUI->OverrideAnimationName = TEXT("");
	ReimportUI->bImportRigidMesh = false;

	if (!ImportUI)
	{
		ImportUI = NewObject<UFbxImportUI>(this, NAME_None, RF_Public);
	}

	bool bSuccess = false;
	//Prevent any UI for automation, unattended and commandlet
	const bool IsUnattended = GIsAutomationTesting || FApp::IsUnattended() || IsRunningCommandlet() || GIsRunningUnattendedScript;
	const bool ShowImportDialogAtReimport = GetDefault<UEditorPerProjectUserSettings>()->bShowImportDialogAtReimport && !IsUnattended;

	if (ImportData == nullptr)
	{
		// An existing import data object was not found, make one here and show the options dialog
		ImportData = UFbxSkeletalMeshImportData::GetImportDataForSkeletalMesh(SkeletalMesh, ImportUI->SkeletalMeshImportData);
		SkeletalMesh->AssetImportData = ImportData;
	}
	check(ImportData != nullptr);

	auto GetSourceFileName = [](UFbxSkeletalMeshImportData* ImportDataPtr, FString& OutFilename, bool bUnattended)->bool
	{
		if (ImportDataPtr == nullptr)
		{
			return false;
		}
		EFBXImportContentType ContentType = ImportDataPtr->ImportContentType;
		TArray<FString> AbsoluteFilenames;
		ImportDataPtr->ExtractFilenames(AbsoluteFilenames);
		
		auto InternalGetSourceFileName = [&ImportDataPtr, &AbsoluteFilenames, &bUnattended, &OutFilename](const int32 SourceIndex, const FText& SourceLabel)->bool
		{
			if (AbsoluteFilenames.Num() > SourceIndex)
			{
				OutFilename = AbsoluteFilenames[SourceIndex];
			}
			else if (!bUnattended)
			{
				GetReimportPathFromUser(SourceLabel, AbsoluteFilenames);
				if (AbsoluteFilenames.Num() < 1)
				{
					return false;
				}
				OutFilename = AbsoluteFilenames[0];
			}
			//Make sure the source file data is up to date
			if (SourceIndex == 0)
			{
				//When we re-import the All content we just update the 
				ImportDataPtr->AddFileName(OutFilename, SourceIndex, SourceLabel.ToString());
			}
			else
			{
				//Refresh the absolute filenames
				AbsoluteFilenames.Reset();
				ImportDataPtr->ExtractFilenames(AbsoluteFilenames);
				//Set both geo and skinning filepath. Reuse existing file path if possible. Use the first filename(geo and skin) if it has to be create.
				FString FilenameToAdd = SourceIndex == 1 ? OutFilename : AbsoluteFilenames.IsValidIndex(1) ? AbsoluteFilenames[1] : AbsoluteFilenames[0];
				ImportDataPtr->AddFileName(FilenameToAdd, 1, NSSkeletalMeshSourceFileLabels::GeometryText().ToString());
				FilenameToAdd = SourceIndex == 2 ? OutFilename : AbsoluteFilenames.IsValidIndex(2) ? AbsoluteFilenames[2] : AbsoluteFilenames[0];
				ImportDataPtr->AddFileName(FilenameToAdd, 2, NSSkeletalMeshSourceFileLabels::SkinningText().ToString());
			}
			return true;
		};

		switch (ContentType)
		{
			case FBXICT_All:
			{
				if (!InternalGetSourceFileName(0, NSSkeletalMeshSourceFileLabels::GeoAndSkinningText()))
				{
					return false;
				}
			}
			break;
			case FBXICT_Geometry:
			{
				if (!InternalGetSourceFileName(1, NSSkeletalMeshSourceFileLabels::GeometryText()))
				{
					return false;
				}
			}
			break;
			case FBXICT_SkinningWeights:
			{
				if (!InternalGetSourceFileName(2, NSSkeletalMeshSourceFileLabels::SkinningText()))
				{
					return false;
				}
			}
			break;
			default:
			{
				if (!InternalGetSourceFileName(0, NSSkeletalMeshSourceFileLabels::GeoAndSkinningText()))
				{
					return false;
				}
			}
		}
		return IFileManager::Get().FileSize(*OutFilename) != INDEX_NONE;
	};

	FString Filename = ImportData->GetFirstFilename();

	ReimportUI->SkeletalMeshImportData = ImportData;
	const FSkeletalMeshModel* SkeletalMeshModel = SkeletalMesh->GetImportedModel();

	//Manage the content type from the source file index
	ReimportUI->bAllowContentTypeImport = SkeletalMeshModel && SkeletalMeshModel->LODModels.Num() > 0 && !SkeletalMeshModel->LODModels[0].RawSkeletalMeshBulkData.IsEmpty();
	if (!ReimportUI->bAllowContentTypeImport)
	{
		//No content type allow reimport All (legacy)
		ImportData->ImportContentType = EFBXImportContentType::FBXICT_All;
	}
	else if (SourceFileIndex != INDEX_NONE)
	{
		//Reimport a specific source file index
		TArray<FString> SourceFilenames;
		ImportData->ExtractFilenames(SourceFilenames);
		if (SourceFilenames.IsValidIndex(SourceFileIndex))
		{
			ImportData->ImportContentType = SourceFileIndex == 0 ? EFBXImportContentType::FBXICT_All : SourceFileIndex == 1 ? EFBXImportContentType::FBXICT_Geometry : EFBXImportContentType::FBXICT_SkinningWeights;
			Filename = SourceFilenames[SourceFileIndex];
		}
	}
	else
	{
		//No source index is provided. Reimport the last imported content.
		int32 LastSourceFileIndex = ImportData->LastImportContentType == EFBXImportContentType::FBXICT_All ? 0 : ImportData->LastImportContentType == EFBXImportContentType::FBXICT_Geometry ? 1 : 2;
		TArray<FString> SourceFilenames;
		ImportData->ExtractFilenames(SourceFilenames);
		if (SourceFilenames.IsValidIndex(LastSourceFileIndex))
		{
			ImportData->ImportContentType = ImportData->LastImportContentType;
			Filename = SourceFilenames[LastSourceFileIndex];
		}
		else
		{
			ImportData->ImportContentType = EFBXImportContentType::FBXICT_All;
		}
	}


	if( !ShowImportDialogAtReimport)
	{
		// Import data already exists, apply it to the fbx import options
		//Some options not supported with skeletal mesh
		ImportData->bBakePivotInVertex = false;
		ImportData->bTransformVertexToAbsolute = true;

		if (!GetSourceFileName(ImportData, Filename, true))
		{
			UE_LOG(LogEditorFactories, Warning, TEXT("-- cannot reimport: source file cannot be found."));
			return EReimportResult::Failed;
		}

		ApplyImportUIToImportOptions(ReimportUI, *ImportOptions);
	}
	else
	{
		ReimportUI->bIsReimport = true;
		ReimportUI->ReimportMesh = Obj;

		bool bImportOperationCanceled = false;
		bool bShowOptionDialog = true;
		bool bForceImportType = true;
		bool bOutImportAll = false;
		bool bIsObjFormat = false;
		bool bIsAutomated = false;
		// @hack to make sure skeleton is set before opening the dialog
		ImportOptions->SkeletonForAnimation = SkeletalMesh->Skeleton;
		ImportOptions->bCreatePhysicsAsset = false;
		ImportOptions->PhysicsAsset = SkeletalMesh->PhysicsAsset;

		ImportOptions = GetImportOptions( FFbxImporter, ReimportUI, bShowOptionDialog, bIsAutomated, Obj->GetPathName(), bOperationCanceled, bOutImportAll, bIsObjFormat, Filename, bForceImportType, FBXIT_SkeletalMesh);

		if (!GetSourceFileName(ImportData, Filename, false))
		{
			UE_LOG(LogEditorFactories, Warning, TEXT("-- cannot reimport: source file cannot be found."));
			return EReimportResult::Failed;
		}
	}

	UE_LOG(LogEditorFactories, Log, TEXT("Performing atomic reimport of [%s]"), *Filename);
	CurrentFilename = Filename;

	if( !bOperationCanceled )
	{
		ImportOptions->bCanShowDialog = !IsUnattended;

		if (ImportOptions->bImportAsSkeletalSkinning)
		{
			ImportOptions->bImportMaterials = false;
			ImportOptions->bImportTextures = false;
			ImportOptions->bImportLOD = false;
			ImportOptions->bImportSkeletalMeshLODs = false;
			ImportOptions->bImportAnimations = false;
			ImportOptions->bImportMorph = false;
			ImportOptions->VertexColorImportOption = EVertexColorImportOption::Ignore;
		}
		else if (ImportOptions->bImportAsSkeletalGeometry)
		{
			ImportOptions->bImportAnimations = false;
			ImportOptions->bUpdateSkeletonReferencePose = false;
		}

		//Save all skinweight profile infos (need a copy, because they will be removed)
		const TArray<FSkinWeightProfileInfo> ExistingSkinWeightProfileInfos = SkeletalMesh->GetSkinWeightProfiles();

		if ( FFbxImporter->ImportFromFile( *Filename, FPaths::GetExtension( Filename ), true ) )
		{
			if ( FFbxImporter->ReimportSkeletalMesh(SkeletalMesh, ImportData) )
			{
				UE_LOG(LogEditorFactories, Log, TEXT("-- imported successfully") );

				// Try to find the outer package so we can dirty it up
				if (SkeletalMesh->GetOuter())
				{
					SkeletalMesh->GetOuter()->MarkPackageDirty();
				}
				else
				{
					SkeletalMesh->MarkPackageDirty();
				}

				bSuccess = true;
			}
			else
			{
				UE_LOG(LogEditorFactories, Warning, TEXT("-- import failed") );
			}
		}
		else
		{
			UE_LOG(LogEditorFactories, Warning, TEXT("-- import failed") );
		}
		FFbxImporter->ReleaseScene(); 

		CleanUp();

		if (bSuccess && ExistingSkinWeightProfileInfos.Num() > 0)
		{
			//Restore skin weight profile infos, then reimport affected LODs
			TArray<FSkinWeightProfileInfo>&SkinWeightsProfile = SkeletalMesh->GetSkinWeightProfiles();
			SkinWeightsProfile = ExistingSkinWeightProfileInfos;
			FLODUtilities::ReimportAlternateSkinWeight(SkeletalMesh, 0, true);
		}

		// Reimporting can have dangerous effects if the mesh is still in the transaction buffer.  Reset the transaction buffer if this is the case
		if(!IsRunningCommandlet() && GEditor->IsObjectInTransactionBuffer( SkeletalMesh ) )
		{
			GEditor->ResetTransaction( LOCTEXT("ReimportSkeletalMeshTransactionReset", "Reimporting a skeletal mesh which was in the undo buffer") );
		}

		return bSuccess ? EReimportResult::Succeeded : EReimportResult::Failed;
	}
	else
	{
		FFbxImporter->ReleaseScene();
		return EReimportResult::Cancelled;
	}
}

int32 UReimportFbxSkeletalMeshFactory::GetPriority() const
{
	return ImportPriority;
}

/*-----------------------------------------------------------------------------
UReimportFbxAnimSequenceFactory
-----------------------------------------------------------------------------*/ 
USkeleton* ChooseSkeleton() 
{
	TSharedRef<SWindow> WidgetWindow = SNew(SWindow)
		.Title(LOCTEXT("ChooseSkeletonWindowTitle", "Choose Skeleton"))
		.ClientSize(FVector2D(500, 600));

	TSharedRef<SSkeletonSelectorWindow> SkeletonSelectorWindow = SNew(SSkeletonSelectorWindow) .WidgetWindow(WidgetWindow);

	WidgetWindow->SetContent(SkeletonSelectorWindow);

	GEditor->EditorAddModalWindow(WidgetWindow);
	return SkeletonSelectorWindow->GetSelectedSkeleton();
}

UReimportFbxAnimSequenceFactory::UReimportFbxAnimSequenceFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	SupportedClass = UAnimSequence::StaticClass();
	Formats.Empty(1);
	Formats.Add(TEXT("fbx;FBX animation"));

	bCreateNew = false;
	bText = false;
}

bool UReimportFbxAnimSequenceFactory::FactoryCanImport(const FString& Filename)
{
	// Return false, we are a reimport only factory
	return false;
}

bool UReimportFbxAnimSequenceFactory::CanReimport( UObject* Obj, TArray<FString>& OutFilenames )
{	
	UAnimSequence* AnimSequence = Cast<UAnimSequence>(Obj);
	if(AnimSequence)
	{
		if (AnimSequence->AssetImportData)
		{
			AnimSequence->AssetImportData->ExtractFilenames(OutFilenames);

			UFbxAssetImportData *FbxAssetImportData = Cast<UFbxAssetImportData>(AnimSequence->AssetImportData);
			if (FbxAssetImportData != nullptr && FbxAssetImportData->bImportAsScene)
			{
				//This mesh was import with a scene import, we cannot reimport it
				return false;
			}
			else if (FPaths::GetExtension(AnimSequence->AssetImportData->GetFirstFilename()) == TEXT("abc"))
			{
				return false;
			}
		}
		else
		{
			OutFilenames.Add(TEXT(""));
		}
		return true;
	}
	return false;
}

void UReimportFbxAnimSequenceFactory::SetReimportPaths( UObject* Obj, const TArray<FString>& NewReimportPaths )
{	
	UAnimSequence* AnimSequence = Cast<UAnimSequence>(Obj);
	if(AnimSequence && ensure(NewReimportPaths.Num() == 1))
	{
		UFbxAnimSequenceImportData* ImportData = UFbxAnimSequenceImportData::GetImportDataForAnimSequence(AnimSequence, ImportUI->AnimSequenceImportData);

		ImportData->UpdateFilenameOnly(NewReimportPaths[0]);
	}
}

EReimportResult::Type UReimportFbxAnimSequenceFactory::Reimport( UObject* Obj )
{
	// Only handle valid skeletal meshes
	if( !Obj || !Obj->IsA( UAnimSequence::StaticClass() ) )
	{
		return EReimportResult::Failed;
	}

	UAnimSequence* AnimSequence = Cast<UAnimSequence>( Obj );
	UFbxAnimSequenceImportData* ImportData = UFbxAnimSequenceImportData::GetImportDataForAnimSequence(AnimSequence, ImportUI->AnimSequenceImportData);
	if ( !ensure(ImportData) )
	{
		return EReimportResult::Failed;
	}

	const FString Filename = ImportData->GetFirstFilename();
	const FString FileExtension = FPaths::GetExtension(Filename);
	const bool bIsNotFBXFile = ( FileExtension.Len() > 0  && FCString::Stricmp( *FileExtension, TEXT("FBX") ) != 0 );

	// Only handle FBX files
	if ( bIsNotFBXFile )
	{
		return EReimportResult::Failed;
	}
	// If there is no file path provided, can't reimport from source
// 	if ( !Filename.Len() )
// 	{
// 		// Since this is a new system most skeletal meshes don't have paths, so logging has been commented out
// 		//UE_LOG(LogEditorFactories, Warning, TEXT("-- cannot reimport: skeletal mesh resource does not have path stored."));
// 		return false;
// 	}

	UE_LOG(LogEditorFactories, Log, TEXT("Performing atomic reimport of [%s]"), *Filename);

	// Ensure that the file provided by the path exists
	if (IFileManager::Get().FileSize(*Filename) == INDEX_NONE)
	{
		UE_LOG(LogEditorFactories, Warning, TEXT("-- cannot reimport: source file cannot be found.") );
		return EReimportResult::Failed;
	}

	UnFbx::FFbxImporter* Importer = UnFbx::FFbxImporter::GetInstance();

	//Pop the message log in case of error
	UnFbx::FFbxLoggerSetter Logger(Importer, false);

	CurrentFilename = Filename;

	USkeleton* Skeleton = AnimSequence->GetSkeleton();
	if (!Skeleton)
	{
		// if it does not exist, ask for one
		Skeleton = ChooseSkeleton();
		if (!Skeleton)
		{
			// If skeleton wasn't found or the user canceled out of the dialog, we cannot proceed, but this reimport factory 
			// has still technically "handled" the reimport, so return true instead of false
			UE_LOG(LogEditorFactories, Warning, TEXT("-- import failed") );
			Importer->AddTokenizedErrorMessage(FTokenizedMessage::Create(EMessageSeverity::Error, LOCTEXT("Error_CouldNotFindSkeleton", "Cannot re-import animation with no skeleton.\nImport failed.")), FFbxErrors::SkeletalMesh_NoBoneFound);
			Importer->ReleaseScene();
			return EReimportResult::Succeeded;
		}
		//Set the selected skeleton in the anim sequence
		AnimSequence->SetSkeleton(Skeleton);
	}

	if ( UEditorEngine::ReimportFbxAnimation(Skeleton, AnimSequence, ImportData, *Filename) )
	{
		UE_LOG(LogEditorFactories, Log, TEXT("-- imported successfully") );

		// update the data in case the file source has changed
		ImportData->Update(UFactory::CurrentFilename);
		AnimSequence->ImportFileFramerate = Importer->GetOriginalFbxFramerate();

		// Try to find the outer package so we can dirty it up
		if (AnimSequence->GetOuter())
		{
			AnimSequence->GetOuter()->MarkPackageDirty();
		}
		else
		{
			AnimSequence->MarkPackageDirty();
		}
	}
	else
	{
		UE_LOG(LogEditorFactories, Warning, TEXT("-- import failed") );
		Importer->AddTokenizedErrorMessage(FTokenizedMessage::Create(EMessageSeverity::Error, LOCTEXT("Error_CouldNotReimportAnimation", "Cannot re-import animation.")), FFbxErrors::Generic_ReimportingObjectFailed);
		Importer->ReleaseScene();
		return EReimportResult::Failed;
	}

	Importer->ReleaseScene(); 

	return EReimportResult::Succeeded;
}

int32 UReimportFbxAnimSequenceFactory::GetPriority() const
{
	return ImportPriority;
}


/*------------------------------------------------------------------------------
	FBlueprintParentFilter implementation.
------------------------------------------------------------------------------*/

class FBlueprintParentFilter : public IClassViewerFilter
{
public:
	/** Classes to not allow any children of into the Class Viewer/Picker. */
	TSet< const UClass* > DisallowedChildrenOfClasses;

	virtual bool IsClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const UClass* InClass, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs ) override
	{
		return InFilterFuncs->IfInChildOfClassesSet(DisallowedChildrenOfClasses, InClass) != EFilterReturn::Passed && !InClass->HasAnyClassFlags(CLASS_Deprecated);
	}

	virtual bool IsUnloadedClassAllowed(const FClassViewerInitializationOptions& InInitOptions, const TSharedRef< const IUnloadedBlueprintData > InUnloadedClassData, TSharedRef< FClassViewerFilterFuncs > InFilterFuncs) override
	{
		return InFilterFuncs->IfInChildOfClassesSet(DisallowedChildrenOfClasses, InUnloadedClassData) != EFilterReturn::Passed && !InUnloadedClassData->HasAnyClassFlags(CLASS_Deprecated);
	}
};


/*------------------------------------------------------------------------------
	UBlueprintFactory implementation.
------------------------------------------------------------------------------*/
UBlueprintFactory::UBlueprintFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	// Look in the config file to determine what the default base class is, if any
	FString ClassPath;
	GConfig->GetString(TEXT("/Script/Engine.Engine"), TEXT("DefaultBlueprintBaseClassName"), /*out*/ ClassPath, GEngineIni);
	UClass* DefaultParentClass = !ClassPath.IsEmpty() 
		? LoadClass<UObject>(nullptr, *ClassPath, nullptr, LOAD_None, nullptr) 
		: nullptr;
	
	if( !DefaultParentClass || !FKismetEditorUtilities::CanCreateBlueprintOfClass(DefaultParentClass) )
	{
		DefaultParentClass = AActor::StaticClass();
	}

	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UBlueprint::StaticClass();
	ParentClass = DefaultParentClass;
}

bool UBlueprintFactory::ConfigureProperties()
{
	// Null the parent class to ensure one is selected
	ParentClass = nullptr;

	// Fill in options
	FClassViewerInitializationOptions Options;
	Options.Mode = EClassViewerMode::ClassPicker;
	Options.DisplayMode = EClassViewerDisplayMode::TreeView;
	Options.bShowObjectRootClass = true;

	// Only want blueprint actor base classes.
	Options.bIsBlueprintBaseOnly = true;

	// This will allow unloaded blueprints to be shown.
	Options.bShowUnloadedBlueprints = true;

	// Enable Class Dynamic Loading
	Options.bEnableClassDynamicLoading = true;

	Options.NameTypeToDisplay = EClassViewerNameTypeToDisplay::Dynamic;

	// Prevent creating blueprints of classes that require special setup (they'll be allowed in the corresponding factories / via other means)
	TSharedPtr<FBlueprintParentFilter> Filter = MakeShareable(new FBlueprintParentFilter);
	Options.ClassFilter = Filter;
	if (!IsMacroFactory())
	{
		Filter->DisallowedChildrenOfClasses.Add(ALevelScriptActor::StaticClass());
		Filter->DisallowedChildrenOfClasses.Add(UAnimInstance::StaticClass());
	}

	// Filter out interfaces in all cases; they can never contain code, so it doesn't make sense to use them as a macro basis
	Filter->DisallowedChildrenOfClasses.Add(UInterface::StaticClass());

	const FText TitleText = LOCTEXT("CreateBlueprintOptions", "Pick Parent Class");
	UClass* ChosenClass = nullptr;
	const bool bPressedOk = SClassPickerDialog::PickClass(TitleText, Options, ChosenClass, UBlueprint::StaticClass());

	if ( bPressedOk )
	{
		ParentClass = ChosenClass;

		FEditorDelegates::OnFinishPickingBlueprintClass.Broadcast(ParentClass);
	}

	return bPressedOk;
};

UObject* UBlueprintFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	// Make sure we are trying to factory a blueprint, then create and init one
	check(Class->IsChildOf(UBlueprint::StaticClass()));

	if ((ParentClass == nullptr) || !FKismetEditorUtilities::CanCreateBlueprintOfClass(ParentClass))
	{
		FFormatNamedArguments Args;
		Args.Add( TEXT("ClassName"), (ParentClass != nullptr) ? FText::FromString( ParentClass->GetName() ) : LOCTEXT("Null", "(null)") );
		FMessageDialog::Open( EAppMsgType::Ok, FText::Format( LOCTEXT("CannotCreateBlueprintFromClass", "Cannot create a blueprint based on the class '{0}'."), Args ) );
		return nullptr;
	}
	else
	{
		UClass* BlueprintClass = nullptr;
		UClass* BlueprintGeneratedClass = nullptr;

		IKismetCompilerInterface& KismetCompilerModule = FModuleManager::LoadModuleChecked<IKismetCompilerInterface>("KismetCompiler");
		KismetCompilerModule.GetBlueprintTypesForClass(ParentClass, BlueprintClass, BlueprintGeneratedClass);

		return FKismetEditorUtilities::CreateBlueprint(ParentClass, InParent, Name, BPTYPE_Normal, BlueprintClass, BlueprintGeneratedClass, CallingContext);
	}
}

UObject* UBlueprintFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return FactoryCreateNew(Class, InParent, Name, Flags, Context, Warn, NAME_None);
}


/*------------------------------------------------------------------------------
	UBlueprintMacroFactory implementation.
------------------------------------------------------------------------------*/
UBlueprintMacroFactory::UBlueprintMacroFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UBlueprint::StaticClass();
	ParentClass = AActor::StaticClass();
}

FText UBlueprintMacroFactory::GetDisplayName() const
{
	return LOCTEXT("BlueprintMacroLibraryFactoryDescription", "Blueprint Macro Library");
}

FName UBlueprintMacroFactory::GetNewAssetThumbnailOverride() const
{
	return TEXT("ClassThumbnail.BlueprintMacroLibrary");
}

uint32 UBlueprintMacroFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Blueprint;
}

FText UBlueprintMacroFactory::GetToolTip() const
{
	return LOCTEXT("BlueprintMacroLibraryTooltip", "Blueprint Macro Libraries are containers of macros to be used in other blueprints. They cannot contain variables, inherit from other blueprints, or be placed in levels. Changes to macros in a Blueprint Macro Library will not take effect until client blueprints are recompiled.");
}

FString UBlueprintMacroFactory::GetToolTipDocumentationExcerpt() const
{
	return TEXT("UBlueprint_Macro");
}

UObject* UBlueprintMacroFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	// Make sure we are trying to factory a blueprint, then create and init one
	check(Class->IsChildOf(UBlueprint::StaticClass()));

	if ((ParentClass == nullptr) || !FKismetEditorUtilities::CanCreateBlueprintOfClass(ParentClass))
	{
		FFormatNamedArguments Args;
		Args.Add( TEXT("ClassName"), (ParentClass != nullptr) ? FText::FromString( ParentClass->GetName() ) : LOCTEXT("Null", "(null)") );
		FMessageDialog::Open( EAppMsgType::Ok, FText::Format( LOCTEXT("CannotCreateBlueprintFromClass", "Cannot create a blueprint based on the class '{0}'."), Args ) );
		return nullptr;
	}
	else
	{
		return FKismetEditorUtilities::CreateBlueprint(ParentClass, InParent, Name, BPTYPE_MacroLibrary, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), CallingContext);
	}
}

FString UBlueprintMacroFactory::GetDefaultNewAssetName() const
{
	return FString(TEXT("NewMacroLibrary"));
}

/*------------------------------------------------------------------------------
BlueprintFunctionLibraryFactory implementation.
------------------------------------------------------------------------------*/

UBlueprintFunctionLibraryFactory::UBlueprintFunctionLibraryFactory(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
	static FBoolConfigValueHelper CanCreateNewHelper(TEXT("CustomBlueprintFunctionLibrary"), TEXT("bCanCreateNew"));
	bCreateNew = CanCreateNewHelper;
	bEditAfterNew = true;
	SupportedClass = UBlueprint::StaticClass();
	ParentClass = UBlueprintFunctionLibrary::StaticClass();
}

FText UBlueprintFunctionLibraryFactory::GetDisplayName() const
{
	return LOCTEXT("BlueprintFunctionLibraryFactoryDescription", "Blueprint Function Library");
}

FName UBlueprintFunctionLibraryFactory::GetNewAssetThumbnailOverride() const
{
	return TEXT("ClassThumbnail.BlueprintFunctionLibrary");
}

uint32 UBlueprintFunctionLibraryFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Blueprint;
}

FText UBlueprintFunctionLibraryFactory::GetToolTip() const
{
	return LOCTEXT("BlueprintFunctionLibraryTooltip", "Blueprint Function Libraries are containers of functions to be used in other blueprints. They cannot contain variables, inherit from other blueprints, or be placed in levels. Changes to functions in a Blueprint Function Library will take effect without recompiling the client blueprints.");
}

FString UBlueprintFunctionLibraryFactory::GetToolTipDocumentationExcerpt() const
{
	return TEXT("UBlueprint_FunctionLibrary");
}

UObject* UBlueprintFunctionLibraryFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	// Make sure we are trying to factory a blueprint, then create and init one
	check(Class->IsChildOf(UBlueprint::StaticClass()));

	if (ParentClass != UBlueprintFunctionLibrary::StaticClass())
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("ClassName"), (ParentClass != nullptr) ? FText::FromString(ParentClass->GetName()) : LOCTEXT("Null", "(null)"));
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(LOCTEXT("CannotCreateBlueprintFromClass", "Cannot create a blueprint based on the class '{0}'."), Args));
		return nullptr;
	}
	else
	{
		return FKismetEditorUtilities::CreateBlueprint(ParentClass, InParent, Name, BPTYPE_FunctionLibrary, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), CallingContext);
	}
}

bool UBlueprintFunctionLibraryFactory::ConfigureProperties()
{
	return true;
}

FString UBlueprintFunctionLibraryFactory::GetDefaultNewAssetName() const
{
	return FString(TEXT("NewFunctionLibrary"));
}

/*------------------------------------------------------------------------------
	UBlueprintInterfaceFactory implementation.
------------------------------------------------------------------------------*/
UBlueprintInterfaceFactory::UBlueprintInterfaceFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UBlueprint::StaticClass();
}

FText UBlueprintInterfaceFactory::GetDisplayName() const
{
	return LOCTEXT("BlueprintInterfaceFactoryDescription", "Blueprint Interface");
}

FName UBlueprintInterfaceFactory::GetNewAssetThumbnailOverride() const
{
	return TEXT("ClassThumbnail.BlueprintInterface");
}

uint32 UBlueprintInterfaceFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Blueprint;
}

FText UBlueprintInterfaceFactory::GetToolTip() const
{
	return LOCTEXT("BlueprintInterfaceTooltip", "A Blueprint Interface is a collection of one or more functions - name only, no implementation - that can be added to other Blueprints. These other Blueprints are then expected to implement the functions of the Blueprint Interface in a unique manner.");
}

FString UBlueprintInterfaceFactory::GetToolTipDocumentationExcerpt() const
{
	return TEXT("UBlueprint_Interface");
}

UObject* UBlueprintInterfaceFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	// Make sure we are trying to factory a blueprint, then create and init one
	check(Class->IsChildOf(UBlueprint::StaticClass()));

	// Force the parent class to be UInterface as per original code
	UClass* ParentClass = UInterface::StaticClass();

	if ((ParentClass == nullptr) || !FKismetEditorUtilities::CanCreateBlueprintOfClass(ParentClass))
	{
		FFormatNamedArguments Args;
		Args.Add( TEXT("ClassName"), (ParentClass != nullptr) ? FText::FromString( ParentClass->GetName() ) : LOCTEXT("Null", "(null)") );
		FMessageDialog::Open( EAppMsgType::Ok, FText::Format( LOCTEXT("CannotCreateBlueprintFromClass", "Cannot create a blueprint based on the class '{0}'."), Args ) );
		return nullptr;
	}
	else
	{
		return FKismetEditorUtilities::CreateBlueprint(ParentClass, InParent, Name, BPTYPE_Interface, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), CallingContext);
	}
}

UObject* UBlueprintInterfaceFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return FactoryCreateNew(Class, InParent, Name, Flags, Context, Warn, NAME_None);
}

FString UBlueprintInterfaceFactory::GetDefaultNewAssetName() const
{
	return FString(TEXT("NewInterface"));
}

/*------------------------------------------------------------------------------
	UCurveFactory implementation.
------------------------------------------------------------------------------*/

UCurveFactory::UCurveFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UCurveBase::StaticClass();

	CurveClass = nullptr;
}

bool UCurveFactory::ConfigureProperties()
{
	// Null the CurveClass so we can get a clean class
	CurveClass = nullptr;

	// Load the classviewer module to display a class picker
	FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");

	// Fill in options
	FClassViewerInitializationOptions Options;
	Options.Mode = EClassViewerMode::ClassPicker;

	TSharedPtr<FAssetClassParentFilter> Filter = MakeShareable(new FAssetClassParentFilter);
	Options.ClassFilter = Filter;

	Filter->DisallowedClassFlags = CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists;
	Filter->AllowedChildrenOfClasses.Add(UCurveBase::StaticClass());

	const FText TitleText = LOCTEXT("CreateCurveOptions", "Pick Curve Class");
	UClass* ChosenClass = nullptr;
	const bool bPressedOk = SClassPickerDialog::PickClass(TitleText, Options, ChosenClass, UCurveBase::StaticClass());

	if ( bPressedOk )
	{
		CurveClass = ChosenClass;
	}

	return bPressedOk;
}

UObject* UCurveFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UCurveBase* NewCurve = nullptr;
	if(CurveClass != nullptr)
	{
		NewCurve = NewObject<UCurveBase>(InParent, CurveClass, Name, Flags);
	}

	return NewCurve;
}

/*------------------------------------------------------------------------------
	UCurveFloatFactory implementation.
------------------------------------------------------------------------------*/

UCurveFloatFactory::UCurveFloatFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UCurveFloat::StaticClass();
	CurveClass = UCurveFloat::StaticClass();
}

bool UCurveFloatFactory::ConfigureProperties()
{
	return true;
}

/*------------------------------------------------------------------------------
	UCurveLinearColorFactory implementation.
------------------------------------------------------------------------------*/

UCurveLinearColorFactory::UCurveLinearColorFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UCurveLinearColor::StaticClass();
	CurveClass = UCurveLinearColor::StaticClass();
}

bool UCurveLinearColorFactory::ConfigureProperties()
{
	return true;
}

/*------------------------------------------------------------------------------
	UCurveVectorFactory implementation.
------------------------------------------------------------------------------*/

UCurveVectorFactory::UCurveVectorFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UCurveVector::StaticClass();
	CurveClass = UCurveVector::StaticClass();
}

bool UCurveVectorFactory::ConfigureProperties()
{
	return true;
}

/*------------------------------------------------------------------------------
	UCurveImportFactory implementation.
------------------------------------------------------------------------------*/

UCurveImportFactory::UCurveImportFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	bCreateNew = false;
	SupportedClass = UCurveBase::StaticClass();

	bEditorImport = true;
	bText = true;

	Formats.Add(TEXT("as;Audio amplitude curve"));
}

// @note jf: for importing a curve from a text format.  this is experimental code for a prototype feature and not fully fleshed out
UObject* UCurveImportFactory::FactoryCreateText( UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, const TCHAR* Type, const TCHAR*& Buffer, const TCHAR* BufferEnd, FFeedbackContext* Warn )
{
	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPreImport(this, InClass, InParent, InName, Type);

	if(	FCString::Stricmp( Type, TEXT( "AS" ) ) == 0 )
	{
		FString Str;
		TCHAR const* BufRead = Buffer;

		// first line is faFile="", we can ignore
		if ( !FParse::Line(&BufRead, Str) ) return nullptr;
		FParse::Next(&BufRead);

		// 2nd line is fps=X
		float KeyFrameHz = 0.f;
		if ( !FParse::Value(BufRead, TEXT("fps="), KeyFrameHz) ) return nullptr;
		if ( !FParse::Line(&BufRead, Str) ) return nullptr;
		FParse::Next(&BufRead);

		// next line is scale=X, we can ignore?
		if ( !FParse::Line(&BufRead, Str) ) return nullptr;
		FParse::Next(&BufRead);
		// next line is smoothing=X, we can ignore?
		if ( !FParse::Line(&BufRead, Str) ) return nullptr;
		FParse::Next(&BufRead);
		// next line is dBValues=X, we can ignore?
		if ( !FParse::Line(&BufRead, Str) ) return nullptr;
		FParse::Next(&BufRead);
		// next line is stereo=X, we can ignore?
		if ( !FParse::Line(&BufRead, Str) ) return nullptr;
		FParse::Next(&BufRead);

		// next line is amplitude=[, then list of CSV floats
		if ( !FParse::Value(BufRead, TEXT("amplitude=["), Str) ) return nullptr;
		BufRead += FCString::Strlen(TEXT("amplitude=["));

		TArray<float> FloatKeys;

		while (1)
		{
			if (!FParse::AlnumToken(BufRead, Str)) break;

			float Key = (float) FCString::Atoi(*Str);
			FloatKeys.Add(Key);

			if (*BufRead == ',')
			{
				BufRead++;
				FParse::Next(&BufRead);
			}
			else
			{
				break;
			}
		}

		// make the curve object and set up the keys
		if (FloatKeys.Num() > 0)
		{
			UCurveFloat* NewCurve = NewObject<UCurveFloat>(InParent,InName,Flags);

			if (NewCurve)
			{
				for (int32 KeyIdx=0; KeyIdx<FloatKeys.Num(); ++KeyIdx)
				{
					float const KeyTime = KeyIdx / KeyFrameHz;
					float const KeyValue = FloatKeys[KeyIdx];
					FKeyHandle const KeyHandle = NewCurve->FloatCurve.AddKey(KeyTime, KeyValue);
					NewCurve->FloatCurve.SetKeyInterpMode(KeyHandle, RCIM_Cubic);
				}
			}

			GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport(this, NewCurve);

			return NewCurve;
		}
	}

	GEditor->GetEditorSubsystem<UImportSubsystem>()->BroadcastAssetPostImport( this, nullptr );
	return nullptr;
}


/*------------------------------------------------------------------------------
	UArchetypeLibraryFactory implementation.
------------------------------------------------------------------------------*/
UObjectLibraryFactory::UObjectLibraryFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UObjectLibrary::StaticClass();
}

UObject* UObjectLibraryFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UObjectLibrary>(InParent, Class, Name, Flags);
}

/*------------------------------------------------------------------------------
	UDataAssetFactory implementation.
------------------------------------------------------------------------------*/

UDataAssetFactory::UDataAssetFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UDataAsset::StaticClass();
}

bool UDataAssetFactory::ConfigureProperties()
{
	// nullptr the DataAssetClass so we can check for selection
	DataAssetClass = nullptr;

	// Load the classviewer module to display a class picker
	FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");

	// Fill in options
	FClassViewerInitializationOptions Options;
	Options.Mode = EClassViewerMode::ClassPicker;

	TSharedPtr<FAssetClassParentFilter> Filter = MakeShareable(new FAssetClassParentFilter);
	Options.ClassFilter = Filter;

	Filter->DisallowedClassFlags = CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_HideDropDown;
	Filter->AllowedChildrenOfClasses.Add(UDataAsset::StaticClass());

	const FText TitleText = LOCTEXT("CreateDataAssetOptions", "Pick Data Asset Class");
	UClass* ChosenClass = nullptr;
	const bool bPressedOk = SClassPickerDialog::PickClass(TitleText, Options, ChosenClass, UDataAsset::StaticClass());

	if ( bPressedOk )
	{
		DataAssetClass = ChosenClass;
	}

	return bPressedOk;
}

UObject* UDataAssetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	if (DataAssetClass != nullptr)
	{
		return NewObject<UDataAsset>(InParent, DataAssetClass, Name, Flags | RF_Transactional);
	}
	else
	{
		// if we have no data asset class, use the passed-in class instead
		check(Class->IsChildOf(UDataAsset::StaticClass()));
		return NewObject<UDataAsset>(InParent, Class, Name, Flags);
	}
}

/*------------------------------------------------------------------------------
	UBlendSpaceFactoryNew.
------------------------------------------------------------------------------*/
UBlendSpaceFactoryNew::UBlendSpaceFactoryNew(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	SupportedClass = UBlendSpace::StaticClass();
	bCreateNew = true;
}

bool UBlendSpaceFactoryNew::ConfigureProperties()
{
	// Null the parent class so we can check for selection later
	TargetSkeleton = nullptr;

	// Load the content browser module to display an asset picker
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	FAssetPickerConfig AssetPickerConfig;

	/** The asset picker will only show skeletal meshes */
	AssetPickerConfig.Filter.ClassNames.Add(USkeleton::StaticClass()->GetFName());
	AssetPickerConfig.Filter.bRecursiveClasses = true;

	/** The delegate that fires when an asset was selected */
	AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateUObject(this, &UBlendSpaceFactoryNew::OnTargetSkeletonSelected);

	/** The default view mode should be a list view */
	AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;


	PickerWindow = SNew(SWindow)
	.Title(LOCTEXT("CreateBlendSpaceOptions", "Pick Skeleton"))
	.ClientSize(FVector2D(500, 600))
	.SupportsMinimize(false) .SupportsMaximize(false)
	[
		SNew(SBorder)
		.BorderImage( FEditorStyle::GetBrush("Menu.Background") )
		[
			ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
		]
	];


	GEditor->EditorAddModalWindow(PickerWindow.ToSharedRef());
	PickerWindow.Reset();

	return TargetSkeleton != nullptr;
}

UObject* UBlendSpaceFactoryNew::FactoryCreateNew(UClass* Class,UObject* InParent,FName Name,EObjectFlags Flags,UObject* Context,FFeedbackContext* Warn)
{
	if (TargetSkeleton)
	{
		UBlendSpace * BlendSpace = NewObject<UBlendSpace>(InParent, Class, Name, Flags);

		BlendSpace->SetSkeleton(TargetSkeleton);
		if (PreviewSkeletalMesh)
		{
			BlendSpace->SetPreviewMesh(PreviewSkeletalMesh);
		}

		return BlendSpace;
	}

	return nullptr;
}

void UBlendSpaceFactoryNew::OnTargetSkeletonSelected(const FAssetData& SelectedAsset)
{
	TargetSkeleton = Cast<USkeleton>(SelectedAsset.GetAsset());
	PickerWindow->RequestDestroyWindow();
}

/*------------------------------------------------------------------------------
	UBlendSpaceFactory1D.
------------------------------------------------------------------------------*/
UBlendSpaceFactory1D::UBlendSpaceFactory1D(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	SupportedClass = UBlendSpace1D::StaticClass();
	bCreateNew = true;
}

bool UBlendSpaceFactory1D::ConfigureProperties()
{
	// Null the parent class so we can check for selection later
	TargetSkeleton = nullptr;

	// Load the content browser module to display an asset picker
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	FAssetPickerConfig AssetPickerConfig;

	/** The asset picker will only show skeletal meshes */
	AssetPickerConfig.Filter.ClassNames.Add(USkeleton::StaticClass()->GetFName());
	AssetPickerConfig.Filter.bRecursiveClasses = true;

	/** The delegate that fires when an asset was selected */
	AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateUObject(this, &UBlendSpaceFactory1D::OnTargetSkeletonSelected);

	/** The default view mode should be a list view */
	AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;

	PickerWindow = SNew(SWindow)
	.Title(LOCTEXT("CreateBlendSpaceOptions", "Pick Skeleton"))
	.ClientSize(FVector2D(500, 600))
	.SupportsMinimize(false) .SupportsMaximize(false)
	[
		SNew(SBorder)
		.BorderImage( FEditorStyle::GetBrush("Menu.Background") )
		[
			ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
		]
	];


	GEditor->EditorAddModalWindow(PickerWindow.ToSharedRef());
	PickerWindow.Reset();

	return TargetSkeleton != nullptr;
}

UObject* UBlendSpaceFactory1D::FactoryCreateNew(UClass* Class,UObject* InParent,FName Name,EObjectFlags Flags,UObject* Context,FFeedbackContext* Warn)
{
	if (TargetSkeleton)
	{
		UBlendSpace1D * BlendSpace = NewObject<UBlendSpace1D>(InParent, Class, Name, Flags);

		BlendSpace->SetSkeleton(TargetSkeleton);
		if (PreviewSkeletalMesh)
		{
			BlendSpace->SetPreviewMesh(PreviewSkeletalMesh);
		}

		return BlendSpace;
	}

	return nullptr;
}

void UBlendSpaceFactory1D::OnTargetSkeletonSelected(const FAssetData& SelectedAsset)
{
	TargetSkeleton = Cast<USkeleton>(SelectedAsset.GetAsset());
	PickerWindow->RequestDestroyWindow();
}

/*------------------------------------------------------------------------------
	UAimOffsetBlendSpaceFactoryNew.
------------------------------------------------------------------------------*/
UAimOffsetBlendSpaceFactoryNew::UAimOffsetBlendSpaceFactoryNew(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UAimOffsetBlendSpace::StaticClass();
	bCreateNew = true;
}

UObject* UAimOffsetBlendSpaceFactoryNew::FactoryCreateNew(UClass* Class,UObject* InParent,FName Name,EObjectFlags Flags,UObject* Context,FFeedbackContext* Warn)
{
	if (TargetSkeleton)
	{
		UAimOffsetBlendSpace * BlendSpace = NewObject<UAimOffsetBlendSpace>(InParent, Class, Name, Flags);

		BlendSpace->SetSkeleton(TargetSkeleton);
		if (PreviewSkeletalMesh)
		{
			BlendSpace->SetPreviewMesh(PreviewSkeletalMesh);
		}

		return BlendSpace;
	}

	return nullptr;
}

/*------------------------------------------------------------------------------
	UAimOffsetBlendSpaceFactory1D.
------------------------------------------------------------------------------*/
UAimOffsetBlendSpaceFactory1D::UAimOffsetBlendSpaceFactory1D(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UAimOffsetBlendSpace1D::StaticClass();
	bCreateNew = true;
}

UObject* UAimOffsetBlendSpaceFactory1D::FactoryCreateNew(UClass* Class,UObject* InParent,FName Name,EObjectFlags Flags,UObject* Context,FFeedbackContext* Warn)
{
	if (TargetSkeleton)
	{
		UAimOffsetBlendSpace1D * BlendSpace = NewObject<UAimOffsetBlendSpace1D>(InParent, Class, Name, Flags);

		BlendSpace->SetSkeleton(TargetSkeleton);
		if (PreviewSkeletalMesh)
		{
			BlendSpace->SetPreviewMesh(PreviewSkeletalMesh);
		}

		return BlendSpace;
	}

	return nullptr;
}

/*------------------------------------------------------------------------------
	UEnumFactory implementation.
------------------------------------------------------------------------------*/
UEnumFactory::UEnumFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UUserDefinedEnum::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UEnumFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	ensure(UUserDefinedEnum::StaticClass() == Class);

	if(!FEnumEditorUtils::IsNameAvailebleForUserDefinedEnum(Name))
	{
		const FText Message = FText::Format( LOCTEXT("EnumWithNameAlreadyExists", "Enum '{0}' already exists. The name must be unique."), FText::FromName( Name ) );
		if(Warn)
		{
			Warn->Log( Message );
		}
		FMessageDialog::Open( EAppMsgType::Ok, Message);
		return nullptr;
	}

	return FEnumEditorUtils::CreateUserDefinedEnum(InParent, Name, Flags);
}

/*------------------------------------------------------------------------------
	UStructureFactory implementation.
------------------------------------------------------------------------------*/
UStructureFactory::UStructureFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UUserDefinedStruct::StaticClass();
	bCreateNew = FStructureEditorUtils::UserDefinedStructEnabled();
	bEditAfterNew = true;
}

UObject* UStructureFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	ensure(UUserDefinedStruct::StaticClass() == Class);
	return FStructureEditorUtils::CreateUserDefinedStruct(InParent, Name, Flags);
}

/*-----------------------------------------------------------------------------
UForceFeedbackAttenuationFactory implementation.
-----------------------------------------------------------------------------*/
UForceFeedbackAttenuationFactory::UForceFeedbackAttenuationFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	SupportedClass = UForceFeedbackAttenuation::StaticClass();
	bCreateNew = true;
	bEditorImport = false;
	bEditAfterNew = true;
}

UObject* UForceFeedbackAttenuationFactory::FactoryCreateNew( UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn )
{
	return NewObject<UForceFeedbackAttenuation>(InParent, InName, Flags);
}

/*-----------------------------------------------------------------------------
	UForceFeedbackEffectFactory implementation.
-----------------------------------------------------------------------------*/
UForceFeedbackEffectFactory::UForceFeedbackEffectFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	SupportedClass = UForceFeedbackEffect::StaticClass();
	bCreateNew = true;
	bEditorImport = false;
	bEditAfterNew = true;
}

UObject* UForceFeedbackEffectFactory::FactoryCreateNew( UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn )
{
	return NewObject<UForceFeedbackEffect>(InParent, InName, Flags);
}

/*-----------------------------------------------------------------------------
	UHapticFeedbackEffectCurveFactory implementation.
-----------------------------------------------------------------------------*/
UHapticFeedbackEffectCurveFactory::UHapticFeedbackEffectCurveFactory(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{

	SupportedClass = UHapticFeedbackEffect_Curve::StaticClass();
	bCreateNew = true;
	bEditorImport = false;
	bEditAfterNew = true;
}

UObject* UHapticFeedbackEffectCurveFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UHapticFeedbackEffect_Curve>(InParent, InName, Flags);
}

/*-----------------------------------------------------------------------------
UHapticFeedbackEffectBufferFactory implementation.
-----------------------------------------------------------------------------*/
UHapticFeedbackEffectBufferFactory::UHapticFeedbackEffectBufferFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	SupportedClass = UHapticFeedbackEffect_Buffer::StaticClass();
	bCreateNew = true;
	bEditorImport = false;
	bEditAfterNew = true;
}

UObject* UHapticFeedbackEffectBufferFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UHapticFeedbackEffect_Buffer>(InParent, InName, Flags);
}

/*-----------------------------------------------------------------------------
UHapticFeedbackEffectSoundWaveFactory implementation.
-----------------------------------------------------------------------------*/
UHapticFeedbackEffectSoundWaveFactory::UHapticFeedbackEffectSoundWaveFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	SupportedClass = UHapticFeedbackEffect_SoundWave::StaticClass();
	bCreateNew = true;
	bEditorImport = false;
	bEditAfterNew = true;
}

UObject* UHapticFeedbackEffectSoundWaveFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UHapticFeedbackEffect_SoundWave>(InParent, InName, Flags);
}

/*-----------------------------------------------------------------------------
USubsurfaceProfileFactory implementation.
-----------------------------------------------------------------------------*/
USubsurfaceProfileFactory::USubsurfaceProfileFactory(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{

	SupportedClass = USubsurfaceProfile::StaticClass();
	bCreateNew = true;
	bEditorImport = false;
	bEditAfterNew = true;
}

UObject* USubsurfaceProfileFactory::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<USubsurfaceProfile>(InParent, InName, Flags);
}

/*-----------------------------------------------------------------------------
	UTouchInterfaceFactory implementation.
-----------------------------------------------------------------------------*/
UTouchInterfaceFactory::UTouchInterfaceFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	SupportedClass = UTouchInterface::StaticClass();

	bCreateNew = true;
	bEditorImport = false;
	bEditAfterNew = true;
}

UObject* UTouchInterfaceFactory::FactoryCreateNew( UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn )
{
	return NewObject<UTouchInterface>(InParent, InName, Flags);
}

/*------------------------------------------------------------------------------
	UCameraAnimFactory implementation.
------------------------------------------------------------------------------*/

UCameraAnimFactory::UCameraAnimFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UCameraAnim::StaticClass();
	bCreateNew = true;
}

FText UCameraAnimFactory::GetDisplayName() const
{
	return LOCTEXT("CameraAnimFactoryDescription", "Camera Anim");
}

FName UCameraAnimFactory::GetNewAssetThumbnailOverride() const
{
	return TEXT("ClassThumbnail.CameraAnim");
}

uint32 UCameraAnimFactory::GetMenuCategories() const
{
	return EAssetTypeCategories::Misc;
}

UObject* UCameraAnimFactory::FactoryCreateNew(UClass* Class,UObject* InParent,FName Name,EObjectFlags Flags,UObject* Context,FFeedbackContext* Warn)
{
	UCameraAnim* NewCamAnim = NewObject<UCameraAnim>(InParent, Class, Name, Flags);
	NewCamAnim->CameraInterpGroup = NewObject<UInterpGroupCamera>(NewCamAnim);
	NewCamAnim->CameraInterpGroup->GroupName = Name;
	return NewCamAnim;
}

/*------------------------------------------------------------------------------
UStringTableFactory implementation.
------------------------------------------------------------------------------*/
UStringTableFactory::UStringTableFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UStringTable::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* UStringTableFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UStringTable>(InParent, Name, Flags);
}

/*------------------------------------------------------------------------------
 UPreviewMeshCollectionFactory implementation.
------------------------------------------------------------------------------*/

UPreviewMeshCollectionFactory::UPreviewMeshCollectionFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UPreviewMeshCollection::StaticClass();
	bCreateNew = true;
}

FText UPreviewMeshCollectionFactory::GetDisplayName() const
{
	return LOCTEXT("PreviewMeshCollection", "Preview Mesh Collection");
}

FText UPreviewMeshCollectionFactory::GetToolTip() const
{
	return LOCTEXT("PreviewMeshCollection_Tooltip", "Preview Mesh Collections are used to build collections of related skeletal meshes that are animated together (such as components of a character)");
}

bool UPreviewMeshCollectionFactory::ConfigureProperties()
{
	if (CurrentSkeleton.IsValid())
	{
		return true;
	}

	USkeleton* Skeleton = ChooseSkeleton();
	if (Skeleton != nullptr)
	{
		CurrentSkeleton = Skeleton;
		return true;
	}

	return false;
}

UObject* UPreviewMeshCollectionFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UPreviewMeshCollection* NewCollection = NewObject<UPreviewMeshCollection>(InParent, Name, Flags);
	NewCollection->Skeleton = CurrentSkeleton.Get();

	return NewCollection;
}

#undef LOCTEXT_NAMESPACE

