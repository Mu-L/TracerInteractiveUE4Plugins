// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDStageImporter.h"

#include "USDAssetImportData.h"
#include "USDConversionUtils.h"
#include "USDErrorUtils.h"
#include "USDGeomMeshConversion.h"
#include "USDLog.h"
#include "USDSchemasModule.h"
#include "USDSchemaTranslator.h"
#include "USDStageImportContext.h"
#include "USDStageImportOptions.h"
#include "USDStageImportOptionsWindow.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/SdfLayer.h"
#include "UsdWrappers/UsdStage.h"
#include "UsdWrappers/UsdTyped.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Dialogs/DlgPickPath.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "IAssetTools.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/StrongObjectPtr.h"


#define LOCTEXT_NAMESPACE "USDStageImporter"

namespace UsdStageImporterImpl
{
	UE::FUsdStage ReadUsdFile(FUsdStageImportContext& ImportContext)
	{
		const FString FilePath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*ImportContext.FilePath);

		UsdUtils::StartMonitoringErrors();

		for ( const UE::FUsdStage& OpenedStage : UnrealUSDWrapper::GetAllStagesFromCache() )
		{
			FString RootPath = OpenedStage.GetRootLayer().GetRealPath();
			FPaths::NormalizeFilename( RootPath );
			if ( ImportContext.FilePath == RootPath )
			{
				ImportContext.bStageWasOriginallyOpen = true;
				break;
			}
		}

		UE::FUsdStage Stage = UnrealUSDWrapper::OpenStage( *FilePath, EUsdInitialLoadSet::LoadAll, ImportContext.bReadFromStageCache );

		TArray<FString> ErrorStrings = UsdUtils::GetErrorsAndStopMonitoring();
		FString Error = FString::Join(ErrorStrings, TEXT("\n"));

		if (!Error.IsEmpty())
		{
			FUsdLogManager::LogMessage( EMessageSeverity::Error, FText::Format( LOCTEXT( "CouldNotImportUSDFile", "Could not import USD file {0}\n {1}" ), FText::FromString( FilePath ), FText::FromString( Error ) ) );
		}
		return Stage;
	}

	FString FindValidPackagePath(const FString& InPackagePath)
	{
		int32 Suffix = 0;
		FString SearchPackagePath = InPackagePath;
		UPackage* ExistingPackage = nullptr;

		do
		{
			// Look for the package in memory
			ExistingPackage = FindPackage(nullptr, *SearchPackagePath);

			// Look for the package on disk
			if (!ExistingPackage && FPackageName::DoesPackageExist(SearchPackagePath))
			{
				ExistingPackage = LoadPackage(nullptr, *SearchPackagePath, LOAD_None);
			}

			SearchPackagePath = InPackagePath + TEXT("_") + LexToString(Suffix++);
		}
		while(ExistingPackage != nullptr);

		// Undo the last SearchPackagePath update, returning the path that worked (vacant Package path)
		return Suffix == 1 ? InPackagePath : InPackagePath + TEXT("_") + LexToString(Suffix - 1);
	}

	/**
	 * Removes any numbered suffix, followed by any number of underscores (e.g. Asset_2, Asset__23231 or Asset94 become 'Asset'), making
	 * sure the string is kept at least one character long.
	*/
	void RemoveNumberedSuffix( FString& Prefix )
	{
		if ( !Prefix.IsNumeric() )
		{
			FString LastChar = Prefix.Right( 1 );
			while ( LastChar.IsNumeric() )
			{
				const bool bAllowShrinking = false;
				Prefix.LeftChopInline( 1, bAllowShrinking );
				LastChar = Prefix.Right( 1 );
			}
			Prefix.Shrink();
		}

		while ( Prefix.Len() > 1 && Prefix.Right( 1 ) == TEXT( "_" ) )
		{
			Prefix.RemoveFromEnd( TEXT( "_" ) );
		}
	}

	FString GetUniqueName(FString Prefix, TSet<FString>& UniqueNames)
	{
		if (!UniqueNames.Contains(Prefix))
		{
			return Prefix;
		}

		RemoveNumberedSuffix(Prefix);

		int32 Suffix = 0;
		FString Result;
		do
		{
			Result = FString::Printf(TEXT("%s_%d"), *Prefix, Suffix++);
		} while (UniqueNames.Contains(Result));

		return Result;
	}

	void SetupSceneActor(FUsdStageImportContext& ImportContext)
	{
		if ( !ImportContext.ImportOptions->bImportActors )
		{
			return;
		}

		ULevel* Level = ImportContext.World->GetCurrentLevel();
		if(!Level)
		{
			return;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.ObjectFlags = ImportContext.ImportObjectFlags;
		SpawnParameters.OverrideLevel = Level;

		// We always spawn another scene actor regardless of collision or whether the level already has one,
		// so that we can fully build our hierarchy separately before resolving collisions according to ExistingActorPolicy
		AActor* Actor = ImportContext.World->SpawnActor(AActor::StaticClass(), nullptr, SpawnParameters);
		Actor->SetActorLabel(ObjectTools::SanitizeObjectName(ImportContext.ObjectName));

		USceneComponent* RootComponent = Actor->GetRootComponent();
		if (!RootComponent)
		{
			RootComponent = NewObject<USceneComponent>(Actor, USceneComponent::GetDefaultSceneRootVariableName(), RF_Transactional);
			RootComponent->Mobility = EComponentMobility::Static;
			RootComponent->bVisualizeComponent = false;

			Actor->SetRootComponent(RootComponent);
			Actor->AddInstanceComponent(RootComponent);
		}

		if (RootComponent && !RootComponent->IsRegistered())
		{
			RootComponent->RegisterComponent();
		}

		ImportContext.SceneActor = Actor;
	}

	AActor* GetExistingSceneActor(FUsdStageImportContext& ImportContext)
	{
		// We always reuse the existing scene actor for a scene, regardless of ReplacePolicy
		FString TargetActorLabel = ObjectTools::SanitizeObjectName(ImportContext.ObjectName);
		for (TActorIterator<AActor> ActorItr(ImportContext.World); ActorItr; ++ActorItr)
		{
			AActor* ThisActor = *ActorItr;

			// Found a top level actor with the same label
			if ( !ThisActor->HasAnyFlags(RF_Transient) &&
				 ThisActor->GetAttachParentActor() == nullptr &&
				 ThisActor->GetActorLabel() == TargetActorLabel &&
				 ThisActor != ImportContext.SceneActor)
			{
				return ThisActor;
			}
		}

		return nullptr;
	}

	void SetupStageForImport( FUsdStageImportContext& ImportContext )
	{
#if USE_USD_SDK
		ImportContext.OriginalMetersPerUnit = UsdUtils::GetUsdStageMetersPerUnit( ImportContext.Stage );
		UsdUtils::SetUsdStageMetersPerUnit( ImportContext.Stage, ImportContext.ImportOptions->MetersPerUnit );
#endif // #if USE_USD_SDK
	}

	void CreateAssetsForPrims(const TArray<UE::FUsdPrim>& Prims, FUsdSchemaTranslationContext& TranslationContext)
	{
		IUsdSchemasModule& UsdSchemasModule = FModuleManager::Get().LoadModuleChecked<IUsdSchemasModule>(TEXT("USDSchemas"));

		for (const UE::FUsdPrim& Prim : Prims)
		{
			if (TSharedPtr<FUsdSchemaTranslator> SchemaTranslator = UsdSchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema(TranslationContext.AsShared(), UE::FUsdTyped(Prim)))
			{
				SchemaTranslator->CreateAssets();
			}
		}

		TranslationContext.CompleteTasks();
	}

	void ImportMaterials(FUsdStageImportContext& ImportContext, FUsdSchemaTranslationContext& TranslationContext)
	{
		if (!ImportContext.ImportOptions->bImportMaterials)
		{
			return;
		}

		TArray< UE::FUsdPrim > MaterialPrims = UsdUtils::GetAllPrimsOfType( ImportContext.Stage.GetPseudoRoot(), TEXT("UsdShadeMaterial") );

		CreateAssetsForPrims(MaterialPrims, TranslationContext);
	}

	void ImportMeshes(FUsdStageImportContext& ImportContext, FUsdSchemaTranslationContext& TranslationContext)
	{
#if USE_USD_SDK
		if (!ImportContext.ImportOptions->bImportGeometry)
		{
			return;
		}

		IUsdSchemasModule& UsdSchemasModule = FModuleManager::Get().LoadModuleChecked<IUsdSchemasModule>(TEXT("USDSchemas"));

		auto PruneCollapsedMeshes = [&UsdSchemasModule, &TranslationContext](const UE::FUsdPrim& UsdPrim) -> bool
		{
			if (TSharedPtr< FUsdSchemaTranslator > SchemaTranslator = UsdSchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema(TranslationContext.AsShared(), UE::FUsdTyped(UsdPrim)))
			{
				return SchemaTranslator->CollapsesChildren(FUsdSchemaTranslator::ECollapsingType::Assets);
			}

			return false;
		};

		// If we're going to interpret LODs and have meshes in the cache that were parsed without interpreting LODs,
		// manually discard them or else we will end up with both versions in the content folder, as they will have different hashes
		if ( ImportContext.ImportOptions->bInterpretLODs && TranslationContext.bAllowInterpretingLODs && ImportContext.AssetsCache.Num() > 0 )
		{
			TMap<UObject*, FString> AssetsCacheInverted;
			for ( const TPair<FString, UObject*>& AssetsCachePair : ImportContext.AssetsCache )
			{
				AssetsCacheInverted.Add( AssetsCachePair.Value, AssetsCachePair.Key );
			}

			// Disable this flag before we search because whether we interpret LODs or not affects our collapsing rules,
			// and we want to find out the exact assets/prims that were parsed without interpreting LODs
			TranslationContext.bAllowInterpretingLODs = false;

			// We do meshes and skelroots separately here instead of just UsdGeomXformable because while SkelRoot and Mesh prims are both UsdGeomXformable,
			// SkelRoots contain the LOD variant set, while the Mesh prims are inside the actual variants, so we need to handle them differently. Plus we have to mind the skeleton anyway
			TArray< UE::FUsdPrim > MeshPrims = UsdUtils::GetAllPrimsOfType( ImportContext.Stage.GetPseudoRoot(), TEXT( "UsdGeomMesh" ), PruneCollapsedMeshes );
			TArray< UE::FUsdPrim > SkelRootPrims = UsdUtils::GetAllPrimsOfType( ImportContext.Stage.GetPseudoRoot(), TEXT( "UsdSkelRoot" ), PruneCollapsedMeshes );

			TranslationContext.bAllowInterpretingLODs = true;

			TArray<UObject*> AssetsToDiscard;
			for ( const UE::FUsdPrim& MeshPrim : MeshPrims )
			{
				if ( !UsdUtils::IsGeomMeshALOD( MeshPrim ) )
				{
					continue;
				}

				int32 NumUsdLODs = UsdUtils::GetNumberOfLODVariants( MeshPrim.GetParent() );
				if ( NumUsdLODs < 2 )
				{
					continue;
				}

				const FString& MeshPrimPath = MeshPrim.GetPrimPath().GetString();

				if ( UObject** FoundAsset = ImportContext.PrimPathsToAssets.Find( MeshPrimPath ) )
				{
					if ( UStaticMesh* OldStaticMesh = Cast<UStaticMesh>( *FoundAsset ) )
					{
						if ( OldStaticMesh->GetNumLODs() != NumUsdLODs )
						{
							AssetsToDiscard.Add(OldStaticMesh);
							ImportContext.PrimPathsToAssets.Remove( MeshPrimPath );
						}
					}
				}
			}

			for ( const UE::FUsdPrim& SkelRootPrim : SkelRootPrims )
			{
				int32 NumUsdLODs = UsdUtils::GetNumberOfLODVariants( SkelRootPrim );
				if ( NumUsdLODs < 2 )
				{
					continue;
				}

				const FString& SkelRootPrimPath = SkelRootPrim.GetPrimPath().GetString();

				if ( UObject** FoundAsset = ImportContext.PrimPathsToAssets.Find( SkelRootPrimPath ) )
				{
					if ( USkeletalMesh* OldSkeletalMesh = Cast<USkeletalMesh>( *FoundAsset ) )
					{
						if ( OldSkeletalMesh->GetLODNum() != NumUsdLODs )
						{
							AssetsToDiscard.Add( OldSkeletalMesh );
							AssetsToDiscard.Add( OldSkeletalMesh->Skeleton );
							ImportContext.PrimPathsToAssets.Remove( SkelRootPrimPath );
						}
					}
				}
			}

			for ( UObject* AssetToDiscard : AssetsToDiscard )
			{
				if ( FString* AssetsCacheKey = AssetsCacheInverted.Find( AssetToDiscard ) )
				{
					ImportContext.AssetsCache.Remove( *AssetsCacheKey );
				}
			}
		}

		TArray< UE::FUsdPrim > MeshPrims = UsdUtils::GetAllPrimsOfType( ImportContext.Stage.GetPseudoRoot(), TEXT("UsdGeomXformable"), PruneCollapsedMeshes );
		CreateAssetsForPrims(MeshPrims, TranslationContext);
#endif // #if USE_USD_SDK
	}

	void ImportActor(UE::FUsdPrim& Prim, FUsdSchemaTranslationContext& TranslationContext)
	{
		IUsdSchemasModule& UsdSchemasModule = FModuleManager::Get().LoadModuleChecked< IUsdSchemasModule >(TEXT("USDSchemas"));
		bool bExpandChilren = true;
		USceneComponent* Component = nullptr;

		// Spawn components and/or actors for this prim
		if (TSharedPtr<FUsdSchemaTranslator> SchemaTranslator = UsdSchemasModule.GetTranslatorRegistry().CreateTranslatorForSchema(TranslationContext.AsShared(),UE::FUsdTyped(Prim)))
		{
			Component = SchemaTranslator->CreateComponents();

			bExpandChilren = !SchemaTranslator->CollapsesChildren(FUsdSchemaTranslator::ECollapsingType::Components);
		}

		// Recurse to children
		if (bExpandChilren)
		{
			USceneComponent* ContextParentComponent = Component ? Component : TranslationContext.ParentComponent;
			TGuardValue<USceneComponent*> ParentComponentGuard(TranslationContext.ParentComponent, ContextParentComponent);

			const bool bTraverseInstanceProxies = true;
			for (UE::FUsdPrim ChildStore : Prim.GetFilteredChildren(bTraverseInstanceProxies))
			{
				ImportActor(ChildStore, TranslationContext);
			}
		}

		if (Component && !Component->IsRegistered())
		{
			Component->RegisterComponent();
		}
	}

	void ImportActors(FUsdStageImportContext& ImportContext, FUsdSchemaTranslationContext& TranslationContext)
	{
		if (!ImportContext.ImportOptions->bImportActors)
		{
			return;
		}

		UE::FUsdPrim RootPrim = ImportContext.Stage.GetPseudoRoot();
		ImportActor(RootPrim, TranslationContext);
	}

	void ImportAnimations(FUsdStageImportContext& ImportContext, FUsdSchemaTranslationContext& TranslationContext)
	{
		if (!ImportContext.ImportOptions->bImportActors)
		{
			return;
		}

		// TODO
	}

	// Assets coming out of USDSchemas module have default names, so here we do our best to provide them with
	// names based on the source prims. This is likely a temporary solution, as it may be interesting to do this in the
	// USDSchemas module itself
	FString GetUserFriendlyName(UObject* Asset, TSet<FString>& UniqueAssetNames)
	{
		if (!Asset)
		{
			return {};
		}

		FString AssetPrefix;
		FString AssetSuffix;
		FString AssetPath = Asset->GetName();

		if (UStaticMesh* Mesh = Cast<UStaticMesh>(Asset))
		{
			AssetPrefix = TEXT("SM_");

			if (UUsdAssetImportData* AssetImportData = Cast<UUsdAssetImportData>(Mesh->AssetImportData))
			{
				AssetPath = AssetImportData->PrimPath;
			}
		}
		else if (USkeletalMesh* SkMesh = Cast<USkeletalMesh>(Asset))
		{
			AssetPrefix = TEXT("SK_");

			if (UUsdAssetImportData* AssetImportData = Cast<UUsdAssetImportData>(SkMesh->AssetImportData))
			{
				AssetPath = AssetImportData->PrimPath;
			}
		}
		else if (USkeleton* Skeleton = Cast<USkeleton>(Asset))
		{
			AssetSuffix = TEXT("_Skeleton");

			// We always set the corresponding mesh as preview mesh on import. Fetching the name here is really important
			// as it can determine the destination path and how the asset conflicts are resolved
			if (USkeletalMesh* SkeletalMesh = Skeleton->GetPreviewMesh())
			{
				if (UUsdAssetImportData* AssetImportData = Cast<UUsdAssetImportData>(SkeletalMesh->AssetImportData))
				{
					AssetPath = AssetImportData->PrimPath;
				}
			}
		}
		else if ( UAnimSequence* AnimSequence = Cast<UAnimSequence>( Asset ) )
		{
			AssetPrefix = TEXT( "Anim_" );

			if (UUsdAssetImportData* AssetImportData = Cast<UUsdAssetImportData>(AnimSequence->AssetImportData))
			{
				AssetPath = AssetImportData->PrimPath;
			}
		}
		else if (UMaterialInterface* Material = Cast<UMaterialInterface>(Asset))
		{
			AssetPrefix = TEXT("M_");

			if (UUsdAssetImportData* AssetImportData = Cast<UUsdAssetImportData>(Material->AssetImportData))
			{
				// The only materials with no prim path are our auto-generated displayColor materials
				AssetPath = AssetImportData->PrimPath.IsEmpty()? TEXT("DisplayColor") : AssetImportData->PrimPath;
			}
		}
		else if (UTexture* Texture = Cast<UTexture>(Asset))
		{
			AssetPrefix = TEXT("T_");

			if (UUsdAssetImportData* AssetImportData = Cast<UUsdAssetImportData>(Texture->AssetImportData))
			{
				AssetPath = AssetImportData->GetFirstFilename();
			}
		}

		FString FinalName = FPaths::GetBaseFilename(AssetPath);
		if ( !FinalName.StartsWith( AssetPrefix ) )
		{
			FinalName = AssetPrefix + FinalName;
		}
		if ( !FinalName.EndsWith( AssetSuffix ) )
		{
			FinalName = FinalName + AssetSuffix;
		}

		// We don't care if our assets overwrite something in the final destination package (that conflict will be
		// handled according to EReplaceAssetPolicy). But we do want these assets to have unique names amongst themselves
		// or else they will overwrite each other when publishing
		FinalName = GetUniqueName( ObjectTools::SanitizeObjectName( FinalName ), UniqueAssetNames );
		UniqueAssetNames.Add(FinalName);

		return FinalName;
	}

	/** Used to discard assets from the cache that are not directly used by this translation context (e.g. old/other things) */
	void DiscardOldAssets( TMap<FString, UObject*>& AssetsCache, const TSet<UObject*> CurrentAssets )
	{
		TArray<FString> KeysToRemove;
		for ( TPair<FString, UObject*>& Pair : AssetsCache )
		{
			if ( !CurrentAssets.Contains( Pair.Value ) )
			{
				KeysToRemove.Add( Pair.Key );
			}
		}
		for ( const FString& KeyToRemove : KeysToRemove )
		{
			AssetsCache.Remove( KeyToRemove );
		}
	}

	void UpdateAssetImportData( UObject* Asset, const FString& MainFilePath, UUsdStageImportOptions* ImportOptions )
	{
		if ( !Asset )
		{
			return;
		}

		UUsdAssetImportData* ImportData = UsdUtils::GetAssetImportData( Asset );
		if ( !ImportData )
		{
			return;
		}

		// Don't force update as textures will already come with this preset to their actual texture path
		if ( ImportData->SourceData.SourceFiles.Num() == 0 )
		{
			ImportData->UpdateFilenameOnly( MainFilePath );
		}

		ImportData->ImportOptions = ImportOptions;
	}

	void UpdateAssetImportData(const TMap<FString, UObject*>& AssetsCache, const FString& MainFilePath, UUsdStageImportOptions* ImportOptions)
	{
		for ( const TPair<FString, UObject*>& AssetPair : AssetsCache )
		{
			UpdateAssetImportData( AssetPair.Value, MainFilePath, ImportOptions );
		}
	}

	// Moves Asset from its folder to the package at DestFullContentPath and sets up its flags.
	// Depending on ReplacePolicy it may replace the existing actor (if it finds one) or just abort
	UObject* PublishAsset(FUsdStageImportContext& ImportContext, UObject* Asset, const FString& DestFullPackagePath, TMap<UObject*, UObject*>& ObjectsToRemap)
	{
		if (!Asset)
		{
			return nullptr;
		}

		EReplaceAssetPolicy ReplacePolicy = ImportContext.ImportOptions->ExistingAssetPolicy;
		FString TargetPackagePath = UPackageTools::SanitizePackageName(DestFullPackagePath);
		FString TargetAssetName = FPaths::GetBaseFilename( TargetPackagePath );
		UObject* ExistingAsset = nullptr;
		UPackage* ExistingPackage = nullptr;

		if ( ReplacePolicy == EReplaceAssetPolicy::Append )
		{
			FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>( "AssetTools" );
			AssetToolsModule.Get().CreateUniqueAssetName( TargetPackagePath, TEXT(""), TargetPackagePath, TargetAssetName );
		}
		else
		{
			// See if we have an existing asset/package
			ExistingPackage = FindPackage( nullptr, *TargetPackagePath );
			if ( !ExistingPackage && FPackageName::DoesPackageExist( TargetPackagePath ) )
			{
				ExistingPackage = LoadPackage( nullptr, *TargetPackagePath, LOAD_None );
			}
			if ( ExistingPackage )
			{
				FSoftObjectPath ObjectPath( TargetPackagePath );
				ExistingAsset = static_cast< UObject* >( FindObjectWithOuter( ExistingPackage, Asset->GetClass() ) );
				if ( !ExistingAsset )
				{
					ExistingAsset = ObjectPath.TryLoad();
				}
			}

			// If we're ignoring assets that conflict, just abort now
			if ( ExistingAsset != nullptr && ExistingAsset != Asset && ReplacePolicy == EReplaceAssetPolicy::Ignore )
			{
				// Redirect any users of our new transient asset to the old, existing asset
				ObjectsToRemap.Add( Asset, ExistingAsset );
				return nullptr;
			}
		}

		// Close editors opened on existing asset if applicable
		bool bAssetWasOpen = false;
		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (ExistingAsset && AssetEditorSubsystem->FindEditorForAsset(ExistingAsset, false) != nullptr)
		{
			AssetEditorSubsystem->CloseAllEditorsForAsset(ExistingAsset);
			bAssetWasOpen = true;
		}

		UPackage* Package = ExistingPackage ? ExistingPackage : CreatePackage(*TargetPackagePath);
		if (!Package)
		{
			FUsdLogManager::LogMessage( EMessageSeverity::Error, FText::Format( LOCTEXT( "PublishFailure", "Failed to get destination package at '{0}' for imported asset '{1}'!" ), FText::FromString( TargetPackagePath ), FText::FromName( Asset->GetFName() ) ) );
			return nullptr;
		}
		Package->FullyLoad();

		FString OldAssetPathName;

		// Strategy copied from FDatasmithImporterImpl::PublicizeAsset
		// Replace existing asset (reimport or conflict) with new asset
		UObject* MovedAsset = ExistingAsset;
		if (ExistingAsset != nullptr && ExistingAsset != Asset && ReplacePolicy == EReplaceAssetPolicy::Replace)
		{
			// Release render state of existing meshes because we'll replace them
			TUniquePtr<FSkinnedMeshComponentRecreateRenderStateContext> SkinnedRecreateRenderStateContext;
			TUniquePtr<FStaticMeshComponentRecreateRenderStateContext> StaticRecreateRenderStateContext;
			if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(ExistingAsset))
			{
				SkinnedRecreateRenderStateContext = MakeUnique<FSkinnedMeshComponentRecreateRenderStateContext>(SkeletalMesh);
			}
			else if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(ExistingAsset))
			{
				StaticRecreateRenderStateContext = MakeUnique<FStaticMeshComponentRecreateRenderStateContext>(StaticMesh);
			}

			OldAssetPathName = ExistingAsset->GetPathName();

			MovedAsset = DuplicateObject<UObject>(Asset, Package, ExistingAsset->GetFName());

			// If mesh's label has changed, update its name
			if (ExistingAsset->GetFName() != Asset->GetFName())
			{
				// We can't dirty the package here. Read the comment around MarkPackageDirty, below
				MovedAsset->Rename(*TargetAssetName, Package, REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
			}

			if (UStaticMesh* DestinationMesh = Cast< UStaticMesh >(MovedAsset))
			{
				// This is done during the mesh build process but we need to redo it after the DuplicateObject since the links are now valid
				for (TObjectIterator< UStaticMeshComponent > It; It; ++It)
				{
					if (It->GetStaticMesh() == DestinationMesh)
					{
						It->FixupOverrideColorsIfNecessary(true);
						It->InvalidateLightingCache();
					}
				}
			}
		}
		else
		{
			// We can't dirty the package here. Read the comment around MarkPackageDirty, below
			Asset->Rename(*TargetAssetName, Package, REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty );
			MovedAsset = Asset;
		}

		if (MovedAsset != Asset)
		{
			ObjectsToRemap.Add(Asset, MovedAsset);
		}

		// Soft object ptrs won't update, so we need to do it manually as we use PreviewMesh when fetching AssetImportData
		// Note that we need to do this even if we never replaced/overwrote assets, as we will at least move from transient to the published folder
		if ( USkeletalMesh* MovedMeshAsset = Cast<USkeletalMesh>( MovedAsset ) )
		{
			if ( USkeleton* Skeleton = MovedMeshAsset->Skeleton )
			{
				Skeleton->SetPreviewMesh( MovedMeshAsset );
			}
		}

		// Important as some assets (e.g. material instances) are created with no flags
		MovedAsset->SetFlags(ImportContext.ImportObjectFlags);
		MovedAsset->ClearFlags(EObjectFlags::RF_Transient | EObjectFlags::RF_DuplicateTransient | EObjectFlags::RF_NonPIEDuplicateTransient);

		// We need to make sure that "dirtying the final package" is not added to the transaction, because if we undo this transaction
		// the assets should remain on their final destination, so we still want the packages to remain marked as dirty (as they're really not on the disk yet).
		// If we didn't suppress, the package would become transactional by this call. When undoing, the assets would still remain on the final package,
		// but the "dirtying" would be undone, so the engine would think the assets weren't dirty (i.e. were already saved), which is not true
		{
			TGuardValue< ITransaction* > SuppressTransaction{ GUndo, nullptr };
			Package->MarkPackageDirty();
		}

		if (!ExistingAsset)
		{
			FAssetRegistryModule::AssetCreated(MovedAsset);
		}
		else if (!OldAssetPathName.IsEmpty())
		{
			FAssetRegistryModule::AssetRenamed(MovedAsset, OldAssetPathName);
		}

		// Reopen asset editor if we were editing the asset
		if (bAssetWasOpen)
		{
			AssetEditorSubsystem->OpenEditorForAsset(MovedAsset);
		}

		return MovedAsset;
	}

	// Move imported assets from transient folder to their final package, updating AssetsCache to point to the moved assets
	void PublishAssets(FUsdStageImportContext& ImportContext, TMap<UObject*, UObject*>& ObjectsToRemap)
	{
		TSet<FString> UniqueAssetNames;

		for (TPair<FString, UObject*>& AssetPair : ImportContext.AssetsCache)
		{
			UObject* Asset = AssetPair.Value;
			if ( !Asset )
			{
				continue;
			}

			FString AssetTypeFolder;
			if ( ImportContext.ImportOptions->bPrimPathFolderStructure )
			{
				UUsdAssetImportData* ImportData = UsdUtils::GetAssetImportData( Asset );

				// For skeletal stuff, the primpaths point to the SkelRoot, so it is useful to place the assets in there,
				// as we'll always have at least the skeletal mesh and the skeleton
				if ( Asset->IsA( USkeletalMesh::StaticClass() ) || Asset->IsA( USkeleton::StaticClass() ) || Asset->IsA( UAnimSequence::StaticClass() ) )
				{
					AssetTypeFolder = ImportData->PrimPath;
				}
				else
				{
					AssetTypeFolder = FPaths::GetPath( ImportData->PrimPath );
				}
			}
			else
			{
				if (Asset->IsA(UMaterialInterface::StaticClass()))
				{
					AssetTypeFolder = "Materials";
				}
				else if (Asset->IsA(UStaticMesh::StaticClass()))
				{
					AssetTypeFolder = "StaticMeshes";
				}
				else if (Asset->IsA(UTexture::StaticClass()))
				{
					AssetTypeFolder = "Textures";
				}
				else if (Asset->IsA(USkeletalMesh::StaticClass()) || Asset->IsA(USkeleton::StaticClass()) || Asset->IsA(UAnimSequence::StaticClass()))
				{
					AssetTypeFolder = "SkeletalMeshes";
				}
			}

			FString TargetAssetName = GetUserFriendlyName(Asset, UniqueAssetNames);
			FString DestPackagePath = FPaths::Combine(ImportContext.PackagePath, ImportContext.ObjectName, AssetTypeFolder, TargetAssetName);
			PublishAsset(ImportContext, Asset, DestPackagePath, ObjectsToRemap);
		}
	}

	void ResolveComponentConflict(USceneComponent* NewRoot, USceneComponent* ExistingRoot, EReplaceActorPolicy ReplacePolicy, TMap<UObject*, UObject*>& ObjectsToRemap)
	{
		if (!NewRoot || !ExistingRoot || ReplacePolicy == EReplaceActorPolicy::Append)
		{
			return;
		}

		ObjectsToRemap.Add(ExistingRoot, NewRoot);

		TArray<USceneComponent*> ExistingComponents = ExistingRoot->GetAttachChildren();
		TArray<USceneComponent*> NewComponents = NewRoot->GetAttachChildren();

		AActor* NewActor = NewRoot->GetOwner();
		AActor* ExistingActor = ExistingRoot->GetOwner();

		const auto CatalogByName = [](AActor* Owner, const TArray<USceneComponent*>& Components, TMap<FString, USceneComponent*>& Map)
		{
			for (USceneComponent* Component : Components)
			{
				if (Component->GetOwner() == Owner)
				{
					Map.Add(Component->GetName(), Component);
				}
			}
		};

		TMap<FString, USceneComponent*> ExistingComponentsByName;
		TMap<FString, USceneComponent*> NewComponentsByName;
		CatalogByName(ExistingActor, ExistingComponents, ExistingComponentsByName);
		CatalogByName(NewActor, NewComponents, NewComponentsByName);

		// Handle conflict between new and existing hierarchies
		for (const TPair<FString, USceneComponent*>& NewPair : NewComponentsByName)
		{
			const FString& Name = NewPair.Key;
			USceneComponent* NewComponent = NewPair.Value;

			if (USceneComponent** FoundExistingComponent = ExistingComponentsByName.Find(Name))
			{
				bool bRecurse = false;

				switch (ReplacePolicy)
				{
				case EReplaceActorPolicy::UpdateTransform:
					(*FoundExistingComponent)->SetRelativeTransform(NewComponent->GetRelativeTransform());
					(*FoundExistingComponent)->AttachToComponent(NewRoot, FAttachmentTransformRules::KeepRelativeTransform);
					bRecurse = true;
					break;
				case EReplaceActorPolicy::Ignore:
					// Note how we're iterating the new hierarchy here, so "ignore" means "keep the existing one"
					NewComponent->DestroyComponent(false);
					(*FoundExistingComponent)->AttachToComponent(NewRoot, FAttachmentTransformRules::KeepRelativeTransform);
					bRecurse = false;
					break;
				case EReplaceActorPolicy::Replace:
				default:
					// Keep NewChild completely, but recurse to replace components and children
					bRecurse = true;
					break;
				}

				if (bRecurse)
				{
					ResolveComponentConflict(NewComponent, *FoundExistingComponent, ReplacePolicy, ObjectsToRemap);
				}
			}
		}

		// Move child components from the existing hierarchy that don't conflict with anything in the new hierarchy,
		// as the new hierarchy is the one that will remain. Do these later so that we don't recurse into them
		for (const TPair<FString, USceneComponent*>& ExistingPair : ExistingComponentsByName)
		{
			const FString& Name = ExistingPair.Key;
			USceneComponent* ExistingComponent = ExistingPair.Value;

			USceneComponent** FoundNewComponent = NewComponentsByName.Find(Name);
			if (!FoundNewComponent)
			{
				ExistingComponent->AttachToComponent(NewRoot, FAttachmentTransformRules::KeepRelativeTransform);
			}
		}
	}

	void RecursiveDestroyActor(AActor* Actor)
	{
		if (!Actor)
		{
			return;
		}

		const bool bResetArray = false;
		TArray<AActor*> Children;
		Actor->GetAttachedActors(Children, bResetArray);

		for (AActor* Child : Children)
		{
			RecursiveDestroyActor(Child);
		}

		Actor->GetWorld()->DestroyActor(Actor);
	}

	void ResolveActorConflict(AActor* NewActor, AActor* ExistingActor, EReplaceActorPolicy ReplacePolicy, TMap<UObject*, UObject*>& ObjectsToRemap)
	{
		if (!NewActor || !ExistingActor || ReplacePolicy == EReplaceActorPolicy::Append)
		{
			return;
		}

		ObjectsToRemap.Add(ExistingActor, NewActor);

		// Collect new and existing actors by label
		const bool bResetArray = false;
		TArray<AActor*> ExistingChildren;
		TArray<AActor*> NewChildren;
		ExistingActor->GetAttachedActors(ExistingChildren, bResetArray);
		NewActor->GetAttachedActors(NewChildren, bResetArray);
		const auto CatalogByLabel = [](const TArray<AActor*>& Actors, TMap<FString, AActor*>& Map)
		{
			for (AActor* Actor : Actors)
			{
				Map.Add(Actor->GetActorLabel(), Actor);
			}
		};
		TMap<FString, AActor*> ExistingChildrenByLabel;
		TMap<FString, AActor*> NewChildrenByLabel;
		CatalogByLabel(ExistingChildren, ExistingChildrenByLabel);
		CatalogByLabel(NewChildren, NewChildrenByLabel);

		// Handle conflicts between new and existing actor hierarchies
		for (const TPair<FString, AActor*>& NewPair : NewChildrenByLabel)
		{
			const FString& Label = NewPair.Key;
			AActor* NewChild = NewPair.Value;

			// There's a conflict
			if (AActor** ExistingChild = ExistingChildrenByLabel.Find(Label))
			{
				bool bRecurse = false;

				switch (ReplacePolicy)
				{
				case EReplaceActorPolicy::UpdateTransform:
					(*ExistingChild)->GetRootComponent()->SetRelativeTransform(NewChild->GetRootComponent()->GetRelativeTransform());
					GEditor->ParentActors( NewActor, *ExistingChild, NAME_None );
					bRecurse = true;
					break;
				case EReplaceActorPolicy::Ignore:
					// Note how we're iterating the new hierarchy here, so "ignore" means "keep the existing one"
					RecursiveDestroyActor(NewChild);
					GEditor->ParentActors(NewActor, *ExistingChild, NAME_None);
					bRecurse = false;
					break;
				case EReplaceActorPolicy::Replace:
				default:
					// Keep NewChild, but recurse to replace components and children
					bRecurse = true;
					break;
				}

				if (bRecurse)
				{
					ResolveActorConflict(NewChild, *ExistingChild, ReplacePolicy, ObjectsToRemap);
				}
			}
		}

		// Handle component hierarchy collisions
		USceneComponent* ExistingRoot = ExistingActor->GetRootComponent();
		USceneComponent* NewRoot = NewActor->GetRootComponent();
		ResolveComponentConflict(NewRoot, ExistingRoot, ReplacePolicy, ObjectsToRemap);

		// Move child actors over from existing hierarchy that don't conflict with anything in new hierarchy
		// Do these later so that we don't recurse into them
		for (const TPair<FString, AActor*>& ExistingPair : ExistingChildrenByLabel)
		{
			const FString& Label = ExistingPair.Key;
			AActor* ExistingChild = ExistingPair.Value;

			AActor** NewChild = NewChildrenByLabel.Find(Label);
			if (NewChild == nullptr)
			{
				GEditor->ParentActors(NewActor, ExistingChild, NAME_None);
			}
		}
	}

	void ResolveActorConflicts(FUsdStageImportContext& ImportContext, AActor* ExistingSceneActor, TMap<UObject*, UObject*>& ObjectsToRemap)
	{
		if ( !ImportContext.ImportOptions->bImportActors )
		{
			return;
		}

		if (!ImportContext.SceneActor)
		{
			FUsdLogManager::LogMessage( EMessageSeverity::Error, LOCTEXT( "NoSceneActor", "Failed to publish actors as there was no scene actor available!" ) );
			return;
		}

		EReplaceActorPolicy ReplacePolicy = ImportContext.ImportOptions->ExistingActorPolicy;

		// No conflicts, nothing to replace or redirect (even with Append replace mode we don't want to redirect references to the existing items)
		if (!ExistingSceneActor || ReplacePolicy == EReplaceActorPolicy::Append)
		{
			return;
		}

		ResolveActorConflict(ImportContext.SceneActor, ExistingSceneActor, ReplacePolicy, ObjectsToRemap);
	}

	// If we just reimported a static mesh, we use this to remap the material references to the existing materials, as any
	// materials we just reimported will be discarded
	void CopyOriginalMaterialAssignment(FUsdStageImportContext& ImportContext, UObject* ExistingAsset, UObject* NewAsset)
	{
		UStaticMesh* ExistingMesh = Cast<UStaticMesh>(ExistingAsset);
		UStaticMesh* NewMesh = Cast<UStaticMesh>(NewAsset);

		if (ExistingAsset && NewMesh)
		{
			int32 NumExistingMaterials = ExistingMesh->StaticMaterials.Num();
			int32 NumNewMaterials = NewMesh->StaticMaterials.Num();

			for (int32 NewMaterialIndex = 0; NewMaterialIndex < NumNewMaterials; ++NewMaterialIndex)
			{
				UMaterialInterface* ExistingMaterial = ExistingMesh->GetMaterial(NewMaterialIndex);

				// Can't use SetMaterial as it starts a scoped transaction that would hold on to our transient assets...
				NewMesh->StaticMaterials[NewMaterialIndex].MaterialInterface = ExistingMaterial;
			}

			// Clear out any other assignments we may have
			for (int32 Index = NumNewMaterials; Index < NumExistingMaterials; ++Index)
			{
				NewMesh->StaticMaterials[Index].MaterialInterface = nullptr;
			}

			return;
		}

		USkeletalMesh* ExistingSkeletalMesh = Cast<USkeletalMesh>(ExistingAsset);
		USkeletalMesh* NewSkeletalMesh = Cast<USkeletalMesh>(NewAsset);
		if (ExistingSkeletalMesh && NewSkeletalMesh)
		{
			NewSkeletalMesh->Materials = ExistingSkeletalMesh->Materials;
			return;
		}
	}

	void CopySkeletonAssignment( FUsdStageImportContext& ImportContext, UObject* ExistingAsset, UObject* NewAsset )
	{
		USkeletalMesh* ExistingSkeletalMesh = Cast<USkeletalMesh>( ExistingAsset );
		USkeletalMesh* NewSkeletalMesh = Cast<USkeletalMesh>( NewAsset );
		if ( ExistingSkeletalMesh && NewSkeletalMesh )
		{
			// Never assign a transient skeleton
			if ( ExistingSkeletalMesh->Skeleton && ExistingSkeletalMesh->Skeleton->GetOutermost() == GetTransientPackage() )
			{
				return;
			}

			// Assign even if ExistingSkeletalMesh has nullptr skeleton because we must be able to cleanup the
			// abandoned Skeleton in the transient package
			NewSkeletalMesh->Skeleton = ExistingSkeletalMesh->Skeleton;
		}

		UAnimSequence* ExistingAnimSequence = Cast<UAnimSequence>( ExistingAsset );
		UAnimSequence* NewAnimSequence = Cast<UAnimSequence>( NewAsset );
		if ( ExistingAnimSequence && NewAnimSequence )
		{
			// Never assign a transient skeleton
			USkeleton* ExistingSkeleton = ExistingAnimSequence->GetSkeleton();
			if ( ExistingSkeleton && ExistingSkeleton->GetOutermost() == GetTransientPackage() )
			{
				return;
			}

			NewAnimSequence->SetSkeleton( ExistingSkeleton );
		}
	}

	// Adapted from FDatasmithImporterImpl::FixReferencesForObject
	void RemapReferences(FUsdStageImportContext& ImportContext, const TMap< UObject*, UObject* >& ObjectsToRemap)
	{
		if (ObjectsToRemap.Num() == 0)
		{
			return;
		}

		TSet<UObject*> Referencers;
		if ( AActor* SceneActor = ImportContext.SceneActor )
		{
			// Remap references to spawned actors
			Referencers.Add( ImportContext.SceneActor->GetWorld()->GetCurrentLevel() );
		}
		for ( const TPair<FString, UObject*>& Pair : ImportContext.AssetsCache )
		{
			// Remap references held by assets that were moved directly to the destination package, and won't be in ObjectsToRemap
			Referencers.Add( Pair.Value );
		}
		for ( const TPair<UObject*, UObject*>& Pair : ObjectsToRemap )
		{
			// Remap internal references between the remapped objects
			Referencers.Add( Pair.Value );
		}

		// Fix references between actors and assets (e.g. mesh in final package referencing material in transient package)
		// Note we don't care if transient assets reference each other, as we'll delete them all at once anyway
		for ( UObject* Referencer : Referencers )
		{
			if ( !Referencer || Referencer->GetOutermost() == GetTransientPackage() )
			{
				continue;
			}

			constexpr bool bNullPrivateRefs = false;
			constexpr bool bIgnoreOuterRef = true;
			constexpr bool bIgnoreArchetypeRef = true;
			FArchiveReplaceObjectRef< UObject > ArchiveReplaceObjectRefInner(
				Referencer,
				ObjectsToRemap,
				bNullPrivateRefs,
				bIgnoreOuterRef,
				bIgnoreArchetypeRef);
		}
	}

	void Cleanup(AActor* NewSceneActor, AActor* ExistingSceneActor, EReplaceActorPolicy ReplacePolicy)
	{
		if ( !NewSceneActor )
		{
			return;
		}

		// By this point all of our actors and components are moved to the new hierarchy, and all references
		// are remapped. So let's clear the replaced existing actors and components
		if (ExistingSceneActor && ExistingSceneActor != NewSceneActor && ReplacePolicy == EReplaceActorPolicy::Replace)
		{
			RecursiveDestroyActor(ExistingSceneActor);
		}
	}

	void CloseStageIfNeeded(FUsdStageImportContext& ImportContext)
	{
#if USE_USD_SDK
		// Remove our imported stage from the stage cache if it wasn't in there to begin with
		if (!ImportContext.bStageWasOriginallyOpen && ImportContext.bReadFromStageCache)
		{
			UnrealUSDWrapper::EraseStageFromCache(ImportContext.Stage);
		}

		// Restore original meters per unit if the stage was already loaded
		if ( ImportContext.bStageWasOriginallyOpen )
		{
			UsdUtils::SetUsdStageMetersPerUnit( ImportContext.Stage, ImportContext.OriginalMetersPerUnit );
		}
#endif // #if USE_USD_SDK
	}

	/** This returns the outer imported folder so that we can return it from the factories and have the content browser navigate to it */
	void FetchMainImportedPackage( FUsdStageImportContext& ImportContext )
	{
		FString PackagePath = UPackageTools::SanitizePackageName( FPaths::Combine( ImportContext.PackagePath, ImportContext.ObjectName ) );

		UPackage* ImportedPackage = FindPackage( nullptr, *PackagePath );
		if ( !ImportedPackage && FPackageName::DoesPackageExist( PackagePath ) )
		{
			ImportedPackage = LoadPackage( nullptr, *PackagePath, LOAD_None );
		}

		if ( !ImportedPackage )
		{
			ImportedPackage = CreatePackage( *PackagePath );
		}

		ImportContext.ImportedPackage = ImportedPackage;
	}
}

void UUsdStageImporter::ImportFromFile(FUsdStageImportContext& ImportContext)
{
#if USE_USD_SDK
	if (!ImportContext.World)
	{
		FUsdLogManager::LogMessage( EMessageSeverity::Error, LOCTEXT( "NoWorldError", "Failed to import USD Stage because the target UWorld is invalid!" ) );
		return;
	}

	ImportContext.Stage = UsdStageImporterImpl::ReadUsdFile(ImportContext);
	if (!ImportContext.Stage)
	{
		FUsdLogManager::LogMessage( EMessageSeverity::Error, LOCTEXT( "NoStageError", "Failed to open the USD Stage!" ) );
		return;
	}

	UsdStageImporterImpl::SetupSceneActor(ImportContext);
	if (!ImportContext.SceneActor && ImportContext.ImportOptions->bImportActors)
	{
		return;
	}

	FUsdDelegates::OnPreUsdImport.Broadcast( ImportContext.FilePath );

	AActor* ExistingSceneActor = UsdStageImporterImpl::GetExistingSceneActor(ImportContext);

	UsdStageImporterImpl::SetupStageForImport(ImportContext);

	TMap<UObject*, UObject*> ObjectsToRemap;
	UsdUtils::FBlendShapeMap BlendShapesByPath;

	TSharedRef<FUsdSchemaTranslationContext> TranslationContext = MakeShared<FUsdSchemaTranslationContext>(ImportContext.Stage, ImportContext.PrimPathsToAssets, ImportContext.AssetsCache, &BlendShapesByPath );
	TranslationContext->Level = ImportContext.World->GetCurrentLevel();
	TranslationContext->ObjectFlags = ImportContext.ImportObjectFlags;
	TranslationContext->Time = ImportContext.ImportOptions->ImportTime;
	TranslationContext->PurposesToLoad = (EUsdPurpose) ImportContext.ImportOptions->PurposesToImport;
	TranslationContext->ParentComponent = ImportContext.SceneActor ? ImportContext.SceneActor->GetRootComponent() : nullptr;
	TranslationContext->bAllowCollapsing = ImportContext.ImportOptions->bCollapse;
	TranslationContext->bAllowInterpretingLODs = ImportContext.ImportOptions->bInterpretLODs;
	TranslationContext->bAllowParsingSkeletalAnimations = ImportContext.ImportOptions->bImportSkeletalAnimations;
	{
		UsdStageImporterImpl::ImportMaterials(ImportContext, TranslationContext.Get());
		UsdStageImporterImpl::ImportMeshes(ImportContext, TranslationContext.Get());
		UsdStageImporterImpl::ImportActors(ImportContext, TranslationContext.Get());
		UsdStageImporterImpl::ImportAnimations(ImportContext, TranslationContext.Get());
	}
	TranslationContext->CompleteTasks();

	UsdStageImporterImpl::DiscardOldAssets(ImportContext.AssetsCache, TranslationContext->CurrentlyUsedAssets);
	UsdStageImporterImpl::UpdateAssetImportData(ImportContext.AssetsCache, ImportContext.FilePath, ImportContext.ImportOptions);
	UsdStageImporterImpl::PublishAssets(ImportContext, ObjectsToRemap);
	UsdStageImporterImpl::ResolveActorConflicts(ImportContext, ExistingSceneActor, ObjectsToRemap);
	UsdStageImporterImpl::RemapReferences(ImportContext, ObjectsToRemap);
	UsdStageImporterImpl::Cleanup(ImportContext.SceneActor, ExistingSceneActor, ImportContext.ImportOptions->ExistingActorPolicy);
	UsdStageImporterImpl::CloseStageIfNeeded(ImportContext);
	UsdStageImporterImpl::FetchMainImportedPackage(ImportContext);

	FUsdDelegates::OnPostUsdImport.Broadcast( ImportContext.FilePath );
#endif // #if USE_USD_SDK
}

bool UUsdStageImporter::ReimportSingleAsset(FUsdStageImportContext& ImportContext, UObject* OriginalAsset, UUsdAssetImportData* OriginalImportData, UObject*& OutReimportedAsset)
{
	OutReimportedAsset = nullptr;
	bool bSuccess = false;

#if USE_USD_SDK
	ImportContext.Stage = UsdStageImporterImpl::ReadUsdFile(ImportContext);
	if (!ImportContext.Stage)
	{
		FUsdLogManager::LogMessage( EMessageSeverity::Error, LOCTEXT( "NoStageError", "Failed to open the USD Stage!" ) );
		return bSuccess;
	}

	FUsdDelegates::OnPreUsdImport.Broadcast(ImportContext.FilePath);

	// We still need the scene actor to remap all other users of the mesh to the new reimported one. It's not critical if we fail though,
	// the goal is to just reimport the asset
	UsdStageImporterImpl::SetupSceneActor(ImportContext);

	UsdStageImporterImpl::SetupStageForImport( ImportContext );

	TMap<UObject*, UObject*> ObjectsToRemap;
	UsdUtils::FBlendShapeMap BlendShapesByPath;

	TSharedRef<FUsdSchemaTranslationContext> TranslationContext = MakeShared<FUsdSchemaTranslationContext>( ImportContext.Stage, ImportContext.PrimPathsToAssets, ImportContext.AssetsCache, &BlendShapesByPath );
	TranslationContext->Level = ImportContext.World->GetCurrentLevel();
	TranslationContext->ObjectFlags = ImportContext.ImportObjectFlags;
	TranslationContext->Time = ImportContext.ImportOptions->ImportTime;
	TranslationContext->PurposesToLoad = (EUsdPurpose) ImportContext.ImportOptions->PurposesToImport;
	TranslationContext->bAllowCollapsing = ImportContext.ImportOptions->bCollapse;
	TranslationContext->bAllowInterpretingLODs = ImportContext.ImportOptions->bInterpretLODs;
	TranslationContext->bAllowParsingSkeletalAnimations = ImportContext.ImportOptions->bImportSkeletalAnimations;
	{
		UE::FUsdPrim TargetPrim = ImportContext.Stage.GetPrimAtPath( UE::FSdfPath( *OriginalImportData->PrimPath ) );
		if ( TargetPrim )
		{
			UsdStageImporterImpl::CreateAssetsForPrims({TargetPrim}, TranslationContext.Get());
		}
	}
	TranslationContext->CompleteTasks();

	// Look for our reimported asset in the assets cache as we may have multiple assets with the same prim path
	UObject* ReimportedObject = nullptr;
	for ( TPair<FString, UObject*> AssetPair : ImportContext.AssetsCache )
	{
		UObject* Asset = AssetPair.Value;
		UUsdAssetImportData* NewAssetImportData = UsdUtils::GetAssetImportData( Asset );

		if ( Asset &&
			 NewAssetImportData &&
			 Asset->GetClass() == OriginalAsset->GetClass() &&
			 NewAssetImportData->PrimPath.Equals( OriginalImportData->PrimPath, ESearchCase::CaseSensitive ) )
		{
			ReimportedObject = Asset;
			break;
		}
	}

	if ( ReimportedObject )
	{
		UsdStageImporterImpl::UpdateAssetImportData( ReimportedObject, ImportContext.FilePath, ImportContext.ImportOptions);

		// Assign things from the original assets before we publish the reimported asset, overwriting it
		UsdStageImporterImpl::CopyOriginalMaterialAssignment(ImportContext, OriginalAsset, ReimportedObject );
		UsdStageImporterImpl::CopySkeletonAssignment(ImportContext, OriginalAsset, ReimportedObject );

		// Just publish the one asset we wanted to reimport. Note that we may have other assets here too, but we'll ignore those e.g. a displayColor material or a skeleton
		OutReimportedAsset = UsdStageImporterImpl::PublishAsset(ImportContext, ReimportedObject, OriginalAsset->GetOutermost()->GetPathName(), ObjectsToRemap);
		UsdStageImporterImpl::RemapReferences( ImportContext, ObjectsToRemap );

		bSuccess = OutReimportedAsset != nullptr && TranslationContext->CurrentlyUsedAssets.Contains( ReimportedObject );
	}

	UsdStageImporterImpl::Cleanup( ImportContext.SceneActor, nullptr, ImportContext.ImportOptions->ExistingActorPolicy );
	UsdStageImporterImpl::CloseStageIfNeeded( ImportContext );
	UsdStageImporterImpl::FetchMainImportedPackage( ImportContext );

	FUsdDelegates::OnPostUsdImport.Broadcast(ImportContext.FilePath);

#endif // #if USE_USD_SDK
	return bSuccess;
}

#undef LOCTEXT_NAMESPACE

