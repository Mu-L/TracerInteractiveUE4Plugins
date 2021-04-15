// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AnimSequence.cpp: Skeletal mesh animation functions.
=============================================================================*/ 

#include "Animation/AnimSequence.h"
#include "Misc/MessageDialog.h"
#include "Logging/LogScopedVerbosityOverride.h"
#include "UObject/FrameworkObjectVersion.h"
#include "Serialization/MemoryReader.h"
#include "UObject/UObjectIterator.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/UObjectBase.h"
#include "CoreGlobals.h"
#include "EngineUtils.h"
#include "AnimEncoding.h"
#include "AnimationUtils.h"
#include "BonePose.h"
#include "AnimationRuntime.h"
#include "Animation/AnimCompress.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/BlendSpaceBase.h"
#include "Animation/Rig.h"
#include "Animation/AnimationSettings.h"
#include "Animation/AnimBoneCompressionCodec.h"
#include "Animation/AnimBoneCompressionSettings.h"
#include "Animation/AnimCurveCompressionCodec.h"
#include "Animation/AnimCurveCompressionSettings.h"
#include "EditorFramework/AssetImportData.h"
#include "Logging/TokenizedMessage.h"
#include "Logging/MessageLog.h"
#include "DerivedDataCacheInterface.h"
#include "Interfaces/ITargetPlatform.h"
#include "Animation/AnimCompressionDerivedData.h"
#include "Animation/AnimCompressionDerivedDataPublic.h"
#include "UObject/UObjectThreadContext.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "Animation/AnimStreamable.h"
#include "Modules/ModuleManager.h"
#include "ProfilingDebugging/CookStats.h"
#include "Animation/CustomAttributesRuntime.h"
#include "Stats/StatsHierarchical.h"
#include "Animation/AnimationPoseData.h"

#define USE_SLERP 0
#define LOCTEXT_NAMESPACE "AnimSequence"

DECLARE_CYCLE_STAT(TEXT("AnimSeq GetBonePose"), STAT_AnimSeq_GetBonePose, STATGROUP_Anim);
DECLARE_CYCLE_STAT(TEXT("AnimSeq EvalCurveData"), STAT_AnimSeq_EvalCurveData, STATGROUP_Anim);

#if ENABLE_COOK_STATS
namespace AnimSequenceCookStats
{
	static FCookStats::FDDCResourceUsageStats UsageStats;
	static FCookStatsManager::FAutoRegisterCallback RegisterCookStats([](FCookStatsManager::AddStatFuncRef AddStat)
	{
		UsageStats.LogStats(AddStat, TEXT("AnimSequence.Usage"), TEXT(""));
	});
}
#endif

CSV_DECLARE_CATEGORY_MODULE_EXTERN(ENGINE_API, Animation);

int32 GPerformFrameStripping = 0;
int32 GPerformFrameStrippingOddFramedAnimations = 0;

static const TCHAR* StripFrameCVarName = TEXT("a.StripFramesOnCompression");
static const TCHAR* OddFrameStripStrippingCVarName = TEXT("a.StripOddFramesWhenFrameStripping");

static FAutoConsoleVariableRef CVarFrameStripping(
	StripFrameCVarName,
	GPerformFrameStripping,
	TEXT("1 = Strip every other frame on animations that have an even number of frames. 0 = off"));

static FAutoConsoleVariableRef CVarOddFrameStripping(
	OddFrameStripStrippingCVarName,
	GPerformFrameStrippingOddFramedAnimations,
	TEXT("1 = When frame stripping apply to animations with an odd number of frames too. 0 = only even framed animations"));



#if WITH_EDITOR

template <typename ArrayType>
FGuid GetArrayGuid(TArrayView<const ArrayType> Array)
{
	FSHA1 Sha;
	Sha.Update((uint8*)Array.GetData(), Array.Num() * Array.GetTypeSize());

	Sha.Final();

	uint32 Hash[5];
	Sha.GetHash((uint8*)Hash);
	FGuid Guid(Hash[0] ^ Hash[4], Hash[1], Hash[2], Hash[3]);
	return Guid;
}

void OnCVarsChanged()
{
	if (GIsInitialLoad)
	{
		return; // not initialized
	}

	/*static TArray<UAnimSequence*> SequenceCache;
	static FString OutputMessage;
	
	SequenceCache.Reset();

	for (TObjectIterator<UAnimSequence> It; It; ++It)
	{
		SequenceCache.Add(*It);
	}

	SequenceCache.Sort([](const UAnimSequence& A, const UAnimSequence& B)
	{
		return A.GetFName() > B.GetFName();
	});

	OutputMessage.Reset();

	for (UAnimSequence* Seq : SequenceCache)
	{
		const FCompressedAnimSequence& AnimData = Seq->CompressedData;
		const FUECompressedAnimData& UEAnimData = AnimData.CompressedDataStructure;
		const int32 Additive = Seq->IsValidAdditive() ? 1 : 0;
		OutputMessage += FString::Printf(TEXT("%s - %.2f Fr:%i Add:%i TO:%i SO:%i CBS:%i\n"), *Seq->GetName(), Seq->SequenceLength, Seq->GetRawNumberOfFrames(), Additive, UEAnimData.CompressedTrackOffsets.Num(), UEAnimData.CompressedScaleOffsets.OffsetData.Num(), UEAnimData.CompressedByteStream.Num());
		OutputMessage += FString::Printf(TEXT("\t K:%i (%i : %i : %i)\n"), (int32)UEAnimData.KeyEncodingFormat, (int32)UEAnimData.TranslationCompressionFormat, (int32)UEAnimData.RotationCompressionFormat, (int32)UEAnimData.ScaleCompressionFormat);
		OutputMessage += FString::Printf(TEXT("\t Curve Codec:%s\n"), AnimData.CurveCompressionCodec ? *AnimData.CurveCompressionCodec->GetPathName() : TEXT("nullptr"));
		OutputMessage += FString::Printf(TEXT("\t TrackOff:%s\n"), *GetArrayGuid<int32>(UEAnimData.CompressedTrackOffsets).ToString());
		OutputMessage += FString::Printf(TEXT("\t ScaleOff:%s\n"), *GetArrayGuid<int32>(UEAnimData.CompressedScaleOffsets.OffsetData).ToString());
		OutputMessage += FString::Printf(TEXT("\t BoneByteStream:%s\n"), *GetArrayGuid<uint8>(UEAnimData.CompressedByteStream).ToString());
		OutputMessage += FString::Printf(TEXT("\t CurveByteStream:%s\n"), *GetArrayGuid<uint8>(AnimData.CompressedCurveByteStream).ToString());
	}

	OutputMessage += FString::Printf(TEXT("\n\nTotalAnims: %i"), SequenceCache.Num());
	FPlatformApplicationMisc::ClipboardCopy(*OutputMessage);*/

	static bool bFirstRun = true;

	static bool bCompressionFrameStrip = (GPerformFrameStripping == 1);
	static bool bOddFramedStrip = (GPerformFrameStrippingOddFramedAnimations == 1);

	static TArray<UAnimSequence*> SequenceCache;
	static FString OutputMessage;

	const bool bCurrentFrameStrip = (GPerformFrameStripping == 1);
	const bool bCurrentOddFramedStrip = (GPerformFrameStrippingOddFramedAnimations == 1);

	const bool bFrameStripChanged = bCompressionFrameStrip != bCurrentFrameStrip;
	const bool bOddFrameStripChanged = bOddFramedStrip != bCurrentOddFramedStrip;

	if (bFrameStripChanged || bOddFrameStripChanged)
	{
		bCompressionFrameStrip = bCurrentFrameStrip;
		bOddFramedStrip = bCurrentOddFramedStrip;

		SequenceCache.Reset();

		if (!bFirstRun) // No need to do this on the first run, only subsequent runs as temp anim sequences from compression may still be around
		{
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}
		bFirstRun = false;

		for (TObjectIterator<UAnimSequence> It; It; ++It)
		{
			SequenceCache.Add(*It);
		}

		if (SequenceCache.Num() == 0)
		{
			return; // Nothing to do
		}

		TArray< TPair<int32, UAnimSequence*> > Sizes;
		
		// Rebake/compress the animations
		for (UAnimSequence* Seq : SequenceCache)
		{
			Seq->RequestSyncAnimRecompression();

			Sizes.Emplace(Seq->GetApproxCompressedSize(), Seq);
		}

		Sizes.Sort([](const TPair<int32, UAnimSequence*>& A, const TPair<int32, UAnimSequence*>& B)
		{
			return A.Key > B.Key;
		});

		OutputMessage.Reset();

		const TCHAR* StripMessage = bCompressionFrameStrip ? TEXT("Stripping: On") : TEXT("Stripping: Off");
		const TCHAR* OddMessage = bOddFramedStrip ? TEXT("Odd Frames: On") : TEXT("Odd Frames: Off");

		OutputMessage += FString::Printf(TEXT("%s - %s\n\n"), StripMessage, OddMessage);

		int32 TotalSize = 0;
		int32 NumAnimations = 0;
		for (const TPair<int32, UAnimSequence*>& Pair : Sizes)
		{
			const bool bIsOddFramed = (Pair.Value->GetNumberOfFrames() % 2) == 0;
			if (bIsOddFramed)
			{
				OutputMessage += FString::Printf(TEXT("%s - %.1fK\n"), *Pair.Value->GetPathName(), (float)Pair.Key / 1000.f);
				TotalSize += Pair.Key;
				NumAnimations++;
			}
		}

		OutputMessage += FString::Printf(TEXT("\n\nTotalAnims: %i TotalSize = %.1fK"), NumAnimations, ((float)TotalSize / 1000.f));
		FPlatformApplicationMisc::ClipboardCopy(*OutputMessage);
	}
}

FAutoConsoleVariableSink AnimationCVarSink(FConsoleCommandDelegate::CreateStatic(&OnCVarsChanged));

FString GetAnimSequenceSpecificCacheKeySuffix(const UAnimSequence& Seq, bool bPerformStripping, float CompressionErrorThresholdScale)
{
	//Make up our content key consisting of:
	//	* Global animation compression version
	//  * Whether to strip frames
	//	* Our raw data GUID
	//	* Our skeleton GUID: If our skeleton changes our compressed data may now be stale
	//	* Baked Additive Flag
	//	* Additive ref pose GUID or hardcoded string if not available
	//	* Compression Settings
	//	* Curve compression settings

	bool bIsValidAdditive = Seq.IsValidAdditive();
	char AdditiveType = bIsValidAdditive ? NibbleToTChar(Seq.AdditiveAnimType) : '0';
	char RefType = bIsValidAdditive ? NibbleToTChar(Seq.RefPoseType) : '0';

	FArcToHexString ArcToHexString;

	ArcToHexString.Ar << CompressionErrorThresholdScale;
	ArcToHexString.Ar << bPerformStripping;
	Seq.BoneCompressionSettings->PopulateDDCKey(ArcToHexString.Ar);
	Seq.CurveCompressionSettings->PopulateDDCKey(ArcToHexString.Ar);

	FString Ret = FString::Printf(TEXT("%i_%s%s%s_%c%c%i_%s_%s"),
		Seq.CompressCommandletVersion,
		*Seq.GetRawDataGuid().ToString(),
		*Seq.GetSkeleton()->GetGuid().ToString(),
		*Seq.GetSkeleton()->GetVirtualBoneGuid().ToString(),
		AdditiveType,
		RefType,
		Seq.RefFrameIndex,
		(bIsValidAdditive && Seq.RefPoseSeq) ? *Seq.RefPoseSeq->GetRawDataGuid().ToString() : TEXT("NoAdditiveGuid"),
		*ArcToHexString.MakeString()
	);

	return Ret;
}
#endif

bool CompressRawAnimSequenceTrack(FRawAnimSequenceTrack& RawTrack, int32 NumFrames, FName ErrorName, float MaxPosDiff, float MaxAngleDiff)
{
	bool bRemovedKeys = false;

	// First part is to make sure we have valid input
	bool const bPosTrackIsValid = (RawTrack.PosKeys.Num() == 1 || RawTrack.PosKeys.Num() == NumFrames);
	if (!bPosTrackIsValid)
	{
		UE_LOG(LogAnimation, Warning, TEXT("Found non valid position track for %s, %d frames, instead of %d. Chopping!"), *ErrorName.ToString(), RawTrack.PosKeys.Num(), NumFrames);
		bRemovedKeys = true;
		RawTrack.PosKeys.RemoveAt(1, RawTrack.PosKeys.Num() - 1);
		RawTrack.PosKeys.Shrink();
		check(RawTrack.PosKeys.Num() == 1);
	}

	bool const bRotTrackIsValid = (RawTrack.RotKeys.Num() == 1 || RawTrack.RotKeys.Num() == NumFrames);
	if (!bRotTrackIsValid)
	{
		UE_LOG(LogAnimation, Warning, TEXT("Found non valid rotation track for %s, %d frames, instead of %d. Chopping!"), *ErrorName.ToString(), RawTrack.RotKeys.Num(), NumFrames);
		bRemovedKeys = true;
		RawTrack.RotKeys.RemoveAt(1, RawTrack.RotKeys.Num() - 1);
		RawTrack.RotKeys.Shrink();
		check(RawTrack.RotKeys.Num() == 1);
	}

	// scale keys can be empty, and that is valid 
	bool const bScaleTrackIsValid = (RawTrack.ScaleKeys.Num() == 0 || RawTrack.ScaleKeys.Num() == 1 || RawTrack.ScaleKeys.Num() == NumFrames);
	if (!bScaleTrackIsValid)
	{
		UE_LOG(LogAnimation, Warning, TEXT("Found non valid Scaleation track for %s, %d frames, instead of %d. Chopping!"), *ErrorName.ToString(), RawTrack.ScaleKeys.Num(), NumFrames);
		bRemovedKeys = true;
		RawTrack.ScaleKeys.RemoveAt(1, RawTrack.ScaleKeys.Num() - 1);
		RawTrack.ScaleKeys.Shrink();
		check(RawTrack.ScaleKeys.Num() == 1);
	}

	// Second part is actual compression.

	// Check variation of position keys
	if ((RawTrack.PosKeys.Num() > 1) && (MaxPosDiff >= 0.0f))
	{
		FVector FirstPos = RawTrack.PosKeys[0];
		bool bFramesIdentical = true;
		for (int32 j = 1; j < RawTrack.PosKeys.Num() && bFramesIdentical; j++)
		{
			if ((FirstPos - RawTrack.PosKeys[j]).SizeSquared() > FMath::Square(MaxPosDiff))
			{
				bFramesIdentical = false;
			}
		}

		// If all keys are the same, remove all but first frame
		if (bFramesIdentical)
		{
			bRemovedKeys = true;
			RawTrack.PosKeys.RemoveAt(1, RawTrack.PosKeys.Num() - 1);
			RawTrack.PosKeys.Shrink();
			check(RawTrack.PosKeys.Num() == 1);
		}
	}

	// Check variation of rotational keys
	if ((RawTrack.RotKeys.Num() > 1) && (MaxAngleDiff >= 0.0f))
	{
		FQuat FirstRot = RawTrack.RotKeys[0];
		bool bFramesIdentical = true;
		for (int32 j = 1; j < RawTrack.RotKeys.Num() && bFramesIdentical; j++)
		{
			if (FQuat::Error(FirstRot, RawTrack.RotKeys[j]) > MaxAngleDiff)
			{
				bFramesIdentical = false;
			}
		}

		// If all keys are the same, remove all but first frame
		if (bFramesIdentical)
		{
			bRemovedKeys = true;
			RawTrack.RotKeys.RemoveAt(1, RawTrack.RotKeys.Num() - 1);
			RawTrack.RotKeys.Shrink();
			check(RawTrack.RotKeys.Num() == 1);
		}
	}

	float MaxScaleDiff = 0.0001f;

	// Check variation of Scaleition keys
	if ((RawTrack.ScaleKeys.Num() > 1) && (MaxScaleDiff >= 0.0f))
	{
		FVector FirstScale = RawTrack.ScaleKeys[0];
		bool bFramesIdentical = true;
		for (int32 j = 1; j < RawTrack.ScaleKeys.Num() && bFramesIdentical; j++)
		{
			if ((FirstScale - RawTrack.ScaleKeys[j]).SizeSquared() > FMath::Square(MaxScaleDiff))
			{
				bFramesIdentical = false;
			}
		}

		// If all keys are the same, remove all but first frame
		if (bFramesIdentical)
		{
			bRemovedKeys = true;
			RawTrack.ScaleKeys.RemoveAt(1, RawTrack.ScaleKeys.Num() - 1);
			RawTrack.ScaleKeys.Shrink();
			check(RawTrack.ScaleKeys.Num() == 1);
		}
	}

	return bRemovedKeys;
}

bool StaticCompressRawAnimData(TArray<FRawAnimSequenceTrack>& RawAnimationData, int32 NumFrames, FName ErrorName, float MaxPosDiff, float MaxAngleDiff)
{
	bool bRemovedKeys = false;

#if WITH_EDITORONLY_DATA
	if (ensureMsgf(RawAnimationData.Num() > 0, TEXT("%s is trying to compress while raw animation is missing"), *ErrorName.ToString()))
	{
		// This removes trivial keys, and this has to happen before the removing tracks
		for (int32 TrackIndex = 0; TrackIndex < RawAnimationData.Num(); TrackIndex++)
		{
			bRemovedKeys |= CompressRawAnimSequenceTrack(RawAnimationData[TrackIndex], NumFrames, ErrorName, MaxPosDiff, MaxAngleDiff);
		}

		bool bCompressScaleKeys = false;
		// go through remove keys if not needed
		for (int32 TrackIndex = 0; TrackIndex < RawAnimationData.Num(); TrackIndex++)
		{
			FRawAnimSequenceTrack const& RawData = RawAnimationData[TrackIndex];
			if (RawData.ScaleKeys.Num() > 0)
			{
				// if scale key exists, see if we can just empty it
				if ((RawData.ScaleKeys.Num() > 1) || (RawData.ScaleKeys[0].Equals(FVector(1.f)) == false))
				{
					bCompressScaleKeys = true;
					break;
				}
			}
		}

		// if we don't have scale, we should delete all scale keys
		// if you have one track that has scale, we still should support scale, so compress scale
		if (!bCompressScaleKeys)
		{
			// then remove all scale keys
			for (int32 TrackIndex = 0; TrackIndex < RawAnimationData.Num(); TrackIndex++)
			{
				FRawAnimSequenceTrack& RawData = RawAnimationData[TrackIndex];
				RawData.ScaleKeys.Empty();
			}
		}
	}
#endif
	return bRemovedKeys;
}

bool StaticCompressRawAnimData(TArray<FRawAnimSequenceTrack>& RawAnimationData, int32 NumFrames, FName ErrorName)
{
	const float MaxPosDiff = 0.0001f;
	const float MaxAngleDiff = 0.0003f;
	return StaticCompressRawAnimData(RawAnimationData, NumFrames, ErrorName, MaxPosDiff, MaxAngleDiff);
}

/////////////////////////////////////////////////////
// FRequestAnimCompressionParams
FRequestAnimCompressionParams::FRequestAnimCompressionParams(bool bInAsyncCompression, bool bInAllowAlternateCompressor, bool bInOutput)
	: bAsyncCompression(bInAsyncCompression)
	, CompressContext(MakeShared<FAnimCompressContext>(bInAllowAlternateCompressor, bInOutput))
{
	InitFrameStrippingFromCVar();
}

FRequestAnimCompressionParams::FRequestAnimCompressionParams(bool bInAsyncCompression, TSharedPtr<FAnimCompressContext> InCompressContext)
	: bAsyncCompression(bInAsyncCompression)
	, CompressContext(InCompressContext)
{
	InitFrameStrippingFromCVar();
}

void FRequestAnimCompressionParams::InitFrameStrippingFromCVar()
{
	bPerformFrameStripping = (GPerformFrameStripping == 1);
	bPerformFrameStrippingOnOddNumberedFrames = (GPerformFrameStrippingOddFramedAnimations == 1);
}

void FRequestAnimCompressionParams::InitFrameStrippingFromPlatform(const class ITargetPlatform* TargetPlatform)
{
#if WITH_EDITOR
	bPerformFrameStripping = false;

	if (UDeviceProfile* DeviceProfile = UDeviceProfileManager::Get().FindProfile(TargetPlatform->IniPlatformName()))
	{
		int32 CVarPlatformFrameStrippingValue = 0;
		if (DeviceProfile->GetConsolidatedCVarValue(StripFrameCVarName, CVarPlatformFrameStrippingValue))
		{
			bPerformFrameStripping = CVarPlatformFrameStrippingValue == 1;
		}

		int32 CVarPlatformOddAnimFrameStrippingValue = 0;
		if (DeviceProfile->GetConsolidatedCVarValue(OddFrameStripStrippingCVarName, CVarPlatformOddAnimFrameStrippingValue))
		{
			bPerformFrameStrippingOnOddNumberedFrames = CVarPlatformOddAnimFrameStrippingValue == 1;
		}
	}
#endif
}

/////////////////////////////////////////////////////
// FRawAnimSequenceTrackNativeDeprecated

//@deprecated with VER_REPLACED_LAZY_ARRAY_WITH_UNTYPED_BULK_DATA
struct FRawAnimSequenceTrackNativeDeprecated
{
	TArray<FVector> PosKeys;
	TArray<FQuat> RotKeys;
	friend FArchive& operator<<(FArchive& Ar, FRawAnimSequenceTrackNativeDeprecated& T)
	{
		return	Ar << T.PosKeys << T.RotKeys;
	}
};

/////////////////////////////////////////////////////
// FCurveTrack

/** Returns true if valid curve weight exists in the array*/
bool FCurveTrack::IsValidCurveTrack()
{
	bool bValid = false;

	if ( CurveName != NAME_None )
	{
		for (int32 I=0; I<CurveWeights.Num(); ++I)
		{
			// it has valid weight
			if (CurveWeights[I]>KINDA_SMALL_NUMBER)
			{
				bValid = true;
				break;
			}
		}
	}

	return bValid;
}

/** This is very simple cut to 1 key method if all is same since I see so many redundant same value in every frame 
 *  Eventually this can get more complicated 
 *  Will return true if compressed to 1. Return false otherwise
 **/
bool FCurveTrack::CompressCurveWeights()
{
	// if always 1, no reason to do this
	if (CurveWeights.Num() > 1)
	{
		bool bCompress = true;
		// first weight
		float FirstWeight = CurveWeights[0];

		for (int32 I=1; I<CurveWeights.Num(); ++I)
		{
			// see if my key is same as previous
			if (fabs(FirstWeight - CurveWeights[I]) > SMALL_NUMBER)
			{
				// if not same, just get out, you don't like to compress this to 1 key
				bCompress = false;
				break;
			}
		} 

		if (bCompress)
		{
			CurveWeights.Empty();
			CurveWeights.Add(FirstWeight);
			CurveWeights.Shrink();
		}

		return bCompress;
	}

	// nothing changed
	return false;
}

/////////////////////////////////////////////////////////////

// since we want this change for hot fix, I can't change header file, 
// next time move this to the header
float GetIntervalPerKey(int32 NumFrames, float SequenceLength) 
{
	return (NumFrames > 1) ? (SequenceLength / (NumFrames-1)) : MINIMUM_ANIMATION_LENGTH;
}

#if WITH_EDITOR
// Handles keeping source raw data in sync when modifying raw data
struct FModifyRawDataSourceGuard
{
private:
	UAnimSequence* ModifyingSequence;

public:
	FModifyRawDataSourceGuard(UAnimSequence* AnimToModify)
		: ModifyingSequence(nullptr)
	{
		check(AnimToModify)
		if (AnimToModify->HasBakedTransformCurves())
		{
			ModifyingSequence = AnimToModify;
			ModifyingSequence->RestoreSourceData();
		}
	}

	~FModifyRawDataSourceGuard()
	{
		if (ModifyingSequence)
		{
			ModifyingSequence->BakeTrackCurvesToRawAnimation();
		}
	}
};
#endif

/////////////////////////////////////////////////////
// UAnimSequence

UAnimSequence::UAnimSequence(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, Interpolation(EAnimInterpolationType::Linear)
	, bEnableRootMotion(false)
	, RootMotionRootLock(ERootMotionRootLock::RefPose)
	, bUseNormalizedRootMotionScale(true)
	, bRootMotionSettingsCopiedFromMontage(false)
	, bUseRawDataOnly(!FPlatformProperties::RequiresCookedData())
#if WITH_EDITOR
	, bCompressionInProgress(false)
#endif
{
	RateScale = 1.0;

#if WITH_EDITORONLY_DATA
	ImportFileFramerate = 0.0f;
	ImportResampleFramerate = 0;
	bAllowFrameStripping = true;
	CompressionErrorThresholdScale = 1.f;

	CustomAttributesGuid.Invalidate();
	BakedCustomAttributesGuid.Invalidate();
#endif
}

void UAnimSequence::PostInitProperties()
{
#if WITH_EDITORONLY_DATA
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}
	MarkerDataUpdateCounter = 0;
#endif
	Super::PostInitProperties();
}

void UAnimSequence::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
#if WITH_EDITORONLY_DATA
	if (AssetImportData)
	{
		OutTags.Add( FAssetRegistryTag(SourceFileTagName(), AssetImportData->GetSourceData().ToJson(), FAssetRegistryTag::TT_Hidden) );
	}
#endif

	OutTags.Add(FAssetRegistryTag(TEXT("Compression Ratio"), FString::Printf(TEXT("%.03f"), (float)GetApproxCompressedSize() / (float)GetUncompressedRawSize()), FAssetRegistryTag::TT_Numerical));
	OutTags.Add(FAssetRegistryTag(TEXT("Compressed Size (KB)"), FString::Printf(TEXT("%.02f"), (float)GetApproxCompressedSize() / 1024.0f), FAssetRegistryTag::TT_Numerical));
	OutTags.Add(FAssetRegistryTag(TEXT("FrameRate"), FString::Printf(TEXT("%.2f"), GetFrameRate()), FAssetRegistryTag::TT_Numerical));
	Super::GetAssetRegistryTags(OutTags);
}

void UAnimSequence::AddReferencedObjects(UObject* This, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(This, Collector);

	UAnimSequence* AnimSeq = CastChecked<UAnimSequence>(This);
	Collector.AddReferencedObject(AnimSeq->CompressedData.BoneCompressionCodec);
	Collector.AddReferencedObject(AnimSeq->CompressedData.CurveCompressionCodec);
}

int32 UAnimSequence::GetUncompressedRawSize() const
{
	int32 BoneRawSize = ((sizeof(FVector) + sizeof(FQuat) + sizeof(FVector)) * RawAnimationData.Num() * NumFrames);
	int32 CurveRawSize = 0;
	for (const FFloatCurve& Curve : RawCurveData.FloatCurves)
	{
		CurveRawSize += sizeof(FFloatCurve);
		CurveRawSize += sizeof(FRichCurveKey) * Curve.FloatCurve.Keys.Num();
	}
	return BoneRawSize + CurveRawSize;
}

int32 UAnimSequence::GetApproxRawSize() const
{
	int32 Total = sizeof(FRawAnimSequenceTrack) * RawAnimationData.Num();
	for (int32 i=0;i<RawAnimationData.Num();++i)
	{
		const FRawAnimSequenceTrack& RawTrack = RawAnimationData[i];
		Total +=
			sizeof( FVector ) * RawTrack.PosKeys.Num() +
			sizeof( FQuat ) * RawTrack.RotKeys.Num() + 
			sizeof( FVector ) * RawTrack.ScaleKeys.Num(); 
	}
	for (const FFloatCurve& Curve : RawCurveData.FloatCurves)
	{
		Total += sizeof(FFloatCurve);
		Total += sizeof(FRichCurveKey) * Curve.FloatCurve.Keys.Num();
	}
	return Total;
}

int32 UAnimSequence::GetApproxBoneCompressedSize() const
{
	return CompressedData.CompressedDataStructure != nullptr ? CompressedData.CompressedDataStructure->GetApproxCompressedSize() : 0;
}

int32 UAnimSequence::GetApproxCompressedSize() const
{
	int32 BoneTotal = GetApproxBoneCompressedSize();
	int32 CurveTotal = CompressedData.CompressedCurveByteStream.Num();
	return BoneTotal + CurveTotal;
}

/**
 * Deserializes old compressed track formats from the specified archive.
 */
static void LoadOldCompressedTrack(FArchive& Ar, FCompressedTrack& Dst, int32 ByteStreamStride)
{
	// Serialize from the archive to a buffer.
	int32 NumBytes = 0;
	Ar << NumBytes;

	TArray<uint8> SerializedData;
	SerializedData.Empty( NumBytes );
	SerializedData.AddUninitialized( NumBytes );
	Ar.Serialize( SerializedData.GetData(), NumBytes );

	// Serialize the key times.
	Ar << Dst.Times;

	// Serialize mins and ranges.
	Ar << Dst.Mins[0] << Dst.Mins[1] << Dst.Mins[2];
	Ar << Dst.Ranges[0] << Dst.Ranges[1] << Dst.Ranges[2];
}

void UAnimSequence::Serialize(FArchive& Ar)
{
	LLM_SCOPE(ELLMTag::Animation);
	
	Ar.UsingCustomVersion(FFrameworkObjectVersion::GUID);

	FRawCurveTracks RawCurveCache;

	if (Ar.IsCooking())
	{
		RawCurveCache.FloatCurves = MoveTemp(RawCurveData.FloatCurves);
		RawCurveData.FloatCurves.Reset();

#if WITH_EDITORONLY_DATA
		RawCurveCache.VectorCurves = MoveTemp(RawCurveData.VectorCurves);
		RawCurveData.VectorCurves.Reset();

		RawCurveCache.TransformCurves = MoveTemp(RawCurveData.TransformCurves);
		RawCurveData.TransformCurves.Reset();
#endif
	}

	Super::Serialize(Ar);

	if (Ar.IsCooking())
	{
		RawCurveData.FloatCurves = MoveTemp(RawCurveCache.FloatCurves);
#if WITH_EDITORONLY_DATA
		RawCurveData.VectorCurves = MoveTemp(RawCurveCache.VectorCurves);
		RawCurveData.TransformCurves = MoveTemp(RawCurveCache.TransformCurves);
#endif
	}

	FStripDataFlags StripFlags( Ar );
	if( !StripFlags.IsEditorDataStripped() )
	{
		Ar << RawAnimationData;
#if WITH_EDITORONLY_DATA
		if (!Ar.IsCooking())
		{
			if (Ar.UE4Ver() >= VER_UE4_ANIMATION_ADD_TRACKCURVES)
			{
				Ar << SourceRawAnimationData;
			}
		}

		// If we have transform curves but no SourceRawAnimationData then we need to rebake
		if (DoesContainTransformCurves() && RawAnimationData.Num() > 0 && SourceRawAnimationData.Num() == 0)
		{
			bNeedsRebake = true;
		}
#endif // WITH_EDITORONLY_DATA
	}

	if (Ar.CustomVer(FFrameworkObjectVersion::GUID) < FFrameworkObjectVersion::MoveCompressedAnimDataToTheDDC)
	{
		// Serialize the compressed byte stream from the archive to the buffer.
		int32 NumBytes;
		Ar << NumBytes;

		TArray<uint8> SerializedData;
		SerializedData.AddUninitialized(NumBytes);
		Ar.Serialize(SerializedData.GetData(), NumBytes);
	}
	else
	{
		const bool bIsCooking = Ar.IsCooking();
		const bool bIsDuplicating = Ar.HasAnyPortFlags(PPF_DuplicateForPIE) || Ar.HasAnyPortFlags(PPF_Duplicate);
		const bool bIsTransacting = Ar.IsTransacting();
		const bool bIsCookingForDedicatedServer = bIsCooking && Ar.CookingTarget()->IsServerOnly();
		const bool bIsCountingMemory = Ar.IsCountingMemory();
		const bool bCookingTargetNeedsCompressedData = bIsCooking && (!UAnimationSettings::Get()->bStripAnimationDataOnDedicatedServer || !bIsCookingForDedicatedServer || bEnableRootMotion);

		bool bSerializeCompressedData = bCookingTargetNeedsCompressedData || bIsDuplicating || bIsTransacting || bIsCountingMemory;
		Ar << bSerializeCompressedData;

		if (bCookingTargetNeedsCompressedData)
		{
			if(GetSkeleton())
			{
				// Validate that we are cooking valid compressed data.
				checkf(Ar.IsObjectReferenceCollector() || (GetSkeletonVirtualBoneGuid() == GetSkeleton()->GetVirtualBoneGuid()), TEXT("Attempting to cook animation '%s' containing invalid virtual bone guid! Animation:%s Skeleton:%s"), *GetFullName(), *GetSkeletonVirtualBoneGuid().ToString(EGuidFormats::HexValuesInBraces), *GetSkeleton()->GetVirtualBoneGuid().ToString(EGuidFormats::HexValuesInBraces));
			}
		}

		if (bSerializeCompressedData)
		{
			SerializeCompressedData(Ar,false);
			Ar << bUseRawDataOnly;
		}
	}

#if WITH_EDITORONLY_DATA
	if ( Ar.IsLoading() && Ar.UE4Ver() < VER_UE4_ASSET_IMPORT_DATA_AS_JSON && !AssetImportData)
	{
		// AssetImportData should always be valid
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}

	// SourceFilePath and SourceFileTimestamp were moved into a subobject
	if ( Ar.IsLoading() && Ar.UE4Ver() < VER_UE4_ADDED_FBX_ASSET_IMPORT_DATA && AssetImportData)
	{
		// AssetImportData should always have been set up in the constructor where this is relevant
		FAssetImportInfo Info;
		Info.Insert(FAssetImportInfo::FSourceFile(SourceFilePath_DEPRECATED));
		AssetImportData->SourceData = MoveTemp(Info);
		
		SourceFilePath_DEPRECATED = TEXT("");
		SourceFileTimestamp_DEPRECATED = TEXT("");
	}

#endif // WITH_EDITORONLY_DATA
}

#if WITH_EDITOR
bool UAnimSequence::IsValidToPlay() const
{
	// make sure sequence length is valid and raw animation data exists, and compressed
	return ( SequenceLength > 0.f);
}
#endif

void UAnimSequence::SortSyncMarkers()
{
	// First make sure all SyncMarkers are within a valid range
	for (auto& SyncMarker : AuthoredSyncMarkers)
	{
		SyncMarker.Time = FMath::Clamp(SyncMarker.Time, 0.f, SequenceLength);
	}

	// Then sort
	AuthoredSyncMarkers.Sort();

	// Then refresh data
	RefreshSyncMarkerDataFromAuthored();
}

void UAnimSequence::GetPreloadDependencies(TArray<UObject*>& OutDeps)
{
	Super::GetPreloadDependencies(OutDeps);

	// We preload the compression settings because we need them loaded during Serialize to lookup the proper codec
	// which is stored as a path/string.
	if (CurveCompressionSettings != nullptr)
	{
		OutDeps.Add(CurveCompressionSettings);
	}

	if (BoneCompressionSettings != nullptr)
	{
		OutDeps.Add(BoneCompressionSettings);
	}
}

void UAnimSequence::PreSave(const class ITargetPlatform* TargetPlatform)
{
#if WITH_EDITOR
	// Could already be compressing
	WaitOnExistingCompression();

	// we have to bake it if it's not baked
	if (DoesNeedRebake())
	{
		BakeTrackCurvesToRawAnimation();
	}

	// make sure if it does contain transform curvesm it contains source data
	// empty track animation still can be made by retargeting to invalid skeleton
	// make sure to not trigger ensure if RawAnimationData is also null
	
	// Why should we not be able to have empty transform curves?
	ensureMsgf(!DoesContainTransformCurves() || (RawAnimationData.Num()==0 || SourceRawAnimationData.Num() != 0), TEXT("Animation : %s"), *GetName());

	if (DoesNeedRecompress())
	{
		RequestSyncAnimRecompression(); // Update Normal data

		ensureAlwaysMsgf(!bUseRawDataOnly,  TEXT("Animation : %s failed to compress"), *GetName());
	}

	if (TargetPlatform)
	{
		// Update compressed data for platform
		FRequestAnimCompressionParams Params(false, false, false);
		Params.InitFrameStrippingFromPlatform(TargetPlatform);
		RequestAnimCompression(Params);
	}

	WaitOnExistingCompression(); // Wait on updated data
#endif

	Super::PreSave(TargetPlatform);
}

bool UAnimSequence::IsPostLoadThreadSafe() const
{
	return true;
}

void UAnimSequence::PostLoad()
{
	//Parent PostLoad will ensure that skeleton is fully loaded
	//before we do anything further in PostLoad
	Super::PostLoad();

#if WITH_EDITOR
	if (!RawDataGuid.IsValid())
	{
		RawDataGuid = GenerateGuidFromRawData();
	}

	// I have to do this first thing in here
	// so that remove all NaNs before even being read
	if(GetLinkerUE4Version() < VER_UE4_ANIMATION_REMOVE_NANS)
	{
		RemoveNaNTracks();
	}

	VerifyTrackMap(nullptr);

#endif // WITH_EDITOR

	// if valid additive, but if base additive isn't 
	// this seems to happen from retargeting sometimes, which we still have to investigate why, 
	// but this causes issue since once this happens this is unrecoverable until you delete from outside of editor
	if (IsValidAdditive())
	{
		if (RefPoseSeq && RefPoseSeq->GetSkeleton() != GetSkeleton())
		{
			// if this happens, there was a issue with retargeting, 
			UE_LOG(LogAnimation, Warning, TEXT("Animation %s - Invalid additive animation base animation (%s)"), *GetName(), *RefPoseSeq->GetName());
			RefPoseSeq = nullptr;
		}
	}

#if WITH_EDITOR
	static bool ForcedRecompressionSetting = FAnimationUtils::GetForcedRecompressionSetting();

	if (ForcedRecompressionSetting)
	{
		//Force recompression
		RawDataGuid = FGuid::NewGuid();
		bUseRawDataOnly = true;
	}

	if (bUseRawDataOnly)
	{
		RequestAnimCompression(FRequestAnimCompressionParams(true, false, false));
	}
#endif

	// Ensure notifies are sorted.
	SortNotifies();

	// No animation data is found. Warn - this should check before we check CompressedTrackOffsets size
	// Otherwise, we'll see empty data set crashing game due to no CompressedTrackOffsets
	// You can't check RawAnimationData size since it gets removed during cooking
	if ( NumFrames == 0 && RawCurveData.FloatCurves.Num() == 0 )
	{
		UE_LOG(LogAnimation, Warning, TEXT("No animation data exists for sequence %s (%s)"), *GetName(), (GetOuter() ? *GetOuter()->GetFullName() : *GetFullName()) );
#if WITH_EDITOR
		if (!IsRunningGame())
		{
			static FName NAME_LoadErrors("LoadErrors");
			FMessageLog LoadErrors(NAME_LoadErrors);

			TSharedRef<FTokenizedMessage> Message = LoadErrors.Warning();
			Message->AddToken(FTextToken::Create(LOCTEXT("EmptyAnimationData1", "The Animation ")));
			Message->AddToken(FAssetNameToken::Create(GetPathName(), FText::FromString(GetName())));
			Message->AddToken(FTextToken::Create(LOCTEXT("EmptyAnimationData2", " has no animation data. Recommend to remove.")));
			LoadErrors.Notify();
		}
#endif
	}
	// @remove temp hack for fixing length
	// @todo need to fix importer/editing feature
	else if ( SequenceLength == 0.f )
	{
		ensure(NumFrames == 1);
		SequenceLength = MINIMUM_ANIMATION_LENGTH;
	}
	// Raw data exists, but missing compress animation data
	else if( GetSkeleton() && !IsCompressedDataValid()
#if WITH_EDITOR
		&& !bCompressionInProgress
#endif
		)
	{
		UE_LOG(LogAnimation, Fatal, TEXT("No animation compression exists for sequence %s (%s)"), *GetName(), (GetOuter() ? *GetOuter()->GetFullName() : *GetFullName()) );
	}

	// If we're in the game and compressed animation data exists, whack the raw data.
	if (FPlatformProperties::RequiresCookedData())
	{
		if (GetSkeleton())
		{
			SetSkeletonVirtualBoneGuid(GetSkeleton()->GetVirtualBoneGuid());
		}
		if( RawAnimationData.Num() > 0  && CompressedData.CompressedByteStream.Num() > 0 )
		{
#if 0//@todo.Cooker/Package...
			// Don't do this on consoles; raw animation data should have been stripped during cook!
			UE_LOG(LogAnimation, Fatal, TEXT("Cooker did not strip raw animation from sequence %s"), *GetName() );
#else
			// Remove raw animation data.
			for ( int32 TrackIndex = 0 ; TrackIndex < RawAnimationData.Num() ; ++TrackIndex )
			{
				FRawAnimSequenceTrack& RawTrack = RawAnimationData[TrackIndex];
				RawTrack.PosKeys.Empty();
				RawTrack.RotKeys.Empty();
				RawTrack.ScaleKeys.Empty();
			}
			
			RawAnimationData.Empty();
#endif
		}
	}

	for(FAnimNotifyEvent& Notify : Notifies)
	{
		if(Notify.DisplayTime_DEPRECATED != 0.0f)
		{
			Notify.Clear();
			Notify.LinkSequence(this, Notify.DisplayTime_DEPRECATED);
		}
		else
		{
			Notify.LinkSequence(this, Notify.GetTime());
		}
	
		if(Notify.Duration != 0.0f)
		{
			Notify.EndLink.LinkSequence(this, Notify.GetTime() + Notify.Duration);
		}
	}

	if (USkeleton* CurrentSkeleton = GetSkeleton())
	{
		for (FSmartName& CurveName : CompressedData.CompressedCurveNames)
		{
			CurrentSkeleton->VerifySmartName(USkeleton::AnimCurveMappingName, CurveName);
		}

#if WITH_EDITOR
		VerifyCurveNames<FTransformCurve>(*CurrentSkeleton, USkeleton::AnimTrackCurveMappingName, RawCurveData.TransformCurves);

		for (const FAnimSyncMarker& SyncMarker : AuthoredSyncMarkers)
		{
			CurrentSkeleton->RegisterMarkerName(SyncMarker.MarkerName);
		}
#endif
	}
}

#if WITH_EDITOR
void ShowResaveMessage(const UAnimSequence* Sequence)
{
	if (IsRunningCommandlet())
	{
		UE_LOG(LogAnimation, Log, TEXT("Resave Animation Required(%s, %s): Fixing track data and recompressing."), *GetNameSafe(Sequence), *Sequence->GetPathName());

		/*static FName NAME_LoadErrors("LoadErrors");
		FMessageLog LoadErrors(NAME_LoadErrors);

		TSharedRef<FTokenizedMessage> Message = LoadErrors.Warning();
		Message->AddToken(FTextToken::Create(LOCTEXT("AnimationNeedsResave1", "The Animation ")));
		Message->AddToken(FAssetNameToken::Create(Sequence->GetPathName(), FText::FromString(GetNameSafe(Sequence))));
		Message->AddToken(FTextToken::Create(LOCTEXT("AnimationNeedsResave2", " needs resave.")));
		LoadErrors.Notify();*/
	}
}

void UAnimSequence::VerifyTrackMap(USkeleton* MySkeleton)
{
	USkeleton* UseSkeleton = (MySkeleton) ? MySkeleton : GetSkeleton();

	if( AnimationTrackNames.Num() != TrackToSkeletonMapTable.Num() && UseSkeleton!=nullptr)
	{
		ShowResaveMessage(this);

		AnimationTrackNames.Empty();
		AnimationTrackNames.AddUninitialized(TrackToSkeletonMapTable.Num());
		for(int32 I=0; I<TrackToSkeletonMapTable.Num(); ++I)
		{
			const FTrackToSkeletonMap& TrackMap = TrackToSkeletonMapTable[I];
			AnimationTrackNames[I] = UseSkeleton->GetReferenceSkeleton().GetBoneName(TrackMap.BoneTreeIndex);
		}
	}
	else if (UseSkeleton != nullptr)
	{
		// first check if any of them needs to be removed
		{
			int32 NumTracks = AnimationTrackNames.Num();
			int32 NumSkeletonBone = UseSkeleton->GetReferenceSkeleton().GetRawBoneNum();

			// the first fix is to make sure 
			bool bNeedsFixing = false;
			// verify all tracks are still valid
			for(int32 TrackIndex=0; TrackIndex<NumTracks; TrackIndex++)
			{
				int32 SkeletonBoneIndex = TrackToSkeletonMapTable[TrackIndex].BoneTreeIndex;
				// invalid index found
				if(SkeletonBoneIndex == INDEX_NONE || NumSkeletonBone <= SkeletonBoneIndex)
				{
					// if one is invalid, fix up for all.
					// you don't know what index got messed up
					bNeedsFixing = true;
					break;
				}
			}

			if(bNeedsFixing)
			{
				ShowResaveMessage(this);

				for(int32 I=NumTracks-1; I>=0; --I)
				{
					int32 BoneTreeIndex = UseSkeleton->GetReferenceSkeleton().FindBoneIndex(AnimationTrackNames[I]);
					if(BoneTreeIndex == INDEX_NONE)
					{
						RemoveTrack(I);
					}
					else
					{
						TrackToSkeletonMapTable[I].BoneTreeIndex = BoneTreeIndex;
					}
				}
			}
		}
		
		for(int32 I=0; I<AnimationTrackNames.Num(); ++I)
		{
			FTrackToSkeletonMap& TrackMap = TrackToSkeletonMapTable[I];
			TrackMap.BoneTreeIndex = UseSkeleton->GetReferenceSkeleton().FindBoneIndex(AnimationTrackNames[I]);
		}		
	}
}

#endif // WITH_EDITOR
void UAnimSequence::BeginDestroy()
{
	// Could already be compressing
	WaitOnExistingCompression(false);

	Super::BeginDestroy();

	ClearCompressedCurveData();
	ClearCompressedBoneData();
}

#if WITH_EDITOR
void UAnimSequence::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if(!IsTemplate())
	{
		// Make sure package is marked dirty when doing stuff like adding/removing notifies
		MarkPackageDirty();
	}

	if (AdditiveAnimType != AAT_None)
	{
		if (RefPoseType == ABPT_None)
		{
			// slate will take care of change
			RefPoseType = ABPT_RefPose;
		}
	}

	if (RefPoseSeq != NULL)
	{
		if (RefPoseSeq->GetSkeleton() != GetSkeleton()) // @todo this may require to be changed when hierarchy of skeletons is introduced
		{
			RefPoseSeq = NULL;
		}
	}

	bool bAdditiveSettingsChanged = false;
	bool bCompressionAffectingSettingsChanged = false;

	if(PropertyChangedEvent.Property)
	{
		const bool bChangedRefFrameIndex = PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UAnimSequence, RefFrameIndex);

		if (bChangedRefFrameIndex)
		{
			bUseRawDataOnly = true;
		}

		if ((bChangedRefFrameIndex && PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive) ||
			PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UAnimSequence, AdditiveAnimType) ||
			PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UAnimSequence, RefPoseSeq) ||
			PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UAnimSequence, RefPoseType))
		{
			bAdditiveSettingsChanged = true;
		}
		
		bCompressionAffectingSettingsChanged =   PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UAnimSequence, bAllowFrameStripping)
											  || PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UAnimSequence, CompressionErrorThresholdScale);
	}

	const bool bNeedPostProcess = !IsCompressedDataValid() || bAdditiveSettingsChanged || bCompressionAffectingSettingsChanged;

	// @Todo fix me: This is temporary fix to make sure they always have compressed data
	if (RawAnimationData.Num() > 0 && bNeedPostProcess)
	{
		PostProcessSequence(false);
	}

	if (PropertyChangedEvent.Property != nullptr)
	{
		if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UAnimSequence, CurveCompressionSettings))
		{
			RequestSyncAnimRecompression(false);
		}

		if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UAnimSequence, BoneCompressionSettings))
		{
			RequestSyncAnimRecompression(false);
		}
	}
}

void UAnimSequence::PostDuplicate(bool bDuplicateForPIE)
{
	// if transform curve exists, mark as bake
	if (DoesContainTransformCurves())
	{
		bNeedsRebake = true;
	}

	Super::PostDuplicate(bDuplicateForPIE);
}
#endif // WITH_EDITOR

// @todo DB: Optimize!
template<typename TimeArray>
static int32 FindKeyIndex(float Time, const TimeArray& Times)
{
	int32 FoundIndex = 0;
	for ( int32 Index = 0 ; Index < Times.Num() ; ++Index )
	{
		const float KeyTime = Times(Index);
		if ( Time >= KeyTime )
		{
			FoundIndex = Index;
		}
		else
		{
			break;
		}
	}
	return FoundIndex;
}

void UAnimSequence::GetBoneTransform(FTransform& OutAtom, int32 TrackIndex, float Time, bool bUseRawData) const
{
	// If the caller didn't request that raw animation data be used . . .
	if ( !bUseRawData && IsCompressedDataValid() )
	{
		FAnimSequenceDecompressionContext DecompContext(SequenceLength, Interpolation, GetFName(), *CompressedData.CompressedDataStructure);
		DecompContext.Seek(Time);
		if (CompressedData.BoneCompressionCodec)
		{
			CompressedData.BoneCompressionCodec->DecompressBone(DecompContext, TrackIndex, OutAtom);
			return;
		}
	}

	ExtractBoneTransform(RawAnimationData, OutAtom, TrackIndex, Time);
}

void UAnimSequence::GetBoneTransform(FTransform& OutAtom, int32 TrackIndex, FAnimSequenceDecompressionContext& DecompContext, bool bUseRawData) const
{
	// If the caller didn't request that raw animation data be used . . .
	if (!bUseRawData && IsCompressedDataValid())
	{
		if (CompressedData.BoneCompressionCodec)
		{
			CompressedData.BoneCompressionCodec->DecompressBone(DecompContext, TrackIndex, OutAtom);
			return;
		}
	}

	ExtractBoneTransform(RawAnimationData, OutAtom, TrackIndex, DecompContext.Time);
}

void UAnimSequence::ExtractBoneTransform(const TArray<struct FRawAnimSequenceTrack>& InRawAnimationData, FTransform& OutAtom, int32 TrackIndex, float Time) const
{
	// Bail out if the animation data doesn't exists (e.g. was stripped by the cooker).
	if(InRawAnimationData.Num() == 0)
	{
		UE_LOG(LogAnimation, Log, TEXT("UAnimSequence::GetBoneTransform : No anim data in AnimSequence[%s]!"),*GetFullName());
		OutAtom.SetIdentity();
		return;
	}

	ExtractBoneTransform(InRawAnimationData[TrackIndex], OutAtom, Time);
}

void UAnimSequence::ExtractBoneTransform(const struct FRawAnimSequenceTrack& RawTrack, FTransform& OutAtom, int32 KeyIndex) const
{
	// Bail out (with rather wacky data) if data is empty for some reason.
	if (RawTrack.PosKeys.Num() == 0 || RawTrack.RotKeys.Num() == 0)
	{
		UE_LOG(LogAnimation, Log, TEXT("UAnimSequence::GetBoneTransform : No anim data in AnimSequence!"));
		OutAtom.SetIdentity();
		return;
	}

	const int32 PosKeyIndex = FMath::Min(KeyIndex, RawTrack.PosKeys.Num() - 1);
	const int32 RotKeyIndex = FMath::Min(KeyIndex, RawTrack.RotKeys.Num() - 1);
	static const FVector DefaultScale3D = FVector(1.f);

	OutAtom.SetTranslation(RawTrack.PosKeys[PosKeyIndex]);
	OutAtom.SetRotation(RawTrack.RotKeys[RotKeyIndex]);
	if (RawTrack.ScaleKeys.Num() > 0)
	{
		const int32 ScaleKeyIndex = FMath::Min(KeyIndex, RawTrack.ScaleKeys.Num() - 1);
		OutAtom.SetScale3D(RawTrack.ScaleKeys[ScaleKeyIndex]);
	}
	else
	{
		OutAtom.SetScale3D(DefaultScale3D);
	}
}

void UAnimSequence::ExtractBoneTransform(const struct FRawAnimSequenceTrack& RawTrack, FTransform& OutAtom, float Time) const
{
	FAnimationUtils::ExtractTransformFromTrack(Time, NumFrames, SequenceLength, RawTrack, Interpolation, OutAtom);
}

void UAnimSequence::HandleAssetPlayerTickedInternal(FAnimAssetTickContext &Context, const float PreviousTime, const float MoveDelta, const FAnimTickRecord &Instance, struct FAnimNotifyQueue& NotifyQueue) const
{
	Super::HandleAssetPlayerTickedInternal(Context, PreviousTime, MoveDelta, Instance, NotifyQueue);

	if (bEnableRootMotion)
	{
		Context.RootMotionMovementParams.Accumulate(ExtractRootMotion(PreviousTime, MoveDelta, Instance.bLooping));
	}
}

FTransform UAnimSequence::ExtractRootTrackTransform(float Pos, const FBoneContainer * RequiredBones) const
{
	const TArray<FTrackToSkeletonMap> & TrackToSkeletonMap = bUseRawDataOnly ? TrackToSkeletonMapTable : CompressedData.CompressedTrackToSkeletonMapTable;

	// we assume root is in first data if available = SkeletonIndex == 0 && BoneTreeIndex == 0)
	if ((TrackToSkeletonMap.Num() > 0) && (TrackToSkeletonMap[0].BoneTreeIndex == 0))
	{
		// if we do have root data, then return root data
		FTransform RootTransform;
		GetBoneTransform(RootTransform, 0, Pos, bUseRawDataOnly);
		return RootTransform;
	}

	// Fallback to root bone from reference skeleton.
	if( RequiredBones )
	{
		const FReferenceSkeleton& RefSkeleton = RequiredBones->GetReferenceSkeleton();
		if( RefSkeleton.GetNum() > 0 )
		{
			return RefSkeleton.GetRefBonePose()[0];
		}
	}

	USkeleton * MySkeleton = GetSkeleton();
	// If we don't have a RequiredBones array, get root bone from default skeleton.
	if( !RequiredBones &&  MySkeleton )
	{
		const FReferenceSkeleton& RefSkeleton = MySkeleton->GetReferenceSkeleton();
		if( RefSkeleton.GetNum() > 0 )
		{
			return RefSkeleton.GetRefBonePose()[0];
		}
	}

	// Otherwise, use identity.
	return FTransform::Identity;
}

FTransform UAnimSequence::ExtractRootMotion(float StartTime, float DeltaTime, bool bAllowLooping) const
{
	FRootMotionMovementParams RootMotionParams;

	if (DeltaTime != 0.f)
	{
		bool const bPlayingBackwards = (DeltaTime < 0.f);

		float PreviousPosition = StartTime;
		float CurrentPosition = StartTime;
		float DesiredDeltaMove = DeltaTime;

		do
		{
			// Disable looping here. Advance to desired position, or beginning / end of animation 
			const ETypeAdvanceAnim AdvanceType = FAnimationRuntime::AdvanceTime(false, DesiredDeltaMove, CurrentPosition, SequenceLength);

			// Verify position assumptions
			ensureMsgf(bPlayingBackwards ? (CurrentPosition <= PreviousPosition) : (CurrentPosition >= PreviousPosition), TEXT("in Animation %s(Skeleton %s) : bPlayingBackwards(%d), PreviousPosition(%0.2f), Current Position(%0.2f)"),
				*GetName(), *GetNameSafe(GetSkeleton()), bPlayingBackwards, PreviousPosition, CurrentPosition);

			RootMotionParams.Accumulate(ExtractRootMotionFromRange(PreviousPosition, CurrentPosition));

			// If we've hit the end of the animation, and we're allowed to loop, keep going.
			if ((AdvanceType == ETAA_Finished) && bAllowLooping)
			{
				const float ActualDeltaMove = (CurrentPosition - PreviousPosition);
				DesiredDeltaMove -= ActualDeltaMove;

				PreviousPosition = bPlayingBackwards ? SequenceLength : 0.f;
				CurrentPosition = PreviousPosition;
			}
			else
			{
				break;
			}
		} while (true);
	}

	return RootMotionParams.GetRootMotionTransform();
}

FTransform UAnimSequence::ExtractRootMotionFromRange(float StartTrackPosition, float EndTrackPosition) const
{
	const FVector DefaultScale(1.f);

	FTransform InitialTransform = ExtractRootTrackTransform(0.f, NULL);
	FTransform StartTransform = ExtractRootTrackTransform(StartTrackPosition, NULL);
	FTransform EndTransform = ExtractRootTrackTransform(EndTrackPosition, NULL);

	// Use old calculation if needed.
	if (bUseNormalizedRootMotionScale)
	{
		//Clear scale as it will muck up GetRelativeTransform
		StartTransform.SetScale3D(FVector(1.f));
		EndTransform.SetScale3D(FVector(1.f));
	}
	else
	{
		if (IsValidAdditive())
		{
			StartTransform.SetScale3D(StartTransform.GetScale3D() + DefaultScale);
			EndTransform.SetScale3D(EndTransform.GetScale3D() + DefaultScale);
		}
	}

	// Transform to Component Space Rotation (inverse root transform from first frame)
	const FTransform RootToComponentRot = FTransform(InitialTransform.GetRotation().Inverse());
	StartTransform = RootToComponentRot * StartTransform;
	EndTransform = RootToComponentRot * EndTransform;

	return EndTransform.GetRelativeTransform(StartTransform);
}

#if WITH_EDITOR
TArray<const UAnimSequence*> CurrentBakingAnims;
#endif

#define DEBUG_POSE_OUTPUT 0

#if DEBUG_POSE_OUTPUT
void DebugPrintBone(const FCompactPose& OutPose, const FCompactPoseBoneIndex& BoneIndex, int32 OutIndent)
{
	for (int i = 0; i < OutIndent; ++i)
	{
		FPlatformMisc::LowLevelOutputDebugString(TEXT("  "));
	}
	const FBoneContainer& Cont = OutPose.GetBoneContainer();

	FName BoneName = Cont.GetReferenceSkeleton().GetBoneName(Cont.MakeMeshPoseIndex(BoneIndex).GetInt());

	FVector T = OutPose[BoneIndex].GetTranslation();

	FPlatformMisc::LowLevelOutputDebugStringf(TEXT("%s - (%.2f, %.2f,%.2f)\n"), *BoneName.ToString(), T.X, T.Y, T.Z);
}
#endif

void UAnimSequence::GetAnimationPose(FAnimationPoseData& OutAnimationPoseData, const FAnimExtractContext& ExtractionContext) const
{
	SCOPE_CYCLE_COUNTER(STAT_GetAnimationPose);

	const FCompactPose& OutPose = OutAnimationPoseData.GetPose();

	// @todo anim: if compressed and baked in the future, we don't have to do this 
	if (UseRawDataForPoseExtraction(OutPose.GetBoneContainer()) && IsValidAdditive())
	{
		if (AdditiveAnimType == AAT_LocalSpaceBase)
		{
			GetBonePose_Additive(OutAnimationPoseData, ExtractionContext);
		}
		else if (AdditiveAnimType == AAT_RotationOffsetMeshSpace)
		{
			GetBonePose_AdditiveMeshRotationOnly(OutAnimationPoseData, ExtractionContext);
		}
	}
	else
	{
		GetBonePose(OutAnimationPoseData, ExtractionContext);
	}

	// Check that all bone atoms coming from animation are normalized
#if DO_CHECK && WITH_EDITORONLY_DATA
	check(OutPose.IsNormalized());
#endif

#if DEBUG_POSE_OUTPUT
	TArray<TArray<int32>> ParentLevel;
	ParentLevel.Reserve(64);
	for (int32 i = 0; i < 64; ++i)
	{
		ParentLevel.Add(TArray<int32>());
	}
	ParentLevel[0].Add(0);

	FPlatformMisc::LowLevelOutputDebugString(TEXT("\nGetAnimationPose\n"));
	
	DebugPrintBone(OutPose, FCompactPoseBoneIndex(0), 0);
	for (FCompactPoseBoneIndex BoneIndex(1); BoneIndex < OutPose.GetNumBones(); ++BoneIndex)
	{
		FCompactPoseBoneIndex ParentIndex = OutPose.GetBoneContainer().GetParentBoneIndex(BoneIndex);
		int32 Indent = 0;
		for (; Indent < ParentLevel.Num(); ++Indent)
		{
			if (ParentLevel[Indent].Contains(ParentIndex.GetInt()))
			{
				break;
			}
		}
		Indent += 1;
		check(Indent < 64);
		ParentLevel[Indent].Add(BoneIndex.GetInt());

		DebugPrintBone(OutPose, BoneIndex, Indent);
	}
#endif
}

void UAnimSequence::GetBonePose(FCompactPose& OutPose, FBlendedCurve& OutCurve, const FAnimExtractContext& ExtractionContext, bool bForceUseRawData) const
{
	FStackCustomAttributes TempAttributes;
	FAnimationPoseData OutAnimationPoseData(OutPose, OutCurve, TempAttributes);
	GetBonePose(OutAnimationPoseData, ExtractionContext, bForceUseRawData);
}

void UAnimSequence::GetBonePose(FAnimationPoseData& OutAnimationPoseData, const FAnimExtractContext& ExtractionContext, bool bForceUseRawData /*= false*/) const
{
	SCOPE_CYCLE_COUNTER(STAT_AnimSeq_GetBonePose);
	CSV_SCOPED_TIMING_STAT(Animation, AnimSeq_GetBonePose);

	FCompactPose& OutPose = OutAnimationPoseData.GetPose();

	const FBoneContainer& RequiredBones = OutPose.GetBoneContainer();
	const bool bUseRawDataForPoseExtraction = bForceUseRawData || UseRawDataForPoseExtraction(RequiredBones);

	const bool bIsBakedAdditive = !bUseRawDataForPoseExtraction && IsValidAdditive();

	const USkeleton* MySkeleton = GetSkeleton();
	if (!MySkeleton)
	{
		if (bIsBakedAdditive)
		{
			OutPose.ResetToAdditiveIdentity(); 
		}
		else
		{
			OutPose.ResetToRefPose();
		}
		return;
	}

	const bool bDisableRetargeting = RequiredBones.GetDisableRetargeting();

	// initialize with ref-pose
	if (bIsBakedAdditive)
	{
		//When using baked additive ref pose is identity
		OutPose.ResetToAdditiveIdentity();
	}
	else
	{
		// if retargeting is disabled, we initialize pose with 'Retargeting Source' ref pose.
		if (bDisableRetargeting)
		{
			TArray<FTransform> const& AuthoredOnRefSkeleton = MySkeleton->GetRefLocalPoses(RetargetSource);
			TArray<FBoneIndexType> const& RequireBonesIndexArray = RequiredBones.GetBoneIndicesArray();

			int32 const NumRequiredBones = RequireBonesIndexArray.Num();
			for (FCompactPoseBoneIndex PoseBoneIndex : OutPose.ForEachBoneIndex())
			{
				int32 const& SkeletonBoneIndex = RequiredBones.GetSkeletonIndex(PoseBoneIndex);

				// Pose bone index should always exist in Skeleton
				checkSlow(SkeletonBoneIndex != INDEX_NONE);
				OutPose[PoseBoneIndex] = AuthoredOnRefSkeleton[SkeletonBoneIndex];
			}
		}
		else
		{
			OutPose.ResetToRefPose();
		}
	}

	// extract curve data . Even if no track, it can contain curve data
	EvaluateCurveData(OutAnimationPoseData.GetCurve(), ExtractionContext.CurrentTime, bUseRawDataForPoseExtraction);

	const int32 NumTracks = bUseRawDataForPoseExtraction ? TrackToSkeletonMapTable.Num() : CompressedData.CompressedTrackToSkeletonMapTable.Num();
	if (NumTracks == 0)
	{
		return;
	}

	const bool bTreatAnimAsAdditive = (IsValidAdditive() && !bUseRawDataForPoseExtraction); // Raw data is never additive
	FRootMotionReset RootMotionReset(bEnableRootMotion, RootMotionRootLock, bForceRootLock, ExtractRootTrackTransform(0.f, &RequiredBones), bTreatAnimAsAdditive);

#if WITH_EDITOR
	// this happens only with editor data
	// Slower path for disable retargeting, that's only used in editor and for debugging.
	if (bUseRawDataForPoseExtraction)
	{
		const bool bShouldUseSourceData = (RequiredBones.ShouldUseSourceData() && SourceRawAnimationData.Num() > 0);
		const TArray<FRawAnimSequenceTrack>& AnimationData = bShouldUseSourceData ? SourceRawAnimationData : RawAnimationData;

		// Warning if we have invalid data

		for (int32 TrackIndex = 0; TrackIndex < NumTracks; TrackIndex++)
		{
			const FRawAnimSequenceTrack& TrackToExtract = AnimationData[TrackIndex];

			// Bail out (with rather wacky data) if data is empty for some reason.
			if (TrackToExtract.PosKeys.Num() == 0 || TrackToExtract.RotKeys.Num() == 0)
			{
				UE_LOG(LogAnimation, Warning, TEXT("UAnimSequence::GetBoneTransform : No anim data in AnimSequence '%s' Track '%s'"), *GetPathName(), *AnimationTrackNames[TrackIndex].ToString());
			}
		}

		BuildPoseFromRawData(AnimationData, TrackToSkeletonMapTable, OutPose, ExtractionContext.CurrentTime, Interpolation, NumFrames, SequenceLength, RetargetSource);

		if ((ExtractionContext.bExtractRootMotion && RootMotionReset.bEnableRootMotion) || RootMotionReset.bForceRootLock)
		{
			RootMotionReset.ResetRootBoneForRootMotion(OutPose[FCompactPoseBoneIndex(0)], RequiredBones);
		}

		GetCustomAttributes(OutAnimationPoseData, ExtractionContext, true);

		return;
	}
#endif // WITH_EDITOR

	DecompressPose(OutPose, CompressedData, ExtractionContext, GetSkeleton(), SequenceLength, Interpolation, bIsBakedAdditive, RetargetSource, GetFName(), RootMotionReset);
	GetCustomAttributes(OutAnimationPoseData, ExtractionContext, false);
}

#if WITH_EDITORONLY_DATA

void UAnimSequence::UpdateCompressedCurveName(SmartName::UID_Type CurveUID, const struct FSmartName& NewCurveName)
{
	for (FSmartName& CurveName : CompressedData.CompressedCurveNames)
	{
		if (CurveName.UID == CurveUID)
		{
			CurveName = NewCurveName;
			break;
		}
	}
}

int32 UAnimSequence::AddNewRawTrack(FName TrackName, FRawAnimSequenceTrack* TrackData)
{
	const int32 SkeletonIndex = GetSkeleton() ? GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(TrackName) : INDEX_NONE;

	if(SkeletonIndex != INDEX_NONE)
	{
		int32 TrackIndex = AnimationTrackNames.IndexOfByKey(TrackName);
		if (TrackIndex != INDEX_NONE)
		{
			if (TrackData)
			{
				RawAnimationData[TrackIndex] = *TrackData;
			}
			return TrackIndex;
		}

		// During compression, we store the track indices on 16 bits
		const int32 MAX_NUM_TRACKS = 65535;
		if (RawAnimationData.Num() >= MAX_NUM_TRACKS)
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("TrackName"), FText::FromName(TrackName));
			FNotificationInfo Info(FText::Format(LOCTEXT("TooManyRawTracks", "Cannot add track with name \"{TrackName}\". An animation sequence cannot contain more than 65535 tracks."), Args));

			Info.bUseLargeFont = false;
			Info.ExpireDuration = 5.0f;

			TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
			if (Notification.IsValid())
			{
				Notification->SetCompletionState(SNotificationItem::CS_Fail);
			}

			UE_LOG(LogAnimation, Error, TEXT("Cannot add track with name \"%s\". An animation sequence cannot contain more than 65535 tracks."), *TrackName.ToString());
		}

		check(AnimationTrackNames.Num() == RawAnimationData.Num());
		TrackIndex = AnimationTrackNames.Add(TrackName);
		TrackToSkeletonMapTable.Add(FTrackToSkeletonMap(SkeletonIndex));
		if (TrackData)
		{
			RawAnimationData.Add(*TrackData);
		}
		else
		{
			RawAnimationData.Add(FRawAnimSequenceTrack());
		}
		return TrackIndex;
	}
	return INDEX_NONE;
}
#endif

void UAnimSequence::GetBonePose_Additive(FCompactPose& OutPose, FBlendedCurve& OutCurve, const FAnimExtractContext& ExtractionContext) const
{
	FStackCustomAttributes TempAttributes;
	FAnimationPoseData OutAnimationPoseData(OutPose, OutCurve, TempAttributes);

	GetBonePose_Additive(OutAnimationPoseData, ExtractionContext);
}

void UAnimSequence::GetBonePose_Additive(FAnimationPoseData& OutAnimationPoseData, const FAnimExtractContext& ExtractionContext) const
{
	FCompactPose& OutPose = OutAnimationPoseData.GetPose();
	FBlendedCurve& OutCurve = OutAnimationPoseData.GetCurve();
	FStackCustomAttributes& OutAttributes = OutAnimationPoseData.GetAttributes();

	if (!IsValidAdditive())
	{
		OutPose.ResetToAdditiveIdentity();
		return;
	}

	// Extract target pose
	GetBonePose(OutAnimationPoseData, ExtractionContext);

	// Extract base pose
	FCompactPose BasePose;
	FBlendedCurve BaseCurve;
	FStackCustomAttributes BaseAttributes;
	
	BasePose.SetBoneContainer(&OutPose.GetBoneContainer());
	BaseCurve.InitFrom(OutCurve);	

	FAnimationPoseData BasePoseData(BasePose, BaseCurve, BaseAttributes);

	GetAdditiveBasePose(BasePoseData, ExtractionContext);

	// Create Additive animation
	FAnimationRuntime::ConvertPoseToAdditive(OutPose, BasePose);
	OutCurve.ConvertToAdditive(BaseCurve);

	FCustomAttributesRuntime::SubtractAttributes(BaseAttributes, OutAttributes);
}

void UAnimSequence::GetAdditiveBasePose(FCompactPose& OutPose, FBlendedCurve& OutCurve, const FAnimExtractContext& ExtractionContext) const
{
	FStackCustomAttributes TempAttributes;
	FAnimationPoseData OutAnimationPoseData(OutPose, OutCurve, TempAttributes);

	GetAdditiveBasePose(OutAnimationPoseData, ExtractionContext);
}

void UAnimSequence::GetAdditiveBasePose(FAnimationPoseData& OutAnimationPoseData, const FAnimExtractContext& ExtractionContext) const
{
	switch (RefPoseType)
	{
		// use whole animation as a base pose. Need BasePoseSeq.
		case ABPT_AnimScaled:
		{
			// normalize time to fit base seq
			const float Fraction = (SequenceLength > 0.f)? FMath::Clamp<float>(ExtractionContext.CurrentTime / SequenceLength, 0.f, 1.f) : 0.f;
			const float BasePoseTime = RefPoseSeq->SequenceLength * Fraction;

			FAnimExtractContext BasePoseExtractionContext(ExtractionContext);
			BasePoseExtractionContext.CurrentTime = BasePoseTime;
			RefPoseSeq->GetBonePose(OutAnimationPoseData, BasePoseExtractionContext, true);
			break;
		}
		// use animation as a base pose. Need BasePoseSeq and RefFrameIndex (will clamp if outside).
		case ABPT_AnimFrame:
		{
			const float Fraction = (RefPoseSeq->NumFrames > 0) ? FMath::Clamp<float>((float)RefFrameIndex / (float)RefPoseSeq->NumFrames, 0.f, 1.f) : 0.f;
			const float BasePoseTime = RefPoseSeq->SequenceLength * Fraction;

			FAnimExtractContext BasePoseExtractionContext(ExtractionContext);
			BasePoseExtractionContext.CurrentTime = BasePoseTime;
			RefPoseSeq->GetBonePose(OutAnimationPoseData, BasePoseExtractionContext, true);
			break;
		}
		// use ref pose of Skeleton as base
		case ABPT_RefPose:
		default:
			OutAnimationPoseData.GetPose().ResetToRefPose();
			break;
	}
}

void UAnimSequence::GetBonePose_AdditiveMeshRotationOnly(FCompactPose& OutPose, FBlendedCurve& OutCurve, const FAnimExtractContext& ExtractionContext) const
{
	FStackCustomAttributes TempAttributes;
	FAnimationPoseData OutAnimationPoseData(OutPose, OutCurve, TempAttributes);

	GetBonePose_AdditiveMeshRotationOnly(OutAnimationPoseData, ExtractionContext);
}

void UAnimSequence::GetBonePose_AdditiveMeshRotationOnly(FAnimationPoseData& OutAnimationPoseData, const FAnimExtractContext& ExtractionContext) const
{
	FCompactPose& OutPose = OutAnimationPoseData.GetPose();
	FBlendedCurve& OutCurve = OutAnimationPoseData.GetCurve();
	FStackCustomAttributes& OutAttributes = OutAnimationPoseData.GetAttributes();

	if (!IsValidAdditive())
	{
		// since this is additive, need to initialize to identity
		OutPose.ResetToAdditiveIdentity();
		return;
	}

	// Get target pose
	GetBonePose(OutAnimationPoseData, ExtractionContext, true);

	// get base pose
	FCompactPose BasePose;
	FBlendedCurve BaseCurve;
	FStackCustomAttributes BaseAttributes;

	FAnimationPoseData BasePoseData(BasePose, BaseCurve, BaseAttributes);

	BasePose.SetBoneContainer(&OutPose.GetBoneContainer());
	BaseCurve.InitFrom(OutCurve);

	GetAdditiveBasePose(BasePoseData, ExtractionContext);

	// Convert them to mesh rotation.
	FAnimationRuntime::ConvertPoseToMeshRotation(OutPose);
	FAnimationRuntime::ConvertPoseToMeshRotation(BasePose);

	// Turn into Additive
	FAnimationRuntime::ConvertPoseToAdditive(OutPose, BasePose);
	OutCurve.ConvertToAdditive(BaseCurve);

	FCustomAttributesRuntime::SubtractAttributes(BaseAttributes, OutAttributes);
}

void UAnimSequence::RetargetBoneTransform(FTransform& BoneTransform, const int32 SkeletonBoneIndex, const FCompactPoseBoneIndex& BoneIndex, const FBoneContainer& RequiredBones, const bool bIsBakedAdditive) const
{
	const USkeleton* MySkeleton = GetSkeleton();
	FAnimationRuntime::RetargetBoneTransform(MySkeleton, RetargetSource, BoneTransform, SkeletonBoneIndex, BoneIndex, RequiredBones, bIsBakedAdditive);
}

#if WITH_EDITOR
/** Utility function to crop data from a RawAnimSequenceTrack */
static int32 CropRawTrack(FRawAnimSequenceTrack& RawTrack, int32 StartKey, int32 NumKeys, int32 TotalNumOfFrames)
{
	check(RawTrack.PosKeys.Num() == 1 || RawTrack.PosKeys.Num() == TotalNumOfFrames);
	check(RawTrack.RotKeys.Num() == 1 || RawTrack.RotKeys.Num() == TotalNumOfFrames);
	// scale key can be empty
	check(RawTrack.ScaleKeys.Num() == 0 || RawTrack.ScaleKeys.Num() == 1 || RawTrack.ScaleKeys.Num() == TotalNumOfFrames);

	if( RawTrack.PosKeys.Num() > 1 )
	{
		RawTrack.PosKeys.RemoveAt(StartKey, NumKeys);
		check(RawTrack.PosKeys.Num() > 0);
		RawTrack.PosKeys.Shrink();
	}

	if( RawTrack.RotKeys.Num() > 1 )
	{
		RawTrack.RotKeys.RemoveAt(StartKey, NumKeys);
		check(RawTrack.RotKeys.Num() > 0);
		RawTrack.RotKeys.Shrink();
	}

	if( RawTrack.ScaleKeys.Num() > 1 )
	{
		RawTrack.ScaleKeys.RemoveAt(StartKey, NumKeys);
		check(RawTrack.ScaleKeys.Num() > 0);
		RawTrack.ScaleKeys.Shrink();
	}

	// Update NumFrames below to reflect actual number of keys.
	return FMath::Max<int32>( RawTrack.PosKeys.Num(), FMath::Max<int32>(RawTrack.RotKeys.Num(), RawTrack.ScaleKeys.Num()) );
}

void UAnimSequence::ResizeSequence(float NewLength, int32 NewNumFrames, bool bInsert, int32 StartFrame/*inclusive */, int32 EndFrame/*inclusive*/)
{
	check (NewNumFrames > 0);
	check (StartFrame < EndFrame);

	int32 OldNumFrames = NumFrames;
	float OldSequenceLength = SequenceLength;

	// verify condition
	NumFrames = NewNumFrames;
	// Update sequence length to match new number of frames.
	SequenceLength = NewLength;

	float Interval = OldSequenceLength / OldNumFrames;
	ensure (Interval == SequenceLength/NumFrames);

	float OldStartTime = StartFrame * Interval;
	float OldEndTime = EndFrame * Interval;
	float Duration = OldEndTime - OldStartTime;

	// re-locate notifies
	for (auto& Notify: Notifies)
	{
		float CurrentTime = Notify.GetTime();
		float NewDuration = 0.f;
		if (bInsert)
		{
			// if state, make sure to adjust end time
			if(Notify.NotifyStateClass)
			{
				float NotifyDuration = Notify.GetDuration();
				float NotifyEnd = CurrentTime + NotifyDuration;
				if(NotifyEnd >= OldStartTime)
				{
					NewDuration = NotifyDuration + Duration;
				}
				else
				{
					NewDuration = NotifyDuration;
				}
			}

			// when insert, we only care about start time
			// if it's later than start time
			if (CurrentTime >= OldStartTime)
			{
				CurrentTime += Duration;
			}
		}
		else
		{
			// if state, make sure to adjust end time
			if(Notify.NotifyStateClass)
			{
				float NotifyDuration = Notify.GetDuration();
				float NotifyEnd = CurrentTime + NotifyDuration;
				NewDuration = NotifyDuration;
				if(NotifyEnd >= OldStartTime && NotifyEnd <= OldEndTime)
				{
					// small number @todo see if there is define for this
					NewDuration = 0.1f;
				}
				else if (NotifyEnd > OldEndTime)
				{
					NewDuration = NotifyEnd-Duration-CurrentTime;
				}
				else
				{
					NewDuration = NotifyDuration;
				}

				NewDuration = FMath::Max(NewDuration, 0.1f);
			}

			if (CurrentTime >= OldStartTime && CurrentTime <= OldEndTime)
			{
				CurrentTime = OldStartTime;
			}
			else if (CurrentTime > OldEndTime)
			{
				CurrentTime -= Duration;
			}
		}

		float ClampedCurrentTime = FMath::Clamp(CurrentTime, 0.f, SequenceLength);
		Notify.LinkSequence(this, ClampedCurrentTime);
		Notify.SetDuration(NewDuration);

		if (ClampedCurrentTime == 0.f)
		{
			Notify.TriggerTimeOffset = GetTriggerTimeOffsetForType(EAnimEventTriggerOffsets::OffsetAfter);
		}
		else if (ClampedCurrentTime == SequenceLength)
		{
			Notify.TriggerTimeOffset = GetTriggerTimeOffsetForType(EAnimEventTriggerOffsets::OffsetBefore);
		}
	}

	for (FAnimSyncMarker& Marker : AuthoredSyncMarkers)
	{
		float CurrentTime = Marker.Time;
		if (bInsert)
		{
			// when insert, we only care about start time
			// if it's later than start time
			if (CurrentTime >= OldStartTime)
			{
				CurrentTime += Duration;
			}
		}
		else
		{
			if (CurrentTime >= OldStartTime && CurrentTime <= OldEndTime)
			{
				CurrentTime = OldStartTime;
			}
			else if (CurrentTime > OldEndTime)
			{
				CurrentTime -= Duration;
			}
		}
		Marker.Time = FMath::Clamp(CurrentTime, 0.f, SequenceLength);
	}
	// resize curves
	RawCurveData.Resize(NewLength, bInsert, OldStartTime, OldEndTime);
}

bool UAnimSequence::InsertFramesToRawAnimData( int32 StartFrame, int32 EndFrame, int32 CopyFrame)
{
	// make sure the copyframe is valid and start frame is valid
	int32 NumFramesToInsert = EndFrame-StartFrame;
	if ((CopyFrame>=0 && CopyFrame<NumFrames) && (StartFrame >= 0 && StartFrame <=NumFrames) && NumFramesToInsert > 0)
	{
#if WITH_EDITOR
		FModifyRawDataSourceGuard Modify(this);
#endif

		for (auto& RawData : RawAnimationData)
		{
			if (RawData.PosKeys.Num() > 1 && RawData.PosKeys.IsValidIndex(CopyFrame))
			{
				auto Source = RawData.PosKeys[CopyFrame];
				RawData.PosKeys.InsertZeroed(StartFrame, NumFramesToInsert);
				for (int32 Index=StartFrame; Index<EndFrame; ++Index)
				{
					RawData.PosKeys[Index] = Source;
				}
			}

			if(RawData.RotKeys.Num() > 1 && RawData.RotKeys.IsValidIndex(CopyFrame))
			{
				auto Source = RawData.RotKeys[CopyFrame];
				RawData.RotKeys.InsertZeroed(StartFrame, NumFramesToInsert);
				for(int32 Index=StartFrame; Index<EndFrame; ++Index)
				{
					RawData.RotKeys[Index] = Source;
				}
			}

			if(RawData.ScaleKeys.Num() > 1 && RawData.ScaleKeys.IsValidIndex(CopyFrame))
			{
				auto Source = RawData.ScaleKeys[CopyFrame];
				RawData.ScaleKeys.InsertZeroed(StartFrame, NumFramesToInsert);

				for(int32 Index=StartFrame; Index<EndFrame; ++Index)
				{
					RawData.ScaleKeys[Index] = Source;
				}
			}
		}

		float const FrameTime = SequenceLength / ((float)NumFrames);

		int32 NewNumFrames = NumFrames + NumFramesToInsert;
		ResizeSequence((float)NewNumFrames * FrameTime, NewNumFrames, true, StartFrame, EndFrame);

		UE_LOG(LogAnimation, Log, TEXT("\tSequenceLength: %f, NumFrames: %d"), SequenceLength, NumFrames);
		
		MarkRawDataAsModified();
		MarkPackageDirty();

		return true;
	}

	return false;
}

bool UAnimSequence::CropRawAnimData( float CurrentTime, bool bFromStart )
{
	// Length of one frame.
	float const FrameTime = SequenceLength / ((float)NumFrames);
	// Save Total Number of Frames before crop
	int32 TotalNumOfFrames = NumFrames;

	// if current frame is 1, do not try crop. There is nothing to crop
	if ( NumFrames <= 1 )
	{
		return false;
	}
	
	// If you're end or beginning, you can't cut all nor nothing. 
	// Avoiding ambiguous situation what exactly we would like to cut 
	// Below it clamps range to 1, TotalNumOfFrames-1
	// causing if you were in below position, it will still crop 1 frame. 
	// To be clearer, it seems better if we reject those inputs. 
	// If you're a bit before/after, we assume that you'd like to crop
	if ( CurrentTime == 0.f || CurrentTime == SequenceLength )
	{
		return false;
	}

	// Find the right key to cut at.
	// This assumes that all keys are equally spaced (ie. won't work if we have dropped unimportant frames etc).
	// The reason I'm changing to TotalNumOfFrames is CT/SL = KeyIndexWithFraction/TotalNumOfFrames
	// To play TotalNumOfFrames, it takes SequenceLength. Each key will take SequenceLength/TotalNumOfFrames
	float const KeyIndexWithFraction = (CurrentTime * (float)(TotalNumOfFrames)) / SequenceLength;
	int32 KeyIndex = bFromStart ? FMath::FloorToInt(KeyIndexWithFraction) : FMath::CeilToInt(KeyIndexWithFraction);
	// Ensure KeyIndex is in range.
	KeyIndex = FMath::Clamp<int32>(KeyIndex, 1, TotalNumOfFrames-1); 
	// determine which keys need to be removed.
	int32 const StartKey = bFromStart ? 0 : KeyIndex;
	int32 const NumKeys = bFromStart ? KeyIndex : TotalNumOfFrames - KeyIndex ;

	// Recalculate NumFrames
	int32 NewNumFrames = TotalNumOfFrames - NumKeys;

	UE_LOG(LogAnimation, Log, TEXT("UAnimSequence::CropRawAnimData %s - CurrentTime: %f, bFromStart: %d, TotalNumOfFrames: %d, KeyIndex: %d, StartKey: %d, NumKeys: %d"), *GetName(), CurrentTime, bFromStart, TotalNumOfFrames, KeyIndex, StartKey, NumKeys);

	// Iterate over tracks removing keys from each one.
	for(int32 i=0; i<RawAnimationData.Num(); i++)
	{
		// Update NewNumFrames below to reflect actual number of keys while we crop the anim data
		CropRawTrack(RawAnimationData[i], StartKey, NumKeys, TotalNumOfFrames);
	}

	for (int32 i = 0; i < SourceRawAnimationData.Num(); ++i)
	{
		CropRawTrack(SourceRawAnimationData[i], StartKey, NumKeys, TotalNumOfFrames);
	}

	// Double check that everything is fine
	for(int32 i=0; i<RawAnimationData.Num(); i++)
	{
		FRawAnimSequenceTrack& RawTrack = RawAnimationData[i];
		check(RawTrack.PosKeys.Num() == 1 || RawTrack.PosKeys.Num() == NewNumFrames);
		check(RawTrack.RotKeys.Num() == 1 || RawTrack.RotKeys.Num() == NewNumFrames);
	}

	// Update sequence length to match new number of frames.
	ResizeSequence((float)NewNumFrames * FrameTime, NewNumFrames, false, StartKey, StartKey+NumKeys);

	UE_LOG(LogAnimation, Log, TEXT("\tSequenceLength: %f, NumFrames: %d"), SequenceLength, NumFrames);

	MarkRawDataAsModified();
	OnRawDataChanged();
	MarkPackageDirty();
	return true;
}

bool UAnimSequence::CompressRawAnimData(float MaxPosDiff, float MaxAngleDiff)
{
	if (RawAnimationData.Num() > 0)
	{
		return StaticCompressRawAnimData(RawAnimationData, NumFrames, GetFName(), MaxPosDiff, MaxAngleDiff);
	}
	return false;
}

bool UAnimSequence::CompressRawAnimData()
{
	if (RawAnimationData.Num() > 0)
	{
		return StaticCompressRawAnimData(RawAnimationData, NumFrames, GetFName());
	}
	return false;
}

/** 
 * Flip Rotation W for the RawTrack
 */
void FlipRotationW(FRawAnimSequenceTrack& RawTrack)
{
	int32 TotalNumOfRotKey = RawTrack.RotKeys.Num();

	for (int32 I=0; I<TotalNumOfRotKey; ++I)
	{
		FQuat& RotKey = RawTrack.RotKeys[I];
		RotKey.W *= -1.f;
	}
}


void UAnimSequence::FlipRotationWForNonRoot(USkeletalMesh * SkelMesh)
{
	if (!GetSkeleton())
	{
		return;
	}

	// Now add additive animation to destination.
	for(int32 TrackIdx=0; TrackIdx<TrackToSkeletonMapTable.Num(); TrackIdx++)
	{
		// Figure out which bone this track is mapped to
		const int32 BoneIndex = TrackToSkeletonMapTable[TrackIdx].BoneTreeIndex;
		if ( BoneIndex > 0 )
		{
			FlipRotationW( RawAnimationData[TrackIdx] );

		}
	}

	// Apply compression
	MarkRawDataAsModified();
	OnRawDataChanged();
}
#endif 

#if WITH_EDITOR
bool UAnimSequence::ShouldPerformStripping(const bool bPerformFrameStripping, const bool bPerformStrippingOnOddFramedAnims) const
{
	const bool bShouldPerformFrameStripping = bPerformFrameStripping && bAllowFrameStripping;

	// Can only do stripping on animations that have an even number of frames once the end frame is removed)
	const bool bIsEvenFramed = ((NumFrames - 1) % 2) == 0;
	const bool bIsValidForStripping = bIsEvenFramed || bPerformStrippingOnOddFramedAnims;

	const bool bStripCandidate = (NumFrames > 10) && bIsValidForStripping;

	return bStripCandidate && bShouldPerformFrameStripping;
}

FString UAnimSequence::GetDDCCacheKeySuffix(const bool bPerformStripping) const
{
	return GetAnimSequenceSpecificCacheKeySuffix(*this, bPerformStripping, CompressionErrorThresholdScale);
}
#endif

void UAnimSequence::WaitOnExistingCompression(const bool bWantResults)
{
#if WITH_EDITOR
	check(IsInGameThread());
	if (bCompressionInProgress)
	{
		COOK_STAT(auto Timer = AnimSequenceCookStats::UsageStats.TimeAsyncWait());
		FAsyncCompressedAnimationsManagement::Get().WaitOnExistingCompression(this, bWantResults);
		bCompressionInProgress = false;
		COOK_STAT(Timer.TrackCyclesOnly()); // Need to get hit/miss and size from WaitOnExistingCompression!
	}
#endif
}

void UAnimSequence::RequestAnimCompression(FRequestAnimCompressionParams Params)
{
#if WITH_EDITOR
	check(IsInGameThread());
	USkeleton* CurrentSkeleton = GetSkeleton();
	if (CurrentSkeleton == nullptr)
	{
		bUseRawDataOnly = true;
		return;
	}

	if (FPlatformProperties::RequiresCookedData())
	{
		return;
	}

	WaitOnExistingCompression(false);

	if (BoneCompressionSettings == nullptr || !BoneCompressionSettings->AreSettingsValid())
	{
		BoneCompressionSettings = FAnimationUtils::GetDefaultAnimationBoneCompressionSettings();
	}

	if (CurveCompressionSettings == nullptr || !CurveCompressionSettings->AreSettingsValid())
	{
		CurveCompressionSettings = FAnimationUtils::GetDefaultAnimationCurveCompressionSettings();
	}

	// Make sure all our required dependencies are loaded
	FAnimationUtils::EnsureAnimSequenceLoaded(*this);

	if (!RawDataGuid.IsValid())
	{
		RawDataGuid = GenerateGuidFromRawData();
	}

	bUseRawDataOnly = true;

	check(!bCompressionInProgress);
	bCompressionInProgress = true;

	// Need to make sure this is up to date.
	VerifyCurveNames<FFloatCurve>(*CurrentSkeleton, USkeleton::AnimCurveMappingName, RawCurveData.FloatCurves);
	VerifyTrackMap(CurrentSkeleton);

	Params.CompressContext->GatherPreCompressionStats(GetName(), GetApproxRawSize(), GetApproxCompressedSize());

	const double CompressionStartTime = FPlatformTime::Seconds();

	const bool bPerformStripping = ShouldPerformStripping(Params.bPerformFrameStripping, Params.bPerformFrameStrippingOnOddNumberedFrames);
	const FString AssetDDCKey = GetDDCCacheKeySuffix(bPerformStripping);

	bool bCompressedDataFromDDC = false;

	TArray<uint8> OutData;
	{
		COOK_STAT(auto Timer = AnimSequenceCookStats::UsageStats.TimeSyncWork());

		FDerivedDataAnimationCompression* AnimCompressor = new FDerivedDataAnimationCompression(TEXT("AnimSeq"), AssetDDCKey, Params.CompressContext);

		const FString FinalDDCKey = FDerivedDataCacheInterface::BuildCacheKey(AnimCompressor->GetPluginName(), AnimCompressor->GetVersionString(), *AnimCompressor->GetPluginSpecificCacheKeySuffix());

		// For debugging DDC/Compression issues		
		const bool bSkipDDC = false;

		if (!bSkipDDC && GetDerivedDataCacheRef().GetSynchronous(*FinalDDCKey, OutData, AnimCompressor->GetDebugContextString()))
		{
			COOK_STAT(Timer.AddHit(OutData.Num()));
			bCompressedDataFromDDC = true;
		}
		else
		{
			// Data does not exist, need to build it.
			// Filter RAW data to get rid of mismatched tracks (translation/rotation data with a different number of keys than there are frames)
			// No trivial key removal is done at this point (impossible error metrics of -1), since all of the techniques will perform it themselves
			CompressRawAnimData(-1.0f, -1.0f);

			FCompressibleAnimRef CompressibleData = MakeShared<FCompressibleAnimData, ESPMode::ThreadSafe>(this, bPerformStripping);
			AnimCompressor->SetCompressibleData(CompressibleData);

			if (bSkipDDC || (CompressCommandletVersion == INDEX_NONE))
			{
				AnimCompressor->Build(OutData);
				COOK_STAT(Timer.AddMiss(OutData.Num()));
			}
			else if (AnimCompressor->CanBuild())
			{
				if (Params.bAsyncCompression)
				{
					FAsyncCompressedAnimationsManagement::Get().RequestAsyncCompression(*AnimCompressor, this, bPerformStripping, OutData);
					COOK_STAT(Timer.TrackCyclesOnly());
				}
				else
				{
					bool bBuilt = false;
					const bool bSuccess = GetDerivedDataCacheRef().GetSynchronous(AnimCompressor, OutData, &bBuilt);
					COOK_STAT(Timer.AddHitOrMiss(!bSuccess || bBuilt ? FCookStats::CallStats::EHitOrMiss::Miss : FCookStats::CallStats::EHitOrMiss::Hit, OutData.Num()));
				}
				AnimCompressor = nullptr;
			}
			else
			{
				COOK_STAT(Timer.TrackCyclesOnly());
			}
		}

		if (AnimCompressor)
		{
			// Would really like to do auto mem management but GetDerivedDataCacheRef().GetSynchronous expects a pointer it can delete
			delete AnimCompressor;
			AnimCompressor = nullptr;
		}
	}

	if (OutData.Num() > 0) // Haven't async compressed
	{
		ApplyCompressedData(OutData);

		if(bCompressedDataFromDDC)
		{
			const double CompressionEndTime = FPlatformTime::Seconds();
			const double CompressionTime = CompressionEndTime - CompressionStartTime;

			TArray<FBoneData> BoneData;
			FAnimationUtils::BuildSkeletonMetaData(GetSkeleton(), BoneData);
			Params.CompressContext->GatherPostCompressionStats(CompressedData, BoneData, GetFName(), CompressionTime, false);
		}

	}
#endif
}

#if WITH_EDITOR
void UAnimSequence::ApplyCompressedData(const FString& DataCacheKeySuffix, const bool bPerformFrameStripping, const TArray<uint8>& Data)
{
	if (GetDDCCacheKeySuffix(bPerformFrameStripping) == DataCacheKeySuffix)
	{
		ApplyCompressedData(Data);
	}
	else
	{
		bCompressionInProgress = false;
	}
}
#endif

void UAnimSequence::ApplyCompressedData(const TArray<uint8>& Data)
{
#if WITH_EDITOR
	bCompressionInProgress = false;
	
	SynchronousCustomAttributesCompression();
#endif
	if(Data.Num() > 0)
	{
		FMemoryReader MemAr(Data);
		SerializeCompressedData(MemAr, true);
		//This is only safe during sync anim compression
		SetSkeletonVirtualBoneGuid(GetSkeleton()->GetVirtualBoneGuid());
		bUseRawDataOnly = false;
	}
}

void UAnimSequence::SerializeCompressedData(FArchive& Ar, bool bDDCData)
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		CompressedData.SerializeCompressedData(Ar, bDDCData, this, this->GetSkeleton(), BoneCompressionSettings, CurveCompressionSettings);
	}
}

#if WITH_EDITOR

bool UAnimSequence::CanBakeAdditive() const
{
	return	(NumFrames > 0) &&
			IsValidAdditive() &&
			GetSkeleton();
}

bool UAnimSequence::DoesSequenceContainZeroScale()
{
	for (const FRawAnimSequenceTrack& RawTrack : RawAnimationData)
	{
		for (const FVector& ScaleKey : RawTrack.ScaleKeys)
		{
			if (ScaleKey.IsZero())
			{
				return true;
			}
		}
	}

	return false;
}

FGuid UAnimSequence::GenerateGuidFromRawData() const
{
	return GenerateGuidFromRawAnimData(RawAnimationData, RawCurveData);

}

void CopyTransformToRawAnimationData(const FTransform& BoneTransform, FRawAnimSequenceTrack& Track, int32 Frame)
{
	Track.PosKeys[Frame] = BoneTransform.GetTranslation();
	Track.RotKeys[Frame] = BoneTransform.GetRotation();
	Track.RotKeys[Frame].Normalize();
	Track.ScaleKeys[Frame] = BoneTransform.GetScale3D();
}

struct FByFramePoseEvalContext
{
public:
	FBoneContainer RequiredBones;

	// Length of one frame.
	const float IntervalTime;

	TArray<FBoneIndexType> RequiredBoneIndexArray;

	FByFramePoseEvalContext(const UAnimSequence* InAnimToEval)
		: FByFramePoseEvalContext(InAnimToEval->SequenceLength, InAnimToEval->GetRawNumberOfFrames(), InAnimToEval->GetSkeleton())
	{}
		
	FByFramePoseEvalContext(float InSequenceLength, int32 InRawNumOfFrames, USkeleton* InSkeleton)
		: IntervalTime(InSequenceLength / ((float)FMath::Max(InRawNumOfFrames - 1, 1)))
	{
		// Initialize RequiredBones for pose evaluation
		RequiredBones.SetUseRAWData(true);

		check(InSkeleton);

		RequiredBoneIndexArray.AddUninitialized(InSkeleton->GetReferenceSkeleton().GetNum());
		for (int32 BoneIndex = 0; BoneIndex < RequiredBoneIndexArray.Num(); ++BoneIndex)
		{
			RequiredBoneIndexArray[BoneIndex] = BoneIndex;
		}

		RequiredBones.InitializeTo(RequiredBoneIndexArray, FCurveEvaluationOption(true), *InSkeleton);
	}

};

void UAnimSequence::BakeOutVirtualBoneTracks(TArray<FRawAnimSequenceTrack>& NewRawTracks, TArray<FName>& NewAnimationTrackNames, TArray<FTrackToSkeletonMap>& NewTrackToSkeletonMapTable)
{
	const int32 NumVirtualBones = GetSkeleton()->GetVirtualBones().Num();
	check( (RawAnimationData.Num() == TrackToSkeletonMapTable.Num()) && (RawAnimationData.Num() == AnimationTrackNames.Num()) ); //Make sure starting data is valid

	NewRawTracks = TArray<FRawAnimSequenceTrack>(RawAnimationData, NumVirtualBones);

	NewTrackToSkeletonMapTable = TArray<FTrackToSkeletonMap>(TrackToSkeletonMapTable, NumVirtualBones);

	NewAnimationTrackNames = TArray<FName>(AnimationTrackNames, NumVirtualBones);

	for (int32 VBIndex = 0; VBIndex < NumVirtualBones; ++VBIndex)
	{
		const int32 TrackIndex = NewRawTracks.Add(FRawAnimSequenceTrack());

		//Init new tracks
		NewRawTracks[TrackIndex].PosKeys.SetNumUninitialized(NumFrames);
		NewRawTracks[TrackIndex].RotKeys.SetNumUninitialized(NumFrames);
		NewRawTracks[TrackIndex].ScaleKeys.SetNumUninitialized(NumFrames);
		
		NewTrackToSkeletonMapTable.Add(FTrackToSkeletonMap(GetSkeleton()->GetReferenceSkeleton().GetRequiredVirtualBones()[VBIndex]));
		NewAnimationTrackNames.Add(GetSkeleton()->GetVirtualBones()[VBIndex].VirtualBoneName);
	}

	FMemMark Mark(FMemStack::Get());
	FByFramePoseEvalContext EvalContext(this);

	//Pose evaluation data
	FCompactPose Pose;
	Pose.SetBoneContainer(&EvalContext.RequiredBones);

	FAnimExtractContext ExtractContext;

	const TArray<FVirtualBoneRefData>& VBRefData = GetSkeleton()->GetReferenceSkeleton().GetVirtualBoneRefData();

	for (int Frame = 0; Frame < NumFrames; ++Frame)
	{
		// Initialise curve data from Skeleton
		FBlendedCurve Curve;
		Curve.InitFrom(EvalContext.RequiredBones);

		//Grab pose for this frame
		const float CurrentFrameTime = Frame * EvalContext.IntervalTime;
		ExtractContext.CurrentTime = CurrentFrameTime;

		FStackCustomAttributes TempAttributes;
		FAnimationPoseData AnimPoseData(Pose, Curve, TempAttributes);
		GetAnimationPose(AnimPoseData, ExtractContext);

		for (int32 VBIndex = 0; VBIndex < VBRefData.Num(); ++VBIndex)
		{
			const FVirtualBoneRefData& VB = VBRefData[VBIndex];
			CopyTransformToRawAnimationData(Pose[FCompactPoseBoneIndex(VB.VBRefSkelIndex)], NewRawTracks[VBIndex + RawAnimationData.Num()], Frame);
		}
	}

	StaticCompressRawAnimData(NewRawTracks, NumFrames, GetFName());
}

bool IsIdentity(const FVector& Pos)
{
	return Pos.Equals(FVector::ZeroVector);
}

bool IsIdentity(const FQuat& Rot)
{
	return Rot.Equals(FQuat::Identity);
}

template<class KeyType>
bool IsKeyArrayValidForRemoval(const TArray<KeyType>& Keys)
{
	return Keys.Num() == 0 || (Keys.Num() == 1 && IsIdentity(Keys[0]));
}

bool IsRawTrackValidForRemoval(const FRawAnimSequenceTrack& Track)
{
	return	IsKeyArrayValidForRemoval(Track.PosKeys) &&
			IsKeyArrayValidForRemoval(Track.RotKeys) &&
			IsKeyArrayValidForRemoval(Track.ScaleKeys);
}

void UAnimSequence::TestEvalauteAnimation() const
{
	FMemMark Mark(FMemStack::Get());
	FByFramePoseEvalContext EvalContext(this);
	EvalContext.RequiredBones.SetUseRAWData(false);

	FCompactPose Pose;
	Pose.SetBoneContainer(&EvalContext.RequiredBones);

	FAnimExtractContext ExtractContext;

	for (int Frame = 0; Frame < NumFrames; ++Frame)
	{
		// Initialise curve data from Skeleton
		FBlendedCurve Curve;
		Curve.InitFrom(EvalContext.RequiredBones);

		//Grab pose for this frame
		const float CurrentFrameTime = Frame * EvalContext.IntervalTime;
		ExtractContext.CurrentTime = CurrentFrameTime;

		FStackCustomAttributes TempAttributes;
		FAnimationPoseData AnimPoseData(Pose, Curve, TempAttributes);
		GetAnimationPose(AnimPoseData, ExtractContext);
	}
}

FFloatCurve* GetFloatCurve(FRawCurveTracks& RawCurveTracks, USkeleton::AnimCurveUID& CurveUID)
{
	return static_cast<FFloatCurve *>(RawCurveTracks.GetCurveData(CurveUID, ERawCurveTrackTypes::RCT_Float));
}

bool IsNewKeyDifferent(const FRichCurveKey& LastKey, float NewValue)
{
	return LastKey.Value != NewValue;
}

void UAnimSequence::BakeOutAdditiveIntoRawData(TArray<FRawAnimSequenceTrack>& NewRawTracks, TArray<FName>& NewAnimationTrackNames, TArray<FTrackToSkeletonMap>& NewTrackToSkeletonMapTable, FRawCurveTracks& NewCurveTracks, TArray<FRawAnimSequenceTrack>& AdditiveBaseAnimationData)
{
	if (!CanBakeAdditive())
	{
		return; // Nothing to do
	}

	USkeleton* MySkeleton = GetSkeleton();
	check(MySkeleton);

	if (RefPoseSeq && RefPoseSeq->HasAnyFlags(EObjectFlags::RF_NeedPostLoad))
	{
		RefPoseSeq->VerifyCurveNames<FFloatCurve>(*MySkeleton, USkeleton::AnimCurveMappingName, RefPoseSeq->RawCurveData.FloatCurves);
	}

	FMemMark Mark(FMemStack::Get());

	FByFramePoseEvalContext EvalContext(this);

	NewRawTracks.Reset(EvalContext.RequiredBoneIndexArray.Num());
	NewRawTracks.SetNum(EvalContext.RequiredBoneIndexArray.Num());

	for (FRawAnimSequenceTrack& RawTrack : NewRawTracks)
	{
		RawTrack.PosKeys.SetNumUninitialized(NumFrames);
		RawTrack.RotKeys.SetNumUninitialized(NumFrames);
		RawTrack.ScaleKeys.SetNumUninitialized(NumFrames);
	}

	// keep the same buffer size
	AdditiveBaseAnimationData = NewRawTracks;

	NewTrackToSkeletonMapTable.Reset(EvalContext.RequiredBoneIndexArray.Num());
	NewTrackToSkeletonMapTable.SetNumUninitialized(EvalContext.RequiredBoneIndexArray.Num());

	NewAnimationTrackNames.SetNumUninitialized(EvalContext.RequiredBoneIndexArray.Num());
	NewAnimationTrackNames.SetNumUninitialized(EvalContext.RequiredBoneIndexArray.Num());

	for (int32 TrackIndex = 0; TrackIndex < EvalContext.RequiredBoneIndexArray.Num(); ++TrackIndex)
	{
		NewTrackToSkeletonMapTable[TrackIndex].BoneTreeIndex = TrackIndex;
		NewAnimationTrackNames[TrackIndex] = GetSkeleton()->GetReferenceSkeleton().GetBoneName(TrackIndex);
	}

	//Pose evaluation data
	FCompactPose Pose;
	Pose.SetBoneContainer(&EvalContext.RequiredBones);
	FCompactPose BasePose;
	BasePose.SetBoneContainer(&EvalContext.RequiredBones);

	FAnimExtractContext ExtractContext;

	for (int Frame = 0; Frame < NumFrames; ++Frame)
	{
		// Initialise curve data from Skeleton
		FBlendedCurve Curve;
		Curve.InitFrom(EvalContext.RequiredBones);

		FBlendedCurve DummyBaseCurve;
		DummyBaseCurve.InitFrom(EvalContext.RequiredBones);

		//Grab pose for this frame
		const float PreviousFrameTime = (Frame - 1) * EvalContext.IntervalTime;
		const float CurrentFrameTime = Frame * EvalContext.IntervalTime;
		ExtractContext.CurrentTime = CurrentFrameTime;

		FStackCustomAttributes BaseAttributes;
		FAnimationPoseData AnimPoseData(Pose, Curve, BaseAttributes);
		GetAnimationPose(AnimPoseData, ExtractContext);

		FStackCustomAttributes AdditiveAttributes;
		FAnimationPoseData AnimBasePoseData(BasePose, DummyBaseCurve, AdditiveAttributes);
		GetAdditiveBasePose(AnimBasePoseData, ExtractContext);

		//Write out every track for this frame
		for (FCompactPoseBoneIndex TrackIndex(0); TrackIndex < NewRawTracks.Num(); ++TrackIndex)
		{
			CopyTransformToRawAnimationData(Pose[TrackIndex], NewRawTracks[TrackIndex.GetInt()], Frame);
			CopyTransformToRawAnimationData(BasePose[TrackIndex], AdditiveBaseAnimationData[TrackIndex.GetInt()], Frame);
		}

		//Write out curve data for this frame
		for (int32 CurveIndex = 0; CurveIndex < Curve.UIDToArrayIndexLUT->Num(); ++CurveIndex)
		{
			USkeleton::AnimCurveUID CurveUID = (USkeleton::AnimCurveUID) CurveIndex;
			int32 ArrayIndex = Curve.GetArrayIndexByUID(CurveUID);
			if (ArrayIndex != INDEX_NONE)
			{
				float CurveWeight = Curve.CurveWeights[ArrayIndex];
				FFloatCurve* RawCurve = GetFloatCurve(NewCurveTracks, CurveUID);
				if (!RawCurve && CurveWeight != 0.f) //Only make a new curve if we are going to give it data
				{
					FSmartName NewCurveName;
					// if we don't have name, there is something wrong here. 
					ensureAlways(MySkeleton->GetSmartNameByUID(USkeleton::AnimCurveMappingName, CurveUID, NewCurveName));
					// curve flags don't matter much for compressed curves
					NewCurveTracks.AddCurveData(NewCurveName, 0, ERawCurveTrackTypes::RCT_Float);
					RawCurve = GetFloatCurve(NewCurveTracks, CurveUID);
				}

				if (RawCurve)
				{
					const bool bHasKeys = RawCurve->FloatCurve.GetNumKeys() > 0;
					if (!bHasKeys)
					{
						//Add pre key of 0
						if (Frame > 0)
						{
							const float PreKeyTime = (Frame - 1)*EvalContext.IntervalTime;
							RawCurve->UpdateOrAddKey(0.f, PreKeyTime);
						}

					}

					if (!bHasKeys || IsNewKeyDifferent(RawCurve->FloatCurve.GetLastKey(), CurveWeight))
					{
						RawCurve->UpdateOrAddKey(CurveWeight, CurrentFrameTime);
						TArray<FRichCurveKey>& CurveKeys = RawCurve->FloatCurve.Keys;
						if (CurveKeys.Num() > 1)
						{
							FRichCurveKey& PrevKey = CurveKeys.Last(1);
							if (PrevKey.Time < (PreviousFrameTime - SMALL_NUMBER)) // Did we skip a frame, if so need to make previous key const
							{
								PrevKey.InterpMode = RCIM_Constant;
							}
						}
					}
				}
			}
		}
	}

	const FSmartNameMapping* Mapping = GetSkeleton()->GetSmartNameContainer(USkeleton::AnimCurveMappingName);
	check(Mapping); // Should always exist
	NewCurveTracks.RefreshName(Mapping);

#if 0 //Validate baked data
	for (FRawAnimSequenceTrack& RawTrack : NewRawTracks)
	{
		for (FQuat& Rot : RawTrack.RotKeys)
		{
			check(Rot.IsNormalized());
		}
	}
#endif

	StaticCompressRawAnimData(NewRawTracks,NumFrames, GetFName());

	// Note on (TrackIndex > 0) below : deliberately stop before track 0, compression code doesn't like getting a completely empty animation
	for (int32 TrackIndex = NewRawTracks.Num() - 1; TrackIndex > 0; --TrackIndex)
	{
		const FRawAnimSequenceTrack& Track = NewRawTracks[TrackIndex];
		if (IsRawTrackValidForRemoval(Track))
		{
			NewRawTracks.RemoveAtSwap(TrackIndex, 1, false);
			NewAnimationTrackNames.RemoveAtSwap(TrackIndex, 1, false);
			NewTrackToSkeletonMapTable.RemoveAtSwap(TrackIndex, 1, false);
		}
	}

}

void UAnimSequence::FlagDependentAnimationsAsRawDataOnly() const
{
	for (TObjectIterator<UAnimSequence> Iter; Iter; ++Iter)
	{
		UAnimSequence* Seq = *Iter;
		if (Seq->RefPoseSeq == this)
		{
			Seq->bUseRawDataOnly = true;
		}
	}
}

void UAnimSequence::UpdateDependentStreamingAnimations() const
{
	for (TObjectIterator<UAnimStreamable> Iter; Iter; ++Iter)
	{
		UAnimStreamable* Seq = *Iter;
		if (Seq->SourceSequence == this)
		{
			Seq->InitFrom(this);
		}
	}
}

#endif

void UAnimSequence::RecycleAnimSequence()
{
#if WITH_EDITORONLY_DATA
	// Clear RawAnimData
	RawAnimationData.Empty();
	RawDataGuid.Invalidate();
	AnimationTrackNames.Empty();
	TrackToSkeletonMapTable.Empty();
	SourceRawAnimationData.Empty(0);
	RawCurveData.Empty();

	ClearCompressedBoneData();
	ClearCompressedCurveData();

	AuthoredSyncMarkers.Empty();
	UniqueMarkerNames.Empty();
	Notifies.Empty();
	AnimNotifyTracks.Empty();
#endif // WITH_EDITORONLY_DATA
}

#if WITH_EDITOR
void UAnimSequence::CleanAnimSequenceForImport()
{
	// Clear RawAnimData
	RawAnimationData.Empty();
	RawDataGuid.Invalidate();
	AnimationTrackNames.Empty();
	TrackToSkeletonMapTable.Empty();
	ClearCompressedBoneData();
	ClearCompressedCurveData();
	SourceRawAnimationData.Empty(0);
}
#endif // WITH_EDITOR


bool UAnimSequence::CopyNotifies(UAnimSequence* SourceAnimSeq, UAnimSequence* DestAnimSeq, bool bShowDialogs /*= true */)
{
#if WITH_EDITOR
	// Abort if source == destination.
	if( SourceAnimSeq == DestAnimSeq )
	{
		return true;
	}

	// If the destination sequence is shorter than the source sequence, we'll be dropping notifies that
	// occur at later times than the dest sequence is long.  Give the user a chance to abort if we
	// find any notifies that won't be copied over.
	if(bShowDialogs && DestAnimSeq->SequenceLength < SourceAnimSeq->SequenceLength)
	{
		for(int32 NotifyIndex=0; NotifyIndex<SourceAnimSeq->Notifies.Num(); ++NotifyIndex)
		{
			// If a notify is found which occurs off the end of the destination sequence, prompt the user to continue.
			const FAnimNotifyEvent& SrcNotifyEvent = SourceAnimSeq->Notifies[NotifyIndex];
			if( SrcNotifyEvent.GetTriggerTime() > DestAnimSeq->SequenceLength )
			{
				const bool bProceed = EAppReturnType::Yes == FMessageDialog::Open( EAppMsgType::YesNo, NSLOCTEXT("UnrealEd", "SomeNotifiesWillNotBeCopiedQ", "Some notifies will not be copied because the destination sequence is not long enough.  Proceed?") );
				if( !bProceed )
				{
					return false;
				}
				else
				{
					break;
				}
			}
		}
	}

	// If the destination sequence contains any notifies, ask the user if they'd like
	// to delete the existing notifies before copying over from the source sequence.
	if(bShowDialogs && DestAnimSeq->Notifies.Num() > 0)
	{
		const bool bDeleteExistingNotifies = EAppReturnType::Yes == FMessageDialog::Open(EAppMsgType::YesNo, FText::Format(
			NSLOCTEXT("UnrealEd", "DestSeqAlreadyContainsNotifiesMergeQ", "The destination sequence already contains {0} notifies.  Delete these before copying?"), FText::AsNumber(DestAnimSeq->Notifies.Num())) );
		if( bDeleteExistingNotifies )
		{
			DestAnimSeq->Notifies.Empty();
			DestAnimSeq->MarkPackageDirty();
		}
	}

	// Do the copy.
	int32 NumNotifiesThatWereNotCopied = 0;

	for(int32 NotifyIndex=0; NotifyIndex<SourceAnimSeq->Notifies.Num(); ++NotifyIndex)
	{
		const FAnimNotifyEvent& SrcNotifyEvent = SourceAnimSeq->Notifies[NotifyIndex];

		// Skip notifies which occur at times later than the destination sequence is long.
		if( SrcNotifyEvent.GetTriggerTime() > DestAnimSeq->SequenceLength )
		{
			++NumNotifiesThatWereNotCopied;
			continue;
		}

		// Copy notify tracks from src to dest if they are missing
		if (SrcNotifyEvent.TrackIndex >= DestAnimSeq->AnimNotifyTracks.Num())
		{
			for (int32 TrackIndex = DestAnimSeq->AnimNotifyTracks.Num(); TrackIndex <= SrcNotifyEvent.TrackIndex; ++TrackIndex)
			{
				DestAnimSeq->AnimNotifyTracks.Add(FAnimNotifyTrack(SourceAnimSeq->AnimNotifyTracks[TrackIndex].TrackName, SourceAnimSeq->AnimNotifyTracks[TrackIndex].TrackColor));
			}
		}

		// Track the location of the new notify.
		int32 NewNotifyIndex = DestAnimSeq->Notifies.AddDefaulted();
		FAnimNotifyEvent& NotifyEvent = DestAnimSeq->Notifies[NewNotifyIndex];
		
		// Copy properties of the NotifyEvent
		NotifyEvent.TrackIndex = SrcNotifyEvent.TrackIndex;
		NotifyEvent.NotifyName = SrcNotifyEvent.NotifyName;
		NotifyEvent.Duration = SrcNotifyEvent.Duration;

		// Copy the notify itself, and point the new one at it.
		if( SrcNotifyEvent.Notify )
		{
			DestAnimSeq->Notifies[NewNotifyIndex].Notify = static_cast<UAnimNotify*>(StaticDuplicateObject(SrcNotifyEvent.Notify, DestAnimSeq, NAME_None, RF_AllFlags, nullptr, EDuplicateMode::Normal, ~EInternalObjectFlags::RootSet));
		}
		else
		{
			DestAnimSeq->Notifies[NewNotifyIndex].Notify = nullptr;
		}

		if( SrcNotifyEvent.NotifyStateClass )
		{
			DestAnimSeq->Notifies[NewNotifyIndex].NotifyStateClass = static_cast<UAnimNotifyState*>(StaticDuplicateObject(SrcNotifyEvent.NotifyStateClass, DestAnimSeq, NAME_None, RF_AllFlags, nullptr, EDuplicateMode::Normal, ~EInternalObjectFlags::RootSet));
		}
		else
		{
			DestAnimSeq->Notifies[NewNotifyIndex].NotifyStateClass = nullptr;
		}
		
		// Copy notify timing
		NotifyEvent.LinkSequence(DestAnimSeq, SrcNotifyEvent.GetTriggerTime());
		NotifyEvent.TriggerTimeOffset = GetTriggerTimeOffsetForType(DestAnimSeq->CalculateOffsetForNotify(NotifyEvent.GetTriggerTime()));

		// Make sure editor knows we've changed something.
		DestAnimSeq->MarkPackageDirty();
		DestAnimSeq->RefreshCacheData();
	}

	// Inform the user if some notifies weren't copied.
	if(bShowDialogs && NumNotifiesThatWereNotCopied > 0)
	{
		FMessageDialog::Open( EAppMsgType::Ok, FText::Format(
			NSLOCTEXT("UnrealEd", "SomeNotifiesWereNotCopiedF", "Because the destination sequence was shorter, {0} notifies were not copied."), FText::AsNumber(NumNotifiesThatWereNotCopied)) );
	}
#endif // WITH_EDITOR

	return true;
}

bool UAnimSequence::IsValidAdditive() const		
{ 
	if (AdditiveAnimType != AAT_None)
	{
		switch (RefPoseType)
		{
		case ABPT_RefPose:
			return true;
		case ABPT_AnimScaled:
			return (RefPoseSeq != NULL);
		case ABPT_AnimFrame:
			return (RefPoseSeq != NULL) && (RefFrameIndex >= 0);
		default:
			return false;
		}
	}

	return false;
}

#if WITH_EDITOR

int32 FindMeshBoneIndexFromBoneName(USkeleton * Skeleton, const FName &BoneName)
{
	USkeletalMesh * PreviewMesh = Skeleton->GetPreviewMesh();
	const int32& SkeletonBoneIndex = Skeleton->GetReferenceSkeleton().FindBoneIndex(BoneName);

	int32 BoneIndex = INDEX_NONE;

	if(SkeletonBoneIndex != INDEX_NONE)
	{
		BoneIndex = Skeleton->GetMeshBoneIndexFromSkeletonBoneIndex(PreviewMesh, SkeletonBoneIndex);
	}

	return BoneIndex;
}
void FillUpTransformBasedOnRig(USkeleton* Skeleton, TArray<FTransform>& NodeSpaceBases, TArray<FTransform> &Rotations, TArray<FVector>& Translations, TArray<bool>& TranslationParentFlags)
{
	TArray<FTransform> SpaceBases;
	FAnimationRuntime::FillUpComponentSpaceTransformsRetargetBasePose(Skeleton, SpaceBases);

	const URig* Rig = Skeleton->GetRig();

	if (Rig)
	{
		// this one has to collect all Nodes in Rig data
		// since we're comparing two of them together. 
		int32 NodeNum = Rig->GetNodeNum();

		if (NodeNum > 0)
		{
			NodeSpaceBases.Empty(NodeNum);
			NodeSpaceBases.AddUninitialized(NodeNum);

			Rotations.Empty(NodeNum);
			Rotations.AddUninitialized(NodeNum);

			Translations.Empty(NodeNum);
			Translations.AddUninitialized(NodeNum);

			TranslationParentFlags.Empty(Translations.Num());
			TranslationParentFlags.AddZeroed(Translations.Num());

			const USkeletalMesh* PreviewMesh = Skeleton->GetPreviewMesh();

			for (int32 Index = 0; Index < NodeNum; ++Index)
			{
				const FName NodeName = Rig->GetNodeName(Index);
				const FName& BoneName = Skeleton->GetRigBoneMapping(NodeName);
				const int32& BoneIndex = FindMeshBoneIndexFromBoneName(Skeleton, BoneName);

				if (BoneIndex == INDEX_NONE)
				{
					// add identity
					NodeSpaceBases[Index].SetIdentity();
					Rotations[Index].SetIdentity();
					Translations[Index] = FVector::ZeroVector;
				}
				else
				{
					// initialize with SpaceBases - assuming World Based
					NodeSpaceBases[Index] = SpaceBases[BoneIndex];
					Rotations[Index] = SpaceBases[BoneIndex];
					Translations[Index] = SpaceBases[BoneIndex].GetLocation();

					const FTransformBase* TransformBase = Rig->GetTransformBaseByNodeName(NodeName);

					if (TransformBase != NULL)
					{
						// orientation constraint			
						const auto& RotConstraint = TransformBase->Constraints[EControlConstraint::Type::Orientation];

						if (RotConstraint.TransformConstraints.Num() > 0)
						{
							const FName& ParentBoneName = Skeleton->GetRigBoneMapping(RotConstraint.TransformConstraints[0].ParentSpace);
							const int32& ParentBoneIndex = FindMeshBoneIndexFromBoneName(Skeleton, ParentBoneName);

							if (ParentBoneIndex != INDEX_NONE)
							{
								Rotations[Index] = SpaceBases[BoneIndex].GetRelativeTransform(SpaceBases[ParentBoneIndex]);
							}
						}

						// translation constraint
						const auto& TransConstraint = TransformBase->Constraints[EControlConstraint::Type::Translation];

						if (TransConstraint.TransformConstraints.Num() > 0)
						{
							const FName& ParentBoneName = Skeleton->GetRigBoneMapping(TransConstraint.TransformConstraints[0].ParentSpace);
							const int32& ParentBoneIndex = FindMeshBoneIndexFromBoneName(Skeleton, ParentBoneName);

							if (ParentBoneIndex != INDEX_NONE)
							{
								// I think translation has to include rotation, otherwise it won't work
								Translations[Index] = SpaceBases[BoneIndex].GetLocation() - SpaceBases[ParentBoneIndex].GetLocation();
								TranslationParentFlags[Index] = true;
							}
						}
					}
				}
			}
		}
	}
}

int32 FindValidTransformParentTrack(const URig* Rig, int32 NodeIndex, bool bTranslate, const TArray<FName>& ValidNodeNames)
{
	int32 ParentIndex = Rig->FindTransformParentNode(NodeIndex, bTranslate);

	// verify if it exists in ValidNodeNames
	if (ParentIndex != INDEX_NONE)
	{
		FName NodeName = Rig->GetNodeName(ParentIndex);

		return ValidNodeNames.Find(NodeName);
	}

	return INDEX_NONE;
}


void UAnimSequence::RemapTracksToNewSkeleton( USkeleton* NewSkeleton, bool bConvertSpaces )
{
	// Verifying that bone (names) for attribute data exist on new skeleton
	if(PerBoneCustomAttributeData.Num())
	{
		USkeleton* OldSkeleton = GetSkeleton();
		for (FCustomAttributePerBoneData& AttributeData : PerBoneCustomAttributeData)
		{
			const FName BoneName = OldSkeleton ? OldSkeleton->GetReferenceSkeleton().GetBoneName(AttributeData.BoneTreeIndex) : NAME_None;
			AttributeData.BoneTreeIndex = NewSkeleton ? NewSkeleton->GetReferenceSkeleton().FindBoneIndex(BoneName) : INDEX_NONE;
		}
		PerBoneCustomAttributeData.RemoveAll([](FCustomAttributePerBoneData& AttributeData) { return AttributeData.BoneTreeIndex == INDEX_NONE; });
		CustomAttributesGuid = FGuid::NewGuid();
	}
	
	// this is not cheap, so make sure it only happens in editor

	// @Todo : currently additive will work fine since we don't bake anything except when we extract
	// but in the future if we bake this can be problem
	if (bConvertSpaces)
	{
		USkeleton* OldSkeleton = GetSkeleton();

		// first check if both has same rig, if so, we'll retarget using it
		if (OldSkeleton && OldSkeleton->GetRig() != NULL && NewSkeleton->GetRig() == OldSkeleton->GetRig() && OldSkeleton->GetPreviewMesh() && NewSkeleton->GetPreviewMesh())
		{
			const URig* Rig = OldSkeleton->GetRig();

			// we'll have to save the relative space bases transform from old ref pose to new refpose
			TArray<FTransform> RelativeToNewSpaceBases;
			// save the ratio of translation change
			TArray<float> OldToNewTranslationRatio;
			// create relative transform in component space between old skeleton and new skeleton
			{
				// first calculate component space ref pose to get the relative transform between
				// two ref poses. It is very important update ref pose before getting here. 
				TArray<FTransform> NewRotations, OldRotations, NewSpaceBases, OldSpaceBases;
				TArray<FVector> NewTranslations, OldTranslations;
				TArray<bool> NewTranslationParentFlags, OldTranslationParentFlags;
				// get the spacebases transform
				FillUpTransformBasedOnRig(NewSkeleton, NewSpaceBases, NewRotations, NewTranslations, NewTranslationParentFlags);
				FillUpTransformBasedOnRig(OldSkeleton, OldSpaceBases, OldRotations, OldTranslations, OldTranslationParentFlags);

				// now we'd like to get the relative transform from old to new ref pose in component space
				// PK2*K2 = PK1*K1*theta where theta => P1*R1*theta = P2*R2 
				// where	P1 - parent transform in component space for original skeleton
				//			R1 - local space of the current bone for original skeleton
				//			P2 - parent transform in component space for new skeleton
				//			R2 - local space of the current bone for new skeleton
				// what we're looking for is theta, so that we can apply that to animated transform
				// this has to have all of nodes since comparing two skeletons, that might have different configuration
				int32 NumNodes = Rig->GetNodeNum();
				// saves the theta data per node
				RelativeToNewSpaceBases.AddUninitialized(NumNodes);
				// saves the translation conversion datao
				OldToNewTranslationRatio.AddUninitialized(NumNodes);

				const TArray<FNode>& Nodes = Rig->GetNodes();
				// calculate the relative transform to new skeleton
				// so that we can apply the delta in component space
				for (int32 NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
				{
					// theta (RelativeToNewTransform) = (P1*R1)^(-1) * P2*R2 where theta => P1*R1*theta = P2*R2
					RelativeToNewSpaceBases[NodeIndex] = NewSpaceBases[NodeIndex].GetRelativeTransform(OldSpaceBases[NodeIndex]); 

					// also savees the translation difference between old to new
					FVector OldTranslation = OldTranslations[NodeIndex];
					FVector NewTranslation = NewTranslations[NodeIndex];

					// skip root because we don't really have clear relative point to test with it
					if (NodeIndex != 0 && NewTranslationParentFlags[NodeIndex] == OldTranslationParentFlags[NodeIndex])
					{
						// only do this if parent status matches, otherwise, you'll have invalid state 
						// where one is based on shoulder, where the other is missing the shoulder node
						float OldTranslationSize = OldTranslation.Size();
						float NewTranslationSize = NewTranslation.Size();

						OldToNewTranslationRatio[NodeIndex] = (FMath::IsNearlyZero(OldTranslationSize)) ? 1.f/*do not touch new translation size*/ : NewTranslationSize / OldTranslationSize;
					}
					else
					{
						OldToNewTranslationRatio[NodeIndex] = 1.f; // set to be 1, we don't know what it is
					}

					UE_LOG(LogAnimation, Verbose, TEXT("Retargeting (%s : %d) : OldtoNewTranslationRatio (%0.2f), Relative Transform (%s)"), *Nodes[NodeIndex].Name.ToString(), NodeIndex, 
						OldToNewTranslationRatio[NodeIndex], *RelativeToNewSpaceBases[NodeIndex].ToString());
					UE_LOG(LogAnimation, Verbose, TEXT("\tOldSpaceBase(%s), NewSpaceBase(%s)"), *OldSpaceBases[NodeIndex].ToString(), *NewSpaceBases[NodeIndex].ToString());
				}
			}

			FAnimSequenceTrackContainer RiggingAnimationData;

			// now convert animation data to rig data
			ConvertAnimationDataToRiggingData(RiggingAnimationData);

			// here we have to watch out the index
			// The RiggingAnimationData will contain only the nodes that are mapped to source skeleton
			// and here we convert everything that is in RiggingAnimationData which means based on source data
			// when mapped back to new skeleton, it will discard results that are not mapped to target skeleton
			
			TArray<FName> SrcValidNodeNames;
			int32 SrcNumTracks = OldSkeleton->GetMappedValidNodes(SrcValidNodeNames);

			// now convert to space bases animation 
			TArray< TArray<FTransform> > ComponentSpaceAnimations, ConvertedLocalSpaceAnimations, ConvertedSpaceAnimations;
			ComponentSpaceAnimations.AddZeroed(SrcNumTracks);
			ConvertedSpaceAnimations.AddZeroed(SrcNumTracks);
			ConvertedLocalSpaceAnimations.AddZeroed(SrcNumTracks);

			int32 NumKeys = NumFrames;
			float Interval = GetIntervalPerKey(NumFrames, SequenceLength);

			// allocate arrays
			for(int32 SrcTrackIndex=0; SrcTrackIndex<SrcNumTracks; ++SrcTrackIndex)
			{
				ComponentSpaceAnimations[SrcTrackIndex].AddUninitialized(NumKeys);
				ConvertedLocalSpaceAnimations[SrcTrackIndex].AddUninitialized(NumKeys);
				ConvertedSpaceAnimations[SrcTrackIndex].AddUninitialized(NumKeys);
			}

			for (int32 SrcTrackIndex=0; SrcTrackIndex<SrcNumTracks; ++SrcTrackIndex)		
			{
				int32 NodeIndex = Rig->FindNode(SrcValidNodeNames[SrcTrackIndex]);
				check (NodeIndex != INDEX_NONE);
				auto& RawAnimation = RiggingAnimationData.AnimationTracks[SrcTrackIndex];

				// find rotation parent node
				int32 RotParentTrackIndex = FindValidTransformParentTrack(Rig, NodeIndex, false, SrcValidNodeNames);
				int32 TransParentTrackIndex = FindValidTransformParentTrack(Rig, NodeIndex, true, SrcValidNodeNames);
				// fill up keys - calculate PK1 * K1
				for(int32 Key=0; Key<NumKeys; ++Key)
				{
					FTransform AnimatedLocalKey;
					ExtractBoneTransform(RiggingAnimationData.AnimationTracks, AnimatedLocalKey, SrcTrackIndex, Interval*Key);

					AnimatedLocalKey.ScaleTranslation(OldToNewTranslationRatio[NodeIndex]);

					if(RotParentTrackIndex != INDEX_NONE)
					{
						FQuat ComponentSpaceRotation = ComponentSpaceAnimations[RotParentTrackIndex][Key].GetRotation() * AnimatedLocalKey.GetRotation();
						ComponentSpaceAnimations[SrcTrackIndex][Key].SetRotation(ComponentSpaceRotation);
					}
					else
					{
						ComponentSpaceAnimations[SrcTrackIndex][Key].SetRotation(AnimatedLocalKey.GetRotation());
					}

					if (TransParentTrackIndex != INDEX_NONE)
					{
						FVector ComponentSpaceTranslation = ComponentSpaceAnimations[TransParentTrackIndex][Key].TransformPosition(AnimatedLocalKey.GetTranslation());
						ComponentSpaceAnimations[SrcTrackIndex][Key].SetTranslation(ComponentSpaceTranslation);
						FVector ParentComponentSpaceScale3D = ComponentSpaceAnimations[TransParentTrackIndex][Key].GetScale3D();
						ComponentSpaceAnimations[SrcTrackIndex][Key].SetScale3D(ParentComponentSpaceScale3D * AnimatedLocalKey.GetScale3D());
					}
					else
					{
						ComponentSpaceAnimations[SrcTrackIndex][Key].SetTranslation(AnimatedLocalKey.GetTranslation());
						ComponentSpaceAnimations[SrcTrackIndex][Key].SetScale3D(AnimatedLocalKey.GetScale3D());
					}
				}
			}

			// now animation is converted to component space
			TArray<struct FRawAnimSequenceTrack> NewRawAnimationData = RiggingAnimationData.AnimationTracks;
			for (int32 SrcTrackIndex=0; SrcTrackIndex<SrcNumTracks; ++SrcTrackIndex)
			{
				int32 NodeIndex = Rig->FindNode(SrcValidNodeNames[SrcTrackIndex]);
				// find rotation parent node
				int32 RotParentTrackIndex = FindValidTransformParentTrack(Rig, NodeIndex, false, SrcValidNodeNames);
				int32 TransParentTrackIndex = FindValidTransformParentTrack(Rig, NodeIndex, true, SrcValidNodeNames);

				// clear translation;
				RelativeToNewSpaceBases[NodeIndex].SetTranslation(FVector::ZeroVector);

				for(int32 Key=0; Key<NumKeys; ++Key)
				{
					// now convert to the new space and save to local spaces
					ConvertedSpaceAnimations[SrcTrackIndex][Key] = RelativeToNewSpaceBases[NodeIndex] * ComponentSpaceAnimations[SrcTrackIndex][Key];

					if(RotParentTrackIndex != INDEX_NONE)
					{
						FQuat LocalRotation = ConvertedSpaceAnimations[RotParentTrackIndex][Key].GetRotation().Inverse() * ConvertedSpaceAnimations[SrcTrackIndex][Key].GetRotation();
						ConvertedLocalSpaceAnimations[SrcTrackIndex][Key].SetRotation(LocalRotation);
					}
					else
					{
						ConvertedLocalSpaceAnimations[SrcTrackIndex][Key].SetRotation(ConvertedSpaceAnimations[SrcTrackIndex][Key].GetRotation());
					}

					if(TransParentTrackIndex != INDEX_NONE)
					{
						FTransform LocalTransform = ConvertedSpaceAnimations[SrcTrackIndex][Key].GetRelativeTransform(ConvertedSpaceAnimations[TransParentTrackIndex][Key]);
						ConvertedLocalSpaceAnimations[SrcTrackIndex][Key].SetTranslation(LocalTransform.GetLocation());
						ConvertedLocalSpaceAnimations[SrcTrackIndex][Key].SetScale3D(LocalTransform.GetScale3D());
					}
					else
					{
						ConvertedLocalSpaceAnimations[SrcTrackIndex][Key].SetTranslation(ConvertedSpaceAnimations[SrcTrackIndex][Key].GetTranslation());
						ConvertedLocalSpaceAnimations[SrcTrackIndex][Key].SetScale3D(ConvertedSpaceAnimations[SrcTrackIndex][Key].GetScale3D());
					}
				}

				auto& RawAnimation = NewRawAnimationData[SrcTrackIndex];
				RawAnimation.PosKeys.Empty(NumKeys);
				RawAnimation.PosKeys.AddUninitialized(NumKeys);
				RawAnimation.RotKeys.Empty(NumKeys);
				RawAnimation.RotKeys.AddUninitialized(NumKeys);
				RawAnimation.ScaleKeys.Empty(NumKeys);
				RawAnimation.ScaleKeys.AddUninitialized(NumKeys);

				for(int32 Key=0; Key<NumKeys; ++Key)
				{
					RawAnimation.PosKeys[Key] = ConvertedLocalSpaceAnimations[SrcTrackIndex][Key].GetLocation();
					RawAnimation.RotKeys[Key] = ConvertedLocalSpaceAnimations[SrcTrackIndex][Key].GetRotation();
					RawAnimation.ScaleKeys[Key] = ConvertedLocalSpaceAnimations[SrcTrackIndex][Key].GetScale3D();

					// normalize rotation
					RawAnimation.RotKeys[Key].Normalize();
				}
			}

			RiggingAnimationData.AnimationTracks = MoveTemp(NewRawAnimationData);
			RiggingAnimationData.TrackNames = MoveTemp(SrcValidNodeNames);

			// set new skeleton
			SetSkeleton(NewSkeleton);

			// convert back to animated data with new skeleton
			const bool bPerformPostProcess = false; //Don't do PostProcess during Remap as any animations we reference may not have been updated yet
			ConvertRiggingDataToAnimationData(RiggingAnimationData, bPerformPostProcess);
		}
		// @todo end rig testing
		// @IMPORTANT: now otherwise this will try to do bone to bone mapping
		else if(OldSkeleton)
		{
			// this only replaces the primary one, it doesn't replace old ones
			TArray<struct FTrackToSkeletonMap> NewTrackToSkeletonMapTable;
			NewTrackToSkeletonMapTable.Empty(AnimationTrackNames.Num());
			NewTrackToSkeletonMapTable.AddUninitialized(AnimationTrackNames.Num());
			for (int32 Track = 0; Track < AnimationTrackNames.Num(); ++Track)
			{
				int32 BoneIndex = NewSkeleton->GetReferenceSkeleton().FindBoneIndex(AnimationTrackNames[Track]);
				NewTrackToSkeletonMapTable[Track].BoneTreeIndex = BoneIndex;
			}

			// now I have all NewTrack To Skeleton Map Table
			// I'll need to compare with old tracks and copy over if SkeletonIndex == 0
			// if SkeletonIndex != 0, we need to see if we can 
			for (int32 TableId = 0; TableId < NewTrackToSkeletonMapTable.Num(); ++TableId)
			{
				if (ensure(TrackToSkeletonMapTable.IsValidIndex(TableId)))
				{
					if (NewTrackToSkeletonMapTable[TableId].BoneTreeIndex != INDEX_NONE)
					{
						TrackToSkeletonMapTable[TableId].BoneTreeIndex = NewTrackToSkeletonMapTable[TableId].BoneTreeIndex;
					}
					else
					{
						// if not found, delete the track data
						RemoveTrack(TableId);
						NewTrackToSkeletonMapTable.RemoveAt(TableId);
						--TableId;
					}
				}
			}

			if (TrackToSkeletonMapTable.Num() == 0)
			{
				// no bones to retarget
				// return with error
				//@todo fail message
			}
			// make sure you do update reference pose before coming here
			
			// first calculate component space ref pose to get the relative transform between
			// two ref poses. It is very important update ref pose before getting here. 
			TArray<FTransform> NewSpaceBaseRefPose, OldSpaceBaseRefPose, RelativeToNewTransform;
			// get the spacebases transform
			FAnimationRuntime::FillUpComponentSpaceTransformsRefPose(NewSkeleton, NewSpaceBaseRefPose);
			FAnimationRuntime::FillUpComponentSpaceTransformsRefPose(OldSkeleton, OldSpaceBaseRefPose);

			const TArray<FTransform>& OldRefPose = OldSkeleton->GetReferenceSkeleton().GetRefBonePose();
			const TArray<FTransform>& NewRefPose = NewSkeleton->GetReferenceSkeleton().GetRefBonePose();

			// now we'd like to get the relative transform from old to new ref pose in component space
			// PK2*K2 = PK1*K1*theta where theta => P1*R1*theta = P2*R2 
			// where	P1 - parent transform in component space for original skeleton
			//			R1 - local space of the current bone for original skeleton
			//			P2 - parent transform in component space for new skeleton
			//			R2 - local space of the current bone for new skeleton
			// what we're looking for is theta, so that we can apply that to animated transform
			int32 NumBones = NewSpaceBaseRefPose.Num();
			// saves the theta data per bone
			RelativeToNewTransform.AddUninitialized(NumBones);
			TArray<float> OldToNewTranslationRatio;
			// saves the translation conversion data
			OldToNewTranslationRatio.AddUninitialized(NumBones);

			// calculate the relative transform to new skeleton
			// so that we can apply the delta in component space
			for(int32 BoneIndex=0; BoneIndex<NumBones; ++BoneIndex)
			{
				// first find bone name of the idnex
				FName BoneName = NewSkeleton->GetReferenceSkeleton().GetRefBoneInfo()[BoneIndex].Name;
				// find it in old index
				int32 OldBoneIndex = OldSkeleton->GetReferenceSkeleton().FindBoneIndex(BoneName);

				// get old bone index
				if(OldBoneIndex != INDEX_NONE)
				{
					// theta (RelativeToNewTransform) = (P1*R1)^(-1) * P2*R2 where theta => P1*R1*theta = P2*R2
					RelativeToNewTransform[BoneIndex] = NewSpaceBaseRefPose[BoneIndex].GetRelativeTransform(OldSpaceBaseRefPose[OldBoneIndex]);

					// also savees the translation difference between old to new
					FVector OldTranslation = OldRefPose[OldBoneIndex].GetTranslation();
					FVector NewTranslation = NewRefPose[BoneIndex].GetTranslation();

					float OldTranslationSize = OldTranslation.Size();
					float NewTranslationSize = NewTranslation.Size();
					OldToNewTranslationRatio[BoneIndex] = (FMath::IsNearlyZero(OldTranslationSize))? 1.f/*do not touch new translation size*/ : NewTranslationSize/OldTranslationSize;
				}
				else
				{
					RelativeToNewTransform[BoneIndex].SetIdentity();
				}
			}

			// 2d array of animated time [boneindex][time key]
			TArray< TArray<FTransform> > AnimatedSpaceBases, ConvertedLocalSpaces, ConvertedSpaceBases;
			AnimatedSpaceBases.AddZeroed(NumBones);
			ConvertedLocalSpaces.AddZeroed(NumBones);
			ConvertedSpaceBases.AddZeroed(NumBones);

			int32 NumKeys = NumFrames;
			float Interval = GetIntervalPerKey(NumFrames, SequenceLength);

			// allocate arrays
			for(int32 BoneIndex=0; BoneIndex<NumBones; ++BoneIndex)
			{
				AnimatedSpaceBases[BoneIndex].AddUninitialized(NumKeys);
				ConvertedLocalSpaces[BoneIndex].AddUninitialized(NumKeys);
				ConvertedSpaceBases[BoneIndex].AddUninitialized(NumKeys);
			}

			// now calculating old animated space bases
			// this one calculates aniamted space per bones and per key
			for(int32 BoneIndex=0; BoneIndex<NumBones; ++BoneIndex)
			{
				FName BoneName = NewSkeleton->GetReferenceSkeleton().GetBoneName(BoneIndex);
				int32 OldBoneIndex = OldSkeleton->GetReferenceSkeleton().FindBoneIndex(BoneName);
				int32 TrackIndex = AnimationTrackNames.Find(BoneName);
				int32 ParentBoneIndex = NewSkeleton->GetReferenceSkeleton().GetParentIndex(BoneIndex);

				if(TrackIndex != INDEX_NONE)
				{
					auto& RawAnimation = RawAnimationData[TrackIndex];
					// fill up keys - calculate PK1 * K1
					for(int32 Key=0; Key<NumKeys; ++Key)
					{
						FTransform AnimatedLocalKey;
						ExtractBoneTransform(RawAnimationData, AnimatedLocalKey, TrackIndex, Interval*Key);

						// note that we apply scale in the animated space
						// at this point, you should have scaled version of animated skeleton
						AnimatedLocalKey.ScaleTranslation(OldToNewTranslationRatio[BoneIndex]);

						if(ParentBoneIndex != INDEX_NONE)
						{
							AnimatedSpaceBases[BoneIndex][Key] = AnimatedLocalKey * AnimatedSpaceBases[ParentBoneIndex][Key];
						}
						else
						{
							AnimatedSpaceBases[BoneIndex][Key] = AnimatedLocalKey;
						}
					}
				}
				else
				{
					// get local spaces from refpose and use that to fill it up
					FTransform LocalTransform = (OldBoneIndex != INDEX_NONE)? OldSkeleton->GetReferenceSkeleton().GetRefBonePose()[OldBoneIndex] : FTransform::Identity;

					for(int32 Key=0; Key<NumKeys; ++Key)
					{
						if(ParentBoneIndex != INDEX_NONE)
						{
							AnimatedSpaceBases[BoneIndex][Key] = LocalTransform * AnimatedSpaceBases[ParentBoneIndex][Key];
						}
						else
						{
							AnimatedSpaceBases[BoneIndex][Key] = LocalTransform;
						}
					}
				}
			}

			// now apply the theta back to the animated space bases
			TArray<struct FRawAnimSequenceTrack> NewRawAnimationData = RawAnimationData;
			for(int32 BoneIndex=0; BoneIndex<NumBones; ++BoneIndex)
			{
				FName BoneName = NewSkeleton->GetReferenceSkeleton().GetBoneName(BoneIndex);
				int32 TrackIndex = AnimationTrackNames.Find(BoneName);
				int32 ParentBoneIndex = NewSkeleton->GetReferenceSkeleton().GetParentIndex(BoneIndex);

				for(int32 Key=0; Key<NumKeys; ++Key)
				{
					// thus PK2 & K2 =  PK1 * K1 * theta where theta = (P1*R1)^(-1) * P2*R2
					// where PK2	: parent transform in component space of animated key for new skeleton
					//		 K2		: local transform of animated key for new skeleton
					//		 PK1	: parent transform in component space of animated key for old skeleton
					//		 K1		: local transform of animated key for old skeleton
					FTransform SpaceBase;
					// we don't just apply it because translation is sensitive
					// we don't like to apply relative transform to tranlsation directly
					// rotation and scale we can, but translation we'd like to use scaled translation instead of transformed location
					// as their relative translation can be different
					SpaceBase.SetRotation(AnimatedSpaceBases[BoneIndex][Key].GetRotation() * RelativeToNewTransform[BoneIndex].GetRotation());
					SpaceBase.SetScale3D(AnimatedSpaceBases[BoneIndex][Key].GetScale3D() * RelativeToNewTransform[BoneIndex].GetScale3D());
					// use animated scaled translation directly
					SpaceBase.SetTranslation(AnimatedSpaceBases[BoneIndex][Key].GetTranslation());
					ConvertedSpaceBases[BoneIndex][Key] = SpaceBase;
					// now calculate local space for animation
					if(ParentBoneIndex != INDEX_NONE)
					{
						// K2 = PK2^(-1) * PK1 * K1 * (P1*R1)^(-1) * P2*R2
						ConvertedLocalSpaces[BoneIndex][Key] = SpaceBase.GetRelativeTransform(ConvertedSpaceBases[ParentBoneIndex][Key]);
					}
					else
					{
						ConvertedLocalSpaces[BoneIndex][Key] = SpaceBase;
					}
				}

				// now save back to animation data
				if(TrackIndex != INDEX_NONE)
				{
					auto& RawAnimation = NewRawAnimationData[TrackIndex];
					RawAnimation.PosKeys.Empty(NumKeys);
					RawAnimation.PosKeys.AddUninitialized(NumKeys);
					RawAnimation.RotKeys.Empty(NumKeys);
					RawAnimation.RotKeys.AddUninitialized(NumKeys);
					RawAnimation.ScaleKeys.Empty(NumKeys);
					RawAnimation.ScaleKeys.AddUninitialized(NumKeys);

					for(int32 Key=0; Key<NumKeys; ++Key)
					{
						RawAnimation.PosKeys[Key] = ConvertedLocalSpaces[BoneIndex][Key].GetLocation();
						RawAnimation.RotKeys[Key] = ConvertedLocalSpaces[BoneIndex][Key].GetRotation();
						RawAnimation.ScaleKeys[Key] = ConvertedLocalSpaces[BoneIndex][Key].GetScale3D();
					}
				}
			}
			RawAnimationData = NewRawAnimationData;
		}
		else
		{
			// this only replaces the primary one, it doesn't replace old ones
			TArray<struct FTrackToSkeletonMap> NewTrackToSkeletonMapTable;
			NewTrackToSkeletonMapTable.Empty(AnimationTrackNames.Num());
			NewTrackToSkeletonMapTable.AddUninitialized(AnimationTrackNames.Num());
			for (int32 Track = 0; Track < AnimationTrackNames.Num(); ++Track)
			{
				int32 BoneIndex = NewSkeleton->GetReferenceSkeleton().FindBoneIndex(AnimationTrackNames[Track]);
				NewTrackToSkeletonMapTable[Track].BoneTreeIndex = BoneIndex;
			}

			// now I have all NewTrack To Skeleton Map Table
			// I'll need to compare with old tracks and copy over if SkeletonIndex == 0
			// if SkeletonIndex != 0, we need to see if we can 
			for (int32 TableId = 0; TableId < NewTrackToSkeletonMapTable.Num(); ++TableId)
			{
				if (ensure(TrackToSkeletonMapTable.IsValidIndex(TableId)))
				{
					if (NewTrackToSkeletonMapTable[TableId].BoneTreeIndex != INDEX_NONE)
					{
						TrackToSkeletonMapTable[TableId].BoneTreeIndex = NewTrackToSkeletonMapTable[TableId].BoneTreeIndex;
					}
					else
					{
						// if not found, delete the track data
						RemoveTrack(TableId);
						NewTrackToSkeletonMapTable.RemoveAt(TableId);
						--TableId;
					}
				}
			}
		}

		// I have to set this here in order for compression
		// that has to happen outside of this after Skeleton changes
		SetSkeleton(NewSkeleton);
	}
	else
	{
		VerifyTrackMap(NewSkeleton);
	}	

	Super::RemapTracksToNewSkeleton(NewSkeleton, bConvertSpaces);
}

void UAnimSequence::PostProcessSequence(bool bForceNewRawDatGuid)
{
	// pre process before compress raw animation data

	// if scale is too small, zero it out. Cause it hard to retarget when compress
	// inverse scale is applied to translation, and causing translation to be huge to retarget, but
	// compression can't handle that much precision. 
	for (auto Iter = RawAnimationData.CreateIterator(); Iter; ++Iter)
	{
		FRawAnimSequenceTrack& RawAnim = (*Iter);

		for (auto ScaleIter = RawAnim.ScaleKeys.CreateIterator(); ScaleIter; ++ScaleIter)
		{
			FVector& Scale3D = *ScaleIter;
			if ( FMath::IsNearlyZero(Scale3D.X) )
			{
				Scale3D.X = 0.f;
			}
			if ( FMath::IsNearlyZero(Scale3D.Y) )
			{
				Scale3D.Y = 0.f;
			}
			if ( FMath::IsNearlyZero(Scale3D.Z) )
			{
				Scale3D.Z = 0.f;
			}
		}

		// make sure Rotation part is normalized before compress
		for(auto RotIter = RawAnim.RotKeys.CreateIterator(); RotIter; ++RotIter)
		{
			FQuat& Rotation = *RotIter;
			if( !Rotation.IsNormalized() )
			{
				Rotation.Normalize();
			}
		}
	}

	CompressRawAnimData();
	// Apply compression
	MarkRawDataAsModified(bForceNewRawDatGuid);
	OnRawDataChanged();
	// initialize notify track
	InitializeNotifyTrack();
	//Make sure we dont have any notifies off the end of the sequence
	ClampNotifiesAtEndOfSequence();
	// mark package as dirty
	MarkPackageDirty();
}

void UAnimSequence::RemoveNaNTracks()
{
	bool bRecompress = false;

	for( int32 TrackIndex=0; TrackIndex<RawAnimationData.Num(); ++TrackIndex )
	{
		const FRawAnimSequenceTrack& RawTrack = RawAnimationData[TrackIndex];

		bool bContainsNaN = false;
		for ( auto Key : RawTrack.PosKeys )
		{
			bContainsNaN |= Key.ContainsNaN();
		}

		if (!bContainsNaN)
		{
			for(auto Key : RawTrack.RotKeys)
			{
				bContainsNaN |= Key.ContainsNaN();
			}
		}

		if (!bContainsNaN)
		{
			for(auto Key : RawTrack.ScaleKeys)
			{
				bContainsNaN |= Key.ContainsNaN();
			}
		}

		if (bContainsNaN)
		{
			UE_LOG(LogAnimation, Warning, TEXT("Animation raw data contains NaNs - Removing the following track [%s Track (%s)]"), (GetOuter() ? *GetOuter()->GetFullName() : *GetFullName()), *AnimationTrackNames[TrackIndex].ToString());
			// remove this track
			RemoveTrack(TrackIndex);
			--TrackIndex;

			bRecompress = true;
		}
	}

	if(bRecompress)
	{
		MarkRawDataAsModified();
		OnRawDataChanged();
	}
}


void UAnimSequence::RemoveAllTracks()
{
	RawAnimationData.Empty();
	AnimationTrackNames.Empty();
	TrackToSkeletonMapTable.Empty();
	SourceRawAnimationData.Empty();
	// clear all transform tracks
	// not deleting curve names from skeleton 
	// since we don't know if that name is used by other assets
	RawCurveData.TransformCurves.Empty();

	// recompress and clear
	PostProcessSequence();
}

void UAnimSequence::RemoveTrack(int32 TrackIndex)
{
	if (RawAnimationData.IsValidIndex(TrackIndex))
	{
		RawAnimationData.RemoveAt(TrackIndex);
		AnimationTrackNames.RemoveAt(TrackIndex);
		TrackToSkeletonMapTable.RemoveAt(TrackIndex);
		// source raw animation only exists if edited
		if (SourceRawAnimationData.IsValidIndex(TrackIndex))
		{
			SourceRawAnimationData.RemoveAt(TrackIndex);
		}

		check (RawAnimationData.Num() == AnimationTrackNames.Num() && AnimationTrackNames.Num() == TrackToSkeletonMapTable.Num() );
	}
}

int32 FindFirstChildTrack(const USkeleton* MySkeleton, const FReferenceSkeleton& RefSkeleton, const TArray<FName>& AnimationTrackNames, FName BoneName)
{
	const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
	if(BoneIndex == INDEX_NONE)
	{
		// get out, nothing to do
		return INDEX_NONE;
	}

	// find children
	TArray<int32> Childs;
	if(MySkeleton->GetChildBones(BoneIndex, Childs) > 0)
	{
		// first look for direct children
		for(auto ChildIndex : Childs)
		{
			FName ChildBoneName = RefSkeleton.GetBoneName(ChildIndex);
			int32 ChildTrackIndex = AnimationTrackNames.Find(ChildBoneName);
			if(ChildTrackIndex != INDEX_NONE)
			{
				// found the new track
				return ChildTrackIndex;
			}
		}

		int32 BestGrandChildIndex = INDEX_NONE;
		// if you didn't find yet, now you have to go through all children
		for(auto ChildIndex : Childs)
		{
			FName ChildBoneName = RefSkeleton.GetBoneName(ChildIndex);
			// now I have to go through all childrewn and find who is earliest since I don't know which one might be the closest one
			int32 GrandChildIndex = FindFirstChildTrack(MySkeleton, RefSkeleton, AnimationTrackNames, ChildBoneName);
			if (GrandChildIndex != INDEX_NONE)
			{
				if (BestGrandChildIndex == INDEX_NONE)
				{
					BestGrandChildIndex = GrandChildIndex;
				}
				else if (BestGrandChildIndex > GrandChildIndex)
				{
					// best should be earlier track index
					BestGrandChildIndex = GrandChildIndex;
				}
			}
		}

		return BestGrandChildIndex;
	}
	else
	{
		// there is no child, just add at the end
		return AnimationTrackNames.Num();
	}
}

int32 UAnimSequence::InsertTrack(const FName& BoneName)
{
#if WITH_EDITOR
	FModifyRawDataSourceGuard Modify(this);
#endif
	return InsertTrackInternal(BoneName);
}

int32 UAnimSequence::InsertTrackInternal(const FName& BoneName)
{
	// first verify if it doesn't exists, if it does, return
	int32 CurrentTrackIndex = AnimationTrackNames.Find(BoneName);
	if (CurrentTrackIndex != INDEX_NONE)
	{
		return CurrentTrackIndex;
	}

	USkeleton * MySkeleton = GetSkeleton();
	// should not call this if skeleton was empty
	if (ensure(MySkeleton) == false)
	{
		return INDEX_NONE;
	}

	const FReferenceSkeleton& RefSkeleton = MySkeleton->GetReferenceSkeleton();
	int32 NewTrackIndex = FindFirstChildTrack(MySkeleton, RefSkeleton, AnimationTrackNames, BoneName);
	int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
	if (NewTrackIndex != INDEX_NONE)
	{
		const TArray<FTransform>& RefPose = RefSkeleton.GetRefBonePose();

		FRawAnimSequenceTrack RawTrack;
		RawTrack.PosKeys.Add(RefPose[BoneIndex].GetTranslation());
		RawTrack.RotKeys.Add(RefPose[BoneIndex].GetRotation());
		RawTrack.ScaleKeys.Add(RefPose[BoneIndex].GetScale3D());

		// now insert to the track
		RawAnimationData.Insert(RawTrack, NewTrackIndex);
		AnimationTrackNames.Insert(BoneName, NewTrackIndex);

		RefreshTrackMapFromAnimTrackNames();

		check(RawAnimationData.Num() == AnimationTrackNames.Num() && AnimationTrackNames.Num() == TrackToSkeletonMapTable.Num());
	}

	return NewTrackIndex;
}

bool UAnimSequence::GetAllAnimationSequencesReferred(TArray<UAnimationAsset*>& AnimationAssets, bool bRecursive /*= true*/)
{
	Super::GetAllAnimationSequencesReferred(AnimationAssets, bRecursive);
	if (RefPoseSeq  && RefPoseSeq != this && !AnimationAssets.Contains(RefPoseSeq))
	{
		RefPoseSeq->HandleAnimReferenceCollection(AnimationAssets, bRecursive);
	}
	return AnimationAssets.Num() > 0;
}

void UAnimSequence::ReplaceReferredAnimations(const TMap<UAnimationAsset*, UAnimationAsset*>& ReplacementMap)
{
	Super::ReplaceReferredAnimations(ReplacementMap);

	if (RefPoseSeq)
	{
		UAnimSequence* const* ReplacementAsset = (UAnimSequence*const*)ReplacementMap.Find(RefPoseSeq);
		if (ReplacementAsset)
		{
			RefPoseSeq = *ReplacementAsset;
		}
	}
}

bool UAnimSequence::AddLoopingInterpolation()
{
	int32 NumTracks = AnimationTrackNames.Num();
	float Interval = GetIntervalPerKey(NumFrames, SequenceLength);

	if(NumFrames > 0)
	{
		// added one more key
		int32 NewNumKeys = NumFrames +1 ;

		// now I need to calculate back to new animation data
		for(int32 TrackIndex=0; TrackIndex<NumTracks; ++TrackIndex)
		{
			auto& RawAnimation = RawAnimationData[TrackIndex];
			if (RawAnimation.PosKeys.Num() > 1)
			{
				FVector FirstKey = RawAnimation.PosKeys[0];
				RawAnimation.PosKeys.Add(FirstKey);
			}

			if(RawAnimation.RotKeys.Num() > 1)
			{
				FQuat FirstKey = RawAnimation.RotKeys[0];
				RawAnimation.RotKeys.Add(FirstKey);
			}

			if(RawAnimation.ScaleKeys.Num() > 1)
			{
				FVector FirstKey = RawAnimation.ScaleKeys[0];
				RawAnimation.ScaleKeys.Add(FirstKey);
			}
		}

		SequenceLength += Interval;
		NumFrames = NewNumKeys;

		PostProcessSequence();
		return true;
	}

	return false;
}
int32 FindParentNodeIndex(URig* Rig, USkeleton* Skeleton, FName ParentNodeName)
{
	const int32& ParentNodeIndex = Rig->FindNode(ParentNodeName);
	const FName& ParentBoneName = Skeleton->GetRigBoneMapping(ParentNodeName);
	
	return Skeleton->GetReferenceSkeleton().FindBoneIndex(ParentBoneName);
}

int32 UAnimSequence::GetSpaceBasedAnimationData(TArray< TArray<FTransform> >& AnimationDataInComponentSpace, FAnimSequenceTrackContainer * RiggingAnimationData) const
{
	USkeleton* MySkeleton = GetSkeleton();

	check(MySkeleton);
	const FReferenceSkeleton& RefSkeleton = MySkeleton->GetReferenceSkeleton();
	int32 NumBones = RefSkeleton.GetNum();

	AnimationDataInComponentSpace.Empty(NumBones);
	AnimationDataInComponentSpace.AddZeroed(NumBones);

	// 2d array of animated time [boneindex][time key]
	int32 NumKeys = NumFrames;
	float Interval = GetIntervalPerKey(NumFrames, SequenceLength);

	// allocate arrays
	for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
	{
		AnimationDataInComponentSpace[BoneIndex].AddUninitialized(NumKeys);
	}

	if (RiggingAnimationData)
	{
		const URig* Rig = MySkeleton->GetRig();

		check(Rig);

		// to fix the issue where parent of rig doesn't correspond to parent of this skeleton
		// we do this in multiple iteration if needed. 
		// this flag will be used to evaluate all of them until done
		TArray<bool> BoneEvaluated;
		BoneEvaluated.AddZeroed(NumBones);

		bool bCompleted = false;
		do
		{
			for(int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
			{
				if ( !BoneEvaluated[BoneIndex] )
				{
					const FName& BoneName = RefSkeleton.GetBoneName(BoneIndex);
					const FName& NodeName = MySkeleton->GetRigNodeNameFromBoneName(BoneName);
					const FTransformBase* TransformBase = Rig->GetTransformBaseByNodeName(NodeName);
					const int32 NodeIndex = RiggingAnimationData->TrackNames.Find(NodeName);
					if(NodeIndex != INDEX_NONE)
					{
						check(TransformBase);

						// now calculate the component space
						const TArray<FRigTransformConstraint>	& RotTransformConstraints = TransformBase->Constraints[EControlConstraint::Type::Orientation].TransformConstraints;

						FQuat ComponentRotation;
						FTransform ComponentTranslation;
						FVector ComponentScale;

						// rotation first
						// this is easy since we just make sure it's evaluated or not
						{
							const FName& ParentNodeName = RotTransformConstraints[0].ParentSpace;
							const FName& ParentBoneName = MySkeleton->GetRigBoneMapping(ParentNodeName);
							const int32& ParentBoneIndex = RefSkeleton.FindBoneIndex(ParentBoneName);

							if(ParentBoneIndex != INDEX_NONE)
							{
								if (BoneEvaluated[ParentBoneIndex])
								{
									for(int32 Key = 0; Key < NumKeys; ++Key)
									{
										ComponentRotation = AnimationDataInComponentSpace[ParentBoneIndex][Key].GetRotation() * RiggingAnimationData->AnimationTracks[NodeIndex].RotKeys[Key];
										AnimationDataInComponentSpace[BoneIndex][Key].SetRotation(ComponentRotation);
									}

									BoneEvaluated[BoneIndex] = true;
								}
							}
							else
							{
								for(int32 Key = 0; Key < NumKeys; ++Key)
								{
									ComponentRotation = RiggingAnimationData->AnimationTracks[NodeIndex].RotKeys[Key];
									AnimationDataInComponentSpace[BoneIndex][Key].SetRotation(ComponentRotation);
								}

								BoneEvaluated[BoneIndex] = true;
							}
						}

						const TArray<FRigTransformConstraint>	& PosTransformConstraints = TransformBase->Constraints[EControlConstraint::Type::Translation].TransformConstraints;

						// now time to check translation
						// this is a bit more complicated
						// since we have to make sure if it's true to start with
						// did we succeed on getting rotation?
						if (BoneEvaluated[BoneIndex])
						{
							const FName& ParentNodeName = PosTransformConstraints[0].ParentSpace;
							const FName& ParentBoneName = MySkeleton->GetRigBoneMapping(ParentNodeName);
							const int32& ParentBoneIndex = RefSkeleton.FindBoneIndex(ParentBoneName);

							if(ParentBoneIndex != INDEX_NONE)
							{
								// this has to be check
								if (BoneEvaluated[ParentBoneIndex])
								{
									for(int32 Key = 0; Key < NumKeys; ++Key)
									{
										const FTransform& AnimCompSpace = AnimationDataInComponentSpace[ParentBoneIndex][Key];
										ComponentTranslation = FTransform(RiggingAnimationData->AnimationTracks[NodeIndex].PosKeys[Key]) * AnimCompSpace;
										AnimationDataInComponentSpace[BoneIndex][Key].SetTranslation(ComponentTranslation.GetTranslation());

										ComponentScale = AnimCompSpace.GetScale3D() * RiggingAnimationData->AnimationTracks[NodeIndex].ScaleKeys[Key];
										AnimationDataInComponentSpace[BoneIndex][Key].SetScale3D(ComponentScale);
									}
								}
								else
								{
									// if we failed to get parent clear the flag
									// because if translation has been calculated, BoneEvaluated[BoneIndex] might be true
									BoneEvaluated[BoneIndex] = false;
								}
							}
							else
							{
								for(int32 Key = 0; Key < NumKeys; ++Key)
								{
									ComponentTranslation = FTransform(RiggingAnimationData->AnimationTracks[NodeIndex].PosKeys[Key]);
									AnimationDataInComponentSpace[BoneIndex][Key].SetTranslation(ComponentTranslation.GetTranslation());
									
									ComponentScale = RiggingAnimationData->AnimationTracks[NodeIndex].ScaleKeys[Key];
									AnimationDataInComponentSpace[BoneIndex][Key].SetScale3D(ComponentScale);
								}
							}
						}
					}
					else
					{
						int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
						const FTransform& LocalSpace = RefSkeleton.GetRefBonePose()[BoneIndex];
						if(ParentIndex != INDEX_NONE)
						{
							// if parent is evaluated, do it
							if (BoneEvaluated[ParentIndex])
							{
								for(int32 Key = 0; Key < NumKeys; ++Key)
								{
									AnimationDataInComponentSpace[BoneIndex][Key] = LocalSpace * AnimationDataInComponentSpace[ParentIndex][Key];
								}

								BoneEvaluated[BoneIndex] = true;
							}
						}
						else
						{
							BoneEvaluated[BoneIndex] = true;

							for(int32 Key = 0; Key < NumKeys; ++Key)
							{
								AnimationDataInComponentSpace[BoneIndex][Key] = LocalSpace;
							}
						}
					}
				}
			}

			bCompleted = true;
			// see if we can get out, brute force for now
			for(int32 BoneIndex = 0; BoneIndex < NumBones && bCompleted; ++BoneIndex)
			{
				bCompleted &= !!BoneEvaluated[BoneIndex];
			}
		} while (bCompleted == false);
	}
	else
	{
		// now calculating old animated space bases
		// this one calculates aniamted space per bones and per key
		for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
		{
			FName BoneName = MySkeleton->GetReferenceSkeleton().GetBoneName(BoneIndex);
			int32 TrackIndex = AnimationTrackNames.Find(BoneName);
			int32 ParentBoneIndex = MySkeleton->GetReferenceSkeleton().GetParentIndex(BoneIndex);

			if (TrackIndex != INDEX_NONE)
			{
				auto& RawAnimation = RawAnimationData[TrackIndex];
				// fill up keys - calculate PK1 * K1
				for (int32 Key = 0; Key < NumKeys; ++Key)
				{
					FTransform AnimatedLocalKey;
					ExtractBoneTransform(RawAnimationData, AnimatedLocalKey, TrackIndex, Interval*Key);

					if (ParentBoneIndex != INDEX_NONE)
					{
						AnimationDataInComponentSpace[BoneIndex][Key] = AnimatedLocalKey * AnimationDataInComponentSpace[ParentBoneIndex][Key];
					}
					else
					{
						AnimationDataInComponentSpace[BoneIndex][Key] = AnimatedLocalKey;
					}
				}
			}
			else
			{
				// get local spaces from refpose and use that to fill it up
				FTransform LocalTransform = MySkeleton->GetReferenceSkeleton().GetRefBonePose()[BoneIndex];

				for (int32 Key = 0; Key < NumKeys; ++Key)
				{
					if (ParentBoneIndex != INDEX_NONE)
					{
						AnimationDataInComponentSpace[BoneIndex][Key] = LocalTransform * AnimationDataInComponentSpace[ParentBoneIndex][Key];
					}
					else
					{
						AnimationDataInComponentSpace[BoneIndex][Key] = LocalTransform;
					}
				}
			}	
		}

	}

	return AnimationDataInComponentSpace.Num();
}

bool UAnimSequence::ConvertAnimationDataToRiggingData(FAnimSequenceTrackContainer& RiggingAnimationData)
{
	USkeleton* MySkeleton = GetSkeleton();
	if (MySkeleton && MySkeleton->GetRig())
	{
		const URig* Rig = MySkeleton->GetRig();
		TArray<FName> ValidNodeNames;
		int32 NumNodes = MySkeleton->GetMappedValidNodes(ValidNodeNames);
		TArray< TArray<FTransform> > AnimationDataInComponentSpace;
		int32 NumBones = GetSpaceBasedAnimationData(AnimationDataInComponentSpace, NULL);

		if (NumBones > 0)
		{
			RiggingAnimationData.Initialize(ValidNodeNames);

			// first we copy all space bases back to it
			for (int32 NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
			{
				struct FRawAnimSequenceTrack& Track = RiggingAnimationData.AnimationTracks[NodeIndex];
				const FName& NodeName = ValidNodeNames[NodeIndex];
				const FName& BoneName = MySkeleton->GetRigBoneMapping(NodeName);
				const int32& BoneIndex = MySkeleton->GetReferenceSkeleton().FindBoneIndex(BoneName);

				if (ensure(BoneIndex != INDEX_NONE))
				{
					Track.PosKeys.Empty(NumFrames);
					Track.RotKeys.Empty(NumFrames);
					Track.ScaleKeys.Empty(NumFrames);
					Track.PosKeys.AddUninitialized(NumFrames);
					Track.RotKeys.AddUninitialized(NumFrames);
					Track.ScaleKeys.AddUninitialized(NumFrames);

					int32 RigConstraintIndex = Rig->FindTransformBaseByNodeName(NodeName);

					if (RigConstraintIndex != INDEX_NONE)
					{
						const auto* RigConstraint = Rig->GetTransformBase(RigConstraintIndex);

						// apply orientation - for now only one
						const TArray<FRigTransformConstraint>& RotationTransformConstraint = RigConstraint->Constraints[EControlConstraint::Type::Orientation].TransformConstraints;

						if (RotationTransformConstraint.Num() > 0)
						{
							const FName& ParentSpace = RotationTransformConstraint[0].ParentSpace;
							const FName& ParentBoneName = MySkeleton->GetRigBoneMapping(ParentSpace);
							const int32& ParentBoneIndex = MySkeleton->GetReferenceSkeleton().FindBoneIndex(ParentBoneName);
							if (ParentBoneIndex != INDEX_NONE)
							{
								// if no rig control, component space is used
								for (int32 KeyIndex = 0; KeyIndex < NumFrames; ++KeyIndex)
								{
									FTransform ParentTransform = AnimationDataInComponentSpace[ParentBoneIndex][KeyIndex];
									FTransform RelativeTransform = AnimationDataInComponentSpace[BoneIndex][KeyIndex].GetRelativeTransform(ParentTransform);
									Track.RotKeys[KeyIndex] = RelativeTransform.GetRotation();
								}
							}
							else
							{
								// if no rig control, component space is used
								for (int32 KeyIndex = 0; KeyIndex < NumFrames; ++KeyIndex)
								{
									Track.RotKeys[KeyIndex] = AnimationDataInComponentSpace[BoneIndex][KeyIndex].GetRotation();
								}
							}
						}
						else
						{
							// if no rig control, component space is used
							for (int32 KeyIndex = 0; KeyIndex < NumFrames; ++KeyIndex)
							{
								Track.RotKeys[KeyIndex] = AnimationDataInComponentSpace[BoneIndex][KeyIndex].GetRotation();
							}
						}

						// apply translation - for now only one
						const TArray<FRigTransformConstraint>& TranslationTransformConstraint = RigConstraint->Constraints[EControlConstraint::Type::Translation].TransformConstraints;

						if (TranslationTransformConstraint.Num() > 0)
						{
							const FName& ParentSpace = TranslationTransformConstraint[0].ParentSpace;
							const FName& ParentBoneName = MySkeleton->GetRigBoneMapping(ParentSpace);
							const int32& ParentBoneIndex = MySkeleton->GetReferenceSkeleton().FindBoneIndex(ParentBoneName);
							if (ParentBoneIndex != INDEX_NONE)
							{
								// if no rig control, component space is used
								for (int32 KeyIndex = 0; KeyIndex < NumFrames; ++KeyIndex)
								{
									FTransform ParentTransform = AnimationDataInComponentSpace[ParentBoneIndex][KeyIndex];
									FTransform RelativeTransform = AnimationDataInComponentSpace[BoneIndex][KeyIndex].GetRelativeTransform(ParentTransform);
									Track.PosKeys[KeyIndex] = RelativeTransform.GetTranslation();
									Track.ScaleKeys[KeyIndex] = RelativeTransform.GetScale3D();
								}
							}
							else
							{
								for (int32 KeyIndex = 0; KeyIndex < NumFrames; ++KeyIndex)
								{
									Track.PosKeys[KeyIndex] = AnimationDataInComponentSpace[BoneIndex][KeyIndex].GetTranslation();
									Track.ScaleKeys[KeyIndex] = AnimationDataInComponentSpace[BoneIndex][KeyIndex].GetScale3D();
								}
							}
						}
						else
						{
							for (int32 KeyIndex = 0; KeyIndex < NumFrames; ++KeyIndex)
							{
								Track.PosKeys[KeyIndex] = AnimationDataInComponentSpace[BoneIndex][KeyIndex].GetTranslation();
								Track.ScaleKeys[KeyIndex] = AnimationDataInComponentSpace[BoneIndex][KeyIndex].GetScale3D();
							}
						}
					}
					else
					{
						// if no rig control, component space is used
						for (int32 KeyIndex = 0; KeyIndex < NumFrames; ++KeyIndex)
						{
							Track.PosKeys[KeyIndex] = AnimationDataInComponentSpace[BoneIndex][KeyIndex].GetTranslation();
							Track.RotKeys[KeyIndex] = AnimationDataInComponentSpace[BoneIndex][KeyIndex].GetRotation();
							Track.ScaleKeys[KeyIndex] = AnimationDataInComponentSpace[BoneIndex][KeyIndex].GetScale3D();
						}
					}
				}
			}
		}

		return true;
	}

	return false;
}

bool UAnimSequence::ConvertRiggingDataToAnimationData(FAnimSequenceTrackContainer& RiggingAnimationData, bool bPerformPostProcess)
{
	if (RiggingAnimationData.GetNum() > 0)
	{
		TArray< TArray<FTransform> > AnimationDataInComponentSpace;
		int32 NumBones = GetSpaceBasedAnimationData(AnimationDataInComponentSpace, &RiggingAnimationData);

		USkeleton* MySkeleton = GetSkeleton();
		TArray<FRawAnimSequenceTrack> OldAnimationData = RawAnimationData;
		TArray<FName> OldAnimationTrackNames = AnimationTrackNames;
		TArray<FName> ValidNodeNames;
		MySkeleton->GetMappedValidNodes(ValidNodeNames);
		// remove from ValidNodeNames if it doesn't belong to AnimationTrackNames
		for (int32 NameIndex=0; NameIndex<ValidNodeNames.Num(); ++NameIndex)
		{
			if (RiggingAnimationData.TrackNames.Contains(ValidNodeNames[NameIndex]) == false)
			{
				ValidNodeNames.RemoveAt(NameIndex);
				--NameIndex;
			}
		}

		int32 ValidNumNodes = ValidNodeNames.Num();

		// get local spaces
		// add all tracks?
		AnimationTrackNames.Empty(ValidNumNodes);
		AnimationTrackNames.AddUninitialized(ValidNumNodes);
		RawAnimationData.Empty(ValidNumNodes);
		RawAnimationData.AddZeroed(ValidNumNodes);

		// if source animation exists, clear it, it won't matter anymore
		if (SourceRawAnimationData.Num() > 0)
		{
			ClearBakedTransformData();
		}

		const FReferenceSkeleton& RefSkeleton = MySkeleton->GetReferenceSkeleton();
		const URig* Rig = MySkeleton->GetRig();
		for (int32 NodeIndex = 0; NodeIndex < ValidNumNodes; ++NodeIndex)
		{
			FName BoneName = MySkeleton->GetRigBoneMapping(ValidNodeNames[NodeIndex]);
			int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);

			if (BoneIndex != INDEX_NONE)
			{
				// add track names
				AnimationTrackNames[NodeIndex] = BoneName;

				// update bone trasfnrom
				FRawAnimSequenceTrack& Track = RawAnimationData[NodeIndex];

				Track.PosKeys.Empty();
				Track.RotKeys.Empty();
				Track.ScaleKeys.Empty();
				Track.PosKeys.AddUninitialized(NumFrames);
				Track.RotKeys.AddUninitialized(NumFrames);
				Track.ScaleKeys.AddUninitialized(NumFrames);

				const int32& ParentBoneIndex = RefSkeleton.GetParentIndex(BoneIndex);

				if(ParentBoneIndex != INDEX_NONE)
				{
					for(int32 KeyIndex = 0; KeyIndex < NumFrames; ++KeyIndex)
					{
						FTransform LocalTransform = AnimationDataInComponentSpace[BoneIndex][KeyIndex].GetRelativeTransform(AnimationDataInComponentSpace[ParentBoneIndex][KeyIndex]);

						Track.PosKeys[KeyIndex] = LocalTransform.GetTranslation();
						Track.RotKeys[KeyIndex] = LocalTransform.GetRotation();
						Track.ScaleKeys[KeyIndex] = LocalTransform.GetScale3D();
					}
				}
				else
				{
					for(int32 KeyIndex = 0; KeyIndex < NumFrames; ++KeyIndex)
					{
						FTransform LocalTransform = AnimationDataInComponentSpace[BoneIndex][KeyIndex];

						Track.PosKeys[KeyIndex] = LocalTransform.GetTranslation();
						Track.RotKeys[KeyIndex] = LocalTransform.GetRotation();
						Track.ScaleKeys[KeyIndex] = LocalTransform.GetScale3D();
					}
				}
			}
		}

		// recreate track map
		TrackToSkeletonMapTable.Empty(AnimationTrackNames.Num());
		TrackToSkeletonMapTable.AddUninitialized(AnimationTrackNames.Num());
		int32 TrackIdx = 0;
		for (auto TrackName : AnimationTrackNames)
		{
			TrackToSkeletonMapTable[TrackIdx++].BoneTreeIndex = MySkeleton->GetReferenceSkeleton().FindBoneIndex(TrackName);
		}

		if (bPerformPostProcess)
		{
			PostProcessSequence();
		}

		return true;
	}

	return false;
}

void UAnimSequence::ClearBakedTransformData()
{
	UE_LOG(LogAnimation, Warning, TEXT("[%s] Detected previous edited data is invalidated. Clearing transform curve data and Source Data. This can happen if you do retarget another animation to this. If not, please report back to Epic. "), *GetName());
	SourceRawAnimationData.Empty();
	//Clear Transform curve data
	RawCurveData.DeleteAllCurveData(ERawCurveTrackTypes::RCT_Transform);
}

void UAnimSequence::BakeTrackCurvesToRawAnimation()
{
	// now bake the curves to the RawAnimationData
	if(NumFrames == 0)
	{
		// fail error?
		return;
	}

	if (!DoesContainTransformCurves())
	{
		if (SourceRawAnimationData.Num() > 0)
		{
			// if curve doesn't exists, we just bring back Source to Raw, and clears Source
			RawAnimationData = MoveTemp(SourceRawAnimationData);
			PostProcessSequence();
		}
	}
	else
	{
		if(SourceRawAnimationData.Num() != 0)
		{
			// we copy SourceRawAnimationData because we'd need to create additive on top of current one
			RawAnimationData = SourceRawAnimationData;
		}

		USkeleton * CurSkeleton = GetSkeleton();
		check(CurSkeleton);
		
		VerifyCurveNames<FTransformCurve>(*CurSkeleton, USkeleton::AnimTrackCurveMappingName, RawCurveData.TransformCurves);
		const FSmartNameMapping*  NameMapping = CurSkeleton->GetSmartNameContainer(USkeleton::AnimTrackCurveMappingName);
		
		// since now I'm about to modify Scale Keys. I should add all of them here at least one key. 
		// if all turns out to be same, it will clear it up. 
		for (auto & RawTrack: RawAnimationData)
		{
			if (RawTrack.ScaleKeys.Num() == 0)
			{
				// at least add one
				static FVector ScaleConstantKey(1.f);
				RawTrack.ScaleKeys.Add(ScaleConstantKey);
			}
		}

		TArray<TPair<const FTransformCurve&, int32>> CurveTrackPairs;
		CurveTrackPairs.Reserve(RawCurveData.TransformCurves.Num());

		for (const auto& Curve : RawCurveData.TransformCurves)
		{
			// find curves first, and then see what is index of this curve
			FName BoneName;

			if(Curve.GetCurveTypeFlag(AACF_Disabled)== false &&
				ensureAlways(NameMapping->GetName(Curve.Name.UID, BoneName)))
			{
				int32 TrackIndex = AnimationTrackNames.Find(BoneName);

				// the animation data doesn't have this track, so insert it
				if(TrackIndex == INDEX_NONE)
				{
					TrackIndex = InsertTrackInternal(BoneName);
					// if it still didn't find, something went horribly wrong
					if(ensure(TrackIndex != INDEX_NONE) == false)
					{
						UE_LOG(LogAnimation, Warning, TEXT("Animation Baking : Error adding %s track."), *BoneName.ToString());
						// I can't do anything about it
						continue;
					}
				}

				CurveTrackPairs.Emplace(Curve, TrackIndex);
			}
		}

		//Cache Source data
		SourceRawAnimationData = RawAnimationData;

		for(const TPair<const FTransformCurve&, int32>& Pair : CurveTrackPairs)
		{
			const FTransformCurve& Curve = Pair.Key;
			const int32 TrackIndex = Pair.Value;
			// now modify data
			auto& RawTrack = RawAnimationData[TrackIndex];

			// since now we're editing keys, 
			// if 1 (which meant constant), just expands to # of frames
			if(RawTrack.PosKeys.Num() == 1)
			{
				FVector OneKey = RawTrack.PosKeys[0];
				RawTrack.PosKeys.Init(OneKey, NumFrames);
			}
			else
			{
				ensure(RawTrack.PosKeys.Num() == NumFrames);
			}

			if(RawTrack.RotKeys.Num() == 1)
			{
				FQuat OneKey = RawTrack.RotKeys[0];
				RawTrack.RotKeys.Init(OneKey, NumFrames);
			}
			else
			{
				ensure(RawTrack.RotKeys.Num() == NumFrames);
			}

			// although we don't allow edit of scale
			// it is important to consider scale when apply transform
			// so make sure this also is included
			if(RawTrack.ScaleKeys.Num() == 1)
			{
				FVector OneKey = RawTrack.ScaleKeys[0];
				RawTrack.ScaleKeys.Init(OneKey, NumFrames);
			}
			else
			{
				ensure(RawTrack.ScaleKeys.Num() == NumFrames);
			}

			// NumFrames can't be zero (filtered earlier)
			float Interval = GetIntervalPerKey(NumFrames, SequenceLength);

			// now we have all data ready to apply
			for(int32 KeyIndex=0; KeyIndex < NumFrames; ++KeyIndex)
			{
				// now evaluate
				FTransformCurve* TransformCurve = static_cast<FTransformCurve*>(RawCurveData.GetCurveData(Curve.Name.UID, ERawCurveTrackTypes::RCT_Transform));

				if(ensure(TransformCurve))
				{
					FTransform AdditiveTransform = TransformCurve->Evaluate(KeyIndex * Interval, 1.0);
					FTransform LocalTransform(RawTrack.RotKeys[KeyIndex], RawTrack.PosKeys[KeyIndex], RawTrack.ScaleKeys[KeyIndex]);
					//  						LocalTransform = LocalTransform * AdditiveTransform;
					//  						RawTrack.RotKeys[KeyIndex] = LocalTransform.GetRotation();
					//  						RawTrack.PosKeys[KeyIndex] = LocalTransform.GetTranslation();
					//  						RawTrack.ScaleKeys[KeyIndex] = LocalTransform.GetScale3D();

					RawTrack.RotKeys[KeyIndex] = LocalTransform.GetRotation() * AdditiveTransform.GetRotation();
					RawTrack.PosKeys[KeyIndex] = LocalTransform.TransformPosition(AdditiveTransform.GetTranslation());
					RawTrack.ScaleKeys[KeyIndex] = LocalTransform.GetScale3D() * AdditiveTransform.GetScale3D();
				}
				else
				{
					FName BoneName = AnimationTrackNames[TrackIndex];
					UE_LOG(LogAnimation, Warning, TEXT("Animation Baking : Missing Curve for %s."), *BoneName.ToString());
				}
			}
		}

		PostProcessSequence();
	}

	bNeedsRebake = false;
}

bool UAnimSequence::DoesNeedRebake() const
{
	return (bNeedsRebake);
}

bool UAnimSequence::DoesContainTransformCurves() const
{
	return (RawCurveData.TransformCurves.Num() > 0);
}

#if WITH_EDITOR
bool  UAnimSequence::HasBakedTransformCurves() const
{
	return DoesContainTransformCurves() && SourceRawAnimationData.Num() > 0;
}

void  UAnimSequence::RestoreSourceData() 
{
	if (HasBakedTransformCurves())
	{
		RawAnimationData = MoveTemp(SourceRawAnimationData);
		bNeedsRebake = true;
	}
}
#endif

void UAnimSequence::AddKeyToSequence(float Time, const FName& BoneName, const FTransform& AdditiveTransform)
{
	// if source animation exists, but doesn't match with raw animation number, it's possible this has been retargetted
	// or for any other reason, track has been modified. Just log here. 
	if (SourceRawAnimationData.Num()>0 && SourceRawAnimationData.Num() != RawAnimationData.Num())
	{
		// currently it contains invalid data to edit
		// clear and start over
		ClearBakedTransformData();
	}

	// find if this already exists, then just add curve data only
	FName CurveName = BoneName;
	USkeleton * CurrentSkeleton = GetSkeleton();
	check (CurrentSkeleton);

	FSmartName NewCurveName;
	CurrentSkeleton->AddSmartNameAndModify(USkeleton::AnimTrackCurveMappingName, CurveName, NewCurveName);

	// add curve - this won't add duplicate curve
	RawCurveData.AddCurveData(NewCurveName, AACF_DriveTrack | AACF_Editable, ERawCurveTrackTypes::RCT_Transform);

	//Add this curve
	FTransformCurve* TransformCurve = static_cast<FTransformCurve*>(RawCurveData.GetCurveData(NewCurveName.UID, ERawCurveTrackTypes::RCT_Transform));
	check(TransformCurve);

	TransformCurve->UpdateOrAddKey(AdditiveTransform, Time);	

	bNeedsRebake = true;
}

void UAnimSequence::ResetAnimation()
{
	// clear everything. Making new animation, so need to reset all the things that belong here
	NumFrames = 0;
	SequenceLength = 0.f;
	RawAnimationData.Empty();
	SourceRawAnimationData.Empty();
	AnimationTrackNames.Empty();
	TrackToSkeletonMapTable.Empty();

	ClearCompressedBoneData();
	ClearCompressedCurveData();

	Notifies.Empty();
	AuthoredSyncMarkers.Empty();
	UniqueMarkerNames.Empty();
	AnimNotifyTracks.Empty();
	RawCurveData.Empty();
	RateScale = 1.f;
}

void UAnimSequence::RefreshTrackMapFromAnimTrackNames()
{
	TrackToSkeletonMapTable.Empty();

	const USkeleton * MySkeleton = GetSkeleton();
	const FReferenceSkeleton& RefSkeleton = MySkeleton->GetReferenceSkeleton();
	const int32 NumBones = AnimationTrackNames.Num();
	TrackToSkeletonMapTable.AddUninitialized(NumBones);

	bool bNeedsFixing = false;
	const int32 NumTracks = AnimationTrackNames.Num();
	for(int32 I=NumTracks-1; I>=0; --I)
	{
		int32 BoneTreeIndex = RefSkeleton.FindBoneIndex(AnimationTrackNames[I]);
		if(BoneTreeIndex == INDEX_NONE)
		{
			RemoveTrack(I);
		}
		else
		{
			TrackToSkeletonMapTable[I].BoneTreeIndex = BoneTreeIndex;
		}
	}
}

uint8* UAnimSequence::FindSyncMarkerPropertyData(int32 SyncMarkerIndex, FArrayProperty*& ArrayProperty)
{
	ArrayProperty = NULL;

	if (AuthoredSyncMarkers.IsValidIndex(SyncMarkerIndex))
	{
		return FindArrayProperty(TEXT("AuthoredSyncMarkers"), ArrayProperty, SyncMarkerIndex);
	}
	return NULL;
}

bool UAnimSequence::CreateAnimation(USkeletalMesh* Mesh)
{
	// create animation from Mesh's ref pose
	if (Mesh)
	{
		ResetAnimation();

		const FReferenceSkeleton& RefSkeleton = Mesh->RefSkeleton;
		SequenceLength = MINIMUM_ANIMATION_LENGTH;
		NumFrames = 1;

		const int32 NumBones = RefSkeleton.GetRawBoneNum();
		RawAnimationData.AddZeroed(NumBones);
		AnimationTrackNames.AddUninitialized(NumBones);

		const TArray<FTransform>& RefBonePose = RefSkeleton.GetRawRefBonePose();

		check (RefBonePose.Num() == NumBones);

		for (int32 BoneIndex=0; BoneIndex<NumBones; ++BoneIndex)
		{
			AnimationTrackNames[BoneIndex] = RefSkeleton.GetBoneName(BoneIndex);

			FRawAnimSequenceTrack& RawTrack = RawAnimationData[BoneIndex];

			RawTrack.PosKeys.Add(RefBonePose[BoneIndex].GetTranslation());
			RawTrack.RotKeys.Add(RefBonePose[BoneIndex].GetRotation());
			RawTrack.ScaleKeys.Add(RefBonePose[BoneIndex].GetScale3D());
		}

		// refresh TrackToskeletonMapIndex
		RefreshTrackMapFromAnimTrackNames();

		// should recreate track map
		PostProcessSequence();
		return true;
	}

	return false;
}

bool UAnimSequence::CreateAnimation(USkeletalMeshComponent* MeshComponent)
{
	if(MeshComponent && MeshComponent->SkeletalMesh)
	{
		USkeletalMesh * Mesh = MeshComponent->SkeletalMesh;

		ResetAnimation();

		const FReferenceSkeleton& RefSkeleton = Mesh->RefSkeleton;
		SequenceLength = MINIMUM_ANIMATION_LENGTH;
		NumFrames = 1;

		const int32 NumBones = RefSkeleton.GetRawBoneNum();
		RawAnimationData.AddZeroed(NumBones);
		AnimationTrackNames.AddUninitialized(NumBones);

		const TArray<FTransform> BoneSpaceTransforms = MeshComponent->GetBoneSpaceTransforms();

		check(BoneSpaceTransforms.Num() >= NumBones);

		for(int32 BoneIndex=0; BoneIndex<NumBones; ++BoneIndex)
		{
			AnimationTrackNames[BoneIndex] = RefSkeleton.GetBoneName(BoneIndex);

			FRawAnimSequenceTrack& RawTrack = RawAnimationData[BoneIndex];

			RawTrack.PosKeys.Add(BoneSpaceTransforms[BoneIndex].GetTranslation());
			RawTrack.RotKeys.Add(BoneSpaceTransforms[BoneIndex].GetRotation());
			RawTrack.ScaleKeys.Add(BoneSpaceTransforms[BoneIndex].GetScale3D());
		}

		// refresh TrackToskeletonMapIndex
		RefreshTrackMapFromAnimTrackNames();

		// should recreate track map
		PostProcessSequence();
		return true;
	}

	return false;
}

bool UAnimSequence::CreateAnimation(UAnimSequence* Sequence)
{
	if(Sequence)
	{
		ResetAnimation();

		SequenceLength = Sequence->SequenceLength;
		NumFrames = Sequence->NumFrames;

		RawAnimationData = Sequence->RawAnimationData;
		AnimationTrackNames = Sequence->AnimationTrackNames;

		Notifies = Sequence->Notifies;
		AnimNotifyTracks = Sequence->AnimNotifyTracks;
		RawCurveData = Sequence->RawCurveData;
		// keep the same setting as source
		bNeedsRebake = Sequence->DoesNeedRebake();
		SourceRawAnimationData = Sequence->SourceRawAnimationData;

		// refresh TrackToskeletonMapIndex
		RefreshTrackMapFromAnimTrackNames();

		// should recreate track map
		PostProcessSequence();
		return true;
	}
	
	return false;
}

#endif

void UAnimSequence::RefreshCacheData()
{
	SortSyncMarkers();
#if WITH_EDITOR
	for (int32 TrackIndex = 0; TrackIndex < AnimNotifyTracks.Num(); ++TrackIndex)
	{
		AnimNotifyTracks[TrackIndex].SyncMarkers.Empty();
	}
	for (FAnimSyncMarker& SyncMarker : AuthoredSyncMarkers)
	{
		const int32 TrackIndex = SyncMarker.TrackIndex;
		if (AnimNotifyTracks.IsValidIndex(TrackIndex))
		{
			AnimNotifyTracks[TrackIndex].SyncMarkers.Add(&SyncMarker);
		}
		else
		{
			// This should not happen, but if it does we must find somewhere else to add it
			ensureMsgf(0, TEXT("AnimNotifyTrack: Wrong indices found"));
			AnimNotifyTracks[0].SyncMarkers.Add(&SyncMarker);
			SyncMarker.TrackIndex = 0;
		}
	}
#endif
	Super::RefreshCacheData();
}

void UAnimSequence::EvaluateCurveData(FBlendedCurve& OutCurve, float CurrentTime, bool bForceUseRawData) const
{
	SCOPE_CYCLE_COUNTER(STAT_AnimSeq_EvalCurveData);

	if (OutCurve.NumValidCurveCount == 0)
	{
		return;
	}

	if (bUseRawDataOnly || bForceUseRawData || !IsCurveCompressedDataValid())
	{
		Super::EvaluateCurveData(OutCurve, CurrentTime, bForceUseRawData);
	}
	else
	{
		CSV_SCOPED_TIMING_STAT(Animation, EvaluateCurveData);
		CompressedData.CurveCompressionCodec->DecompressCurves(CompressedData, OutCurve, CurrentTime);
	}
}

float UAnimSequence::EvaluateCurveData(SmartName::UID_Type CurveUID, float CurrentTime, bool bForceUseRawData) const
{
	SCOPE_CYCLE_COUNTER(STAT_AnimSeq_EvalCurveData);

	if (bUseRawDataOnly || bForceUseRawData || !IsCurveCompressedDataValid())
	{
		return Super::EvaluateCurveData(CurveUID, CurrentTime, bForceUseRawData);
	}
	else
	{
		return CompressedData.CurveCompressionCodec->DecompressCurve(CompressedData, CurveUID, CurrentTime);
	}
}

bool UAnimSequence::HasCurveData(SmartName::UID_Type CurveUID, bool bForceUseRawData) const
{
	if (bUseRawDataOnly || bForceUseRawData || !IsCurveCompressedDataValid())
	{
		return Super::HasCurveData(CurveUID, bForceUseRawData);
	}

	for (const FSmartName& CurveName : CompressedData.CompressedCurveNames)
	{
		if (CurveName.UID == CurveUID)
		{
			return true;
		}
	}

	return false;
}

void UAnimSequence::RefreshSyncMarkerDataFromAuthored()
{
#if WITH_EDITOR
	MarkerDataUpdateCounter++;
#endif

	if (AuthoredSyncMarkers.Num() > 0)
	{
		UniqueMarkerNames.Reset();
		UniqueMarkerNames.Reserve(AuthoredSyncMarkers.Num());

		const FAnimSyncMarker* PreviousMarker = nullptr;
		for (const FAnimSyncMarker& Marker : AuthoredSyncMarkers)
		{
			UniqueMarkerNames.AddUnique(Marker.MarkerName);
			PreviousMarker = &Marker;
		}
	}
	else
	{
		UniqueMarkerNames.Empty();
	}

#if WITH_EDITOR
	check(IsInGameThread());

	// Update blend spaces that may be referencing us
	for(TObjectIterator<UBlendSpaceBase> It; It; ++It)
	{
		if(!It->HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad))
		{
			It->RuntimeValidateMarkerData();
		}
	}
#endif
}

bool IsMarkerValid(const FAnimSyncMarker* Marker, bool bLooping, const TArray<FName>& ValidMarkerNames)
{
	return (Marker == nullptr && !bLooping) || (Marker && ValidMarkerNames.Contains(Marker->MarkerName));
}

void UAnimSequence::AdvanceMarkerPhaseAsLeader(bool bLooping, float MoveDelta, const TArray<FName>& ValidMarkerNames, float& CurrentTime, FMarkerPair& PrevMarker, FMarkerPair& NextMarker, TArray<FPassedMarker>& MarkersPassed) const
{
	check(MoveDelta != 0.f);
	const bool bPlayingForwards = MoveDelta > 0.f;
	float CurrentMoveDelta = MoveDelta;

	// Hard to reproduce issue triggering this, ensure & clamp for now
	ensureMsgf(CurrentTime >= 0.f && CurrentTime <= SequenceLength, TEXT("Current time inside of AdvanceMarkerPhaseAsLeader is out of range %.3f of 0.0 to %.3f\n    Sequence: %s"), CurrentTime, SequenceLength, *GetFullName());

	CurrentTime = FMath::Clamp(CurrentTime, 0.f, SequenceLength);

	if (bPlayingForwards)
	{
		while (true)
		{
			if (NextMarker.MarkerIndex == -1)
			{
				float PrevCurrentTime = CurrentTime;
				CurrentTime = FMath::Min(CurrentTime + CurrentMoveDelta, SequenceLength);
				NextMarker.TimeToMarker = SequenceLength - CurrentTime;
				PrevMarker.TimeToMarker -= CurrentTime - PrevCurrentTime; //Add how far we moved to distance from previous marker
				break;
			}
			const FAnimSyncMarker& NextSyncMarker = AuthoredSyncMarkers[NextMarker.MarkerIndex];
			checkSlow(ValidMarkerNames.Contains(NextSyncMarker.MarkerName));

			if (CurrentMoveDelta > NextMarker.TimeToMarker)
			{
				CurrentTime = NextSyncMarker.Time;
				CurrentMoveDelta -= NextMarker.TimeToMarker;

				PrevMarker.MarkerIndex = NextMarker.MarkerIndex;
				PrevMarker.TimeToMarker = -CurrentMoveDelta;

				int32 PassedMarker = MarkersPassed.Add(FPassedMarker());
				MarkersPassed[PassedMarker].PassedMarkerName = NextSyncMarker.MarkerName;
				MarkersPassed[PassedMarker].DeltaTimeWhenPassed = CurrentMoveDelta;

				float MarkerTimeOffset = 0.f;
				do
				{
					++NextMarker.MarkerIndex;
					if (NextMarker.MarkerIndex >= AuthoredSyncMarkers.Num())
					{
						if (!bLooping)
						{
							NextMarker.MarkerIndex = -1;
							break;
						}
						NextMarker.MarkerIndex = 0;
						MarkerTimeOffset = SequenceLength;
					}
				} while (!ValidMarkerNames.Contains(AuthoredSyncMarkers[NextMarker.MarkerIndex].MarkerName));
				if (NextMarker.MarkerIndex != -1)
				{
					NextMarker.TimeToMarker = MarkerTimeOffset + AuthoredSyncMarkers[NextMarker.MarkerIndex].Time - CurrentTime;
				}
			}
			else
			{
				CurrentTime = FMath::Fmod(CurrentTime + CurrentMoveDelta, SequenceLength);
				if (CurrentTime < 0.f)
				{
					CurrentTime += SequenceLength;
				}
				NextMarker.TimeToMarker -= CurrentMoveDelta;
				PrevMarker.TimeToMarker -= CurrentMoveDelta;
				break;
			}
		}
	}
	else
	{
		while (true)
		{
			if (PrevMarker.MarkerIndex == -1)
			{
				float PrevCurrentTime = CurrentTime;
				CurrentTime = FMath::Max(CurrentTime + CurrentMoveDelta, 0.f);
				PrevMarker.TimeToMarker = CurrentTime;
				NextMarker.TimeToMarker -= CurrentTime - PrevCurrentTime; //Add how far we moved to distance from previous marker
				break;
			}
			const FAnimSyncMarker& PrevSyncMarker = AuthoredSyncMarkers[PrevMarker.MarkerIndex];
			checkSlow(ValidMarkerNames.Contains(PrevSyncMarker.MarkerName));
			
			if (CurrentMoveDelta < PrevMarker.TimeToMarker)
			{
				CurrentTime = PrevSyncMarker.Time;
				CurrentMoveDelta -= PrevMarker.TimeToMarker;

				NextMarker.MarkerIndex = PrevMarker.MarkerIndex;
				NextMarker.TimeToMarker = -CurrentMoveDelta;

				int32 PassedMarker = MarkersPassed.Add(FPassedMarker());
				MarkersPassed[PassedMarker].PassedMarkerName = PrevSyncMarker.MarkerName;
				MarkersPassed[PassedMarker].DeltaTimeWhenPassed = CurrentMoveDelta;

				float MarkerTimeOffset = 0.f;
				do
				{
					--PrevMarker.MarkerIndex;
					if (PrevMarker.MarkerIndex < 0)
					{
						if (!bLooping)
						{
							PrevMarker.MarkerIndex = -1;
							break;
						}
						PrevMarker.MarkerIndex = AuthoredSyncMarkers.Num() - 1;
						MarkerTimeOffset -= SequenceLength;
					}
				} while (!ValidMarkerNames.Contains(AuthoredSyncMarkers[PrevMarker.MarkerIndex].MarkerName));
				if (PrevMarker.MarkerIndex != -1)
				{
					PrevMarker.TimeToMarker = MarkerTimeOffset + AuthoredSyncMarkers[PrevMarker.MarkerIndex].Time - CurrentTime;
				}
			}
			else
			{
				CurrentTime = FMath::Fmod(CurrentTime + CurrentMoveDelta, SequenceLength);
				if (CurrentTime < 0.f)
				{
					CurrentTime += SequenceLength;
				}
				PrevMarker.TimeToMarker -= CurrentMoveDelta;
				NextMarker.TimeToMarker -= CurrentMoveDelta;
				break;
			}
		}
	}

	check(CurrentTime >= 0.f && CurrentTime <= SequenceLength);
}

void AdvanceMarkerForwards(int32& Marker, FName MarkerToFind, bool bLooping, const TArray<FAnimSyncMarker>& AuthoredSyncMarkers)
{
	int32 MaxIterations = AuthoredSyncMarkers.Num();
	while ((AuthoredSyncMarkers[Marker].MarkerName != MarkerToFind) && (--MaxIterations >= 0))
	{
		++Marker;
		if (Marker == AuthoredSyncMarkers.Num() && !bLooping)
		{
			break;
		}
		Marker %= AuthoredSyncMarkers.Num();
	}

	if (!AuthoredSyncMarkers.IsValidIndex(Marker) || (AuthoredSyncMarkers[Marker].MarkerName != MarkerToFind))
	{
		Marker = MarkerIndexSpecialValues::AnimationBoundary;
	}
}

int32 MarkerCounterSpaceTransform(int32 MaxMarker, int32 Source)
{
	return MaxMarker - 1 - Source;
}

void AdvanceMarkerBackwards(int32& Marker, FName MarkerToFind, bool bLooping, const TArray<FAnimSyncMarker>& AuthoredSyncMarkers)
{
	int32 MaxIterations = AuthoredSyncMarkers.Num();
	const int32 MarkerMax = AuthoredSyncMarkers.Num();
	int32 Counter = MarkerCounterSpaceTransform(MarkerMax, Marker);
	while ((AuthoredSyncMarkers[Marker].MarkerName != MarkerToFind) && (--MaxIterations >= 0))
	{
		if ((Marker == 0) && !bLooping)
		{
			break;
		}
		Counter = (Counter + 1) % MarkerMax;
		Marker = MarkerCounterSpaceTransform(MarkerMax, Counter);
	}

	if (!AuthoredSyncMarkers.IsValidIndex(Marker) || (AuthoredSyncMarkers[Marker].MarkerName != MarkerToFind))
	{
		Marker = MarkerIndexSpecialValues::AnimationBoundary;
	}
}

bool MarkerMatchesPosition(const UAnimSequence* Sequence, int32 MarkerIndex, FName CorrectMarker)
{
	checkf(MarkerIndex != MarkerIndexSpecialValues::Unitialized, TEXT("Uninitialized marker supplied to MarkerMatchesPosition. Anim: %s Expecting marker %s (Added to help debug Jira OR-9675)"), *Sequence->GetName(), *CorrectMarker.ToString());
	return MarkerIndex == MarkerIndexSpecialValues::AnimationBoundary || CorrectMarker == Sequence->AuthoredSyncMarkers[MarkerIndex].MarkerName;
}

void UAnimSequence::ValidateCurrentPosition(const FMarkerSyncAnimPosition& Position, bool bPlayingForwards, bool bLooping, float&CurrentTime, FMarkerPair& PreviousMarker, FMarkerPair& NextMarker) const
{
	if (bPlayingForwards)
	{
		if (!MarkerMatchesPosition(this, PreviousMarker.MarkerIndex, Position.PreviousMarkerName))
		{
			AdvanceMarkerForwards(PreviousMarker.MarkerIndex, Position.PreviousMarkerName, bLooping, AuthoredSyncMarkers);
			NextMarker.MarkerIndex = (PreviousMarker.MarkerIndex + 1);
			if(NextMarker.MarkerIndex >= AuthoredSyncMarkers.Num())
			{
				NextMarker.MarkerIndex = bLooping ? NextMarker.MarkerIndex % AuthoredSyncMarkers.Num() : MarkerIndexSpecialValues::AnimationBoundary;
			}
		}

		if (!MarkerMatchesPosition(this, NextMarker.MarkerIndex, Position.NextMarkerName))
		{
			AdvanceMarkerForwards(NextMarker.MarkerIndex, Position.NextMarkerName, bLooping, AuthoredSyncMarkers);
		}
	}
	else
	{
		const int32 MarkerRange = AuthoredSyncMarkers.Num();
		if (!MarkerMatchesPosition(this, NextMarker.MarkerIndex, Position.NextMarkerName))
		{
			AdvanceMarkerBackwards(NextMarker.MarkerIndex, Position.NextMarkerName, bLooping, AuthoredSyncMarkers);
			if(NextMarker.MarkerIndex == MarkerIndexSpecialValues::AnimationBoundary || (NextMarker.MarkerIndex == 0 && bLooping))
			{
				PreviousMarker.MarkerIndex = AuthoredSyncMarkers.Num() - 1;
			}
			else
			{
				PreviousMarker.MarkerIndex = NextMarker.MarkerIndex - 1;
			}
		}
		if (!MarkerMatchesPosition(this, PreviousMarker.MarkerIndex, Position.PreviousMarkerName))
		{
			AdvanceMarkerBackwards(PreviousMarker.MarkerIndex, Position.PreviousMarkerName, bLooping, AuthoredSyncMarkers);
		}
	}

	checkSlow(MarkerMatchesPosition(this, PreviousMarker.MarkerIndex, Position.PreviousMarkerName));
	checkSlow(MarkerMatchesPosition(this, NextMarker.MarkerIndex, Position.NextMarkerName));

	// Only reset position if we found valid markers. Otherwise stay where we are to not pop.
	if ((PreviousMarker.MarkerIndex != MarkerIndexSpecialValues::AnimationBoundary) && (NextMarker.MarkerIndex != MarkerIndexSpecialValues::AnimationBoundary))
	{
		CurrentTime = GetCurrentTimeFromMarkers(PreviousMarker, NextMarker, Position.PositionBetweenMarkers);
	}
}

bool UAnimSequence::UseRawDataForPoseExtraction(const FBoneContainer& RequiredBones) const
{
	return bUseRawDataOnly || (GetSkeletonVirtualBoneGuid() != GetSkeleton()->GetVirtualBoneGuid()) || RequiredBones.GetDisableRetargeting() || RequiredBones.ShouldUseRawData() || RequiredBones.ShouldUseSourceData();
}

void UAnimSequence::GetCustomAttributes(FAnimationPoseData& OutAnimationPoseData, const FAnimExtractContext& ExtractionContext, bool bUseRawData) const
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_GetCustomAttributes);

	const FBoneContainer& RequiredBones = OutAnimationPoseData.GetPose().GetBoneContainer();
	FStackCustomAttributes& OutAttributes = OutAnimationPoseData.GetAttributes();

#if WITH_EDITOR
	if (bUseRawData)
	{
		for (const FCustomAttributePerBoneData& BoneAttributes : PerBoneCustomAttributeData)
		{
			const FCompactPoseBoneIndex PoseBoneIndex = RequiredBones.GetCompactPoseIndexFromSkeletonIndex(BoneAttributes.BoneTreeIndex);

			for (const FCustomAttribute& Attribute : BoneAttributes.Attributes)
			{
				FCustomAttributesRuntime::GetAttributeValue(OutAttributes, PoseBoneIndex, Attribute, ExtractionContext);
			}
		}
	}
	else
#endif // WITH_EDITOR
	{
		for (const FBakedCustomAttributePerBoneData& BakedBoneAttributes : BakedPerBoneCustomAttributeData)
		{
			const FCompactPoseBoneIndex PoseBoneIndex = RequiredBones.GetCompactPoseIndexFromSkeletonIndex(BakedBoneAttributes.BoneTreeIndex);
			for (const FBakedFloatCustomAttribute& Attribute : BakedBoneAttributes.FloatAttributes)
			{
				const ECustomAttributeBlendType BlendType = FCustomAttributesRuntime::GetAttributeBlendType(Attribute.AttributeName);
				const float Value = Attribute.FloatCurve.Eval(ExtractionContext.CurrentTime);
				OutAttributes.AddBoneAttribute<float>(PoseBoneIndex, Attribute.AttributeName, BlendType, Value);
			}

			for (const FBakedIntegerCustomAttribute& Attribute : BakedBoneAttributes.IntAttributes)
			{
				const ECustomAttributeBlendType BlendType = FCustomAttributesRuntime::GetAttributeBlendType(Attribute.AttributeName);
				const int32 Value = Attribute.IntCurve.Evaluate(ExtractionContext.CurrentTime);
				OutAttributes.AddBoneAttribute<int32>(PoseBoneIndex, Attribute.AttributeName, BlendType, Value);
			}

			for (const FBakedStringCustomAttribute& Attribute : BakedBoneAttributes.StringAttributes)
			{
				static const FString DefaultValue = TEXT("");
				const FString Value = Attribute.StringCurve.Eval(ExtractionContext.CurrentTime, DefaultValue);
				OutAttributes.AddBoneAttribute<FString>(PoseBoneIndex, Attribute.AttributeName, ECustomAttributeBlendType::Override, Value);
			}
		}
	}
}

#if WITH_EDITOR
void UAnimSequence::RemoveCustomAttribute(const FName& BoneName, const FName& AttributeName)
{
	FCustomAttributePerBoneData* DataPtr = PerBoneCustomAttributeData.FindByPredicate([BoneName, this](FCustomAttributePerBoneData& Attribute)
	{
		return Attribute.BoneTreeIndex == GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(BoneName);
	});

	if (DataPtr)
	{
		const int32 NumRemoved = DataPtr->Attributes.RemoveAll([AttributeName](FCustomAttribute& Attribute)
		{
			return Attribute.Name == AttributeName;
		});

		// In case there are no custom attributes left for this bone, remove the wrapping structure entry as well
		if (DataPtr->Attributes.Num() == 0)
		{
			ensure(PerBoneCustomAttributeData.RemoveAll([DataPtr](FCustomAttributePerBoneData& Attribute)
			{
				return Attribute.BoneTreeIndex == DataPtr->BoneTreeIndex;
			}) == 1);
		}

		if (NumRemoved)
		{
			// Update the Guid used to keep track of raw / baked versions
			CustomAttributesGuid = FGuid::NewGuid();
		}
	}
}

void UAnimSequence::RemoveAllCustomAttributesForBone(const FName& BoneName)
{
	const USkeleton* CurrentSkeleton = GetSkeleton();

	if (CurrentSkeleton)
	{
		const int32 BoneIndex = CurrentSkeleton->GetReferenceSkeleton().FindBoneIndex(BoneName);

		if (BoneIndex != INDEX_NONE)
		{
			const int32 NumRemoved = PerBoneCustomAttributeData.RemoveAll([BoneIndex](const FCustomAttributePerBoneData PerBoneData)
			{
				return PerBoneData.BoneTreeIndex == BoneIndex;
			});

			if (NumRemoved)
			{
				// Update the Guid used to keep track of raw / baked versions
				CustomAttributesGuid = FGuid::NewGuid();
			}
		}
	}
}

void UAnimSequence::RemoveAllCustomAttributes()
{
	if (PerBoneCustomAttributeData.Num())
	{
		// Update the Guid used to keep track of raw / baked versions
		CustomAttributesGuid = FGuid::NewGuid();
	}

	PerBoneCustomAttributeData.Empty();
}

void UAnimSequence::GetCustomAttributesForBone(const FName& BoneName, TArray<FCustomAttribute>& OutAttributes) const
{
	const USkeleton* CurrentSkeleton = GetSkeleton();

	if (CurrentSkeleton)
	{
		const int32 BoneIndex = CurrentSkeleton->GetReferenceSkeleton().FindBoneIndex(BoneName);

		if (BoneIndex != INDEX_NONE)
		{
			for (const FCustomAttributePerBoneData& PerBoneData : PerBoneCustomAttributeData)
			{
				if (PerBoneData.BoneTreeIndex == BoneIndex)
				{
					OutAttributes.Append(PerBoneData.Attributes);
				}
			}
		}
	}
}

// Helper functionality to populate a curve by sampling the custom attribute data
template<typename DataType, typename CurveType>
void ConvertAttributeToAdditive(const FCustomAttribute& AdditiveAttribute, const FCustomAttribute& RefAttribute, CurveType& InOutCurve, float SamplingTime, int32 NumberOfFrames, TFunctionRef<float(float Time)> GetReferenceTime)
{
	for (int32 Frame = 0; Frame < NumberOfFrames; ++Frame)
	{
		const float CurrentFrameTime = Frame * SamplingTime;

		DataType AdditiveValue;
		FCustomAttributesRuntime::GetAttributeValue(AdditiveAttribute, CurrentFrameTime, AdditiveValue);

		DataType RefValue;
		FCustomAttributesRuntime::GetAttributeValue(RefAttribute, GetReferenceTime(CurrentFrameTime), RefValue);

		const DataType Value = RefValue - AdditiveValue;
		InOutCurve.AddKey(CurrentFrameTime, Value);
	}
}

void UAnimSequence::SynchronousCustomAttributesCompression()
{
	// If we are additive, we'll need to sample the base pose (against we're additive) and subtract the attributes from the base ones
	const bool bShouldSampleBasePose = IsValidAdditive() && RefPoseType != ABPT_RefPose;
	
	BakedPerBoneCustomAttributeData.Empty(PerBoneCustomAttributeData.Num());

	auto ProcessCustomAttribute = [this](const FCustomAttribute& Attribute, FBakedCustomAttributePerBoneData& BakedBoneAttributes)
	{		
		switch (static_cast<EVariantTypes>(Attribute.VariantType))
		{
			case EVariantTypes::Float:
			{
				FBakedFloatCustomAttribute& BakedFloatAttribute = BakedBoneAttributes.FloatAttributes.AddDefaulted_GetRef();
				BakedFloatAttribute.AttributeName = Attribute.Name;

				FSimpleCurve& FloatCurve = BakedFloatAttribute.FloatCurve;

				TArray<FSimpleCurveKey> Keys;
				for (int32 KeyIndex = 0; KeyIndex < Attribute.Times.Num(); ++KeyIndex)
				{
					const FVariant& VariantValue = Attribute.Values[KeyIndex];
					FloatCurve.AddKey(Attribute.Times[KeyIndex], VariantValue.GetValue<float>());
				}

				FloatCurve.SetDefaultValue(FloatCurve.GetFirstKey().Value);
				FloatCurve.RemoveRedundantKeys(0.f);
				break;
			}

			case EVariantTypes::Int32:
			{
				FBakedIntegerCustomAttribute& BakedIntAttribute = BakedBoneAttributes.IntAttributes.AddDefaulted_GetRef();
				BakedIntAttribute.AttributeName = Attribute.Name;

				FIntegralCurve& IntCurve = BakedIntAttribute.IntCurve;
				for (int32 KeyIndex = 0; KeyIndex < Attribute.Times.Num(); ++KeyIndex)
				{
					const FVariant& VariantValue = Attribute.Values[KeyIndex];
					IntCurve.AddKey(Attribute.Times[KeyIndex], VariantValue.GetValue<int32>());
				}

				IntCurve.SetDefaultValue(IntCurve.GetKey(IntCurve.GetFirstKeyHandle()).Value);
				IntCurve.RemoveRedundantKeys();
				break;
			}

			case EVariantTypes::String:
			{
				FBakedStringCustomAttribute& BakedStringAttribute = BakedBoneAttributes.StringAttributes.AddDefaulted_GetRef();
				BakedStringAttribute.AttributeName = Attribute.Name;

				FStringCurve& StringCurve = BakedStringAttribute.StringCurve;
				for (int32 KeyIndex = 0; KeyIndex < Attribute.Times.Num(); ++KeyIndex)
				{
					const FVariant& VariantValue = Attribute.Values[KeyIndex];
					StringCurve.AddKey(Attribute.Times[KeyIndex], VariantValue.GetValue<FString>());
				}

				StringCurve.SetDefaultValue(StringCurve.GetKey(StringCurve.GetFirstKeyHandle()).Value);
				StringCurve.RemoveRedundantKeys();
				break;
			}

			default:
			{
				ensureMsgf(false, TEXT("Invalid data variant type for custom attribute, only int32, float and FString are currently supported"));
				break;
			}
		}
	};

	if (bShouldSampleBasePose)
	{
		// Behaviour for determining the time to sample the base pose attributes
		auto GetBasePoseTimeToSample = [this](float InTime) -> float
		{
			float BasePoseTime = 0.f;

			if (RefPoseType == ABPT_AnimScaled)
			{
				const float Fraction = (SequenceLength > 0.f) ? FMath::Clamp<float>(InTime / SequenceLength, 0.f, 1.f) : 0.f;
				BasePoseTime = RefPoseSeq->SequenceLength * Fraction;
			}
			else if (RefPoseType == ABPT_AnimFrame)
			{
				const float Fraction = (RefPoseSeq->NumFrames > 0) ? FMath::Clamp<float>((float)RefFrameIndex / (float)RefPoseSeq->NumFrames, 0.f, 1.f) : 0.f;
				BasePoseTime = RefPoseSeq->SequenceLength * Fraction;

			}

			return BasePoseTime;
		};

		const FReferenceSkeleton& RefSkeleton = GetSkeleton()->GetReferenceSkeleton();

		// Helper struct to match sample timings with regular additive baking
		FByFramePoseEvalContext EvalContext(this);
		for (const FCustomAttributePerBoneData& BoneAttributes : PerBoneCustomAttributeData)
		{
			FBakedCustomAttributePerBoneData& BakedBoneAttributes = BakedPerBoneCustomAttributeData.AddDefaulted_GetRef();
			BakedBoneAttributes.BoneTreeIndex = BoneAttributes.BoneTreeIndex;

			TArray<FCustomAttribute> ReferenceSequenceAttributes;			
			RefPoseSeq->GetCustomAttributesForBone(RefSkeleton.GetBoneName(BoneAttributes.BoneTreeIndex), ReferenceSequenceAttributes);

			// Check whether or not the base sequence has any attributes
			if (!ReferenceSequenceAttributes.Num())
			{
				for (const FCustomAttribute& Attribute : BoneAttributes.Attributes)
				{
					ProcessCustomAttribute(Attribute, BakedBoneAttributes);
				}
			}
			else
			{
				for (const FCustomAttribute& Attribute : BoneAttributes.Attributes)
				{
					// Try and find equivalent in reference sequence
					const FCustomAttribute* RefAttribute = ReferenceSequenceAttributes.FindByPredicate([Attribute](const FCustomAttribute& Attr)
					{	
						return Attribute.Name == Attr.Name && Attribute.VariantType == Attr.VariantType;
					});

					if (RefAttribute)
					{
						switch (static_cast<EVariantTypes>(Attribute.VariantType))
						{
							case EVariantTypes::Float:
							{
								FBakedFloatCustomAttribute& BakedFloatAttribute = BakedBoneAttributes.FloatAttributes.AddDefaulted_GetRef();
								BakedFloatAttribute.AttributeName = Attribute.Name;

								FSimpleCurve& FloatCurve = BakedFloatAttribute.FloatCurve;
								ConvertAttributeToAdditive<float, FSimpleCurve>(Attribute, *RefAttribute, FloatCurve, EvalContext.IntervalTime, NumFrames, GetBasePoseTimeToSample);
								FloatCurve.RemoveRedundantKeys(0.f);

								break;
							}

							case EVariantTypes::Int32:
							{
								FBakedIntegerCustomAttribute& BakedIntAttribute = BakedBoneAttributes.IntAttributes.AddDefaulted_GetRef();
								BakedIntAttribute.AttributeName = Attribute.Name;

								FIntegralCurve& IntCurve = BakedIntAttribute.IntCurve;
								ConvertAttributeToAdditive<int32, FIntegralCurve>(Attribute, *RefAttribute, IntCurve, EvalContext.IntervalTime, NumFrames, GetBasePoseTimeToSample);
								IntCurve.RemoveRedundantKeys();
							
								break;
							}

							case EVariantTypes::String:
							{
								ProcessCustomAttribute(Attribute, BakedBoneAttributes);
								break;
							}
						}
					}
					else
					{
						ProcessCustomAttribute(Attribute, BakedBoneAttributes);
					}					
				}
			}
		}
	}
	else
	{
		for (const FCustomAttributePerBoneData& BoneAttributes : PerBoneCustomAttributeData)
		{
			FBakedCustomAttributePerBoneData& BakedBoneAttributes = BakedPerBoneCustomAttributeData.AddDefaulted_GetRef();
			BakedBoneAttributes.BoneTreeIndex = BoneAttributes.BoneTreeIndex;

			for (const FCustomAttribute& Attribute : BoneAttributes.Attributes)
			{
				ProcessCustomAttribute(Attribute, BakedBoneAttributes);
			}
		}
	}

	// Match baked/raw attributes guid
	BakedCustomAttributesGuid = CustomAttributesGuid;
}

FCustomAttributePerBoneData& UAnimSequence::FindOrAddCustomAttributeForBone(const FName& BoneName)
{
	FCustomAttributePerBoneData* DataPtr = PerBoneCustomAttributeData.FindByPredicate([BoneName, this](FCustomAttributePerBoneData& Attribute)
	{
		return Attribute.BoneTreeIndex == GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(BoneName);
	});

	return DataPtr ? *DataPtr : PerBoneCustomAttributeData.AddDefaulted_GetRef();
}
#endif // WITH_EDITOR

void UAnimSequence::AdvanceMarkerPhaseAsFollower(const FMarkerTickContext& Context, float DeltaRemaining, bool bLooping, float& CurrentTime, FMarkerPair& PreviousMarker, FMarkerPair& NextMarker) const
{
	const bool bPlayingForwards = DeltaRemaining > 0.f;

	ValidateCurrentPosition(Context.GetMarkerSyncStartPosition(), bPlayingForwards, bLooping, CurrentTime, PreviousMarker, NextMarker);
	if (bPlayingForwards)
	{
		int32 PassedMarkersIndex = 0;
		do
		{
			if (NextMarker.MarkerIndex == -1)
			{
				check(!bLooping || Context.GetMarkerSyncEndPosition().NextMarkerName == NAME_None); // shouldnt have an end of anim marker if looping
				CurrentTime = FMath::Min(CurrentTime + DeltaRemaining, SequenceLength);
				break;
			}
			else if (PassedMarkersIndex < Context.MarkersPassedThisTick.Num())
			{
				PreviousMarker.MarkerIndex = NextMarker.MarkerIndex;
				checkSlow(NextMarker.MarkerIndex != -1);
				const FPassedMarker& PassedMarker = Context.MarkersPassedThisTick[PassedMarkersIndex];
				AdvanceMarkerForwards(NextMarker.MarkerIndex, PassedMarker.PassedMarkerName, bLooping, AuthoredSyncMarkers);
				if (NextMarker.MarkerIndex == -1)
				{
					DeltaRemaining = PassedMarker.DeltaTimeWhenPassed;
				}
				++PassedMarkersIndex;
			}
		} while (PassedMarkersIndex < Context.MarkersPassedThisTick.Num());

		const FMarkerSyncAnimPosition& End = Context.GetMarkerSyncEndPosition();
		
		if (End.NextMarkerName == NAME_None)
		{
			NextMarker.MarkerIndex = -1;
		}

		if (NextMarker.MarkerIndex != -1 && Context.MarkersPassedThisTick.Num() > 0)
		{
			AdvanceMarkerForwards(NextMarker.MarkerIndex, End.NextMarkerName, bLooping, AuthoredSyncMarkers);
		}

		//Validation
		if (NextMarker.MarkerIndex != -1)
		{
			check(AuthoredSyncMarkers[NextMarker.MarkerIndex].MarkerName == End.NextMarkerName);
		}

		// End Validation
		// Only reset position if we found valid markers. Otherwise stay where we are to not pop.
		if ((PreviousMarker.MarkerIndex != MarkerIndexSpecialValues::AnimationBoundary) && (NextMarker.MarkerIndex != MarkerIndexSpecialValues::AnimationBoundary))
		{
			CurrentTime = GetCurrentTimeFromMarkers(PreviousMarker, NextMarker, End.PositionBetweenMarkers);
		}
	}
	else
	{
		int32 PassedMarkersIndex = 0;
		do
		{
			if (PreviousMarker.MarkerIndex == -1)
			{
				check(!bLooping || Context.GetMarkerSyncEndPosition().PreviousMarkerName == NAME_None); // shouldn't have an end of anim marker if looping
				CurrentTime = FMath::Max(CurrentTime + DeltaRemaining, 0.f);
				break;
			}
			else if (PassedMarkersIndex < Context.MarkersPassedThisTick.Num())
			{
				NextMarker.MarkerIndex = PreviousMarker.MarkerIndex;
				checkSlow(PreviousMarker.MarkerIndex != -1);
				const FPassedMarker& PassedMarker = Context.MarkersPassedThisTick[PassedMarkersIndex];
				AdvanceMarkerBackwards(PreviousMarker.MarkerIndex, PassedMarker.PassedMarkerName, bLooping, AuthoredSyncMarkers);
				if (PreviousMarker.MarkerIndex == -1)
				{
					DeltaRemaining = PassedMarker.DeltaTimeWhenPassed;
				}
				++PassedMarkersIndex;
			}
		} while (PassedMarkersIndex < Context.MarkersPassedThisTick.Num());

		const FMarkerSyncAnimPosition& End = Context.GetMarkerSyncEndPosition();

		if (PreviousMarker.MarkerIndex != -1 && Context.MarkersPassedThisTick.Num() > 0)
		{
			AdvanceMarkerBackwards(PreviousMarker.MarkerIndex, End.PreviousMarkerName, bLooping, AuthoredSyncMarkers);
		}

		if (End.PreviousMarkerName == NAME_None)
		{
			PreviousMarker.MarkerIndex = -1;
		}

		//Validation
		if (PreviousMarker.MarkerIndex != -1)
		{
			check(AuthoredSyncMarkers[PreviousMarker.MarkerIndex].MarkerName == End.PreviousMarkerName);
		}

		// End Validation
		// Only reset position if we found valid markers. Otherwise stay where we are to not pop.
		if ((PreviousMarker.MarkerIndex != MarkerIndexSpecialValues::AnimationBoundary) && (NextMarker.MarkerIndex != MarkerIndexSpecialValues::AnimationBoundary))
		{
			CurrentTime = GetCurrentTimeFromMarkers(PreviousMarker, NextMarker, End.PositionBetweenMarkers);
		}
	}
}

void UAnimSequence::GetMarkerIndicesForTime(float CurrentTime, bool bLooping, const TArray<FName>& ValidMarkerNames, FMarkerPair& OutPrevMarker, FMarkerPair& OutNextMarker) const
{
	const int LoopModStart = bLooping ? -1 : 0;
	const int LoopModEnd = bLooping ? 2 : 1;

	OutPrevMarker.MarkerIndex = -1;
	OutPrevMarker.TimeToMarker = -CurrentTime;
	OutNextMarker.MarkerIndex = -1;
	OutNextMarker.TimeToMarker = SequenceLength - CurrentTime;

	for (int32 LoopMod = LoopModStart; LoopMod < LoopModEnd; ++LoopMod)
	{
		const float LoopModTime = LoopMod * SequenceLength;
		for (int Idx = 0; Idx < AuthoredSyncMarkers.Num(); ++Idx)
		{
			const FAnimSyncMarker& Marker = AuthoredSyncMarkers[Idx];
			if (ValidMarkerNames.Contains(Marker.MarkerName))
			{
				const float MarkerTime = Marker.Time + LoopModTime;
				if (MarkerTime < CurrentTime)
				{
					OutPrevMarker.MarkerIndex = Idx;
					OutPrevMarker.TimeToMarker = MarkerTime - CurrentTime;
				}
				else if (MarkerTime >= CurrentTime)
				{
					OutNextMarker.MarkerIndex = Idx;
					OutNextMarker.TimeToMarker = MarkerTime - CurrentTime;
					break; // Done
				}
			}
		}
		if (OutNextMarker.MarkerIndex != -1)
		{
			break; // Done
		}
	}
}

FMarkerSyncAnimPosition UAnimSequence::GetMarkerSyncPositionfromMarkerIndicies(int32 PrevMarker, int32 NextMarker, float CurrentTime) const
{
	FMarkerSyncAnimPosition SyncPosition;
	float PrevTime, NextTime;
	
	if (PrevMarker != -1 && ensureAlwaysMsgf(AuthoredSyncMarkers.IsValidIndex(PrevMarker),
		TEXT("%s - MarkerCount: %d, PrevMarker : %d, NextMarker: %d, CurrentTime : %0.2f"), *GetFullName(), AuthoredSyncMarkers.Num(), PrevMarker, NextMarker, CurrentTime))
	{
		PrevTime = AuthoredSyncMarkers[PrevMarker].Time;
		SyncPosition.PreviousMarkerName = AuthoredSyncMarkers[PrevMarker].MarkerName;
	}
	else
	{
		PrevTime = 0.f;
	}

	if (NextMarker != -1 && ensureAlwaysMsgf(AuthoredSyncMarkers.IsValidIndex(NextMarker),
		TEXT("%s - MarkerCount: %d, PrevMarker : %d, NextMarker: %d, CurrentTime : %0.2f"), *GetFullName(), AuthoredSyncMarkers.Num(), PrevMarker, NextMarker, CurrentTime))
	{
		NextTime = AuthoredSyncMarkers[NextMarker].Time;
		SyncPosition.NextMarkerName = AuthoredSyncMarkers[NextMarker].MarkerName;
	}
	else
	{
		NextTime = SequenceLength;
	}

	// Account for looping
	if(PrevTime > NextTime)
	{
		PrevTime = (PrevTime > CurrentTime) ? PrevTime - SequenceLength : PrevTime;
		NextTime = (NextTime < CurrentTime) ? NextTime + SequenceLength : NextTime;
	}
	else if (PrevTime > CurrentTime)
	{
		CurrentTime += SequenceLength;
	}

	if (PrevTime == NextTime)
	{
		PrevTime -= SequenceLength;
	}

	check(NextTime > PrevTime);

	SyncPosition.PositionBetweenMarkers = (CurrentTime - PrevTime) / (NextTime - PrevTime);
	return SyncPosition;
}

float UAnimSequence::GetCurrentTimeFromMarkers(FMarkerPair& PrevMarker, FMarkerPair& NextMarker, float PositionBetweenMarkers) const
{
	float PrevTime = (PrevMarker.MarkerIndex != -1) ? AuthoredSyncMarkers[PrevMarker.MarkerIndex].Time : 0.f;
	float NextTime = (NextMarker.MarkerIndex != -1) ? AuthoredSyncMarkers[NextMarker.MarkerIndex].Time : SequenceLength;

	if (PrevTime >= NextTime)
	{
		PrevTime -= SequenceLength; //Account for looping
	}
	float CurrentTime = PrevTime + PositionBetweenMarkers * (NextTime - PrevTime);

	PrevMarker.TimeToMarker = PrevTime - CurrentTime;
	NextMarker.TimeToMarker = NextTime - CurrentTime;

	if (CurrentTime < 0.f)
	{
		CurrentTime += SequenceLength;
	}
	CurrentTime = FMath::Clamp<float>(CurrentTime, 0, SequenceLength);

	return CurrentTime;
}

void UAnimSequence::GetMarkerIndicesForPosition(const FMarkerSyncAnimPosition& SyncPosition, bool bLooping, FMarkerPair& OutPrevMarker, FMarkerPair& OutNextMarker, float& OutCurrentTime) const
{
	// If we're not looping, assume we're playing a transition and we need to stay where we are.
	if (!bLooping)
	{
		OutPrevMarker.MarkerIndex = INDEX_NONE;
		OutNextMarker.MarkerIndex = INDEX_NONE;

		for (int32 Idx = 0; Idx<AuthoredSyncMarkers.Num(); Idx++)
		{
			const FAnimSyncMarker& SyncMarker = AuthoredSyncMarkers[Idx];
			const float MarkerTime = SyncMarker.Time;

			if (OutCurrentTime > MarkerTime && SyncMarker.MarkerName == SyncPosition.PreviousMarkerName)
			{
				OutPrevMarker.MarkerIndex = Idx;
				OutPrevMarker.TimeToMarker = MarkerTime - OutCurrentTime;
			}
			else if (OutCurrentTime < MarkerTime && SyncMarker.MarkerName == SyncPosition.NextMarkerName)
			{
				OutNextMarker.MarkerIndex = Idx;
				OutNextMarker.TimeToMarker = MarkerTime - OutCurrentTime;
				break;
			}
		}

		ensureMsgf(OutCurrentTime >= 0.f && OutCurrentTime <= SequenceLength, TEXT("Current time inside of GetMarkerIndicesForPosition is out of range %.3f of 0.0 to %.3f\n    Sequence: %s"), OutCurrentTime, SequenceLength, *GetFullName());
		return;
	}

	if (SyncPosition.PreviousMarkerName == NAME_None)
	{
		OutPrevMarker.MarkerIndex = -1;
		check(SyncPosition.NextMarkerName != NAME_None);

		for (int32 Idx = 0; Idx < AuthoredSyncMarkers.Num(); ++Idx)
		{
			const FAnimSyncMarker& Marker = AuthoredSyncMarkers[Idx];
			if (Marker.MarkerName == SyncPosition.NextMarkerName)
			{
				OutNextMarker.MarkerIndex = Idx;
				OutCurrentTime = GetCurrentTimeFromMarkers(OutPrevMarker, OutNextMarker, SyncPosition.PositionBetweenMarkers);
				return;
			}
		}
		// Should have found a marker above!
		checkf(false, TEXT("Next Marker not found in GetMarkerIndicesForPosition. Anim: %s Expecting marker %s (Added to help debug Jira OR-9675)"), *GetName(), *SyncPosition.NextMarkerName.ToString());
	}

	if (SyncPosition.NextMarkerName == NAME_None)
	{
		OutNextMarker.MarkerIndex = -1;
		check(SyncPosition.PreviousMarkerName != NAME_None);

		for (int32 Idx = AuthoredSyncMarkers.Num() - 1; Idx >= 0; --Idx)
		{
			const FAnimSyncMarker& Marker = AuthoredSyncMarkers[Idx];
			if (Marker.MarkerName == SyncPosition.PreviousMarkerName)
			{
				OutPrevMarker.MarkerIndex = Idx;
				OutCurrentTime = GetCurrentTimeFromMarkers(OutPrevMarker, OutNextMarker, SyncPosition.PositionBetweenMarkers);
				return;
			}
		}
		// Should have found a marker above!
		checkf(false, TEXT("Previous Marker not found in GetMarkerIndicesForPosition. Anim: %s Expecting marker %s (Added to help debug Jira OR-9675)"), *GetName(), *SyncPosition.PreviousMarkerName.ToString());
	}

	float DiffToCurrentTime = FLT_MAX;
	const float CurrentInputTime  = OutCurrentTime;

	for (int32 PrevMarkerIdx = 0; PrevMarkerIdx < AuthoredSyncMarkers.Num(); ++PrevMarkerIdx)
	{
		const FAnimSyncMarker& PrevMarker = AuthoredSyncMarkers[PrevMarkerIdx];
		if (PrevMarker.MarkerName == SyncPosition.PreviousMarkerName)
		{
			const int32 EndMarkerSearchStart = PrevMarkerIdx + 1;

			const int32 EndCount = bLooping ? AuthoredSyncMarkers.Num() + EndMarkerSearchStart : AuthoredSyncMarkers.Num();
			for (int32 NextMarkerCount = EndMarkerSearchStart; NextMarkerCount < EndCount; ++NextMarkerCount)
			{
				const int32 NextMarkerIdx = NextMarkerCount % AuthoredSyncMarkers.Num();

				if (AuthoredSyncMarkers[NextMarkerIdx].MarkerName == SyncPosition.NextMarkerName)
				{
					float NextMarkerTime = AuthoredSyncMarkers[NextMarkerIdx].Time;
					if (NextMarkerTime < PrevMarker.Time)
					{
						NextMarkerTime += SequenceLength;
					}
					float ThisCurrentTime = PrevMarker.Time + SyncPosition.PositionBetweenMarkers * (NextMarkerTime - PrevMarker.Time);
					if (ThisCurrentTime > SequenceLength)
					{
						ThisCurrentTime -= SequenceLength;
					}
					float ThisDiff = FMath::Abs(ThisCurrentTime - CurrentInputTime);
					if (ThisDiff < DiffToCurrentTime)
					{
						DiffToCurrentTime = ThisDiff;
						OutPrevMarker.MarkerIndex = PrevMarkerIdx;
						OutNextMarker.MarkerIndex = NextMarkerIdx;
						OutCurrentTime = GetCurrentTimeFromMarkers(OutPrevMarker, OutNextMarker, SyncPosition.PositionBetweenMarkers);
					}

					// this marker test is done, move onto next one
					break;
				}
			}

			// If we get here and we haven't found a match and we are not looping then there 
			// is no point running the rest of the loop set up something as relevant as we can and carry on
			if (OutPrevMarker.MarkerIndex == MarkerIndexSpecialValues::Unitialized)
			{
				//Find nearest previous marker that is earlier than our current time
				DiffToCurrentTime = OutCurrentTime - PrevMarker.Time;
				int32 PrevMarkerToUse = PrevMarkerIdx + 1;
				while (DiffToCurrentTime > 0.f && PrevMarkerToUse < AuthoredSyncMarkers.Num())
				{
					DiffToCurrentTime = OutCurrentTime - AuthoredSyncMarkers[PrevMarkerToUse].Time;
					++PrevMarkerToUse;
				}
				OutPrevMarker.MarkerIndex = PrevMarkerToUse - 1;	// We always go one past the marker we actually want to use
				
				OutNextMarker.MarkerIndex = -1;						// This goes to minus one as the very fact we are here means
																	// that there is no next marker to use
				OutCurrentTime = GetCurrentTimeFromMarkers(OutPrevMarker, OutNextMarker, SyncPosition.PositionBetweenMarkers);
				break; // no need to keep searching, we are done
			}
		}
	}
	// Should have found a markers above!
	checkf(OutPrevMarker.MarkerIndex != MarkerIndexSpecialValues::Unitialized, TEXT("Prev Marker not found in GetMarkerIndicesForPosition. Anim: %s Expecting marker %s (Added to help debug Jira OR-9675)"), *GetName(), *SyncPosition.PreviousMarkerName.ToString());
	checkf(OutNextMarker.MarkerIndex != MarkerIndexSpecialValues::Unitialized, TEXT("Next Marker not found in GetMarkerIndicesForPosition. Anim: %s Expecting marker %s (Added to help debug Jira OR-9675)"), *GetName(), *SyncPosition.NextMarkerName.ToString());
}

float UAnimSequence::GetFirstMatchingPosFromMarkerSyncPos(const FMarkerSyncAnimPosition& InMarkerSyncGroupPosition) const
{
	if ((InMarkerSyncGroupPosition.PreviousMarkerName == NAME_None) || (InMarkerSyncGroupPosition.NextMarkerName == NAME_None))
	{
		return 0.f;
	}

	for (int32 PrevMarkerIdx = 0; PrevMarkerIdx < AuthoredSyncMarkers.Num()-1; PrevMarkerIdx++)
	{
		const FAnimSyncMarker& PrevMarker = AuthoredSyncMarkers[PrevMarkerIdx];
		const FAnimSyncMarker& NextMarker = AuthoredSyncMarkers[PrevMarkerIdx+1];
		if ((PrevMarker.MarkerName == InMarkerSyncGroupPosition.PreviousMarkerName) && (NextMarker.MarkerName == InMarkerSyncGroupPosition.NextMarkerName))
		{
			return FMath::Lerp(PrevMarker.Time, NextMarker.Time, InMarkerSyncGroupPosition.PositionBetweenMarkers);
		}
	}

	return 0.f;
}

float UAnimSequence::GetNextMatchingPosFromMarkerSyncPos(const FMarkerSyncAnimPosition& InMarkerSyncGroupPosition, const float& StartingPosition) const
{
	if ((InMarkerSyncGroupPosition.PreviousMarkerName == NAME_None) || (InMarkerSyncGroupPosition.NextMarkerName == NAME_None))
	{
		return StartingPosition;
	}

	for (int32 PrevMarkerIdx = 0; PrevMarkerIdx < AuthoredSyncMarkers.Num() - 1; PrevMarkerIdx++)
	{
		const FAnimSyncMarker& PrevMarker = AuthoredSyncMarkers[PrevMarkerIdx];
		const FAnimSyncMarker& NextMarker = AuthoredSyncMarkers[PrevMarkerIdx + 1];

		if (NextMarker.Time < StartingPosition)
		{
			continue;
		}

		if ((PrevMarker.MarkerName == InMarkerSyncGroupPosition.PreviousMarkerName) && (NextMarker.MarkerName == InMarkerSyncGroupPosition.NextMarkerName))
		{
			const float FoundTime = FMath::Lerp(PrevMarker.Time, NextMarker.Time, InMarkerSyncGroupPosition.PositionBetweenMarkers);
			if (FoundTime < StartingPosition)
			{
				continue;
			}
			return FoundTime;
		}
	}

	return StartingPosition;
}

float UAnimSequence::GetPrevMatchingPosFromMarkerSyncPos(const FMarkerSyncAnimPosition& InMarkerSyncGroupPosition, const float& StartingPosition) const
{
	if ((InMarkerSyncGroupPosition.PreviousMarkerName == NAME_None) || (InMarkerSyncGroupPosition.NextMarkerName == NAME_None) || (AuthoredSyncMarkers.Num() < 2))
	{
		return StartingPosition;
	}

	for (int32 PrevMarkerIdx = AuthoredSyncMarkers.Num() - 2; PrevMarkerIdx >= 0; PrevMarkerIdx--)
	{
		const FAnimSyncMarker& PrevMarker = AuthoredSyncMarkers[PrevMarkerIdx];
		const FAnimSyncMarker& NextMarker = AuthoredSyncMarkers[PrevMarkerIdx + 1];

		if (PrevMarker.Time > StartingPosition)
		{
			continue;
		}

		if ((PrevMarker.MarkerName == InMarkerSyncGroupPosition.PreviousMarkerName) && (NextMarker.MarkerName == InMarkerSyncGroupPosition.NextMarkerName))
		{
			const float FoundTime = FMath::Lerp(PrevMarker.Time, NextMarker.Time, InMarkerSyncGroupPosition.PositionBetweenMarkers);
			if (FoundTime > StartingPosition)
			{
				continue;
			}
			return FoundTime;
		}
	}

	return StartingPosition;
}

void UAnimSequence::EnableRootMotionSettingFromMontage(bool bInEnableRootMotion, const ERootMotionRootLock::Type InRootMotionRootLock)
{
	if (!bRootMotionSettingsCopiedFromMontage)
	{
		bEnableRootMotion = bInEnableRootMotion;
		RootMotionRootLock = InRootMotionRootLock;
		bRootMotionSettingsCopiedFromMontage = true;
	}
}

#if WITH_EDITOR
void UAnimSequence::OnRawDataChanged()
{
	ClearCompressedBoneData();
	bUseRawDataOnly = true;

	RequestAsyncAnimRecompression(false);
	//MDW - Once we have async anim ddc requests we should do this too
	//RequestDependentAnimRecompression();
}
#endif

bool UAnimSequence::IsCompressedDataValid() const
{
	// For bone compressed data, we don't check if we have a codec. It is valid to have no compressed data
	// if we have no raw data. This can happen with sequences that only has curves.

	if (RawAnimationData.Num() == 0)
	{
		return true;
	}

	return CompressedData.CompressedDataStructure != nullptr;
}

bool UAnimSequence::IsCurveCompressedDataValid() const
{
	// For curve compressed data, we don't check if we have a codec. It is valid to have no compressed data
	// if we have no raw data. This can happen with sequences that only has bones.

	if (CompressedData.CompressedCurveByteStream.Num() == 0 && RawCurveData.FloatCurves.Num() != 0)
	{
		// No compressed data but we have raw data
		if (!IsValidAdditive())
		{
			return false;
		}

		// Additive sequences can have raw curves that all end up being 0.0 (e.g. they 100% match the base sequence curves)
		// in which case there will be no compressed curve data.
	}

	return true;
}

void UAnimSequence::ClearCompressedBoneData()
{
	CompressedData.ClearCompressedBoneData();
}

void UAnimSequence::ClearCompressedCurveData()
{
	CompressedData.ClearCompressedCurveData();
}

/*-----------------------------------------------------------------------------
	AnimNotify& subclasses
-----------------------------------------------------------------------------*/

#if !UE_BUILD_SHIPPING

void GatherAnimSequenceStats(FOutputDevice& Ar)
{
	int32 AnimationKeyFormatNum[AKF_MAX];
	int32 TranslationCompressionFormatNum[ACF_MAX];
	int32 RotationCompressionFormatNum[ACF_MAX];
	int32 ScaleCompressionFormatNum[ACF_MAX];
	FMemory::Memzero( AnimationKeyFormatNum, AKF_MAX * sizeof(int32) );
	FMemory::Memzero( TranslationCompressionFormatNum, ACF_MAX * sizeof(int32) );
	FMemory::Memzero( RotationCompressionFormatNum, ACF_MAX * sizeof(int32) );
	FMemory::Memzero( ScaleCompressionFormatNum, ACF_MAX * sizeof(int32) );

	Ar.Logf( TEXT(" %60s, Frames,NTT,NRT, NT1,NR1, TotTrnKys,TotRotKys,Codec,ResBytes"), TEXT("Sequence Name") );
	int32 GlobalNumTransTracks = 0;
	int32 GlobalNumRotTracks = 0;
	int32 GlobalNumScaleTracks = 0;
	int32 GlobalNumTransTracksWithOneKey = 0;
	int32 GlobalNumRotTracksWithOneKey = 0;
	int32 GlobalNumScaleTracksWithOneKey = 0;
	int32 GlobalApproxCompressedSize = 0;
	int32 GlobalApproxKeyDataSize = 0;
	int32 GlobalNumTransKeys = 0;
	int32 GlobalNumRotKeys = 0;
	int32 GlobalNumScaleKeys = 0;

	for( TObjectIterator<UAnimSequence> It; It; ++It )
	{
		UAnimSequence* Seq = *It;

		if (Seq->CompressedData.CompressedDataStructure == nullptr || !Seq->CompressedData.BoneCompressionCodec->IsA<UAnimCompress>())
		{
			continue;	// Custom codec we know nothing about, skip it
		}

		int32 NumTransTracks = 0;
		int32 NumRotTracks = 0;
		int32 NumScaleTracks = 0;
		int32 TotalNumTransKeys = 0;
		int32 TotalNumRotKeys = 0;
		int32 TotalNumScaleKeys = 0;
		float TranslationKeySize = 0.0f;
		float RotationKeySize = 0.0f;
		float ScaleKeySize = 0.0f;
		int32 OverheadSize = 0;
		int32 NumTransTracksWithOneKey = 0;
		int32 NumRotTracksWithOneKey = 0;
		int32 NumScaleTracksWithOneKey = 0;

		const FUECompressedAnimData& AnimData = static_cast<FUECompressedAnimData&>(*Seq->CompressedData.CompressedDataStructure);

		AnimationFormat_GetStats(
			AnimData,
			NumTransTracks,
			NumRotTracks,
			NumScaleTracks,
			TotalNumTransKeys,
			TotalNumRotKeys,
			TotalNumScaleKeys,
			TranslationKeySize,
			RotationKeySize,
			ScaleKeySize, 
			OverheadSize,
			NumTransTracksWithOneKey,
			NumRotTracksWithOneKey,
			NumScaleTracksWithOneKey);

		GlobalNumTransTracks += NumTransTracks;
		GlobalNumRotTracks += NumRotTracks;
		GlobalNumScaleTracks += NumScaleTracks;
		GlobalNumTransTracksWithOneKey += NumTransTracksWithOneKey;
		GlobalNumRotTracksWithOneKey += NumRotTracksWithOneKey;
		GlobalNumScaleTracksWithOneKey += NumScaleTracksWithOneKey;

		GlobalApproxCompressedSize += Seq->GetApproxCompressedSize();
		GlobalApproxKeyDataSize += (int32)((TotalNumTransKeys * TranslationKeySize) + (TotalNumRotKeys * RotationKeySize) + (TotalNumScaleKeys * ScaleKeySize));

		GlobalNumTransKeys += TotalNumTransKeys;
		GlobalNumRotKeys += TotalNumRotKeys;
		GlobalNumScaleKeys += TotalNumScaleKeys;

		Ar.Logf(TEXT(" %60s, %3i, %3i,%3i,%3i, %3i,%3i,%3i, %10i,%10i,%10i, %s, %d"),
			*Seq->GetName(),
			Seq->GetRawNumberOfFrames(),
			NumTransTracks, NumRotTracks, NumScaleTracks,
			NumTransTracksWithOneKey, NumRotTracksWithOneKey, NumScaleTracksWithOneKey,
			TotalNumTransKeys, TotalNumRotKeys, TotalNumScaleKeys,
			*FAnimationUtils::GetAnimationKeyFormatString(AnimData.KeyEncodingFormat),
			(int32)Seq->GetResourceSizeBytes(EResourceSizeMode::EstimatedTotal) );
	}
	Ar.Logf( TEXT("======================================================================") );
	Ar.Logf( TEXT("Total Num Tracks: %i trans, %i rot, %i scale, %i trans1, %i rot1, %i scale1"), GlobalNumTransTracks, GlobalNumRotTracks, GlobalNumScaleTracks, GlobalNumTransTracksWithOneKey, GlobalNumRotTracksWithOneKey, GlobalNumScaleTracksWithOneKey  );
	Ar.Logf( TEXT("Total Num Keys: %i trans, %i rot, %i scale"), GlobalNumTransKeys, GlobalNumRotKeys, GlobalNumScaleKeys );

	Ar.Logf( TEXT("Approx Compressed Memory: %i bytes"), GlobalApproxCompressedSize);
	Ar.Logf( TEXT("Approx Key Data Memory: %i bytes"), GlobalApproxKeyDataSize);
}

#endif // !UE_BUILD_SHIPPING


#undef LOCTEXT_NAMESPACE 
