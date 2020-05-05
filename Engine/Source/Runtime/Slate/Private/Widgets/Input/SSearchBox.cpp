// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/Input/SSearchBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SThrobber.h"

const double SSearchBox::FilterDelayAfterTyping = 0.25f;

void SSearchBox::Construct( const FArguments& InArgs )
{
	check(InArgs._Style);

	SearchResultData = InArgs._SearchResultData;
	bIsSearching = InArgs._IsSearching;
	OnSearchDelegate = InArgs._OnSearch;
	OnTextChangedDelegate = InArgs._OnTextChanged;
	OnTextCommittedDelegate = InArgs._OnTextCommitted;
	DelayChangeNotificationsWhileTyping = InArgs._DelayChangeNotificationsWhileTyping;

	InactiveFont = InArgs._Style->TextBoxStyle.Font;
	ActiveFont = InArgs._Style->ActiveFontInfo;

	SEditableTextBox::Construct( SEditableTextBox::FArguments()
		.Style( &InArgs._Style->TextBoxStyle )
		.Font( this, &SSearchBox::GetWidgetFont )
		.Text( InArgs._InitialText.Get() )
		.HintText( InArgs._HintText )
		.SelectAllTextWhenFocused( InArgs._SelectAllTextWhenFocused )
		.RevertTextOnEscape( true )
		.ClearKeyboardFocusOnCommit( false )
		.OnTextChanged( this, &SSearchBox::HandleTextChanged )
		.OnTextCommitted( this, &SSearchBox::HandleTextCommitted )
		.MinDesiredWidth( InArgs._MinDesiredWidth )
		.OnKeyDownHandler( InArgs._OnKeyDownHandler )
	);

	// If we want to have the buttons appear to the left of the text box we have to insert the slots instead of add them
	int32 SlotIndex = InArgs._Style->bLeftAlignButtons ? 0 : Box->NumSlots();

	// Add a throbber to show if there is a search running.
	Box->InsertSlot(SlotIndex++)
	.AutoWidth()
	.Padding(0, 0, 2, 0)
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		SNew(SCircularThrobber)
		.Radius(9.0f)
		.Visibility(this, &SSearchBox::GetIsSearchingThrobberVisibility)
		.ToolTipText(NSLOCTEXT("SearchBox", "Searching", "Searching..."))
		.ColorAndOpacity(FSlateColor::UseForeground())
	];

	// If a search delegate was bound, add a previous and next button
	if (OnSearchDelegate.IsBound())
	{
		// Search result data text
		Box->InsertSlot(SlotIndex++)
		.AutoWidth()
		.Padding(0, 0, 2, 0)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Visibility(this, &SSearchBox::GetSearchResultDataVisibility)
			.Text(this, &SSearchBox::GetSearchResultText)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];

		// Previous result button
		Box->InsertSlot(SlotIndex++)
		.AutoWidth()
		.Padding(InArgs._Style->ImagePadding)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Fill)
		[
			SNew(SButton)
			.ButtonStyle( FCoreStyle::Get(), "NoBorder" )
			.ContentPadding( FMargin(5, 0) )
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked( this, &SSearchBox::OnClickedSearch, SSearchBox::Previous )
			.ForegroundColor( FSlateColor::UseForeground() )
			.IsFocusable(false)
			.Visibility(this, &SSearchBox::GetSearchResultNavigationButtonVisibility)
			[
				SNew(SImage)
				.Image( &InArgs._Style->UpArrowImage )
				.ColorAndOpacity( FSlateColor::UseForeground() )
			]
		];
		// Next result button
		Box->InsertSlot(SlotIndex++)
		.AutoWidth()
		.Padding(InArgs._Style->ImagePadding)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Fill)
		[
			SNew(SButton)
			.ButtonStyle( FCoreStyle::Get(), "NoBorder" )
			.ContentPadding( FMargin(5, 0) )
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked( this, &SSearchBox::OnClickedSearch, SSearchBox::Next )
			.ForegroundColor( FSlateColor::UseForeground() )
			.IsFocusable(false)
			.Visibility(this, &SSearchBox::GetSearchResultNavigationButtonVisibility)
			[
				SNew(SImage)
				.Image( &InArgs._Style->DownArrowImage )
				.ColorAndOpacity( FSlateColor::UseForeground() )
			]
		];
	}

	// Add a search glass image so that the user knows this text box is for searching
	Box->InsertSlot(SlotIndex++)
	.AutoWidth()
	.Padding(InArgs._Style->ImagePadding)
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		SNew(SImage)
		.Visibility(this, &SSearchBox::GetSearchGlassVisibility)
		.Image( &InArgs._Style->GlassImage )
		.ColorAndOpacity( FSlateColor::UseForeground() )
	];

	// Add an X to clear the search whenever there is some text typed into it
	Box->InsertSlot(SlotIndex++)
	.AutoWidth()
	.Padding(InArgs._Style->ImagePadding)
	.HAlign(HAlign_Center)
	.VAlign(VAlign_Center)
	[
		SNew(SButton)
		.Visibility(this, &SSearchBox::GetXVisibility)
		.ButtonStyle( FCoreStyle::Get(), "NoBorder" )
		.ContentPadding(0)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.OnClicked( this, &SSearchBox::OnClearSearch )
		.ForegroundColor( FSlateColor::UseForeground() )
		// Allow the button to steal focus so that the search text will be automatically committed. Afterwards focus will be returned to the text box.
		// If the user is keyboard-centric, they'll "ctrl+a, delete" to clear the search
		.IsFocusable(true)
		[
			SNew(SImage)
			.Image( &InArgs._Style->ClearImage )
			.ColorAndOpacity( FSlateColor::UseForeground() )
		]
	];
}

EActiveTimerReturnType SSearchBox::TriggerOnTextChanged( double InCurrentTime, float InDeltaTime, FText NewText )
{
	// Reset the flag first in case the delegate winds up triggering HandleTextChanged
	ActiveTimerHandle.Reset();

	OnTextChangedDelegate.ExecuteIfBound( NewText );
	return EActiveTimerReturnType::Stop;
}

void SSearchBox::HandleTextChanged(const FText& NewText)
{
	// Remove the existing registered tick if necessary
	if ( ActiveTimerHandle.IsValid() )
	{
		UnRegisterActiveTimer( ActiveTimerHandle.Pin().ToSharedRef() );
	}

	if ( DelayChangeNotificationsWhileTyping.Get() && HasKeyboardFocus() )
	{
		ActiveTimerHandle = RegisterActiveTimer( FilterDelayAfterTyping, FWidgetActiveTimerDelegate::CreateSP( this, &SSearchBox::TriggerOnTextChanged, NewText ) );
	}
	else
	{
		OnTextChangedDelegate.ExecuteIfBound( NewText );
	}
}

void SSearchBox::HandleTextCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	if ( ActiveTimerHandle.IsValid() )
	{
		UnRegisterActiveTimer( ActiveTimerHandle.Pin().ToSharedRef() );
	}

	OnTextCommittedDelegate.ExecuteIfBound( NewText, CommitType );
}

FText SSearchBox::GetSearchResultText() const
{
	TOptional<FSearchResultData> CurrentSearchResultData = SearchResultData.Get();
	if (CurrentSearchResultData.IsSet())
	{
		return FText::Format(NSLOCTEXT("SearchBox", "SearchResultFormat", "{0} / {1}"), 
			CurrentSearchResultData.GetValue().CurrentSearchResultIndex, CurrentSearchResultData.GetValue().NumSearchResults);
	}
	else
	{
		return FText();
	}
}

EVisibility SSearchBox::GetSearchResultNavigationButtonVisibility() const
{
	if (SearchResultData.IsBound())
	{
		return SearchResultData.Get().IsSet()
			? EVisibility::Visible
			: EVisibility::Collapsed;
	}
	else
	{
		return EVisibility::Visible;
	}
}

EVisibility SSearchBox::GetXVisibility() const
{
	return (EditableText->GetText().IsEmpty())
		? EVisibility::Collapsed
		: EVisibility::Visible;
}

EVisibility SSearchBox::GetSearchResultDataVisibility() const
{
	return SearchResultData.Get().IsSet()
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

EVisibility SSearchBox::GetIsSearchingThrobberVisibility() const
{
	return (bIsSearching.Get())
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

EVisibility SSearchBox::GetSearchGlassVisibility() const
{
	return (EditableText->GetText().IsEmpty())
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

FReply SSearchBox::OnClickedSearch(SSearchBox::SearchDirection Direction)
{
	OnSearchDelegate.ExecuteIfBound(Direction);
	return FReply::Handled();
}

FReply SSearchBox::OnClearSearch()
{
	// When we get here, the button will already have stolen focus, thus committing any unset values in the search box.
	// This will have allowed any widgets which depend on its state to update themselves prior to the search box being cleared,
	// which happens now. This is important as the act of clearing the search text may also destroy those widgets (for example,
	// if the search box is being used as a filter).
	this->SetText( FText::GetEmpty() );

	// Finally set focus back to the editable text
	return FReply::Handled().SetUserFocus(EditableText.ToSharedRef(), EFocusCause::SetDirectly);
}

FSlateFontInfo SSearchBox::GetWidgetFont() const
{
	return EditableText->GetText().IsEmpty() ? InactiveFont : ActiveFont;
}
