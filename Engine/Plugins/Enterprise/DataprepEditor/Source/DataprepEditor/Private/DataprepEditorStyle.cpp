// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataprepEditorStyle.h"

#include "DataprepEditorModule.h"

#include "EditorStyleSet.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"

#define IMAGE_PLUGIN_BRUSH( RelativePath, ... ) FSlateImageBrush( FDataprepEditorStyle::InContent( RelativePath, ".png" ), __VA_ARGS__ )
#define BOX_BRUSH( RelativePath, ... ) FSlateBoxBrush( FDataprepEditorStyle::InContent( RelativePath, ".png" ), __VA_ARGS__ )

#define DEFAULT_FONT(...) FCoreStyle::GetDefaultFontStyle(__VA_ARGS__)

TSharedPtr<FSlateStyleSet> FDataprepEditorStyle::StyleSet;

void FDataprepEditorStyle::Initialize()
{
	if (StyleSet.IsValid())
	{
		return;
	}

	StyleSet = MakeShared<FSlateStyleSet>(GetStyleSetName());

	StyleSet->SetContentRoot(FPaths::EngineContentDir() / TEXT("Editor/Slate"));
	StyleSet->SetCoreContentRoot(FPaths::EngineContentDir() / TEXT("Slate"));

	const FVector2D Icon16x16(16.0f, 16.0f);
	const FVector2D Icon20x20(20.0f, 20.0f);
	const FVector2D Icon24x24(24.0f, 24.0f);
	const FVector2D Icon32x32(32.0f, 32.0f);
	const FVector2D Icon40x40(40.0f, 40.0f);

	StyleSet->Set("DataprepEditor.Producer", new IMAGE_PLUGIN_BRUSH("Icons/Producer24", Icon20x20));
	StyleSet->Set("DataprepEditor.Producer.Selected", new IMAGE_PLUGIN_BRUSH("Icons/Producer24", Icon20x20));

	StyleSet->Set("DataprepEditor.SaveScene", new IMAGE_PLUGIN_BRUSH("Icons/SaveScene", Icon40x40));
	StyleSet->Set("DataprepEditor.SaveScene.Small", new IMAGE_PLUGIN_BRUSH("Icons/SaveScene", Icon20x20));
	StyleSet->Set("DataprepEditor.SaveScene.Selected", new IMAGE_PLUGIN_BRUSH("Icons/SaveScene", Icon40x40));
	StyleSet->Set("DataprepEditor.SaveScene.Selected.Small", new IMAGE_PLUGIN_BRUSH("Icons/SaveScene", Icon20x20));

	StyleSet->Set("DataprepEditor.ShowDataprepSettings", new IMAGE_PLUGIN_BRUSH("Icons/IconOptions", Icon40x40));
	StyleSet->Set("DataprepEditor.ShowDatasmithSceneSettings", new IMAGE_PLUGIN_BRUSH("Icons/IconOptions", Icon40x40));

	StyleSet->Set("DataprepEditor.BuildWorld", new IMAGE_PLUGIN_BRUSH("Icons/BuildWorld", Icon40x40));
	StyleSet->Set("DataprepEditor.BuildWorld.Small", new IMAGE_PLUGIN_BRUSH("Icons/BuildWorld", Icon20x20));
	StyleSet->Set("DataprepEditor.BuildWorld.Selected", new IMAGE_PLUGIN_BRUSH("Icons/BuildWorld", Icon40x40));
	StyleSet->Set("DataprepEditor.BuildWorld.Selected.Small", new IMAGE_PLUGIN_BRUSH("Icons/BuildWorld", Icon20x20));

	StyleSet->Set("DataprepEditor.CommitWorld", new IMAGE_PLUGIN_BRUSH("Icons/CommitWorld", Icon40x40));
	StyleSet->Set("DataprepEditor.CommitWorld.Small", new IMAGE_PLUGIN_BRUSH("Icons/CommitWorld", Icon20x20));
	StyleSet->Set("DataprepEditor.CommitWorld.Selected", new IMAGE_PLUGIN_BRUSH("Icons/CommitWorld", Icon40x40));
	StyleSet->Set("DataprepEditor.CommitWorld.Selected.Small", new IMAGE_PLUGIN_BRUSH("Icons/CommitWorld", Icon20x20));

	StyleSet->Set("DataprepEditor.ExecutePipeline", new IMAGE_PLUGIN_BRUSH("Icons/ExecutePipeline", Icon40x40));
	StyleSet->Set("DataprepEditor.ExecutePipeline.Small", new IMAGE_PLUGIN_BRUSH("Icons/ExecutePipeline", Icon20x20));
	StyleSet->Set("DataprepEditor.ExecutePipeline.Selected", new IMAGE_PLUGIN_BRUSH("Icons/ExecutePipeline", Icon40x40));
	StyleSet->Set("DataprepEditor.ExecutePipeline.Selected.Small", new IMAGE_PLUGIN_BRUSH("Icons/ExecutePipeline", Icon20x20));

	StyleSet->Set("DataprepEditor.TrackNode.Slot", new IMAGE_PLUGIN_BRUSH("CircleBox", Icon32x32));

	StyleSet->Set("DataprepEditor.Node.Body", new BOX_BRUSH("Node_Body", FMargin(16.f/64.f, 25.f/64.f, 16.f/64.f, 16.f/64.f)));

	StyleSet->Set( "DataprepEditor.SectionFont", DEFAULT_FONT( "Bold", 10 ) );

	// Dataprep action UI
	{
		StyleSet->Set("Dataprep.Background.Black", FLinearColor(FColor(26, 26, 26)) );
		StyleSet->Set("Dataprep.TextSeparator.Color", FLinearColor(FColor(200, 200, 200, 200)) );

		StyleSet->Set("DataprepEditor.SoftwareCursor_Grab", new IMAGE_PLUGIN_BRUSH("Icons/cursor_grab", Icon20x20));
		StyleSet->Set("DataprepEditor.SoftwareCursor_Hand", new IMAGE_PLUGIN_BRUSH("Icons/cursor_hand", Icon20x20));

		StyleSet->Set("DataprepAction.Outter.Regular.Padding", FMargin(1.f, 2.f, 4.f, 2.f));
		StyleSet->Set("DataprepAction.Outter.Selected.Padding", FMargin(0.f));
		StyleSet->Set("DataprepAction.Body.Padding", FMargin(6.f, 2.f, 2.f, 2.f));
		StyleSet->Set("DataprepAction.Steps.Padding", FMargin(11.f, 2.f, 6.f, 2.f));
		StyleSet->Set("DataprepAction.DragAndDrop", FLinearColor(FColor(212, 212, 59)) );

		StyleSet->Set("DataprepAction.OutlineColor", FLinearColor(FColor(10, 177, 51)));
		StyleSet->Set("DataprepAction.BackgroundColor", FLinearColor(FColor(61, 61, 61)));

		{
			FTextBlockStyle TilteTextStyle = FEditorStyle::GetWidgetStyle< FTextBlockStyle >("NormalText");
			TilteTextStyle.SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 11));
			StyleSet->Set("DataprepAction.TitleTextStyle", TilteTextStyle);

			FEditableTextBoxStyle TitleEditableText = FEditorStyle::GetWidgetStyle< FEditableTextBoxStyle >("ViewportMenu.EditableText");
			TitleEditableText.SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 11));

			StyleSet->Set( "DataprepAction.TitleInlineEditableText", FInlineEditableTextBlockStyle()
				.SetTextStyle(TilteTextStyle)
				.SetEditableTextBoxStyle(TitleEditableText)
			);
		}

		StyleSet->Set("DataprepAction.EmptyStep.Background.Hovered", FLinearColor(FColor(66, 66, 66)));
		StyleSet->Set("DataprepAction.EmptyStep.Background.Normal", FLinearColor(FColor(57, 57, 57)));
		StyleSet->Set("DataprepAction.EmptyStep.Outer.Hovered", FLinearColor(FColor(117, 117, 117)));
		StyleSet->Set("DataprepAction.EmptyStep.Outer.Normal", FLinearColor(FColor(85, 85, 85)));
		StyleSet->Set("DataprepAction.EmptyStep.Text.Hovered", FLinearColor(FColor(230, 230, 230)));
		StyleSet->Set("DataprepAction.EmptyStep.Text.Normal", FLinearColor(FColor(117, 117, 117)));

		StyleSet->Set("DataprepAction.EmptyStep.Bottom.Padding", FMargin(0.f, 0.f, 0.f, 5.f));

		StyleSet->Set("DataprepActionSteps.BackgroundColor", FLinearColor(FColor(26, 26, 26)));

		StyleSet->Set("DataprepActionStep.BackgroundColor", FLinearColor(FColor(93, 93, 93)) );
		StyleSet->Set("DataprepActionStep.DragAndDrop", FLinearColor(FColor(212, 212, 59)) );
		StyleSet->Set("DataprepActionStep.Selected", FLinearColor(FColor(1, 202, 252)) );
		StyleSet->Set("DataprepActionStep.Filter.OutlineColor", FLinearColor(FColor(220, 125, 67)));
		StyleSet->Set("DataprepActionStep.Operation.OutlineColor", FLinearColor(FColor(67, 177, 220)) );
		StyleSet->Set("DataprepActionStep.Separator.Color", FLinearColor(FColor(182, 219, 192)) );

		StyleSet->Set("DataprepActionStep.Outter.Regular.Padding", FMargin(10.f, 3.f, 10.f, 3.f));
		StyleSet->Set("DataprepActionStep.Outter.Selected.Padding", FMargin(10.f, 0.f, 4.f, 0.f));
		StyleSet->Set("DataprepActionStep.Padding", FMargin(15.f, 3.f, 5.f, 3.f));

		StyleSet->Set("DataprepActionStep.DnD.Outter.Padding", FMargin(0.f, 5.f, 0.f, 5.f));
		StyleSet->Set("DataprepActionStep.DnD.Inner.Padding", FMargin(5.f, 5.f, 5.f, 5.f));

		StyleSet->Set("DataprepActionBlock.ContentBackgroundColor.Old", FLinearColor(0.11f, 0.11f, 0.11f));
		{
			FTextBlockStyle TilteTextBlockStyle = FEditorStyle::GetWidgetStyle< FTextBlockStyle >("NormalText");
			TilteTextBlockStyle.SetFont(FCoreStyle::GetDefaultFontStyle("Bold", 10));
			StyleSet->Set("DataprepActionBlock.TitleTextBlockStyle", TilteTextBlockStyle);
		}


		StyleSet->Set("Graph.ActionStepNode.PreviewColor", FLinearColor(0.822786f, 0.715693f, 0.0f, 1.f));

		{
			FTextBlockStyle TilteTextBlockStyle = FEditorStyle::GetWidgetStyle< FTextBlockStyle >("NormalText");
			TilteTextBlockStyle.SetFont(FCoreStyle::GetDefaultFontStyle("Italic", 7))
				.SetShadowOffset(FVector2D::ZeroVector)
				.SetColorAndOpacity(GetColor("Graph.ActionStepNode.PreviewColor"))
				.SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.7f));
			StyleSet->Set("DataprepActionBlock.PreviewTextBlockStyle", TilteTextBlockStyle);
		}
	}

	// DataprepGraphEditor
	{
		StyleSet->Set("Graph.TrackEnds.BackgroundColor", FLinearColor(0.05f, 0.05f, 0.05f, 0.2f));
		StyleSet->Set("Graph.TrackInner.BackgroundColor", FLinearColor(FColor(50, 50, 50, 200)));
		
		StyleSet->Set("Graph.ActionNode.BackgroundColor", FLinearColor(0.115861f, 0.115861f, 0.115861f));
		{
			FTextBlockStyle GraphActionNodeTitle = FTextBlockStyle(/*NormalText*/)
			.SetColorAndOpacity( FLinearColor(230.0f/255.0f,230.0f/255.0f,230.0f/255.0f) )
			.SetFont( FCoreStyle::GetDefaultFontStyle("Bold", 14) );
			StyleSet->Set( "Graph.ActionNode.Title", GraphActionNodeTitle );

			FEditableTextBoxStyle GraphActionNodeTitleEditableText = FEditableTextBoxStyle()
			.SetFont(GraphActionNodeTitle.Font);
			StyleSet->Set( "Graph.ActionNode.NodeTitleEditableText", GraphActionNodeTitleEditableText );

			StyleSet->Set( "Graph.ActionNode.TitleInlineEditableText", FInlineEditableTextBlockStyle()
				.SetTextStyle(GraphActionNodeTitle)
				.SetEditableTextBoxStyle(GraphActionNodeTitleEditableText)
			);
		}

		StyleSet->Set("Graph.ActionNode.Margin", FMargin(2.f, 0.0f, 2.f, 0.0f));
		StyleSet->Set("Graph.ActionNode.DesiredSize", FVector2D(300.f, 300.f));
	}

	FSlateStyleRegistry::RegisterSlateStyle(*StyleSet.Get());
}

void FDataprepEditorStyle::Shutdown()
{
	if (StyleSet.IsValid())
	{
		FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSet.Get());
		ensure(StyleSet.IsUnique());
		StyleSet.Reset();
	}
}

FName FDataprepEditorStyle::GetStyleSetName()
{
	static FName StyleName("DataprepEditorStyle");
	return StyleName;
}

FString FDataprepEditorStyle::InContent(const FString& RelativePath, const ANSICHAR* Extension)
{
	static FString BaseDir = IPluginManager::Get().FindPlugin(DATAPREPEDITOR_MODULE_NAME)->GetBaseDir() + TEXT("/Resources");
	return (BaseDir / RelativePath) + Extension;
}
