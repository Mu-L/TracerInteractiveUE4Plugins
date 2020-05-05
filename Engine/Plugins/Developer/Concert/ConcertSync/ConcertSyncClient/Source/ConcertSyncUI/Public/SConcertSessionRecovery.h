// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SlateFwd.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

struct FConcertClientSessionActivity;
struct FConcertClientInfo;
class SConcertSessionActivities;
class FConcertSessionActivitiesOptions;

/**
 * Displays the list of activities available for recovery and let the user select what should or shouldn't be recovered.
 */
class CONCERTSYNCUI_API SConcertSessionRecovery : public SCompoundWidget
{
public:
	/** Used to pull activities from a session. Used to fetch and display the activities of an archived session. */
	using FFetchActivitiesFunc = TFunction<bool(TArray<TSharedPtr<FConcertClientSessionActivity>>& /*InOutActivities*/, int32& /*OutFetchedCount*/, FText& /*ErrorMsg*/)>;

	/** Used to map an activity to its client. */
	using FGetActivityClientInfoFunc = TFunction<const FConcertClientInfo*(FGuid /*ClientId*/)>;

public:
	SLATE_BEGIN_ARGS(SConcertSessionRecovery)
		: _ClientAvatarColorColumnVisibility(EVisibility::Collapsed)
		, _ClientNameColumnVisibility(EVisibility::Collapsed)
		, _OperationColumnVisibility(EVisibility::Visible)
		, _PackageColumnVisibility(EVisibility::Collapsed)
		, _DetailsAreaVisibility(EVisibility::Collapsed)
		, _IsConnectionActivityFilteringEnabled(false)
		, _IsLockActivityFilteringEnabled(false)
		, _IsPackageActivityFilteringEnabled(true)
		, _IsTransactionActivityFilteringEnabled(true)
		, _IsIgnoredActivityFilteringEnabled(false)
		, _AreRecoverAllAndCancelButtonsVisible(true)
		, _IsRecoverThroughButtonsVisible(true) {}

		/** An introduction text to put the user in context. */
		SLATE_ARGUMENT(FText, IntroductionText)

		/** The windows hosting this widget. */
		SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)

		/** If bound, invoked iteratively to populate the activity list. */
		SLATE_ARGUMENT(FFetchActivitiesFunc, OnFetchActivities)

		/** If bound, invoked to map an activity to a client info. */
		SLATE_ARGUMENT(FGetActivityClientInfoFunc, OnMapActivityToClient)

		/** Invoked when the user clicks the 'recover' button. */
		SLATE_ARGUMENT(TFunction<bool(TSharedPtr<FConcertClientSessionActivity>)>, OnRestore)

		/** Invoked when the user clicks the 'cancel' button. */
		SLATE_ARGUMENT(TFunction<void()>, OnCancel)

		/** Show/hide the column displaying the avatar color of the client who performed the activity. */
		SLATE_ARGUMENT(EVisibility, ClientAvatarColorColumnVisibility)

		/** Show/hide the column showing the display name of the client who performed the activity. */
		SLATE_ARGUMENT(EVisibility, ClientNameColumnVisibility)

		/** Show/hide the column displaying the operation represented by the activity. */
		SLATE_ARGUMENT(EVisibility, OperationColumnVisibility)

		/** Show/hide the column displaying affected package. */
		SLATE_ARGUMENT(EVisibility, PackageColumnVisibility)

		/** Show/hide the details area widget. */
		SLATE_ARGUMENT(EVisibility, DetailsAreaVisibility)

		/** Show/hide the check box in the 'View Options' to filter connection activities (join/leave session). */
		SLATE_ARGUMENT(bool, IsConnectionActivityFilteringEnabled)

		/** Show/hide the check box in the 'View Options' to filter lock activities (lock/unlock assets). */
		SLATE_ARGUMENT(bool, IsLockActivityFilteringEnabled)

		/** Show/hide the check box in the 'View Options' to filter package activities (create/delete/save/rename). */
		SLATE_ARGUMENT(bool, IsPackageActivityFilteringEnabled)

		/** Show/hide the check box in the 'View Options' to filter transaction activities. */
		SLATE_ARGUMENT(bool, IsTransactionActivityFilteringEnabled)

		/** Show/hide the check box in the 'View Options' to filter unrecoverable activities. */
		SLATE_ARGUMENT(bool, IsIgnoredActivityFilteringEnabled)

		/** Show/hide the recover all and cancel buttons (so that if this widget is embedded in another one, the buttons can be replaced and put in a more appropriate location). */
		SLATE_ATTRIBUTE(bool, AreRecoverAllAndCancelButtonsVisible)

		/** Show/hide the recover though button. */
		SLATE_ATTRIBUTE(bool, IsRecoverThroughButtonsVisible)

		/** Show/hide a message overlay above the activities list explaining why no activities are displayed. */
		SLATE_ATTRIBUTE(FText, NoActivitiesReasonText)
	SLATE_END_ARGS();

	/**
	 * Construct the recovery widget.
	 * @param InArgs The widgets arguments and attributes.
	 */
	void Construct(const FArguments& InArgs);

	/** Returns the activity, selected by the user, through which the session should be (or was) recovered or null to prevent recovery. */
	TSharedPtr<FConcertClientSessionActivity> GetRecoverThroughItem() { return RecoveryThroughItem; }

	/** Removes all activities from the view and reset the activity stream. */
	void Reset();

	/** Returns the total number of activities currently stored (no filter applied). */
	int32 GetTotalActivityNum() const;

	/** Returns the tooltip shown for the 'Recovery All' button. */
	FText GetRecoverAllButtonTooltip() const;

	/** Returns the most recent activity available, ignoring the current filter. */
	TSharedPtr<FConcertClientSessionActivity> GetMostRecentActivity() const;

private:
	void OnSearchTextChanged(const FText& InFilterText);
	void OnSearchTextCommitted(const FText& InFilterText, ETextCommit::Type CommitType);
	FText HighlightSearchText() const;

	FReply OnCancelRecoveryClicked();
	FReply OnRecoverAllClicked();

	EVisibility GetRecoverThroughButtonVisibility(TSharedPtr<FConcertClientSessionActivity> Activity);
	FText GetRecoverThroughButtonTooltip() const;
	TSharedPtr<SWidget> MakeRecoverThroughWidget(TWeakPtr<FConcertClientSessionActivity>, const FName&);
	void RecoverThrough(TSharedPtr<FConcertClientSessionActivity> Item);

	/** Close the windows hosting this recovery widget. */
	void DismissWindow();

private:
	/** Display the session activities. */
	TSharedPtr<SConcertSessionActivities> ActivityView;

	/** Controls the various display options of the view. */
	TSharedPtr<FConcertSessionActivitiesOptions> ActivityViewOptions;

	/** The activity selected when the user click 'Recover' or 'Recover Through' buttons. */
	TSharedPtr<FConcertClientSessionActivity> RecoveryThroughItem;

	/** The parent window hosting this widget. */
	TWeakPtr<SWindow> ParentWindow;

	/** The widget used to enter the text to search. */
	TSharedPtr<SSearchBox> SearchBox;

	/** The search text entered in the search box. */
	FText SearchText;

	/** The text displayed at the top to summarize the purpose of the window. */
	FText IntroductionText;

	/** Externally control the 'recovery through' button visibility. The internal logic to show it is run first, if it is still visible, this is evaluated.*/
	TAttribute<bool> IsRecoverThroughButtonVisible;

	/** The function invoked when the user clicks the restore button. Might not be bound. */
	TFunction<bool(TSharedPtr<FConcertClientSessionActivity>)> OnRestoreFn;

	/** The function invoked when the user clicks the cancel button. Might not be bound. */
	TFunction<void()> OnCancelFn;
};
