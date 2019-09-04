// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MagicLeapHandTracking.h"
#include "IMagicLeapHandTrackingPlugin.h"
#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "LiveLinkSourceFactory.h"
#include "ILiveLinkClient.h"
#include "IMagicLeapPlugin.h"
#include "Misc/App.h"
#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"

#define LOCTEXT_NAMESPACE "MagicLeapHandTracking"

void FMagicLeapHandTracking::ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid)
{
	LiveLinkClient = InClient;
	LiveLinkSourceGuid = InSourceGuid;
	bNewLiveLinkClient = true;
}

bool FMagicLeapHandTracking::IsSourceStillValid() const
{
	return LiveLinkClient != nullptr;
}

bool FMagicLeapHandTracking::RequestSourceShutdown()
{
	LiveLinkClient = nullptr;
	LiveLinkSourceGuid.Invalidate();
	return true;
}

FText FMagicLeapHandTracking::GetSourceMachineName() const
{
	return FText().FromString(FPlatformProcess::ComputerName());
}

FText FMagicLeapHandTracking::GetSourceStatus() const
{
	return LOCTEXT("MagicLeapHandTrackingLiveLinkStatus", "Active");
}

FText FMagicLeapHandTracking::GetSourceType() const
{
	return LOCTEXT("MagicLeapHandTrackingLiveLinkSourceType", "MagicLeap Hand Tracking");
}

#define MLHTBONE(keyenum, name, parent) BoneKeypoints.Add(keyenum); StaticData.BoneNames.Add(name); StaticData.BoneParents.Add(parent);
void FMagicLeapHandTracking::SetupLiveLinkData(FLiveLinkSkeletonStaticData& StaticData)
{
	StaticData.BoneNames.Reserve(EHandTrackingKeypointCount);

	// Array of bone indices to parent bone index
	StaticData.BoneParents.Reserve(EHandTrackingKeypointCount);

	BoneKeypoints.Reserve(EHandTrackingKeypointCount);

	// Only one can be the root (-1 parent)!

	MLHTBONE(EHandTrackingKeypoint::Thumb_Tip, ThumbTip_Name,			1);
	MLHTBONE(EHandTrackingKeypoint::Thumb_IP,	ThumbIP_Name,			2);
	MLHTBONE(EHandTrackingKeypoint::Thumb_MCP, ThumbMCP_Name,			3);
	MLHTBONE(EHandTrackingKeypoint::Thumb_CMC, ThumbCMC_Name,			23); //3

	MLHTBONE(EHandTrackingKeypoint::Index_Tip, IndexFingerTip_Name,		5);
	MLHTBONE(EHandTrackingKeypoint::Index_DIP, IndexFingerDIP_Name,		6);
	MLHTBONE(EHandTrackingKeypoint::Index_PIP, IndexFingerPIP_Name,		7);
	MLHTBONE(EHandTrackingKeypoint::Index_MCP, IndexFingerMCP_Name,		23); //7

	MLHTBONE(EHandTrackingKeypoint::Middle_Tip, MiddleFingerTip_Name,	9);
	MLHTBONE(EHandTrackingKeypoint::Middle_DIP, MiddleFingerDIP_Name,	10);
	MLHTBONE(EHandTrackingKeypoint::Middle_PIP, MiddleFingerPIP_Name,	11);
	MLHTBONE(EHandTrackingKeypoint::Middle_MCP, MiddleFingerMCP_Name,	23); //11

	MLHTBONE(EHandTrackingKeypoint::Ring_Tip, RingFingerTip_Name,		13);
	MLHTBONE(EHandTrackingKeypoint::Ring_DIP, RingFingerDIP_Name,		14);
	MLHTBONE(EHandTrackingKeypoint::Ring_PIP, RingFingerPIP_Name,		15);
	MLHTBONE(EHandTrackingKeypoint::Ring_MCP, RingFingerMCP_Name,		23); //15

	MLHTBONE(EHandTrackingKeypoint::Pinky_Tip, PinkyFingerTip_Name,		17);
	MLHTBONE(EHandTrackingKeypoint::Pinky_DIP, PinkyFingerDIP_Name,		18);
	MLHTBONE(EHandTrackingKeypoint::Pinky_PIP, PinkyFingerPIP_Name,		19);
	MLHTBONE(EHandTrackingKeypoint::Pinky_MCP, PinkyFingerMCP_Name,		23); //19

	MLHTBONE(EHandTrackingKeypoint::Wrist_Center,	WristCenter_Name,	23);
	MLHTBONE(EHandTrackingKeypoint::Wrist_Ulnar,	WristUlnar_Name,	23);
	MLHTBONE(EHandTrackingKeypoint::Wrist_Radial,	WristRadial_Name,	23);

	MLHTBONE(EHandTrackingKeypoint::Hand_Center, HandCenter_Name,		-1); //23 //Root
}
#undef MLHTBONE

void FMagicLeapHandTracking::UpdateLiveLinkTransforms(TArray<FTransform>& OutTransforms, const FMagicLeapHandTracking::FHandState& HandState)
{
	// Live link transforms need to be in the hierarchical skeleton, so each in the space of its parent.
	// The hand tracking transforms are in world space.
	// Sadly hand tracking transforms can be unused, and contain only an identity matrix.  So we do some weird stuff to keep those
	// identity in the hierarchical skeleton.

	if (EHandTrackingKeypointCount != BoneKeypoints.Num())
	{
		return;
	}

	for (int32 i = 0; i < EHandTrackingKeypointCount; ++i)
	{
		const FTransformRecord& TransformRecord = HandState.GetTransform(BoneKeypoints[i]);
		bool bWritten = TransformRecord.bWritten;
		if (bWritten)
		{
			int32 ParentIndex = BoneParents[i];
			if (ParentIndex < 0)
			{
				// We are at the root, so use it.  (even if it is identity)
				OutTransforms[i] = TransformRecord.Transform;
			}
			else
			{
				const FTransformRecord* PossibleParentTransformRecord = &(HandState.GetTransform(BoneKeypoints[ParentIndex]));
				while (!PossibleParentTransformRecord->bWritten && ParentIndex >= 0)
				{
					PossibleParentTransformRecord = &(HandState.GetTransform(BoneKeypoints[ParentIndex]));
					ParentIndex = BoneParents[ParentIndex];
				}
				if (PossibleParentTransformRecord->bWritten)
				{
					// We found a parent with a transform, so we will be relative to that.
					// Any intermediates are identity.
					OutTransforms[i] = TransformRecord.Transform * PossibleParentTransformRecord->Transform.Inverse();
				}
				else
				{
					check(ParentIndex < 0);
					// We are at the root, but it is unwritten, so go ahead and use the original transform.
					// Everything above it was unwritten, and identity.
					OutTransforms[i] = TransformRecord.Transform;
				}
			}
		}
		else
		{
			// if this transform is not written leave it as is.
		}
	}
}

void FMagicLeapHandTracking::UpdateLiveLink()
{
	check(IsInGameThread());

	if (LiveLinkClient && bIsHandTrackingStateValid && IMagicLeapPlugin::Get().IsMagicLeapHMDValid())
	{
		FLiveLinkSubjectKey LeftKey = FLiveLinkSubjectKey(LiveLinkSourceGuid, LiveLinkLeftHandTrackingSubjectName);
		FLiveLinkSubjectKey RightKey = FLiveLinkSubjectKey(LiveLinkSourceGuid, LiveLinkRightHandTrackingSubjectName);

		if (bNewLiveLinkClient)
		{
			LiveLinkClient->RemoveSubject_AnyThread(LeftKey);
			LiveLinkClient->RemoveSubject_AnyThread(RightKey);

			FLiveLinkStaticDataStruct SkeletalDataLeft(FLiveLinkSkeletonStaticData::StaticStruct());
			FLiveLinkSkeletonStaticData* SkeletonDataLeftPtr = SkeletalDataLeft.Cast<FLiveLinkSkeletonStaticData>();
			SetupLiveLinkData(*SkeletonDataLeftPtr);

			FLiveLinkStaticDataStruct SkeletalDataRight;
			SkeletalDataRight.InitializeWith(SkeletalDataLeft);

			// Initialize frame data
			{
				LiveLinkLeftFrame.InitializeWith(FLiveLinkAnimationFrameData::StaticStruct(), nullptr);
				LiveLinkRightFrame.InitializeWith(FLiveLinkAnimationFrameData::StaticStruct(), nullptr);
				FLiveLinkAnimationFrameData* LiveLinkLeftFramePtr = LiveLinkLeftFrame.Cast<FLiveLinkAnimationFrameData>();
				FLiveLinkAnimationFrameData* LiveLinkRightFramePtr = LiveLinkRightFrame.Cast<FLiveLinkAnimationFrameData>();
				LiveLinkLeftFramePtr->Transforms.Reserve(EHandTrackingKeypointCount);
				LiveLinkRightFramePtr->Transforms.Reserve(EHandTrackingKeypointCount);
				for (size_t i = 0; i < EHandTrackingKeypointCount; ++i)
				{
					LiveLinkRightFramePtr->Transforms.Add(FTransform::Identity);
					LiveLinkRightFramePtr->Transforms.Add(FTransform::Identity);
				}
			}

			LiveLinkClient->PushSubjectStaticData_AnyThread(LeftKey, ULiveLinkAnimationRole::StaticClass(), MoveTemp(SkeletalDataLeft));
			LiveLinkClient->PushSubjectStaticData_AnyThread(RightKey, ULiveLinkAnimationRole::StaticClass(), MoveTemp(SkeletalDataRight));
			bNewLiveLinkClient = false;
		}

		FLiveLinkAnimationFrameData* LiveLinkLeftFramePtr = LiveLinkLeftFrame.Cast<FLiveLinkAnimationFrameData>();
		FLiveLinkAnimationFrameData* LiveLinkRightFramePtr = LiveLinkRightFrame.Cast<FLiveLinkAnimationFrameData>();

		LiveLinkLeftFramePtr->WorldTime = LiveLinkRightFramePtr->WorldTime = FPlatformTime::Seconds();
		LiveLinkLeftFramePtr->MetaData.SceneTime = LiveLinkRightFramePtr->MetaData.SceneTime = FQualifiedFrameTime(FApp::GetTimecode(), FApp::GetTimecodeFrameRate());

		// Update the transforms for each subject from tracking data
		UpdateLiveLinkTransforms(LiveLinkLeftFramePtr->Transforms, LeftHand);
		UpdateLiveLinkTransforms(LiveLinkRightFramePtr->Transforms, RightHand);

		// Copy the data locally and share it with the LiveLink client
		FLiveLinkFrameDataStruct NewLiveLinkLeftFrame;
		FLiveLinkFrameDataStruct NewLiveLinkRightFrame;
		NewLiveLinkLeftFrame.InitializeWith(LiveLinkLeftFrame);
		NewLiveLinkRightFrame.InitializeWith(LiveLinkRightFrame);
		LiveLinkClient->PushSubjectFrameData_AnyThread(LeftKey, MoveTemp(LiveLinkLeftFrame));
		LiveLinkClient->PushSubjectFrameData_AnyThread(RightKey, MoveTemp(NewLiveLinkRightFrame));
	}
}

#undef LOCTEXT_NAMESPACE
