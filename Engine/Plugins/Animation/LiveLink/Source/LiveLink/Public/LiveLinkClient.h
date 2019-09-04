// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ILiveLinkClient.h"

#include "Algo/Transform.h"
#include "HAL/CriticalSection.h"
#include "ILiveLinkSource.h"
#include "LiveLinkSourceSettings.h"
#include "LiveLinkRefSkeleton.h"
#include "LiveLinkTypes.h"
#include "LiveLinkVirtualSubject.h"
#include "Tickable.h"
#include "UObject/GCObject.h"

class ULiveLinkPreset;
class ULiveLinkSourceSettings;
class ULiveLinkSubjectBase;
class FLiveLinkSourceCollection;


// Live Link Log Category
DECLARE_LOG_CATEGORY_EXTERN(LogLiveLink, Log, All);

DECLARE_STATS_GROUP(TEXT("Live Link"), STATGROUP_LiveLink, STATCAT_Advanced);

struct FLiveLinkSubjectTimeSyncData
{
	bool bIsValid = false;
	FFrameTime OldestSampleTime;
	FFrameTime NewestSampleTime;
	FFrameRate SampleFrameRate;
};

PRAGMA_DISABLE_DEPRECATION_WARNINGS
struct FLiveLinkSkeletonStaticData;

class FLiveLinkClient_Base_DEPRECATED : public ILiveLinkClient
{
public:
	//~ Begin ILiveLinkClient implementation
	virtual void PushSubjectSkeleton(FGuid SourceGuid, FName SubjectName, const FLiveLinkRefSkeleton& RefSkeleton) override;
	virtual void PushSubjectData(FGuid SourceGuid, FName SubjectName, const FLiveLinkFrameData& FrameData) override;
	virtual void ClearSubject(FName SubjectName) override;
	virtual void GetSubjectNames(TArray<FName>& SubjectNames) override;
	virtual const FLiveLinkSubjectFrame* GetSubjectData(FName InSubjectName) override;
	virtual const FLiveLinkSubjectFrame* GetSubjectDataAtWorldTime(FName InSubjectName, double InWorldTime) override;
	virtual const FLiveLinkSubjectFrame* GetSubjectDataAtSceneTime(FName InSubjectName, const FTimecode& InSceneTime) override;
	virtual const TArray<FLiveLinkFrame>* GetSubjectRawFrames(FName SubjectName) override;
	virtual void ClearSubjectsFrames(FName SubjectName) override;
	virtual void ClearAllSubjectsFrames() override;
	virtual void AddSourceToSubjectWhiteList(FName SubjectName, FGuid SourceGuid) override;
	virtual void RemoveSourceFromSubjectWhiteList(FName SubjectName, FGuid SourceGuid) override;
	//~ End ILiveLinkClient implementation

protected:
	virtual void AquireLock_Deprecation() = 0;
	virtual void ReleaseLock_Deprecation() = 0;
	virtual void ClearFrames_Deprecation(const FLiveLinkSubjectKey& SubjectKey) = 0;
	virtual FLiveLinkSkeletonStaticData* GetSubjectAnimationStaticData_Deprecation(const FLiveLinkSubjectKey& SubjectKey) = 0;
};
PRAGMA_ENABLE_DEPRECATION_WARNINGS


class LIVELINK_API FLiveLinkClient : public FLiveLinkClient_Base_DEPRECATED
{
public:
	FLiveLinkClient();
	virtual ~FLiveLinkClient();

	//~ Begin ILiveLinkClient implementation
	virtual FGuid AddSource(TSharedPtr<ILiveLinkSource> Source) override;
	virtual bool CreateSource(const FLiveLinkSourcePreset& SourcePreset) override;
	virtual void RemoveSource(TSharedPtr<ILiveLinkSource> Source) override;
	virtual bool HasSourceBeenAdded(TSharedPtr<ILiveLinkSource> Source) const override;
	virtual TArray<FGuid> GetSources() const override;
	virtual FLiveLinkSourcePreset GetSourcePreset(FGuid SourceGuid, UObject* DuplicatedObjectOuter) const override;
	virtual FText GetSourceType(FGuid EntryGuid) const override;

	virtual void PushSubjectStaticData_AnyThread(const FLiveLinkSubjectKey& SubjectKey, TSubclassOf<ULiveLinkRole> Role, FLiveLinkStaticDataStruct&& StaticData) override;
	virtual void PushSubjectFrameData_AnyThread(const FLiveLinkSubjectKey& SubjectKey, FLiveLinkFrameDataStruct&& FrameData) override;

	virtual bool CreateSubject(const FLiveLinkSubjectPreset& SubjectPreset) override;
	virtual void RemoveSubject_AnyThread(const FLiveLinkSubjectKey& SubjectKey) override;
	virtual void ClearSubjectsFrames_AnyThread(FLiveLinkSubjectName SubjectName) override;
	virtual void ClearSubjectsFrames_AnyThread(const FLiveLinkSubjectKey& InSubjectKey) override;
	virtual void ClearAllSubjectsFrames_AnyThread() override;

	virtual FLiveLinkSubjectPreset GetSubjectPreset(const FLiveLinkSubjectKey& SubjectKey, UObject* DuplicatedObjectOuter) const override;
	virtual TArray<FLiveLinkSubjectKey> GetSubjects(bool bIncludeDisabledSubject, bool bIncludeVirtualSubject) const override;

	virtual bool IsSubjectValid(const FLiveLinkSubjectKey& SubjectKey) const override;
	virtual bool IsSubjectValid(FLiveLinkSubjectName SubjectName) const override;
	virtual bool IsSubjectEnabled(const FLiveLinkSubjectKey& SubjectKey, bool bUseSnapshot) const override;
	virtual bool IsSubjectEnabled(FLiveLinkSubjectName SubjectName) const override;
	virtual void SetSubjectEnabled(const FLiveLinkSubjectKey& SubjectKey, bool bEnabled) override;
	virtual bool IsSubjectTimeSynchronized(const FLiveLinkSubjectKey& SubjectKey) const override;
	virtual bool IsSubjectTimeSynchronized(FLiveLinkSubjectName SubjectName) const override;

	virtual TSubclassOf<ULiveLinkRole> GetSubjectRole(const FLiveLinkSubjectKey& SubjectKey) const override;
	virtual TSubclassOf<ULiveLinkRole> GetSubjectRole(FLiveLinkSubjectName SubjectName) const override;
	virtual TArray<FLiveLinkSubjectKey> GetSubjectsSupportingRole(TSubclassOf<ULiveLinkRole> SupportedRole, bool bIncludeDisabledSubject, bool bIncludeVirtualSubject) const override;
	virtual bool DoesSubjectSupportsRole(const FLiveLinkSubjectKey& SubjectKey, TSubclassOf<ULiveLinkRole> SupportedRole) const override;

	virtual bool EvaluateFrame_AnyThread(FLiveLinkSubjectName SubjectName, TSubclassOf<ULiveLinkRole> Role, FLiveLinkSubjectFrameData& OutFrame) override;
	virtual bool EvaluateFrameAtWorldTime_AnyThread(FLiveLinkSubjectName SubjectName, double WorldTime, TSubclassOf<ULiveLinkRole> DesiredRole, FLiveLinkSubjectFrameData& OutFrame) override;
	virtual bool EvaluateFrameAtSceneTime_AnyThread(FLiveLinkSubjectName SubjectName, const FTimecode& SceneTime, TSubclassOf<ULiveLinkRole> DesiredRole, FLiveLinkSubjectFrameData& OutFrame) override;

	virtual FSimpleMulticastDelegate& OnLiveLinkSourcesChanged() override;
	virtual FSimpleMulticastDelegate& OnLiveLinkSubjectsChanged() override;
	virtual FOnLiveLinkSourceChangedDelegate& OnLiveLinkSourceAdded() override;
	virtual FOnLiveLinkSourceChangedDelegate& OnLiveLinkSourceRemoved() override;
	virtual FOnLiveLinkSubjectChangedDelegate& OnLiveLinkSubjectAdded() override;
	virtual FOnLiveLinkSubjectChangedDelegate& OnLiveLinkSubjectRemoved() override;

	virtual bool RegisterForSubjectFrames(FLiveLinkSubjectName SubjectName, const FOnLiveLinkSubjectStaticDataReceived::FDelegate& OnStaticDataReceived, const FOnLiveLinkSubjectFrameDataReceived::FDelegate& OnFrameDataReceived, FDelegateHandle& OutStaticDataReceivedHandle, FDelegateHandle& OutFrameDataReceivedHandle, TSubclassOf<ULiveLinkRole>& OutSubjectRole, FLiveLinkStaticDataStruct* OutStaticData = nullptr) override;
	virtual void UnregisterSubjectFramesHandle(FLiveLinkSubjectName InSubjectName, FDelegateHandle InStaticDataReceivedHandle, FDelegateHandle InFrameDataReceivedHandle) override;
	//~ End ILiveLinkClient implementation

	/** The tick callback to update the pending work and clear the subject's snapshot*/
	void Tick();

	/** Remove the specified source from the live link client */
	void RemoveSource(FGuid InEntryGuid);

	/** Remove all sources from the live link client */
	void RemoveAllSources();

	/** Add a new virtual subject to the client */
	void AddVirtualSubject(FLiveLinkSubjectName VirtualSubjectName, TSubclassOf<ULiveLinkVirtualSubject> VirtualSubjectClass);

	/** Is the supplied subject virtual */
	bool IsVirtualSubject(const FLiveLinkSubjectKey& Subject) const;

	/** Callback when property changed for one of the source settings */
	void OnPropertyChanged(FGuid EntryGuid, const FPropertyChangedEvent& PropertyChangedEvent);

	TArray<FGuid> GetDisplayableSources() const;

	UObject* GetSubjectSettings(const FLiveLinkSubjectKey& SubjectKey) const;

	ULiveLinkSourceSettings* GetSourceSettings(FGuid InEntryGuid) const;

	FLiveLinkSubjectTimeSyncData GetTimeSyncData(FLiveLinkSubjectName SubjectName);

	FText GetSourceMachineName(FGuid EntryGuid) const;
	FText GetSourceStatus(FGuid EntryGuid) const;
	UE_DEPRECATED(4.23, "FLiveLinkClient::GetSourceTypeForEntry is deprecated. Please use GetSourceType instead!")
	FText GetSourceTypeForEntry(FGuid EntryGuid) const { return GetSourceType(EntryGuid); }
	UE_DEPRECATED(4.23, "FLiveLinkClient::GetMachineNameForEntry is deprecated. Please use GetSourceMachineName instead!")
	FText GetMachineNameForEntry(FGuid EntryGuid) const { return GetSourceMachineName(EntryGuid); }
	UE_DEPRECATED(4.23, "FLiveLinkClient::GetEntryStatusForEntry is deprecated. Please use GetSourceStatus instead!")
	FText GetEntryStatusForEntry(FGuid EntryGuid) const { return GetSourceStatus(EntryGuid); }
	UE_DEPRECATED(4.23, "FLiveLinkClient::GetSourceEntries is deprecated. Please use ILiveLinkClient::GetSources instead!")
	const TArray<FGuid>& GetSourceEntries() const;
	UE_DEPRECATED(4.23, "FLiveLinkClient::AddVirtualSubject(FName) is deprecated. Please use AddVirtualSubject(FName, TSubClass<ULiveLinkVirtualSubject>) instead!")
	void AddVirtualSubject(FName NewVirtualSubjectName);
	UE_DEPRECATED(4.23, "FLiveLinkClient::OnLiveLinkSourcesChanged is deprecated. Please use OnLiveLinkSourceAdded instead!")
	FDelegateHandle RegisterSourcesChangedHandle(const FSimpleMulticastDelegate::FDelegate& SourcesChanged);
	UE_DEPRECATED(4.23, "FLiveLinkClient::OnLiveLinkSourcesChanged is deprecated. Please use OnLiveLinkSourceAdded instead!")
	void UnregisterSourcesChangedHandle(FDelegateHandle Handle);

protected:
	//~ Begin FLiveLinkClient_Base_DEPRECATED implementation
	virtual void AquireLock_Deprecation() override;
	virtual void ReleaseLock_Deprecation() override;
	virtual void ClearFrames_Deprecation(const FLiveLinkSubjectKey& SubjectKey) override;
	virtual FLiveLinkSkeletonStaticData* GetSubjectAnimationStaticData_Deprecation(const FLiveLinkSubjectKey& SubjectKey) override;
	//~ End FLiveLinkClient_Base_DEPRECATED implementation

private:
	/** Struct that hold the pending static data that will be pushed next tick. */
	struct FPendingSubjectStatic
	{
		FLiveLinkSubjectKey SubjectKey;
		TSubclassOf<ULiveLinkRole> Role;
		FLiveLinkStaticDataStruct StaticData;
	};

	/** Struct that hold the pending frame data that will be pushed next tick. */
	struct FPendingSubjectFrame
	{
		FLiveLinkSubjectKey SubjectKey;
		FLiveLinkFrameDataStruct FrameData;
	};

private:
	/** Remove old sources & subject,  */
	void DoPendingWork();

	/** Update the added sources */
	void UpdateSources();

	/**
	 * Build subject data so that during the rest of the tick it can be read without
	 * thread locking or mem copying
	 */
	void BuildThisTicksSubjectSnapshot();

	/** Registered with each subject and called when it changes */
	void OnSubjectChangedHandler();

	void PushSubjectStaticData_Internal(FPendingSubjectStatic&& SubjectStaticData);
	void PushSubjectFrameData_Internal(FPendingSubjectFrame&& SubjectFrameData);

	/** Remove all sources. */
	void Shutdown();

private:
	/** The current collection used. */
	TUniquePtr<FLiveLinkSourceCollection> Collection;

	/** Pending static info to add to a subject. */
	TArray<FPendingSubjectStatic> SubjectStaticToPush;

	/** Pending frame info to add to a subject. */
	TArray<FPendingSubjectFrame> SubjectFrameToPush;

	/** Key funcs for looking up a set of cached keys by its layout element */
	TMap<FLiveLinkSubjectName, FLiveLinkSubjectKey> EnabledSubjects;

	/** Lock to stop multiple threads accessing the CurrentPreset at the same time */
	mutable FCriticalSection CollectionAccessCriticalSection;

	/** Delegate the preset has changed */
	FSimpleMulticastDelegate OnLiveLinkPresetChanged;

	struct FSubjectFramesReceivedHandles
	{
		FOnLiveLinkSubjectStaticDataReceived OnStaticDataReceived;
		FOnLiveLinkSubjectFrameDataReceived OnFrameDataReceived;
	};

	/** Map of delegates to notify interested parties when the client receives a static or data frame for each subject */
	TMap<FLiveLinkSubjectName, FSubjectFramesReceivedHandles> SubjectFrameReceivedHandles;
};
