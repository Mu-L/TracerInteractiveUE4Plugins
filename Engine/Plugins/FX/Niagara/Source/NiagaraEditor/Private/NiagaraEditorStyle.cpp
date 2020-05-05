// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraEditorStyle.h"

#include "Framework/Application/SlateApplication.h"
#include "EditorStyleSet.h"
#include "Slate/SlateGameResources.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "Styling/CoreStyle.h"
#include "Interfaces/IPluginManager.h"
#include "Classes/EditorStyleSettings.h"

TSharedPtr< FSlateStyleSet > FNiagaraEditorStyle::NiagaraEditorStyleInstance = NULL;

void FNiagaraEditorStyle::Initialize()
{
	if (!NiagaraEditorStyleInstance.IsValid())
	{
		NiagaraEditorStyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*NiagaraEditorStyleInstance);
	}
}

void FNiagaraEditorStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*NiagaraEditorStyleInstance);
	ensure(NiagaraEditorStyleInstance.IsUnique());
	NiagaraEditorStyleInstance.Reset();
}

FName FNiagaraEditorStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("NiagaraEditorStyle"));
	return StyleSetName;
}

NIAGARAEDITOR_API FString RelativePathToPluginPath(const FString& RelativePath, const ANSICHAR* Extension)
{
	static FString ContentDir = IPluginManager::Get().FindPlugin(TEXT("Niagara"))->GetContentDir();
	return (ContentDir / RelativePath) + Extension;
}

#define IMAGE_BRUSH( RelativePath, ... ) FSlateImageBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define IMAGE_CORE_BRUSH( RelativePath, ... ) FSlateImageBrush( FPaths::EngineContentDir() / "Editor/Slate" / RelativePath + TEXT(".png"), __VA_ARGS__ )
#define IMAGE_PLUGIN_BRUSH( RelativePath, ... ) FSlateImageBrush( RelativePathToPluginPath( RelativePath, ".png" ), __VA_ARGS__ )
#define BOX_BRUSH( RelativePath, ... ) FSlateBoxBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define BOX_CORE_BRUSH( RelativePath, ... ) FSlateBoxBrush( FPaths::EngineContentDir() / "Editor/Slate" / RelativePath + TEXT(".png"), __VA_ARGS__ )
#define BOX_PLUGIN_BRUSH( RelativePath, ... ) FSlateBoxBrush( RelativePathToPluginPath( RelativePath, ".png"), __VA_ARGS__ )
#define BORDER_BRUSH( RelativePath, ... ) FSlateBorderBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define DEFAULT_FONT(...) FCoreStyle::GetDefaultFontStyle(__VA_ARGS__)

const FVector2D Icon8x8(8.0f, 8.0f);
const FVector2D Icon12x12(12.0f, 12.0f);
const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon20x20(20.0f, 20.0f);
const FVector2D Icon40x40(40.0f, 40.0f);
const FVector2D Icon64x64(64.0f, 64.0f);

TSharedRef< FSlateStyleSet > FNiagaraEditorStyle::Create()
{
	const FTextBlockStyle NormalText = FEditorStyle::GetWidgetStyle<FTextBlockStyle>("NormalText");
	const FEditableTextBoxStyle NormalEditableTextBox = FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");
	const FSpinBoxStyle NormalSpinBox = FEditorStyle::GetWidgetStyle<FSpinBoxStyle>("SpinBox");

	TSharedRef< FSlateStyleSet > Style = MakeShareable(new FSlateStyleSet("NiagaraEditorStyle"));
	Style->SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate/Niagara"));

	// Stats
	FTextBlockStyle CategoryText = FTextBlockStyle(NormalText)
		.SetFont(DEFAULT_FONT("Regular", 10))
		.SetShadowOffset(FVector2D(0, 1))
		.SetShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.9f));

	Style->Set("NiagaraEditor.StatsText", CategoryText);

	// Asset picker
	FTextBlockStyle AssetPickerBoldAssetNameText = FTextBlockStyle(NormalText)
		.SetColorAndOpacity(FLinearColor::White)
		.SetFont(DEFAULT_FONT("Bold", 9));

	Style->Set("NiagaraEditor.AssetPickerBoldAssetNameText", AssetPickerBoldAssetNameText);

	FTextBlockStyle AssetPickerAssetNameText = FTextBlockStyle(NormalText)
		.SetColorAndOpacity(FLinearColor::White)
		.SetFont(DEFAULT_FONT("Regular", 9));

	Style->Set("NiagaraEditor.AssetPickerAssetNameText", AssetPickerAssetNameText);

	FTextBlockStyle AssetPickerAssetCategoryText = FTextBlockStyle(NormalText)
		.SetFont(DEFAULT_FONT("Bold", 11));

	Style->Set("NiagaraEditor.AssetPickerAssetCategoryText", AssetPickerAssetCategoryText);


	FTextBlockStyle AssetPickerAssetSubcategoryText = FTextBlockStyle(NormalText)
		.SetFont(DEFAULT_FONT("Bold", 10));

	Style->Set("NiagaraEditor.AssetPickerAssetSubcategoryText", AssetPickerAssetSubcategoryText);

	// New Asset Dialog
	FTextBlockStyle NewAssetDialogOptionText = FTextBlockStyle(NormalText)
		.SetFont(DEFAULT_FONT("Bold", 11));

	Style->Set("NiagaraEditor.NewAssetDialog.OptionText", NewAssetDialogOptionText);

	FTextBlockStyle NewAssetDialogHeaderText = FTextBlockStyle(NormalText)
		.SetColorAndOpacity(FLinearColor::White)
		.SetFont(DEFAULT_FONT("Bold", 10));

	Style->Set("NiagaraEditor.NewAssetDialog.HeaderText", NewAssetDialogHeaderText);

	FTextBlockStyle NewAssetDialogSubHeaderText = FTextBlockStyle(NormalText)
		.SetColorAndOpacity(FLinearColor::White)
		.SetFont(DEFAULT_FONT("Bold", 11));

	Style->Set("NiagaraEditor.NewAssetDialog.SubHeaderText", NewAssetDialogSubHeaderText);

	Style->Set("NiagaraEditor.NewAssetDialog.AddButton", FButtonStyle()
		.SetNormal(BOX_CORE_BRUSH("Common/FlatButton", 2.0f / 8.0f, FLinearColor(0, 0, 0, .25f)))
		.SetHovered(BOX_CORE_BRUSH("Common/FlatButton", 2.0f / 8.0f, FEditorStyle::GetSlateColor("SelectionColor")))
		.SetPressed(BOX_CORE_BRUSH("Common/FlatButton", 2.0f / 8.0f, FEditorStyle::GetSlateColor("SelectionColor_Pressed")))
	);

	Style->Set("NiagaraEditor.NewAssetDialog.SubBorderColor", FLinearColor(FColor(48, 48, 48)));
	Style->Set("NiagaraEditor.NewAssetDialog.ActiveOptionBorderColor", FLinearColor(FColor(96, 96, 96)));

	Style->Set("NiagaraEditor.NewAssetDialog.SubBorder", new BOX_CORE_BRUSH("Common/GroupBorderLight", FMargin(4.0f / 16.0f)));

	// Emitter Header
	FTextBlockStyle HeadingText = FTextBlockStyle(NormalText)
		.SetFont(DEFAULT_FONT("Regular", 14));

	FEditableTextBoxStyle HeadingEditableTextBox = FEditableTextBoxStyle(NormalEditableTextBox)
		.SetFont(DEFAULT_FONT("Regular", 14));

	Style->Set("NiagaraEditor.HeadingTextBlock", HeadingText);

	Style->Set("NiagaraEditor.HeadingEditableTextBox", HeadingEditableTextBox);

	Style->Set("NiagaraEditor.HeadingInlineEditableText", FInlineEditableTextBlockStyle()
		.SetTextStyle(HeadingText)
		.SetEditableTextBoxStyle(HeadingEditableTextBox));

	FTextBlockStyle TabText = FTextBlockStyle(NormalText)
		.SetFont(DEFAULT_FONT("Regular", 12))
		.SetShadowOffset(FVector2D(0, 1))
		.SetShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.9f));
	
	Style->Set("NiagaraEditor.AttributeSpreadsheetTabText", TabText);

	FTextBlockStyle SubduedHeadingText = FTextBlockStyle(NormalText)
		.SetFont(DEFAULT_FONT("Regular", 14))
		.SetColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f)));
	
	Style->Set("NiagaraEditor.SubduedHeadingTextBox", SubduedHeadingText);

	// Details
	FTextBlockStyle DetailsHeadingText = FTextBlockStyle(NormalText)
		.SetFont(DEFAULT_FONT("Bold", 9));

	Style->Set("NiagaraEditor.DetailsHeadingText", DetailsHeadingText);

	// Parameters
	FSlateFontInfo ParameterFont = DEFAULT_FONT("Regular", 8);

	Style->Set("NiagaraEditor.ParameterFont", ParameterFont);

	FTextBlockStyle ParameterText = FTextBlockStyle(NormalText)
		.SetFont(ParameterFont);

	Style->Set("NiagaraEditor.ParameterText", ParameterText);

	FEditableTextBoxStyle ParameterEditableTextBox = FEditableTextBoxStyle(NormalEditableTextBox)
		.SetFont(ParameterFont);

	Style->Set("NiagaraEditor.ParameterEditableTextBox", ParameterEditableTextBox);

	Style->Set("NiagaraEditor.ParameterInlineEditableText", FInlineEditableTextBlockStyle()
		.SetTextStyle(ParameterText)
		.SetEditableTextBoxStyle(ParameterEditableTextBox));

	FSpinBoxStyle ParameterSpinBox = FSpinBoxStyle(NormalSpinBox)
		.SetTextPadding(1);

	Style->Set("NiagaraEditor.ParameterSpinbox", ParameterSpinBox);

	Style->Set("NiagaraEditor.ParameterName.NamespaceBorder", new BOX_PLUGIN_BRUSH("Icons/NamespaceBorder", FMargin(4.0f / 16.0f)));

	Style->Set("NiagaraEditor.ParameterName.NamespaceText", FTextBlockStyle(NormalText)
		.SetFont(DEFAULT_FONT("Bold", 8))
		.SetColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.9f))
		.SetShadowOffset(FVector2D(1, 1))
		.SetShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.7f)));

	Style->Set("NiagaraEditor.ParameterName.NamespaceTextDark", FTextBlockStyle(NormalText)
		.SetFont(DEFAULT_FONT("Bold", 8))
		.SetColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f))
		.SetShadowOffset(FVector2D(1, 1))
		.SetShadowColorAndOpacity(FLinearColor(1.0, 1.0, 1.0, 0.25f)));

	Style->Set("NiagaraEditor.ParameterName.TypeText", FTextBlockStyle(NormalText)
		.SetFont(DEFAULT_FONT("Regular", 8))
		.SetColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.5f)));

	Style->Set("NiagaraEditor.Stack.HighlightedButtonBrush", new BOX_CORE_BRUSH("Common/ButtonHoverHint", FMargin(4 / 16.0f), GetDefault<UEditorStyleSettings>()->SelectionColor));

	// Parameter Map View
	Style->Set("NiagaraEditor.Stack.DepressedHighlightedButtonBrush", new BOX_CORE_BRUSH("Common/ButtonHoverHint", FMargin(4 / 16.0f), GetDefault<UEditorStyleSettings>()->PressedSelectionColor));
	Style->Set("NiagaraEditor.Stack.ViewOptionsShadowColor", FLinearColor::Black);
	Style->Set("NiagaraEditor.Stack.FlatButtonColor", FLinearColor(FColor(205, 205, 205)));

	const FVector2D ViewOptionsShadowOffset = FVector2D(0, 1);
	Style->Set("NiagaraEditor.Stack.ViewOptionsShadowOffset", ViewOptionsShadowOffset);

	// Code View
	{
		Style->Set("NiagaraEditor.CodeView.Checkbox.Text", FTextBlockStyle(NormalText)
			.SetFont(DEFAULT_FONT("Bold", 12))
			.SetColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.9f))
			.SetShadowOffset(FVector2D(1, 1))
			.SetShadowColorAndOpacity(FLinearColor(0, 0, 0, 0.9f)));

		const int32 LogFontSize = 9;
		FSlateFontInfo LogFont = DEFAULT_FONT("Mono", LogFontSize);
		FTextBlockStyle NormalLogText = FTextBlockStyle(NormalText)
			.SetFont(LogFont)
			.SetColorAndOpacity(FLinearColor(FColor(0xffffffff)))
			.SetSelectedBackgroundColor(FLinearColor(FColor(0xff666666)));
		Style->Set("NiagaraEditor.CodeView.Hlsl.Normal", NormalLogText);
	}

	// Selected Emitter
	FSlateFontInfo SelectedEmitterUnsupportedSelectionFont = DEFAULT_FONT("Regular", 10);
	FTextBlockStyle SelectedEmitterUnsupportedSelectionText = FTextBlockStyle(NormalText)
		.SetFont(SelectedEmitterUnsupportedSelectionFont)
		.SetColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
	Style->Set("NiagaraEditor.SelectedEmitter.UnsupportedSelectionText", SelectedEmitterUnsupportedSelectionText);

	// Toolbar Icons
	Style->Set("NiagaraEditor.Apply", new IMAGE_BRUSH("Icons/icon_Niagara_Apply_40x", Icon40x40));
	Style->Set("NiagaraEditor.Apply.Small", new IMAGE_BRUSH("Icons/icon_Niagara_Apply_40x", Icon20x20));
	Style->Set("NiagaraEditor.Compile", new IMAGE_BRUSH("Icons/icon_compile_40x", Icon40x40));
	Style->Set("NiagaraEditor.Compile.Small", new IMAGE_BRUSH("Icons/icon_compile_40x", Icon20x20));
	Style->Set("NiagaraEditor.AddEmitter", new IMAGE_BRUSH("Icons/icon_AddObject_40x", Icon40x40));
	Style->Set("NiagaraEditor.AddEmitter.Small", new IMAGE_BRUSH("Icons/icon_AddObject_40x", Icon20x20));
	Style->Set("NiagaraEditor.UnlockToChanges", new IMAGE_BRUSH("Icons/icon_levels_unlocked_40x", Icon40x40));
	Style->Set("NiagaraEditor.UnlockToChanges.Small", new IMAGE_BRUSH("Icons/icon_levels_unlocked_40x", Icon20x20));
	Style->Set("NiagaraEditor.LockToChanges", new IMAGE_BRUSH("Icons/icon_levels_LockedReadOnly_40x", Icon40x40));
	Style->Set("NiagaraEditor.LockToChanges.Small", new IMAGE_BRUSH("Icons/icon_levels_LockedReadOnly_40x", Icon20x20));
	Style->Set("NiagaraEditor.SimulationOptions", new IMAGE_PLUGIN_BRUSH("Icons/Commands/icon_simulationOptions_40x", Icon40x40));
	Style->Set("NiagaraEditor.SimulationOptions.Small", new IMAGE_PLUGIN_BRUSH("Icons/Commands/icon_simulationOptions_40x", Icon20x20));

	Style->Set("Niagara.CompileStatus.Unknown", new IMAGE_BRUSH("Icons/CompileStatus_Working", Icon40x40));
	Style->Set("Niagara.CompileStatus.Unknown.Small", new IMAGE_BRUSH("Icons/CompileStatus_Working", Icon20x20));
	Style->Set("Niagara.CompileStatus.Error",   new IMAGE_BRUSH("Icons/CompileStatus_Fail", Icon40x40));
	Style->Set("Niagara.CompileStatus.Error.Small", new IMAGE_BRUSH("Icons/CompileStatus_Fail", Icon20x20));
	Style->Set("Niagara.CompileStatus.Good",    new IMAGE_BRUSH("Icons/CompileStatus_Good", Icon40x40));
	Style->Set("Niagara.CompileStatus.Good.Small", new IMAGE_BRUSH("Icons/CompileStatus_Good", Icon20x20));
	Style->Set("Niagara.CompileStatus.Warning", new IMAGE_BRUSH("Icons/CompileStatus_Warning", Icon40x40));
	Style->Set("Niagara.CompileStatus.Warning.Small", new IMAGE_BRUSH("Icons/CompileStatus_Warning", Icon20x20));
	Style->Set("Niagara.Asset.ReimportAsset.Needed", new IMAGE_BRUSH("Icons/icon_Reimport_Needed_40x", Icon40x40));
	Style->Set("Niagara.Asset.ReimportAsset.Default", new IMAGE_BRUSH("Icons/icon_Reimport_40x", Icon40x40));
	
	Style->Set("Niagaraeditor.OverviewNode.IsolatedColor", FLinearColor::Yellow);
	Style->Set("Niagaraeditor.OverviewNode.NotIsolatedColor", FLinearColor::Transparent);

	// Icons
	Style->Set("NiagaraEditor.Isolate", new IMAGE_PLUGIN_BRUSH("Icons/Isolate", Icon16x16));

	Style->Set("NiagaraEditor.Scratch", new IMAGE_PLUGIN_BRUSH("Icons/Scratch", Icon16x16, FLinearColor::Yellow));
	
	// Emitter details customization
	Style->Set("NiagaraEditor.MaterialWarningBorder", new BOX_CORE_BRUSH("Common/GroupBorderLight", FMargin(4.0f / 16.0f)));

	// Asset colors
	Style->Set("NiagaraEditor.AssetColors.System", FLinearColor(1.0f, 0.0f, 0.0f));
	Style->Set("NiagaraEditor.AssetColors.Emitter", FLinearColor(1.0f, 0.3f, 0.0f));
	Style->Set("NiagaraEditor.AssetColors.Script", FLinearColor(1.0f, 1.0f, 0.0f));
	Style->Set("NiagaraEditor.AssetColors.ParameterCollection", FLinearColor(1.0f, 1.0f, 0.3f));
	Style->Set("NiagaraEditor.AssetColors.ParameterCollectionInstance", FLinearColor(1.0f, 1.0f, 0.7f));

	// Script factory thumbnails
	Style->Set("NiagaraEditor.Thumbnails.DynamicInputs", new IMAGE_BRUSH("Icons/NiagaraScriptDynamicInputs_64x", Icon64x64));
	Style->Set("NiagaraEditor.Thumbnails.Functions", new IMAGE_BRUSH("Icons/NiagaraScriptFunction_64x", Icon64x64));
	Style->Set("NiagaraEditor.Thumbnails.Modules", new IMAGE_BRUSH("Icons/NiagaraScriptModules_64x", Icon64x64));

	// Renderer class icons
	Style->Set("ClassIcon.NiagaraSpriteRendererProperties", new IMAGE_PLUGIN_BRUSH("Icons/Renderers/renderer_sprite", Icon16x16));
	Style->Set("ClassIcon.NiagaraMeshRendererProperties", new IMAGE_PLUGIN_BRUSH("Icons/Renderers/renderer_mesh", Icon16x16));
	Style->Set("ClassIcon.NiagaraRibbonRendererProperties", new IMAGE_PLUGIN_BRUSH("Icons/Renderers/renderer_ribbon", Icon16x16));
	Style->Set("ClassIcon.NiagaraLightRendererProperties", new IMAGE_PLUGIN_BRUSH("Icons/Renderers/renderer_light", Icon16x16));
	Style->Set("ClassIcon.NiagaraRendererProperties", new IMAGE_PLUGIN_BRUSH("Icons/Renderers/renderer_default", Icon16x16));

	//GPU/CPU icons
	Style->Set("NiagaraEditor.Stack.GPUIcon", new IMAGE_PLUGIN_BRUSH("Icons/Simulate_GPU_x40", Icon16x16));
	Style->Set("NiagaraEditor.Stack.CPUIcon", new IMAGE_PLUGIN_BRUSH("Icons/Simulate_CPU_x40", Icon16x16));


	// Niagara sequence
	Style->Set("NiagaraEditor.NiagaraSequence.DefaultTrackColor", FLinearColor(0, .25f, 0));

	// Niagara platform set customization
	Style->Set("NiagaraEditor.PlatformSet.DropdownButton", new IMAGE_CORE_BRUSH("Common/ComboArrow", Icon8x8));

	Style->Set("NiagaraEditor.PlatformSet.ButtonText", FTextBlockStyle(NormalText)
		.SetFont(DEFAULT_FONT("Bold", 10))
		.SetColorAndOpacity(FLinearColor(0.72f, 0.72f, 0.72f))
		.SetHighlightColor(FLinearColor(1, 1, 1)));

	const FString SmallRoundedButtonStart(TEXT("Common/SmallRoundedButtonLeft"));
	const FString SmallRoundedButtonMiddle(TEXT("Common/SmallRoundedButtonCentre"));
	const FString SmallRoundedButtonEnd(TEXT("Common/SmallRoundedButtonRight"));

	const FSlateColor SelectionColor = FEditorStyle::GetSlateColor("SelectionColor");
	const FSlateColor SelectionColor_Pressed = FEditorStyle::GetSlateColor("SelectionColor_Pressed");

	{
		const FLinearColor NormalColor(0.15, 0.15, 0.15, 1);

		Style->Set("NiagaraEditor.PlatformSet.StartButton", FCheckBoxStyle()
			.SetCheckBoxType(ESlateCheckBoxType::ToggleButton)
			.SetUncheckedImage(BOX_CORE_BRUSH(*SmallRoundedButtonStart, FMargin(7.f / 16.f), NormalColor))
			.SetUncheckedPressedImage(BOX_CORE_BRUSH(*SmallRoundedButtonStart, FMargin(7.f / 16.f), SelectionColor_Pressed))
			.SetUncheckedHoveredImage(BOX_CORE_BRUSH(*SmallRoundedButtonStart, FMargin(7.f / 16.f), SelectionColor_Pressed))
			.SetCheckedHoveredImage(BOX_CORE_BRUSH(*SmallRoundedButtonStart, FMargin(7.f / 16.f), SelectionColor))
			.SetCheckedPressedImage(BOX_CORE_BRUSH(*SmallRoundedButtonStart, FMargin(7.f / 16.f), SelectionColor))
			.SetCheckedImage(BOX_CORE_BRUSH(*SmallRoundedButtonStart, FMargin(7.f / 16.f), SelectionColor)));

		Style->Set("NiagaraEditor.PlatformSet.MiddleButton", FCheckBoxStyle()
			.SetCheckBoxType(ESlateCheckBoxType::ToggleButton)
			.SetUncheckedImage(BOX_CORE_BRUSH(*SmallRoundedButtonMiddle, FMargin(7.f / 16.f), NormalColor))
			.SetUncheckedPressedImage(BOX_CORE_BRUSH(*SmallRoundedButtonMiddle, FMargin(7.f / 16.f), SelectionColor_Pressed))
			.SetUncheckedHoveredImage(BOX_CORE_BRUSH(*SmallRoundedButtonMiddle, FMargin(7.f / 16.f), SelectionColor_Pressed))
			.SetCheckedHoveredImage(BOX_CORE_BRUSH(*SmallRoundedButtonMiddle, FMargin(7.f / 16.f), SelectionColor))
			.SetCheckedPressedImage(BOX_CORE_BRUSH(*SmallRoundedButtonMiddle, FMargin(7.f / 16.f), SelectionColor))
			.SetCheckedImage(BOX_CORE_BRUSH(*SmallRoundedButtonMiddle, FMargin(7.f / 16.f), SelectionColor)));

		Style->Set("NiagaraEditor.PlatformSet.EndButton", FCheckBoxStyle()
			.SetCheckBoxType(ESlateCheckBoxType::ToggleButton)
			.SetUncheckedImage(BOX_CORE_BRUSH(*SmallRoundedButtonEnd, FMargin(7.f / 16.f), NormalColor))
			.SetUncheckedPressedImage(BOX_CORE_BRUSH(*SmallRoundedButtonEnd, FMargin(7.f / 16.f), SelectionColor_Pressed))
			.SetUncheckedHoveredImage(BOX_CORE_BRUSH(*SmallRoundedButtonEnd, FMargin(7.f / 16.f), SelectionColor_Pressed))
			.SetCheckedHoveredImage(BOX_CORE_BRUSH(*SmallRoundedButtonEnd, FMargin(7.f / 16.f), SelectionColor))
			.SetCheckedPressedImage(BOX_CORE_BRUSH(*SmallRoundedButtonEnd, FMargin(7.f / 16.f), SelectionColor))
			.SetCheckedImage(BOX_CORE_BRUSH(*SmallRoundedButtonEnd, FMargin(7.f / 16.f), SelectionColor)));
	}

	Style->Set("NiagaraEditor.PlatformSet.Include", new IMAGE_CORE_BRUSH("Icons/PlusSymbol_12x", Icon12x12));
	Style->Set("NiagaraEditor.PlatformSet.Exclude", new IMAGE_CORE_BRUSH("Icons/MinusSymbol_12x", Icon12x12));
	Style->Set("NiagaraEditor.PlatformSet.Remove", new IMAGE_CORE_BRUSH("Icons/Cross_12x", Icon12x12));

	const FSlateColor SelectionColor_Inactive = FEditorStyle::GetSlateColor("SelectionColor_Inactive");

	Style->Set("NiagaraEditor.PlatformSet.TreeView", FTableRowStyle()
		.SetEvenRowBackgroundBrush(FSlateNoResource())
		.SetEvenRowBackgroundHoveredBrush(IMAGE_CORE_BRUSH("Common/Selection", Icon8x8, SelectionColor_Inactive))
		.SetOddRowBackgroundBrush(FSlateNoResource())
		.SetOddRowBackgroundHoveredBrush(IMAGE_CORE_BRUSH("Common/Selection", Icon8x8, SelectionColor_Inactive))
		.SetSelectorFocusedBrush(FSlateNoResource())
		.SetActiveBrush(IMAGE_CORE_BRUSH("Common/Selection", Icon8x8, SelectionColor))
		.SetActiveHoveredBrush(IMAGE_CORE_BRUSH("Common/Selection", Icon8x8, SelectionColor))
		.SetInactiveBrush(IMAGE_CORE_BRUSH("Common/Selection", Icon8x8, SelectionColor_Inactive))
		.SetInactiveHoveredBrush(IMAGE_CORE_BRUSH("Common/Selection", Icon8x8, SelectionColor_Inactive)));

	Style->Set("NiagaraEditor.DropTarget.BackgroundColor", FLinearColor(1.0f, 1.0f, 1.0f, 0.25f));
	Style->Set("NiagaraEditor.DropTarget.BackgroundColorHover", FLinearColor(1.0f, 1.0f, 1.0f, 0.1f));
	Style->Set("NiagaraEditor.DropTarget.BorderVertical", new IMAGE_PLUGIN_BRUSH("Icons/StackDropTargetBorder_Vertical", FVector2D(2, 8), FLinearColor::White, ESlateBrushTileType::Vertical));
	Style->Set("NiagaraEditor.DropTarget.BorderHorizontal", new IMAGE_PLUGIN_BRUSH("Icons/StackDropTargetBorder_Horizontal", FVector2D(8, 2), FLinearColor::White, ESlateBrushTileType::Horizontal));

	Style->Set("NiagaraEditor.ScriptGraph.SearchBorderColor", FLinearColor(.1f, .1f, .1f, 1.f));

	return Style;
}

#undef IMAGE_PLUGIN_BRUSH
#undef IMAGE_CORE_BRUSH
#undef IMAGE_BRUSH
#undef BOX_BRUSH
#undef BORDER_BRUSH
#undef BOX_CORE_BRUSH
#undef DEFAULT_FONT

void FNiagaraEditorStyle::ReloadTextures()
{
	FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
}

const ISlateStyle& FNiagaraEditorStyle::Get()
{
	return *NiagaraEditorStyleInstance;
}
