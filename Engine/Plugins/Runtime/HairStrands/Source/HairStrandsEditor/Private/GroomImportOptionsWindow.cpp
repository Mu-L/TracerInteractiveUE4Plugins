// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomImportOptionsWindow.h"

#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "GroomImportOptions.h"
#include "IDetailsView.h"
#include "Interfaces/IMainFrameModule.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "GroomAsset.h"

#define LOCTEXT_NAMESPACE "GroomImportOptionsWindow"

enum class EHairDescriptionStatus
{
	Valid,
	NoGroup,
	NoCurve,
	Unknown
};

static EHairDescriptionStatus GetStatus(UGroomHairGroupsPreview* Description)
{
	if (Description)
	{
		for (const FGroomHairGroupPreview& Group : Description->Groups)
		{
			if (Group.CurveCount == 0)
				return EHairDescriptionStatus::NoCurve;
		}

		if (Description->Groups.Num() == 0)
		{
			return EHairDescriptionStatus::NoGroup;
		}
		return EHairDescriptionStatus::Valid;
	}
	else
	{
		return EHairDescriptionStatus::Unknown;
	}
}

void SGroomImportOptionsWindow::Construct(const FArguments& InArgs)
{
	ImportOptions = InArgs._ImportOptions;
	GroupsPreview = InArgs._GroupsPreview;
	WidgetWindow = InArgs._WidgetWindow;

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;

	DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailsView->SetObject(ImportOptions);
	
	DetailsView2 = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailsView2->SetObject(GroupsPreview);

	
	const EHairDescriptionStatus Status = GetStatus(GroupsPreview);

	FText ValidationText;
	FLinearColor ValidationColor(1,1,1);
	if (Status == EHairDescriptionStatus::Valid)
	{
		ValidationText = LOCTEXT("GroomOptionsWindow_ValidationText0", "Valid");
		ValidationColor = FLinearColor(0, 0.80f, 0, 1);
	}
	else if (Status == EHairDescriptionStatus::NoCurve)
	{
		ValidationText = LOCTEXT("GroomOptionsWindow_ValidationText1", "Invalid. Some groups have 0 curves.");
		ValidationColor = FLinearColor(0.80f, 0, 0, 1);
	}
	else if (Status == EHairDescriptionStatus::NoGroup)
	{
		ValidationText = LOCTEXT("GroomOptionsWindow_ValidationText2", "Invalid. The groom does not contain any group.");
		ValidationColor = FLinearColor(1, 0, 0, 1);
	}
	else
	{
		ValidationText = LOCTEXT("GroomOptionsWindow_ValidationText3", "Unknown");
	}

	this->ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)
		[
			SNew(SBorder)
			.Padding(FMargin(3))
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Font(FEditorStyle::GetFontStyle("CurveEd.LabelFont"))
					.Text(LOCTEXT("CurrentFile", "Current File: "))
				]
				+ SHorizontalBox::Slot()
				.Padding(5, 0, 0, 0)
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(FEditorStyle::GetFontStyle("CurveEd.InfoFont"))
					.Text(InArgs._FullPath)
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)
		[
			SNew(SBorder)
			.Padding(FMargin(3))
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Font(FEditorStyle::GetFontStyle("CurveEd.LabelFont"))
					.Text(LOCTEXT("GroomOptionsWindow_StatusFile", "Status File: "))
				]
				+ SHorizontalBox::Slot()
				.Padding(5, 0, 0, 0)
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(FEditorStyle::GetFontStyle("CurveEd.InfoFont"))
					.Text(ValidationText)
					.ColorAndOpacity(ValidationColor)
				]
			]
		]

		+ SVerticalBox::Slot()
		.Padding(2)
		.MaxHeight(500.0f)
		[
			DetailsView->AsShared()
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2)		
		[
			DetailsView2->AsShared()
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
				SAssignNew(ImportButton, SButton)
				.HAlign(HAlign_Center)
				.Text(InArgs._ButtonLabel)
				.IsEnabled(this, &SGroomImportOptionsWindow::CanImport)
				.OnClicked(this, &SGroomImportOptionsWindow::OnImport)
			]
			+ SUniformGridPanel::Slot(1, 0)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("Cancel", "Cancel"))
				.OnClicked(this, &SGroomImportOptionsWindow::OnCancel)
			]
		]
	];
}

bool SGroomImportOptionsWindow::CanImport()  const
{
	if (GroupsPreview)
	{
		for (const FGroomHairGroupPreview& Group : GroupsPreview->Groups)
		{
			if (Group.CurveCount == 0)
				return false;
		}

		if (GroupsPreview->Groups.Num() == 0)
		{
			return false;
		}
	}
	return true;
}

enum class EGroomOptionsVisibility : uint8
{
	None = 0x00,
	ConversionOptions = 0x01,
	BuildOptions = 0x02,
	All = ConversionOptions | BuildOptions
};

ENUM_CLASS_FLAGS(EGroomOptionsVisibility);

TSharedPtr<SGroomImportOptionsWindow> DisplayOptions(
	UGroomImportOptions* ImportOptions, 
	UGroomHairGroupsPreview* GroupsPreview,
	const FString& FilePath, 
	EGroomOptionsVisibility VisibilityFlag, 
	FText WindowTitle, 
	FText InButtonLabel)
{
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(WindowTitle)
		.SizingRule(ESizingRule::Autosized);

	TSharedPtr<SGroomImportOptionsWindow> OptionsWindow;

	FProperty* ConversionOptionsProperty = FindFProperty<FProperty>(ImportOptions->GetClass(), GET_MEMBER_NAME_CHECKED(UGroomImportOptions, ConversionSettings));
	if (ConversionOptionsProperty)
	{
		if (EnumHasAnyFlags(VisibilityFlag, EGroomOptionsVisibility::ConversionOptions))
		{
			ConversionOptionsProperty->SetMetaData(TEXT("ShowOnlyInnerProperties"), TEXT("1"));
			ConversionOptionsProperty->SetMetaData(TEXT("Category"), TEXT("Conversion"));
		}
		else
		{
			// Note that UGroomImportOptions HideCategories named "Hidden",
			// but the hiding doesn't work with ShowOnlyInnerProperties 
			ConversionOptionsProperty->RemoveMetaData(TEXT("ShowOnlyInnerProperties"));
			ConversionOptionsProperty->SetMetaData(TEXT("Category"), TEXT("Hidden"));
		}
	}

	FString FileName = FPaths::GetCleanFilename(FilePath);
	Window->SetContent
	(
		SAssignNew(OptionsWindow, SGroomImportOptionsWindow)
		.ImportOptions(ImportOptions)
		.GroupsPreview(GroupsPreview)
		.WidgetWindow(Window)
		.FullPath(FText::FromString(FileName))
		.ButtonLabel(InButtonLabel)
	);

	TSharedPtr<SWindow> ParentWindow;

	if (FModuleManager::Get().IsModuleLoaded("MainFrame"))
	{
		IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
		ParentWindow = MainFrame.GetParentWindow();
	}

	FSlateApplication::Get().AddModalWindow(Window, ParentWindow, false);

	return OptionsWindow;
}

TSharedPtr<SGroomImportOptionsWindow> SGroomImportOptionsWindow::DisplayImportOptions(UGroomImportOptions* ImportOptions, UGroomHairGroupsPreview* GroupsPreview, const FString& FilePath)
{
	return DisplayOptions(ImportOptions, GroupsPreview, FilePath, EGroomOptionsVisibility::All, LOCTEXT("GroomImportWindowTitle", "Groom Import Options"), LOCTEXT("Import", "Import"));
}

TSharedPtr<SGroomImportOptionsWindow> SGroomImportOptionsWindow::DisplayRebuildOptions(UGroomImportOptions* ImportOptions, UGroomHairGroupsPreview* GroupsPreview, const FString& FilePath)
{
	return DisplayOptions(ImportOptions, GroupsPreview, FilePath, EGroomOptionsVisibility::BuildOptions, LOCTEXT("GroomRebuildWindowTitle ", "Groom Build Options"), LOCTEXT("Build", "Build"));
}


#undef LOCTEXT_NAMESPACE
