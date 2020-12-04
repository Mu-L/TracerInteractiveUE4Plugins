// Copyright Epic Games, Inc. All Rights Reserved.

#include "Framework/MultiBox/MultiBox.h"
#include "Misc/ConfigCacheIni.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Layout/WidgetPath.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBorder.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/MultiBox/ToolMenuBase.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STileView.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Framework/MultiBox/SToolBarButtonBlock.h"
#include "Framework/MultiBox/SMenuEntryBlock.h"
#include "Framework/MultiBox/SMenuSeparatorBlock.h"
#include "Framework/MultiBox/MultiBoxCustomization.h"
#include "Framework/MultiBox/SClippingHorizontalBox.h"
#include "Framework/MultiBox/SWidgetBlock.h"
#include "Framework/Commands/UICommandDragDropOp.h"
#include "SUniformToolbarPanel.h"

#define LOCTEXT_NAMESPACE "MultiBox"


TAttribute<bool> FMultiBoxSettings::UseSmallToolBarIcons;
TAttribute<bool> FMultiBoxSettings::DisplayMultiboxHooks;
FMultiBoxSettings::FConstructToolTip FMultiBoxSettings::ToolTipConstructor = FConstructToolTip::CreateStatic( &FMultiBoxSettings::ConstructDefaultToolTip );


FMultiBoxSettings::FMultiBoxSettings()
{
	ResetToolTipConstructor();
}

TSharedRef< SToolTip > FMultiBoxSettings::ConstructDefaultToolTip( const TAttribute<FText>& ToolTipText, const TSharedPtr<SWidget>& OverrideContent, const TSharedPtr<const FUICommandInfo>& Action )
{
	if ( OverrideContent.IsValid() )
	{
		return SNew( SToolTip )
		[
			OverrideContent.ToSharedRef()
		];
	}

	return SNew( SToolTip ).Text( ToolTipText );
}

void FMultiBoxSettings::ResetToolTipConstructor()
{
	ToolTipConstructor = FConstructToolTip::CreateStatic( &FMultiBoxSettings::ConstructDefaultToolTip );
}

const FMultiBoxCustomization FMultiBoxCustomization::None( NAME_None );


TSharedRef< SWidget > SMultiBlockBaseWidget::AsWidget()
{
	return this->AsShared();
}

TSharedRef< const SWidget > SMultiBlockBaseWidget::AsWidget() const
{
	return this->AsShared();
}

void SMultiBlockBaseWidget::SetOwnerMultiBoxWidget(TSharedRef< SMultiBoxWidget > InOwnerMultiBoxWidget)
{
	OwnerMultiBoxWidget = InOwnerMultiBoxWidget;
}

void SMultiBlockBaseWidget::SetMultiBlock(TSharedRef< const FMultiBlock > InMultiBlock)
{
	MultiBlock = InMultiBlock;
}

void SMultiBlockBaseWidget::SetMultiBlockLocation(EMultiBlockLocation::Type InLocation, bool bInSectionContainsIcons)
{
	Location = InLocation;
	bSectionContainsIcons = bInSectionContainsIcons;
}

EMultiBlockLocation::Type SMultiBlockBaseWidget::GetMultiBlockLocation()
{
	return Location;
}

void SMultiBlockBaseWidget::OnDragEnter( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent )
{
	if ( DragDropEvent.GetOperationAs<FUICommandDragDropOp>().IsValid() )
	{
		OwnerMultiBoxWidget.Pin()->OnCustomCommandDragEnter( MultiBlock.ToSharedRef(), MyGeometry, DragDropEvent );
	}
}

FReply SMultiBlockBaseWidget::OnDragOver( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent )
{
	if ( DragDropEvent.GetOperationAs<FUICommandDragDropOp>().IsValid() )
	{
		OwnerMultiBoxWidget.Pin()->OnCustomCommandDragged( MultiBlock.ToSharedRef(), MyGeometry, DragDropEvent );
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SMultiBlockBaseWidget::OnDrop( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent )
{
	if ( DragDropEvent.GetOperationAs<FUICommandDragDropOp>().IsValid() )
	{
		OwnerMultiBoxWidget.Pin()->OnCustomCommandDropped();
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

bool SMultiBlockBaseWidget::IsInEditMode() const
{
	if (OwnerMultiBoxWidget.IsValid())
	{
		return OwnerMultiBoxWidget.Pin()->GetMultiBox()->IsInEditMode();
	}

	return false;
}

/**
 * Creates a MultiBlock widget for this MultiBlock
 *
 * @param	InOwnerMultiBoxWidget	The widget that will own the new MultiBlock widget
 * @param	InLocation				The location information for the MultiBlock widget
 *
 * @return  MultiBlock widget object
 */
TSharedRef< IMultiBlockBaseWidget > FMultiBlock::MakeWidget( TSharedRef< SMultiBoxWidget > InOwnerMultiBoxWidget, EMultiBlockLocation::Type InLocation, bool bSectionContainsIcons ) const
{
	TSharedRef< IMultiBlockBaseWidget > NewMultiBlockWidget = ConstructWidget();

	// Tell the widget about its parent MultiBox widget
	NewMultiBlockWidget->SetOwnerMultiBoxWidget( InOwnerMultiBoxWidget );

	// Assign ourselves to the MultiBlock widget
	NewMultiBlockWidget->SetMultiBlock( AsShared() );

	// Pass location information to widget.
	NewMultiBlockWidget->SetMultiBlockLocation(InLocation, bSectionContainsIcons);

	// Work out what style the widget should be using
	const ISlateStyle* const StyleSet = InOwnerMultiBoxWidget->GetStyleSet();
	const FName& StyleName = InOwnerMultiBoxWidget->GetStyleName();

	// Build up the widget
	NewMultiBlockWidget->BuildMultiBlockWidget(StyleSet, StyleName);

	return NewMultiBlockWidget;
}

void FMultiBlock::SetSearchable( bool InSearchable )
{
	bSearchable = InSearchable;
}
bool FMultiBlock::GetSearchable() const
{
	return bSearchable;
}

/**
 * Constructor
 *
 * @param	InType	Type of MultiBox
 * @param	bInShouldCloseWindowAfterMenuSelection	Sets whether or not the window that contains this multibox should be destroyed after the user clicks on a menu item in this box
 */
FMultiBox::FMultiBox(const EMultiBoxType InType, FMultiBoxCustomization InCustomization, const bool bInShouldCloseWindowAfterMenuSelection)
	: bHasSearchWidget(false)
	, CommandLists()
	, Blocks()
	, StyleSet( &FCoreStyle::Get() )
	, StyleName( "ToolBar" )
	, Type( InType )
	, bShouldCloseWindowAfterMenuSelection( bInShouldCloseWindowAfterMenuSelection )
{
}

FMultiBox::~FMultiBox()
{
}

TSharedRef<FMultiBox> FMultiBox::Create( const EMultiBoxType InType, FMultiBoxCustomization InCustomization, const bool bInShouldCloseWindowAfterMenuSelection )
{
	TSharedRef<FMultiBox> NewBox = MakeShareable( new FMultiBox( InType, InCustomization, bInShouldCloseWindowAfterMenuSelection ) );

	return NewBox;
}

/**
 * Adds a MultiBlock to this MultiBox, to the end of the list
 */
void FMultiBox::AddMultiBlock( TSharedRef< const FMultiBlock > InBlock )
{
	checkSlow( !Blocks.Contains( InBlock ) );

	if( InBlock->GetActionList().IsValid() )
	{
		CommandLists.AddUnique( InBlock->GetActionList() );
	}

	Blocks.Add( InBlock );
}

void FMultiBox::AddMultiBlockToFront(TSharedRef< const FMultiBlock > InBlock)
{
	checkSlow(!Blocks.Contains(InBlock));

	if (InBlock->GetActionList().IsValid())
	{
		CommandLists.AddUnique(InBlock->GetActionList());
	}

	Blocks.Insert(InBlock, 0);
}

void FMultiBox::RemoveCustomMultiBlock( TSharedRef< const FMultiBlock> InBlock )
{
	if( IsCustomizable() )
	{
		int32 Index = Blocks.Find( InBlock );

		// Remove the block from the visual list
		if( Index != INDEX_NONE )
		{
			Blocks.RemoveAt( Index );
		}

	}
}

void FMultiBox::InsertCustomMultiBlock( TSharedRef<const FMultiBlock> InBlock, int32 Index )
{
	if (IsCustomizable() && ensure(InBlock->GetExtensionHook() != NAME_None))
	{
		int32 ExistingIndex = Blocks.Find( InBlock );

		FName DestinationBlockName = NAME_None;
		FName DestinationSectionName = NAME_None;
		if (Blocks.IsValidIndex(Index))
		{
			DestinationBlockName = Blocks[Index]->GetExtensionHook();

			int32 DestinationSectionEndIndex = INDEX_NONE;
			int32 DestinationSectionIndex = GetSectionEditBounds(Index, DestinationSectionEndIndex);
			if (Blocks.IsValidIndex(DestinationSectionIndex))
			{
				DestinationSectionName = Blocks[DestinationSectionIndex]->GetExtensionHook();
			}
		}

		if (InBlock->IsPartOfHeading())
		{
			if (InBlock->GetExtensionHook() == DestinationSectionName)
			{
				return;
			}

			if (ExistingIndex != INDEX_NONE)
			{
				int32 SourceSectionEndIndex = INDEX_NONE;
				int32 SourceSectionIndex = GetSectionEditBounds(ExistingIndex, SourceSectionEndIndex);
				if (SourceSectionIndex != INDEX_NONE && SourceSectionEndIndex != INDEX_NONE)
				{
					bool bHadSeparator = Blocks[SourceSectionIndex]->IsSeparator();

					TArray< TSharedRef< const FMultiBlock > > BlocksToMove;
					BlocksToMove.Reset(SourceSectionEndIndex - SourceSectionIndex + 1);
					for (int32 BlockIdx = SourceSectionIndex; BlockIdx < SourceSectionEndIndex; ++BlockIdx)
					{
						BlocksToMove.Add(Blocks[BlockIdx]);
					}

					Blocks.RemoveAt(SourceSectionIndex, SourceSectionEndIndex - SourceSectionIndex, false);

					if (Index > SourceSectionIndex)
					{
						Index -= BlocksToMove.Num();
					}

					if (Index == 0)
					{
						// Add missing separator for next section
						if (Blocks.Num() > 0 && (Blocks[0]->GetType() == EMultiBlockType::Heading))
						{
							BlocksToMove.Add(MakeShareable(new FMenuSeparatorBlock(Blocks[0]->GetExtensionHook(), /* bInIsPartOfHeading=*/ true)));
						}
					}
					else
					{
						// Add separator to beginning of section
						if (BlocksToMove.Num() > 0 && (BlocksToMove[0]->GetType() == EMultiBlockType::Heading))
						{
							BlocksToMove.Insert(MakeShareable(new FMenuSeparatorBlock(BlocksToMove[0]->GetExtensionHook(), /* bInIsPartOfHeading=*/ true)), 0);
						}
					}

					Blocks.Insert(BlocksToMove, Index);

					// Menus do not start with separators, remove separator if one exists
					if (Blocks.Num() > 0 && Blocks[0]->IsSeparator())
					{
						Blocks.RemoveAt(0, 1, false);
					}

					if (UToolMenuBase* ToolMenu = GetToolMenu())
					{
						ToolMenu->UpdateMenuCustomizationFromMultibox(SharedThis(this));
					}
				}
			}
		}
		else
		{
			if (ExistingIndex != INDEX_NONE)
			{
				Blocks.RemoveAt(ExistingIndex);
				if (ExistingIndex < Index)
				{
					--Index;
				}
			}

			Blocks.Insert(InBlock, Index);

			if (UToolMenuBase* ToolMenu = GetToolMenu())
			{
				ToolMenu->UpdateMenuCustomizationFromMultibox(SharedThis(this));
			}
		}
	}
}

/**
 * Creates a MultiBox widget for this MultiBox
 *
 * @return  MultiBox widget object
 */
TSharedRef< SMultiBoxWidget > FMultiBox::MakeWidget( bool bSearchable, FOnMakeMultiBoxBuilderOverride* InMakeMultiBoxBuilderOverride /* = nullptr */, TAttribute<float> InMaxHeight )
{	
	TSharedRef< SMultiBoxWidget > NewMultiBoxWidget =
		SNew( SMultiBoxWidget );

	// Set whether this box should be searched
	NewMultiBoxWidget->SetSearchable( bSearchable );

	// Assign ourselves to the MultiBox widget
	NewMultiBoxWidget->SetMultiBox( AsShared() );

	// Set the maximum height the MultiBox widget should be
	NewMultiBoxWidget->SetMaxHeight( InMaxHeight );

	if( (InMakeMultiBoxBuilderOverride != nullptr) && (InMakeMultiBoxBuilderOverride->IsBound()) )
	{
		TSharedRef<FMultiBox> ThisMultiBox = AsShared();
		InMakeMultiBoxBuilderOverride->Execute( ThisMultiBox, NewMultiBoxWidget );
	}
	else
	{
		// Build up the widget
		NewMultiBoxWidget->BuildMultiBoxWidget();
	}
	
#if PLATFORM_MAC
	if(Type == EMultiBoxType::MenuBar)
	{
		NewMultiBoxWidget->SetVisibility(EVisibility::Collapsed);
	}
#endif
	
	return NewMultiBoxWidget;
}

bool FMultiBox::IsCustomizable() const
{
	if (UToolMenuBase* ToolMenu = GetToolMenu())
	{
		return ToolMenu->IsEditing();
	}

	return false;
}

FName FMultiBox::GetCustomizationName() const
{
	return NAME_None;
}

TSharedPtr<FMultiBlock> FMultiBox::MakeMultiBlockFromCommand( TSharedPtr<const FUICommandInfo> CommandInfo, bool bCommandMustBeBound ) const
{
	TSharedPtr<FMultiBlock> NewBlock;

	// Find the command list that processes this command
	TSharedPtr<const FUICommandList> CommandList;

	for (int32 CommandListIndex = 0; CommandListIndex < CommandLists.Num(); ++CommandListIndex )
	{
		TSharedPtr<const FUICommandList> TestCommandList = CommandLists[CommandListIndex];
		if( TestCommandList->GetActionForCommand( CommandInfo.ToSharedRef() ) != NULL )
		{
			CommandList = TestCommandList;
			break;
		}
	}

	
	if( !bCommandMustBeBound && !CommandList.IsValid() && CommandLists.Num() > 0 )
	{
		// The first command list is the main command list and other are commandlists added from extension points
		// Use the main command list if one was not found
		CommandList = CommandLists[0];
	}

	if( !bCommandMustBeBound || CommandList.IsValid() )
	{
		// Only toolbars and menu buttons are supported currently
		switch ( Type )
		{
		case EMultiBoxType::ToolBar:
		case EMultiBoxType::UniformToolBar:
			{
				NewBlock = MakeShareable( new FToolBarButtonBlock( CommandInfo, CommandList ) );
			}
			break;
		case EMultiBoxType::Menu:
			{
				NewBlock = MakeShareable( new FMenuEntryBlock( NAME_None, CommandInfo, CommandList ) );
			}
			break;
		}
	}

	return NewBlock;

}

TSharedPtr<const FMultiBlock> FMultiBox::FindBlockFromNameAndType(const FName InName, const EMultiBlockType InType) const
{
	for (const auto& Block : Blocks)
	{
		if (Block->GetExtensionHook() == InName && Block->GetType() == InType)
		{
			return Block;
		}
	}

	return nullptr;
}

int32 FMultiBox::GetSectionEditBounds(const int32 Index, int32& OutSectionEndIndex) const
{
	// Only used by edit mode, identifies sections by heading blocks
	if (!IsInEditMode())
	{
		return INDEX_NONE;
	}

	int32 SectionBeginIndex = INDEX_NONE;
	for (int32 BlockIdx = Index; BlockIdx >= 0; --BlockIdx)
	{
		if (Blocks[BlockIdx]->GetType() == EMultiBlockType::Heading)
		{
			if (BlockIdx > 0 && Blocks[BlockIdx - 1]->IsSeparator() && Blocks[BlockIdx]->GetExtensionHook() == Blocks[BlockIdx - 1]->GetExtensionHook())
			{
				SectionBeginIndex = BlockIdx - 1;
			}
			else
			{
				SectionBeginIndex = BlockIdx;
			}
			break;
		}
	}

	OutSectionEndIndex = Blocks.Num();
	for (int32 BlockIdx = Index + 1; BlockIdx < Blocks.Num(); ++BlockIdx)
	{
		if (Blocks[BlockIdx]->GetType() == EMultiBlockType::Heading)
		{
			if (BlockIdx > 0 && Blocks[BlockIdx - 1]->IsSeparator() && Blocks[BlockIdx]->GetExtensionHook() == Blocks[BlockIdx - 1]->GetExtensionHook())
			{
				OutSectionEndIndex = BlockIdx - 1;
			}
			else
			{
				OutSectionEndIndex = BlockIdx;
			}
			break;
		}
	}

	return SectionBeginIndex;
}

UToolMenuBase* FMultiBox::GetToolMenu() const
{
	return WeakToolMenu.Get();
}

bool FMultiBox::IsInEditMode() const
{
	UToolMenuBase* ToolMenu = GetToolMenu();
	return ToolMenu && ToolMenu->IsEditing();
}

void SMultiBoxWidget::Construct( const FArguments& InArgs )
{
	ContentScale = InArgs._ContentScale;
}

TSharedRef<ITableRow> SMultiBoxWidget::GenerateTiles(TSharedPtr<SWidget> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow< TSharedPtr<SWidget> >, OwnerTable)
		[
			Item.ToSharedRef()
		];
}

float SMultiBoxWidget::GetItemWidth() const
{
	float MaxItemWidth = 0;
	for (int32 i = 0; i < TileViewWidgets.Num(); ++i)
	{
		MaxItemWidth = FMath::Max(TileViewWidgets[i]->GetDesiredSize().X, MaxItemWidth);
	}
	return MaxItemWidth;
}

float SMultiBoxWidget::GetItemHeight() const
{
	float MaxItemHeight = 0;
	for (int32 i = 0; i < TileViewWidgets.Num(); ++i)
	{
		MaxItemHeight = FMath::Max(TileViewWidgets[i]->GetDesiredSize().Y, MaxItemHeight);
	}
	return MaxItemHeight;
}

bool SMultiBoxWidget::IsBlockBeingDragged( TSharedPtr<const FMultiBlock> Block ) const
{
	if( DragPreview.PreviewBlock.IsValid() )
	{
		return DragPreview.PreviewBlock->GetActualBlock() == Block;
	}

	return false;
}

EVisibility SMultiBoxWidget::GetCustomizationBorderDragVisibility(const FName InBlockName, const EMultiBlockType InBlockType, bool& bOutInsertAfter) const
{
	bOutInsertAfter = false;

	if (DragPreview.PreviewBlock.IsValid())
	{
		const TArray< TSharedRef< const FMultiBlock > >& Blocks = MultiBox->GetBlocks();
		if (Blocks.IsValidIndex(DragPreview.InsertIndex))
		{
			if (InBlockName != NAME_None)
			{
				const TSharedRef< const FMultiBlock >& DropDestination = Blocks[DragPreview.InsertIndex];
				if (DropDestination->GetExtensionHook() == InBlockName && DropDestination->GetType() == InBlockType)
				{
					return EVisibility::Visible;
				}
			}
		}
		else if (Blocks.Num() == DragPreview.InsertIndex)
		{
			if (Blocks.Num() > 0 && Blocks.Last()->GetExtensionHook() == InBlockName && Blocks.Last()->GetType() == InBlockType)
			{
				bOutInsertAfter = true;
				return EVisibility::Visible;
			}
		}
	}

	return EVisibility::Collapsed;
}

void SMultiBoxWidget::AddBlockWidget( const FMultiBlock& Block, TSharedPtr<SHorizontalBox> HorizontalBox, TSharedPtr<SVerticalBox> VerticalBox, EMultiBlockLocation::Type InLocation, bool bSectionContainsIcons )
{
	check( MultiBox.IsValid() );

	bool bDisplayExtensionHooks = FMultiBoxSettings::DisplayMultiboxHooks.Get() && Block.GetExtensionHook() != NAME_None;

	TSharedRef<SWidget> BlockWidget = Block.MakeWidget(SharedThis(this), InLocation, bSectionContainsIcons)->AsWidget();

	TWeakPtr<SWidget> BlockWidgetWeakPtr = BlockWidget;
	TWeakPtr<const FMultiBlock> BlockWeakPtr = Block.AsShared();

	const ISlateStyle* const StyleSet = MultiBox->GetStyleSet();

	TSharedPtr<SWidget> FinalWidget;

	const EMultiBlockType BlockType = Block.GetType();

	if (MultiBox->ModifyBlockWidgetAfterMake.IsBound())
	{
		FinalWidget = MultiBox->ModifyBlockWidgetAfterMake.Execute(SharedThis(this), Block, BlockWidget);
	}
	else
	{
		FinalWidget = BlockWidget;
	}

	TSharedRef<SWidget> FinalWidgetWithHook = 
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.HAlign(HAlign_Center)
		.AutoHeight()
		[
			SNew(STextBlock)
			.Visibility(bDisplayExtensionHooks ? EVisibility::Visible : EVisibility::Collapsed)
			.ColorAndOpacity(StyleSet->GetColor("MultiboxHookColor"))
			.Text(FText::FromName(Block.GetExtensionHook()))
		]
		+ SVerticalBox::Slot()
		[
			FinalWidget.ToSharedRef()
		];

	switch (MultiBox->GetType())
	{
	case EMultiBoxType::MenuBar:
	case EMultiBoxType::ToolBar:
		{
			HorizontalBox->AddSlot()
			.AutoWidth()
			.Padding(0)
			[
				FinalWidgetWithHook
			];
		}
		break;
	case EMultiBoxType::VerticalToolBar:
		{
			if (UniformToolbarPanel.IsValid())
			{
				UniformToolbarPanel->AddSlot()
				[
					FinalWidgetWithHook
				];
			}
			else
			{
				VerticalBox->AddSlot()
				.AutoHeight()
				.Padding(0.0f, 1.0f, 0.0f, 1.0f)
				[
					FinalWidgetWithHook
				];
			}
		}
		break;
	case EMultiBoxType::UniformToolBar:
		{
			UniformToolbarPanel->AddSlot()
			[
				FinalWidgetWithHook
			];
		}
		break;
	case EMultiBoxType::ButtonRow:
		{
			TileViewWidgets.Add( FinalWidget.ToSharedRef() );
		}
		break;
	case EMultiBoxType::Menu:
		{
			VerticalBox->AddSlot()
			.AutoHeight()
			.Padding( 1.0f, 0.0f, 1.0f, 0.0f )
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Visibility(bDisplayExtensionHooks ? EVisibility::Visible : EVisibility::Collapsed)
					.ColorAndOpacity(StyleSet->GetColor("MultiboxHookColor"))
					.Text(FText::FromName(Block.GetExtensionHook()))
				]
				+SHorizontalBox::Slot()
				[
					FinalWidget.ToSharedRef()
				]
			];
		}
		break;
	}
}

void SMultiBoxWidget::SetSearchable(bool InSearchable)
{
	bSearchable = InSearchable;
}
bool SMultiBoxWidget::GetSearchable() const
{
	return bSearchable;
}

/** Creates the SearchTextWidget if the MultiBox has requested one */
void SMultiBoxWidget::CreateSearchTextWidget()
{
	if (!MultiBox->bHasSearchWidget)
	{
		return;
	}

	SearchTextWidget = 
		SNew(SSearchBox)
		   .HintText(LOCTEXT("SearchHint", "Search"))
		   .OnTextChanged(this, &SMultiBoxWidget::OnFilterTextChanged);

	TSharedRef< FWidgetBlock > NewWidgetBlock(new FWidgetBlock(SearchTextWidget.ToSharedRef(), FText::GetEmpty(), false));
	NewWidgetBlock->SetSearchable(false);

	MultiBox->AddMultiBlockToFront(NewWidgetBlock);
}

/** Called when the SearchText changes */
void SMultiBoxWidget::OnFilterTextChanged(const FText& InFilterText)
{
	SearchText = InFilterText;

	FilterMultiBoxEntries();
}

/**
 * Builds this MultiBox widget up from the MultiBox associated with it
 */
void SMultiBoxWidget::BuildMultiBoxWidget()
{
	check( MultiBox.IsValid() );

	// Grab the list of blocks, early out if there's nothing to fill the widget with
	const TArray< TSharedRef< const FMultiBlock > >& Blocks = MultiBox->GetBlocks();
	if ( Blocks.Num() == 0 )
	{
		return;
	}

	CreateSearchTextWidget();

	// Select background brush based on the type of multibox.
	const ISlateStyle* const StyleSet = MultiBox->GetStyleSet();
	const FName& StyleName = MultiBox->GetStyleName();
	const FSlateBrush* BackgroundBrush = StyleSet->GetBrush( StyleName, ".Background" );

	// Create a box panel that the various multiblocks will resides within
	// @todo Slate MultiBox: Expose margins and other useful bits
	TSharedPtr< SVerticalBox > VerticalBox;
	TSharedPtr< SWidget > MainWidget;
	TSharedPtr<SHorizontalBox> HorizontalBox;

	/** The current row of buttons for if the multibox type is a button row */
	TSharedPtr<SHorizontalBox> ButtonRow;

	TSharedPtr< STileView< TSharedPtr<SWidget> > > TileView;

	switch (MultiBox->GetType())
	{
	case EMultiBoxType::MenuBar:
	case EMultiBoxType::ToolBar:
		{
			MainWidget = HorizontalBox = ClippedHorizontalBox = SNew(SClippingHorizontalBox)
				.BackgroundBrush(BackgroundBrush)
				.OnWrapButtonClicked(FOnGetContent::CreateSP(this, &SMultiBoxWidget::OnWrapButtonClicked))
				.StyleSet(StyleSet)
				.StyleName(StyleName);
		}
		break;
	case EMultiBoxType::VerticalToolBar:
		{
			MainWidget = VerticalBox = SNew(SVerticalBox);
		}
		break;
	case EMultiBoxType::UniformToolBar:
		{
			MainWidget = UniformToolbarPanel =
				SAssignNew(UniformToolbarPanel, SUniformToolbarPanel)
				.Orientation(Orient_Horizontal)
				.StyleSet(StyleSet)
				.StyleName(StyleName)
				.MinUniformSize(StyleSet->GetFloat(StyleName, ".MinUniformToolbarSize", 0.0f))
				.MaxUniformSize(StyleSet->GetFloat(StyleName, ".MaxUniformToolbarSize", 0.0f))
				.OnDropdownOpened(FOnGetContent::CreateSP(this, &SMultiBoxWidget::OnWrapButtonClicked));
		}
		break;
	case EMultiBoxType::ButtonRow:
		{
			MainWidget = TileView = SNew(STileView< TSharedPtr<SWidget> >)
				.OnGenerateTile(this, &SMultiBoxWidget::GenerateTiles)
				.ListItemsSource(&TileViewWidgets)
				.ItemWidth(this, &SMultiBoxWidget::GetItemWidth)
				.ItemHeight(this, &SMultiBoxWidget::GetItemHeight)
				.SelectionMode(ESelectionMode::None);
		}
		break;
	case EMultiBoxType::Menu:
		{
			if (MaxHeight.IsSet())
			{
				MainWidget = SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.MaxHeight(MaxHeight)
					[
						// wrap menu content in a scrollbox to support vertical scrolling if needed
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SAssignNew(VerticalBox, SVerticalBox)
						]
					];
			}
			else
			{
				// wrap menu content in a scrollbox to support vertical scrolling if needed
				MainWidget = SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(VerticalBox, SVerticalBox)
					];
			}
		}
		break;
	}
	
	bool bInsideGroup = false;

	// Start building up the actual UI from each block in this MultiBox
	bool bSectionContainsIcons = false;
	int32 NextMenuSeparator = INDEX_NONE;

	for( int32 Index = 0; Index < Blocks.Num(); Index++ )
	{
		// If we've passed the last menu separator, scan for the next one (the end of the list is also considered a menu separator for the purposes of this index)
		if (NextMenuSeparator < Index)
		{
			bSectionContainsIcons = false;
			for (++NextMenuSeparator; NextMenuSeparator < Blocks.Num(); ++NextMenuSeparator)
			{
				const FMultiBlock& TestBlock = *Blocks[NextMenuSeparator];
				if (!bSectionContainsIcons && TestBlock.HasIcon())
				{
					bSectionContainsIcons = true;
				}

				if (TestBlock.GetType() == EMultiBlockType::Separator)
				{
					break;
				}
			}
		}

		const FMultiBlock& Block = *Blocks[Index];
		EMultiBlockLocation::Type Location = EMultiBlockLocation::None;
		
		// Determine the location of the current block, used for group styling information
		{
			// Check if we are a start or end block
			if (Block.IsGroupStartBlock())
			{
				bInsideGroup = true;
			}
			else if (Block.IsGroupEndBlock())
			{
				bInsideGroup = false;
			}

			// Check if we are next to a start or end block
			bool bIsNextToStartBlock = false;
			bool bIsNextToEndBlock = false;
			if (Index + 1 < Blocks.Num())
			{
				const FMultiBlock& NextBlock = *Blocks[Index + 1];
				if ( NextBlock.IsGroupEndBlock() )
				{
					bIsNextToEndBlock = true;
				}
			}
			if (Index > 0)
			{
				const FMultiBlock& PrevBlock = *Blocks[Index - 1];
				if ( PrevBlock.IsGroupStartBlock() )
				{
					bIsNextToStartBlock = true;
				}
			}

			// determine location
			if (bInsideGroup)
			{
				// assume we are in the middle of a group
				Location = EMultiBlockLocation::Middle;

				// We are the start of a group
				if (bIsNextToStartBlock && !bIsNextToEndBlock)
				{
					Location = EMultiBlockLocation::Start;
				}
				// we are the end of a group
				else if (!bIsNextToStartBlock && bIsNextToEndBlock)
				{
					Location = EMultiBlockLocation::End;
				}
				// we are the only block in a group
				else if (bIsNextToStartBlock && bIsNextToEndBlock)
				{
					Location = EMultiBlockLocation::None;
				}
			}
		}


		if( DragPreview.IsValid() && DragPreview.InsertIndex == Index )
		{
			// Add the drag preview before if we have it. This block shows where the custom block will be 
			// added if the user drops it
			AddBlockWidget( *DragPreview.PreviewBlock, HorizontalBox, VerticalBox, EMultiBlockLocation::None, bSectionContainsIcons );
		}
		
		// Do not add a block if it is being dragged
		if( !IsBlockBeingDragged( Blocks[Index] ) )
		{
			AddBlockWidget( Block, HorizontalBox, VerticalBox, Location, bSectionContainsIcons );
		}
	}

	// Add the wrap button as the final block
	if (ClippedHorizontalBox.IsValid())
	{
		ClippedHorizontalBox->AddWrapButton();
	}

	// Setup the root border widget
	TSharedPtr< SBorder > RootBorder;
	switch (MultiBox->GetType())
	{
	case EMultiBoxType::MenuBar:
	case EMultiBoxType::ToolBar:
		{
			RootBorder =
				SNew( SBorder )
				.Padding(0)
				.BorderImage( FCoreStyle::Get().GetBrush("NoBorder") )
				// Assign the box panel as the child
				[
					MainWidget.ToSharedRef()
				];
		}
		break;
	default:
		{
			RootBorder =
				SNew( SBorder )
				.Padding(0)
				.BorderImage( BackgroundBrush )
				.ForegroundColor( FCoreStyle::Get().GetSlateColor("DefaultForeground") )
				// Assign the box panel as the child
				[
					MainWidget.ToSharedRef()
				];
		}
		break;
	}

	// Prevent tool-tips spawned by child widgets from drawing on top of our main widget
	RootBorder->EnableToolTipForceField( true );

	ChildSlot
	[
		RootBorder.ToSharedRef()
	];

}

TSharedRef<SWidget> SMultiBoxWidget::OnWrapButtonClicked()
{
	FMenuBuilder MenuBuilder(true, NULL, TSharedPtr<FExtender>(), false, GetStyleSet());
	{ 
		const int32 ClippedIndex = ClippedHorizontalBox.IsValid() ? ClippedHorizontalBox->GetClippedIndex() : UniformToolbarPanel->GetClippedIndex();
		// Iterate through the array of blocks telling each one to add itself to the menu
		const TArray< TSharedRef< const FMultiBlock > >& Blocks = MultiBox->GetBlocks();
		for (int32 BlockIdx = ClippedIndex; BlockIdx < Blocks.Num(); ++BlockIdx)
		{
			// Skip the first entry if its a separator
			if (BlockIdx != ClippedIndex || !Blocks[BlockIdx]->IsSeparator())
			{
				Blocks[BlockIdx]->CreateMenuEntry(MenuBuilder);
			}
		}
	}

	return MenuBuilder.MakeWidget();
}

void SMultiBoxWidget::UpdateDropAreaPreviewBlock( TSharedRef<const FMultiBlock> MultiBlock, TSharedPtr<FUICommandDragDropOp> DragDropContent, const FGeometry& DragAreaGeometry, const FVector2D& DragPos )
{
	const FName BlockName = DragDropContent->ItemName;
	const EMultiBlockType BlockType = DragDropContent->BlockType;
	FName OriginMultiBox = DragDropContent->OriginMultiBox;

	FVector2D LocalDragPos = DragAreaGeometry.AbsoluteToLocal( DragPos );

	FVector2D DrawSize = DragAreaGeometry.GetDrawSize();

	bool bIsDraggingSection = DragDropContent->bIsDraggingSection;

	bool bAddedNewBlock = false;
	bool bValidCommand = true;
	if (!DragPreview.IsSameBlockAs(BlockName, BlockType))
	{
		TSharedPtr<const FMultiBlock> ExistingBlock = MultiBox->FindBlockFromNameAndType(BlockName, BlockType);
		// Check that the command does not already exist and that we can create it or that we are dragging an existing block in this box
		if( !ExistingBlock.IsValid() || ( ExistingBlock.IsValid() && OriginMultiBox == MultiBox->GetCustomizationName() ) )
		{
			TSharedPtr<const FMultiBlock> NewBlock = ExistingBlock;

			if( NewBlock.IsValid() )
			{
				DragPreview.Reset();
				DragPreview.BlockName = BlockName;
				DragPreview.BlockType = BlockType;
				DragPreview.PreviewBlock = 
					MakeShareable(
						new FDropPreviewBlock( 
							NewBlock.ToSharedRef(), 
							NewBlock->MakeWidget( SharedThis(this), EMultiBlockLocation::None, NewBlock->HasIcon() ) )
					);

				bAddedNewBlock = true;
			}
		}
		else
		{
			// this command cannot be dropped here
			bValidCommand = false;
		}
	}

	if( bValidCommand )
	{
		// determine whether or not to insert before or after
		bool bInsertBefore = false;
		if( MultiBox->GetType() == EMultiBoxType::ToolBar )
		{
			DragPreview.InsertOrientation  = EOrientation::Orient_Horizontal;
			if( LocalDragPos.X < DrawSize.X / 2 )
			{
				// Insert before horizontally
				bInsertBefore = true;
			}
			else
			{
				// Insert after horizontally
				bInsertBefore = false;
			}
		}
		else 
		{
			DragPreview.InsertOrientation  = EOrientation::Orient_Vertical;
			if( LocalDragPos.Y < DrawSize.Y / 2 )
			{
				// Insert before vertically
				bInsertBefore = true;
			}
			else
			{
				// Insert after vertically
				bInsertBefore = false;
			}
		}

		int32 CurrentIndex = DragPreview.InsertIndex;
		DragPreview.InsertIndex = INDEX_NONE;
		// Find the index of the multiblock being dragged over. This is where we will insert the new block
		if( DragPreview.PreviewBlock.IsValid() )
		{
			const TArray< TSharedRef< const FMultiBlock > >& Blocks = MultiBox->GetBlocks();
			int32 HoverIndex = Blocks.IndexOfByKey(MultiBlock);
			int32 HoverSectionEndIndex = INDEX_NONE;
			int32 HoverSectionBeginIndex = MultiBox->GetSectionEditBounds(HoverIndex, HoverSectionEndIndex);

			if (bIsDraggingSection)
			{
				// Hovering over final block means insert at end of list
				if ((HoverIndex == Blocks.Num() - 1) && Blocks.Num() > 0)
				{
					DragPreview.InsertIndex = Blocks.Num();
				}
				else if (Blocks.IsValidIndex(HoverSectionBeginIndex))
				{
					DragPreview.InsertIndex = HoverSectionBeginIndex;
				}
			}
			else if (HoverIndex != INDEX_NONE)
			{
				if (MultiBlock->IsPartOfHeading())
				{
					if (MultiBlock->IsSeparator())
					{
						// Move insert index above separator of heading
						DragPreview.InsertIndex = HoverIndex;
					}
					else
					{
						// Move insert index after heading
						DragPreview.InsertIndex = HoverIndex + 1;
					}
				}
				else
				{
					if (bInsertBefore)
					{
						DragPreview.InsertIndex = HoverIndex;
					}
					else
					{
						DragPreview.InsertIndex = HoverIndex + 1;
					}
				}
			}
		}
	}
}

EVisibility SMultiBoxWidget::GetCustomizationVisibility( TWeakPtr<const FMultiBlock> BlockWeakPtr, TWeakPtr<SWidget> BlockWidgetWeakPtr ) const
{
	if( MultiBox->IsInEditMode() && BlockWidgetWeakPtr.IsValid() && BlockWeakPtr.IsValid() && (!DragPreview.PreviewBlock.IsValid() || BlockWeakPtr.Pin() != DragPreview.PreviewBlock->GetActualBlock() ) )
	{
		// If in edit mode and this is not the block being dragged, the customization widget should be visible if the default block beging customized would have been visible
		return BlockWeakPtr.Pin()->GetAction().IsValid() && BlockWidgetWeakPtr.Pin()->GetVisibility() == EVisibility::Visible ? EVisibility::Visible : EVisibility::Collapsed;
	}
	else
	{
		return EVisibility::Collapsed;
	}
}

void SMultiBoxWidget::OnCustomCommandDragEnter( TSharedRef<const FMultiBlock> MultiBlock, const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent )
{
	if( MultiBlock != DragPreview.PreviewBlock && MultiBox->IsInEditMode() )
	{
		TSharedPtr<FUICommandDragDropOp> DragDropContent = StaticCastSharedPtr<FUICommandDragDropOp>( DragDropEvent.GetOperation() );

		UpdateDropAreaPreviewBlock( MultiBlock, DragDropContent, MyGeometry, DragDropEvent.GetScreenSpacePosition() );
	}
}


void SMultiBoxWidget::OnCustomCommandDragged( TSharedRef<const FMultiBlock> MultiBlock, const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent )
{
	if( MultiBlock != DragPreview.PreviewBlock && MultiBox->IsInEditMode() )
	{
		TSharedPtr<FUICommandDragDropOp> DragDropContent = StaticCastSharedPtr<FUICommandDragDropOp>( DragDropEvent.GetOperation() );

		UpdateDropAreaPreviewBlock( MultiBlock, DragDropContent, MyGeometry, DragDropEvent.GetScreenSpacePosition() );
	}
}

void SMultiBoxWidget::OnCustomCommandDropped()
{
	if( DragPreview.IsValid() )
	{	

		// Check that the command does not already exist and that we can create it or that we are dragging an exisiting block in this box
		TSharedPtr<const FMultiBlock> Block = MultiBox->FindBlockFromNameAndType(DragPreview.BlockName, DragPreview.BlockType);
		if(Block.IsValid())
		{
			if (Block->IsSeparator() && Block->IsPartOfHeading())
			{
				TSharedPtr<const FMultiBlock> HeadingBlock = MultiBox->FindBlockFromNameAndType(DragPreview.BlockName, EMultiBlockType::Heading);
				if (HeadingBlock.IsValid())
				{
					Block = HeadingBlock;
				}
			}

			MultiBox->InsertCustomMultiBlock( Block.ToSharedRef(), DragPreview.InsertIndex );
		}

		DragPreview.Reset();

		BuildMultiBoxWidget();
	}
}

void SMultiBoxWidget::OnDropExternal()
{
	// The command was not dropped in this widget
	if( DragPreview.IsValid() )
	{
		DragPreview.Reset();

		BuildMultiBoxWidget();
	}
}

FReply SMultiBoxWidget::OnDragOver( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent )
{
	if ( DragDropEvent.GetOperationAs<FUICommandDragDropOp>().IsValid() && MultiBox->IsInEditMode() )
	{
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SMultiBoxWidget::OnDrop( const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent )
{
	if ( DragDropEvent.GetOperationAs<FUICommandDragDropOp>().IsValid() )
	{
		OnCustomCommandDropped();
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

bool SMultiBoxWidget::SupportsKeyboardFocus() const
{
	return true;
}

FReply SMultiBoxWidget::FocusNextWidget(EUINavigation NavigationType)
{
	TSharedPtr<SWidget> FocusWidget = FSlateApplication::Get().GetKeyboardFocusedWidget();
	if(FocusWidget.IsValid())
	{
		FWidgetPath FocusPath;
		FSlateApplication::Get().GeneratePathToWidgetUnchecked( FocusWidget.ToSharedRef(), FocusPath );
		if (FocusPath.IsValid())
		{
			FWeakWidgetPath WeakFocusPath = FocusPath;
			FWidgetPath NextFocusPath = WeakFocusPath.ToNextFocusedPath(NavigationType);
			if ( NextFocusPath.Widgets.Num() > 0 )
			{
				return FReply::Handled().SetUserFocus(NextFocusPath.Widgets.Last().Widget, EFocusCause::Navigation);
			}
		}
	}

	return FReply::Unhandled();
}

FReply SMultiBoxWidget::OnFocusReceived( const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent )
{
	ResetSearch();

	if (InFocusEvent.GetCause() == EFocusCause::Navigation)
	{
		// forward focus to children
		return FocusNextWidget( EUINavigation::Next );
	}

	return FReply::Unhandled();
}

FReply SMultiBoxWidget::OnKeyDown( const FGeometry& MyGeometry, const FKeyEvent& KeyEvent )
{
	SCompoundWidget::OnKeyDown( MyGeometry, KeyEvent );

	// allow use of up and down keys to transfer focus/hover state
	if( KeyEvent.GetKey() == EKeys::Up )
	{
		return FocusNextWidget( EUINavigation::Previous );
	}
	else if( KeyEvent.GetKey() == EKeys::Down )
	{
		return FocusNextWidget( EUINavigation::Next );
	}

	return FReply::Unhandled();
}

FReply SMultiBoxWidget::OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent)
{
	FReply Reply = FReply::Unhandled();

	if (bSearchable && SearchText.IsEmpty())
	{
		// Check for special characters
		const TCHAR Character = InCharacterEvent.GetCharacter();
		BeginSearch(Character);
		Reply = FReply::Handled();
	}

	return Reply;
}

void SMultiBoxWidget::BeginSearch(const TCHAR InChar)
{
	// Certain characters are not allowed
	bool bIsCharAllowed = true;
	{
		if (InChar <= 0x1F)
		{
			bIsCharAllowed = false;
		}
	}

	if (bIsCharAllowed)
	{
		FString NewSearchText;
		NewSearchText += InChar;

		if (SearchTextWidget.IsValid() && SearchBlockWidget.IsValid())
		{
			// Make the search box visible and focused
			SearchBlockWidget->SetVisibility(EVisibility::Visible);
			FSlateApplication::Get().SetUserFocus(0, SearchTextWidget);

			SearchTextWidget->SetText(FText::FromString(NewSearchText));
		}
	}
}

void SMultiBoxWidget::ResetSearch()
{
	// Empty search text
	if (SearchTextWidget.IsValid())
	{
		SearchTextWidget->SetText(FText::GetEmpty());
	}
}

void SMultiBoxWidget::FilterMultiBoxEntries()
{
	if (SearchText.IsEmpty())
	{
		for (auto It = MultiBoxWidgets.CreateConstIterator(); It; ++It)
		{
			It.Key()->SetVisibility(EVisibility::Visible);
		}

		if (SearchBlockWidget.IsValid())
		{
			SearchBlockWidget->SetVisibility(EVisibility::Collapsed);
		}

		// Return focus to parent widget
		FSlateApplication::Get().SetUserFocus(0, SharedThis(this));

		return;
	}

	for(auto It = MultiBoxWidgets.CreateConstIterator(); It; ++It)
	{
		// Non-searched elements should not be rendered while searching
		if(It.Value().IsEmpty())
		{
			if(SearchText.IsEmpty())
			{
				It.Key()->SetVisibility( EVisibility::Visible );
			}
			else
			{
				It.Key()->SetVisibility( EVisibility::Collapsed );
			}
		}
		else
		{
			// Compare widget text to the current search text
			if( It.Value().ToString().Contains( SearchText.ToString() ) )
			{
				It.Key()->SetVisibility( EVisibility::Visible );
			}
			else
			{
				It.Key()->SetVisibility( EVisibility::Collapsed );
			}
		}
	}

	if (SearchBlockWidget.IsValid())
	{
		SearchBlockWidget->SetVisibility(EVisibility::Visible);
	}
}

FText SMultiBoxWidget::GetSearchText() const
{
	return SearchText;
}

TSharedPtr<SWidget> SMultiBoxWidget::GetSearchTextWidget()
{
	return SearchTextWidget;
}

void SMultiBoxWidget::SetSearchBlockWidget(TSharedPtr<SWidget> InWidget)
{
	SearchBlockWidget = InWidget;
}

void SMultiBoxWidget::AddSearchElement( TSharedPtr<SWidget> BlockWidget, FText BlockDisplayText )
{
	AddElement(BlockWidget, BlockDisplayText, true);
}

void SMultiBoxWidget::AddElement(TSharedPtr<SWidget> BlockWidget, FText BlockDisplayText, bool bInSearchable)
{
	 // Non-Searchable widgets shouldn't have search text
	if (!bInSearchable)
	{
		BlockDisplayText = FText::GetEmpty();
	}

	MultiBoxWidgets.Add(BlockWidget, BlockDisplayText);
}


bool SMultiBoxWidget::OnVisualizeTooltip(const TSharedPtr<SWidget>& TooltipContent)
{
	// tooltips on multibox widgets are not supported outside of the editor or programs
	return !GIsEditor && !FGenericPlatformProperties::IsProgram();
}
#undef LOCTEXT_NAMESPACE
