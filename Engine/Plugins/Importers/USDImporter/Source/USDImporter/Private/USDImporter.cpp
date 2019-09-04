// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "USDImporter.h"
#include "Misc/ScopedSlowTask.h"
#include "AssetSelection.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "Editor/MainFrame/Public/Interfaces/IMainFrameModule.h"
#include "ObjectTools.h"
#include "MessageLogModule.h"
#include "IMessageLogListing.h"
#include "AssetRegistryModule.h"
#include "PackageTools.h"
#include "USDConversionUtils.h"
#include "StaticMeshImporter.h"
#include "Engine/StaticMesh.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "USDImporterProjectSettings.h"
#include "USDPrimResolverKind.h"

#include "USDMemory.h"

#if USE_USD_SDK
#include "USDIncludesStart.h"

#include "pxr/base/tf/token.h"
#include "pxr/usd/usd/modelAPI.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usd/variantSets.h"

#include "USDIncludesEnd.h"
#endif // #if USE_USD_SDK

#define LOCTEXT_NAMESPACE "USDImportPlugin"

DEFINE_LOG_CATEGORY(LogUSDImport);


class SUSDOptionsWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SUSDOptionsWindow)
		: _ImportOptions(nullptr)
	{}

	SLATE_ARGUMENT(UObject*, ImportOptions)
		SLATE_ARGUMENT(TSharedPtr<SWindow>, WidgetWindow)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs)
	{
		ImportOptions = InArgs._ImportOptions;
		Window = InArgs._WidgetWindow;
		bShouldImport = false;

		TSharedPtr<SBox> DetailsViewBox;
		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(2)
			[
				SAssignNew(DetailsViewBox, SBox)
				.MaxDesiredHeight(450.0f)
				.MinDesiredWidth(550.0f)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.Padding(2)
			[
				SNew(SUniformGridPanel)
				.SlotPadding(2)
				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("USDOptionWindow_Import", "Import"))
					.OnClicked(this, &SUSDOptionsWindow::OnImport)
				]
				+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.Text(LOCTEXT("USDOptionWindow_Cancel", "Cancel"))
					.ToolTipText(LOCTEXT("USDOptionWindow_Cancel_ToolTip", "Cancels importing this USD file"))
					.OnClicked(this, &SUSDOptionsWindow::OnCancel)
				]
			]
		];

		FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		FDetailsViewArgs DetailsViewArgs;
		DetailsViewArgs.bAllowSearch = false;
		DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		TSharedPtr<IDetailsView> DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);

		DetailsViewBox->SetContent(DetailsView.ToSharedRef());
		DetailsView->SetObject(ImportOptions);

	}

	virtual bool SupportsKeyboardFocus() const override { return true; }

	FReply OnImport()
	{
		bShouldImport = true;
		if (Window.IsValid())
		{
			Window.Pin()->RequestDestroyWindow();
		}
		return FReply::Handled();
	}


	FReply OnCancel()
	{
		bShouldImport = false;
		if (Window.IsValid())
		{
			Window.Pin()->RequestDestroyWindow();
		}
		return FReply::Handled();
	}

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
	{
		if (InKeyEvent.GetKey() == EKeys::Escape)
		{
			return OnCancel();
		}

		return FReply::Unhandled();
	}

	bool ShouldImport() const
	{
		return bShouldImport;
	}

private:
	UObject* ImportOptions;
	TWeakPtr< SWindow > Window;
	bool bShouldImport;
};


UUSDImporter::UUSDImporter(const FObjectInitializer& Initializer)
	: Super(Initializer)
{
}

#if USE_USD_SDK
TArray<UObject*> UUSDImporter::ImportMeshes(FUsdImportContext& ImportContext, const TArray<FUsdAssetPrimToImport>& PrimsToImport)
{
	FScopedSlowTask SlowTask(1.0f, LOCTEXT("ImportingUSDMeshes", "Importing USD Meshes"));
	SlowTask.Visibility = ESlowTaskVisibility::ForceVisible;
	int32 MeshCount = 0;

	EUsdMeshImportType MeshImportType = ImportContext.ImportOptions->MeshImportType;

	// Make unique names
	TMap<FString, int> ExistingNamesToCount;

	ImportContext.PathToImportAssetMap.Reserve(PrimsToImport.Num());

	TArray<UObject*> ImportedAssets;

	const FString& ContentDirectoryLocation = ImportContext.ImportPathName;

	const FString RootPrimName = USDToUnreal::ConvertString( ImportContext.Stage.Get()->GetDefaultPrim().GetName().GetString().c_str() );
	const FString RootPrimDirectoryLocation = RootPrimName + TEXT("/");

	for (const FUsdAssetPrimToImport& PrimToImport : PrimsToImport)
	{
		FString FinalPackagePathName = ContentDirectoryLocation;
		SlowTask.EnterProgressFrame(1.0f / PrimsToImport.Num(), FText::Format(LOCTEXT("ImportingUSDMesh", "Importing Mesh {0} of {1}"), MeshCount + 1, PrimsToImport.Num()));

		FString NewPackageName;

		bool bShouldImport = false;

		// when importing only one mesh we just use the existing package and name created
		{
			FScopedUsdAllocs UsdAllocs;
			{
				const pxr::UsdPrim& Prim = PrimToImport.Prim.Get();

				auto GetEnclosingModelPrim = []( const pxr::UsdPrim& Prim ) -> pxr::UsdPrim
				{
					pxr::UsdPrim ModelPrim = Prim.GetParent();

					while ( ModelPrim )
					{
						if ( IUsdPrim::IsKindChildOf( ModelPrim, "model" ) )
						{
							break;
						}
						else
						{
							ModelPrim = ModelPrim.GetParent();
						}
					}

					return ModelPrim.IsValid() ? ModelPrim : Prim;
				};

				const pxr::UsdPrim& ModelPrim = GetEnclosingModelPrim( Prim );

				const FString RawPrimName = USDToUnreal::ConvertString(Prim.GetName().GetString());

				pxr::UsdModelAPI ModelApi = pxr::UsdModelAPI( ModelPrim );

				std::string RawAssetName;
				ModelApi.GetAssetName( &RawAssetName );

				FString AssetName = USDToUnreal::ConvertString( RawAssetName );
				FString MeshName = ObjectTools::SanitizeObjectName(RawPrimName);

				if (ImportContext.ImportOptions->bGenerateUniquePathPerUSDPrim)
				{
					FString USDPath = USDToUnreal::ConvertString(Prim.GetPrimPath().GetString().c_str());

					pxr::SdfAssetPath AssetPath;
					if ( ModelApi.GetAssetIdentifier( &AssetPath ) )
					{
						std::string AssetIdentifier = AssetPath.GetAssetPath();
						USDPath = USDToUnreal::ConvertString(AssetIdentifier.c_str());

						USDPath = FPaths::ConvertRelativePathToFull( RootPrimDirectoryLocation, USDPath );

						FPackageName::TryConvertFilenameToLongPackageName( USDPath, USDPath );
						USDPath.RemoveFromEnd( AssetName );
					}

					FString VariantName;

					if ( ModelPrim.HasVariantSets() )
					{
						pxr::UsdVariantSet ModelVariantSet = ModelPrim.GetVariantSet( "modelingVariant" );
						if ( ModelVariantSet.IsValid() )
						{
							std::string VariantSelection = ModelVariantSet.GetVariantSelection();

							if ( VariantSelection.length() > 0 )
							{
								VariantName = USDToUnreal::ConvertString( VariantSelection.c_str() );
							}
						}
					}

					if ( !VariantName.IsEmpty() )
					{
						USDPath = USDPath / VariantName;
					}

					USDPath.RemoveFromStart(TEXT("/"));
					USDPath.RemoveFromEnd(RawPrimName);
					FinalPackagePathName /= (USDPath / MeshName);
				}
				else if (FPackageName::IsValidObjectPath(PrimToImport.AssetPath))
				{
					FinalPackagePathName = PrimToImport.AssetPath;
				}
				else if (!PrimToImport.AssetPath.IsEmpty())
				{
					FinalPackagePathName /= PrimToImport.AssetPath;
				}
				else
				{
					// Make unique names
					int* ExistingCount = ExistingNamesToCount.Find(MeshName);
					if (ExistingCount)
					{
						MeshName += TEXT("_");
						MeshName.AppendInt(*ExistingCount);
						++(*ExistingCount);
					}
					else
					{
						ExistingNamesToCount.Add(MeshName, 1);
					}

					FinalPackagePathName / MeshName;
				}
			}

			NewPackageName = UPackageTools::SanitizePackageName(FinalPackagePathName);
		
			// Once we've already imported it we dont need to import it again
			if(!ImportContext.PathToImportAssetMap.Contains(NewPackageName))
			{
				UPackage* Package = CreatePackage(nullptr, *NewPackageName);

				Package->FullyLoad();

				ImportContext.Parent = Package;
				ImportContext.ObjectName = FPackageName::GetShortName(FinalPackagePathName);

				bShouldImport = true;
			}
			else
			{
				ImportedAssets.Add( ImportContext.PathToImportAssetMap[NewPackageName] );
			}

		}

		if(bShouldImport)
		{
			UObject* NewMesh = ImportSingleMesh(ImportContext, MeshImportType, PrimToImport);

			if (NewMesh)
			{
				FAssetRegistryModule::AssetCreated(NewMesh);

				NewMesh->MarkPackageDirty();
				ImportContext.PathToImportAssetMap.Add(NewPackageName, NewMesh);
				ImportedAssets.Add(NewMesh);
				++MeshCount;
			}
		}
	}

	return ImportedAssets;
}

UObject* UUSDImporter::ImportSingleMesh(FUsdImportContext& ImportContext, EUsdMeshImportType ImportType, const FUsdAssetPrimToImport& PrimToImport)
{
	UObject* NewMesh = nullptr;

	if (ImportType == EUsdMeshImportType::StaticMesh)
	{
		NewMesh = FUSDStaticMeshImporter::ImportStaticMesh(ImportContext, PrimToImport);
	}

	return NewMesh;
}
#endif // #if USE_USD_SDK

bool UUSDImporter::ShowImportOptions(UObject& ImportOptions)
{
	TSharedPtr<SWindow> ParentWindow;

	if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
	{
		IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
		ParentWindow = MainFrame.GetParentWindow();
	}

	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("USDImportSettings", "USD Import Options"))
		.SizingRule(ESizingRule::Autosized);


	TSharedPtr<SUSDOptionsWindow> OptionsWindow;
	Window->SetContent
	(
		SAssignNew(OptionsWindow, SUSDOptionsWindow)
		.ImportOptions(&ImportOptions)
		.WidgetWindow(Window)
	);

	FSlateApplication::Get().AddModalWindow(Window, ParentWindow, false);

	return OptionsWindow->ShouldImport();
}

#if USE_USD_SDK
TUsdStore< pxr::UsdStageRefPtr > UUSDImporter::ReadUSDFile(FUsdImportContext& ImportContext, const FString& Filename)
{
	FString FilePath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*Filename);
	FilePath = FPaths::GetPath(FilePath) + TEXT("/");
	FString CleanFilename = FPaths::GetCleanFilename(Filename);

	TUsdStore< pxr::UsdStageRefPtr > Stage = UnrealUSDWrapper::ImportUSDFile(TCHAR_TO_ANSI(*FilePath), TCHAR_TO_ANSI(*CleanFilename));

	const char* Errors = UnrealUSDWrapper::GetErrors();
	if (Errors)
	{
		FString ErrorStr = USDToUnreal::ConvertString(Errors);
		ImportContext.AddErrorMessage(EMessageSeverity::Error, FText::Format(LOCTEXT("CouldNotImportUSDFile", "Could not import USD file {0}\n {1}"), FText::FromString(CleanFilename), FText::FromString(ErrorStr)));
	}
	return Stage;
}

void FUsdImportContext::Init(UObject* InParent, const FString& InName, const TUsdStore< pxr::UsdStageRefPtr >& InStage)
{
	Parent = InParent;
	ObjectName = InName;
	ImportPathName = InParent->GetOutermost()->GetName();

	// Path should not include the filename
	ImportPathName.RemoveFromEnd(FString(TEXT("/"))+InName);

	ImportObjectFlags = RF_Public | RF_Standalone | RF_Transactional;

	TSubclassOf<UUSDPrimResolver> ResolverClass = GetDefault<UUSDImporterProjectSettings>()->CustomPrimResolver;
	if (!ResolverClass)
	{
		ResolverClass = UUSDPrimResolverKind::StaticClass();
	}

	PrimResolver = NewObject<UUSDPrimResolver>(GetTransientPackage(), ResolverClass);
	PrimResolver->Init();

	Stage = InStage;
	RootPrim = (*InStage)->GetPseudoRoot();

	bApplyWorldTransformToGeometry = false;
	bFindUnrealAssetReferences = false;
}
#endif // #if USE_USD_SDK

void FUsdImportContext::AddErrorMessage(EMessageSeverity::Type MessageSeverity, FText ErrorMessage)
{
	TokenizedErrorMessages.Add(FTokenizedMessage::Create(MessageSeverity, ErrorMessage));
	UE_LOG(LogUSDImport, Error, TEXT("%s"), *ErrorMessage.ToString());
}

void FUsdImportContext::DisplayErrorMessages(bool bAutomated)
{
	if(!bAutomated)
	{
		//Always clear the old message after an import or re-import
		const TCHAR* LogTitle = TEXT("USDImport");
		FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
		TSharedPtr<class IMessageLogListing> LogListing = MessageLogModule.GetLogListing(LogTitle);
		LogListing->SetLabel(FText::FromString("USD Import"));
		LogListing->ClearMessages();

		if (TokenizedErrorMessages.Num() > 0)
		{
			LogListing->AddMessages(TokenizedErrorMessages);
			MessageLogModule.OpenMessageLog(LogTitle);
		}
	}
	else
	{
		for (const TSharedRef<FTokenizedMessage>& Message : TokenizedErrorMessages)
		{
			UE_LOG(LogUSDImport, Error, TEXT("%s"), *Message->ToText().ToString());
		}
	}
}

void FUsdImportContext::ClearErrorMessages()
{
	TokenizedErrorMessages.Empty();
}

#undef LOCTEXT_NAMESPACE

