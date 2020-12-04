// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "TraceServices/Model/TimingProfiler.h"

// Insights
#include "Insights/ITimingViewExtender.h"
#include "Insights/ViewModels/TimingEventSearch.h" // for TTimingEventSearchCache
#include "Insights/ViewModels/TimingEventsTrack.h"

class FThreadTrackEvent;
class FTimingEventSearchParameters;
class FGpuTimingTrack;
class FCpuTimingTrack;
class STimingView;
struct FSlateBrush;

////////////////////////////////////////////////////////////////////////////////////////////////////

class FThreadTimingSharedState : public Insights::ITimingViewExtender, public TSharedFromThis<FThreadTimingSharedState>
{
private:
	struct FThreadGroup
	{
		const TCHAR* Name; /**< The thread group name; pointer to string owned by ThreadProvider. */
		bool bIsVisible;  /**< Toggle to show/hide all thread timelines associated with this group at once. Used also as default for new thread timelines. */
		uint32 NumTimelines; /**< Number of thread timelines associated with this group. */
		int32 Order; //**< Order index used for sorting. Inherited from last thread timeline associated with this group. **/

		int32 GetOrder() const { return Order; }
	};

public:
	explicit FThreadTimingSharedState(STimingView* InTimingView) : TimingView(InTimingView) {}
	virtual ~FThreadTimingSharedState() = default;

	TSharedPtr<FGpuTimingTrack> GetGpuTrack() { return GpuTrack; }
	TSharedPtr<FCpuTimingTrack> GetCpuTrack(uint32 InThreadId);

	bool IsGpuTrackVisible() const;
	bool IsCpuTrackVisible(uint32 InThreadId) const;

	void GetVisibleCpuThreads(TSet<uint32>& OutSet) const;

	//////////////////////////////////////////////////
	// ITimingViewExtender interface

	virtual void OnBeginSession(Insights::ITimingViewSession& InSession) override;
	virtual void OnEndSession(Insights::ITimingViewSession& InSession) override;
	virtual void Tick(Insights::ITimingViewSession& InSession, const Trace::IAnalysisSession& InAnalysisSession) override;
	virtual void ExtendFilterMenu(Insights::ITimingViewSession& InSession, FMenuBuilder& InMenuBuilder) override;

	//////////////////////////////////////////////////

	bool IsAllGpuTracksToggleOn() const { return bShowHideAllGpuTracks; }
	void SetAllGpuTracksToggle(bool bOnOff);
	void ShowAllGpuTracks() { SetAllGpuTracksToggle(true); }
	void HideAllGpuTracks() { SetAllGpuTracksToggle(false); }
	void ShowHideAllGpuTracks() { SetAllGpuTracksToggle(!IsAllGpuTracksToggleOn()); }

	bool IsAllCpuTracksToggleOn() const { return bShowHideAllCpuTracks; }
	void SetAllCpuTracksToggle(bool bOnOff);
	void ShowAllCpuTracks() { SetAllCpuTracksToggle(true); }
	void HideAllCpuTracks() { SetAllCpuTracksToggle(false); }
	void ShowHideAllCpuTracks() { SetAllCpuTracksToggle(!IsAllCpuTracksToggleOn()); }

private:
	void CreateThreadGroupsMenu(FMenuBuilder& MenuBuilder);

	bool ToggleTrackVisibilityByGroup_IsChecked(const TCHAR* InGroupName) const;
	void ToggleTrackVisibilityByGroup_Execute(const TCHAR* InGroupName);

private:
	STimingView* TimingView;

	bool bShowHideAllGpuTracks;
	bool bShowHideAllCpuTracks;

	TSharedPtr<FGpuTimingTrack> GpuTrack;

	/** Maps thread id to track pointer. */
	TMap<uint32, TSharedPtr<FCpuTimingTrack>> CpuTracks;

	/** Maps thread group name to thread group info. */
	TMap<const TCHAR*, FThreadGroup> ThreadGroups;

	uint64 TimingProfilerTimelineCount;
	uint64 LoadTimeProfilerTimelineCount;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FThreadTimingTrack : public FTimingEventsTrack
{
	INSIGHTS_DECLARE_RTTI(FThreadTimingTrack, FTimingEventsTrack)

public:
	typedef typename Trace::ITimeline<Trace::FTimingProfilerEvent>::FTimelineEventInfo TimelineEventInfo;

	explicit FThreadTimingTrack(FThreadTimingSharedState& InSharedState, const FString& InName, const TCHAR* InGroupName, uint32 InTimelineIndex, uint32 InThreadId)
		: FTimingEventsTrack(InName)
		, SharedState(InSharedState)
		, GroupName(InGroupName)
		, TimelineIndex(InTimelineIndex)
		, ThreadId(InThreadId)
	{
	}

	virtual ~FThreadTimingTrack() {}

	const TCHAR* GetGroupName() const { return GroupName; };

	uint32 GetTimelineIndex() const { return TimelineIndex; }
	//void SetTimelineIndex(uint32 InTimelineIndex) { TimelineIndex = InTimelineIndex; }

	uint32 GetThreadId() const { return ThreadId; }
	//void SetThreadId(uint32 InThreadId) { ThreadId = InThreadId; }

	virtual void BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context) override;
	virtual void BuildFilteredDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context) override;

	virtual void PostDraw(const ITimingTrackDrawContext& Context) const override;

	virtual void InitTooltip(FTooltipDrawState& InOutTooltip, const ITimingEvent& InTooltipEvent) const override;

	virtual const TSharedPtr<const ITimingEvent> GetEvent(float InPosX, float InPosY, const FTimingTrackViewport& Viewport) const override;
	virtual const TSharedPtr<const ITimingEvent> SearchEvent(const FTimingEventSearchParameters& InSearchParameters) const override;

	virtual void UpdateEventStats(ITimingEvent& InOutEvent) const override;
	virtual void OnEventSelected(const ITimingEvent& InSelectedEvent) const override;
	virtual void OnClipboardCopyEvent(const ITimingEvent& InSelectedEvent) const override;
	virtual void BuildContextMenu(FMenuBuilder& MenuBuilder) override;

private:
	void DrawSelectedEventInfo(const FThreadTrackEvent& SelectedEvent, const FTimingTrackViewport& Viewport, const FDrawContext& DrawContext, const FSlateBrush* WhiteBrush, const FSlateFontInfo& Font) const;

	bool FindTimingProfilerEvent(const FThreadTrackEvent& InTimingEvent, TFunctionRef<void(double, double, uint32, const Trace::FTimingProfilerEvent&)> InFoundPredicate) const;
	bool FindTimingProfilerEvent(const FTimingEventSearchParameters& InParameters, TFunctionRef<void(double, double, uint32, const Trace::FTimingProfilerEvent&)> InFoundPredicate) const;

	void GetParentAndRoot(const FThreadTrackEvent& TimingEvent,
						  TSharedPtr<FThreadTrackEvent>& OutParentTimingEvent,
						  TSharedPtr<FThreadTrackEvent>& OutRootTimingEvent) const;

	static void CreateFThreadTrackEventFromInfo(const TimelineEventInfo& InEventInfo, const TSharedRef<const FBaseTimingTrack> InTrack, int32 InDepth, TSharedPtr<FThreadTrackEvent> &OutTimingEvent);
	static bool TimerIndexToTimerId(uint32 InTimerIndex, uint32 & OutTimerId);

private:
	FThreadTimingSharedState& SharedState;

	const TCHAR* GroupName;
	uint32 TimelineIndex;
	uint32 ThreadId;

	// Search cache
	mutable TTimingEventSearchCache<Trace::FTimingProfilerEvent> SearchCache;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FCpuTimingTrack : public FThreadTimingTrack
{
public:
	explicit FCpuTimingTrack(FThreadTimingSharedState& InSharedState, const FString& InName, const TCHAR* InGroupName, uint32 InTimelineIndex, uint32 InThreadId)
		: FThreadTimingTrack(InSharedState, InName, InGroupName, InTimelineIndex, InThreadId)
	{
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FGpuTimingTrack : public FThreadTimingTrack
{
public:
	explicit FGpuTimingTrack(FThreadTimingSharedState& InSharedState, const FString& InName, const TCHAR* InGroupName, uint32 InTimelineIndex, uint32 InThreadId)
		: FThreadTimingTrack(InSharedState, InName, InGroupName, InTimelineIndex, InThreadId)
	{
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////
