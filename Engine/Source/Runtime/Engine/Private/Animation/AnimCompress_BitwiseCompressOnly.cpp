// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AnimCompress_BitwiseCompressionOnly.cpp: Bitwise animation compression only; performs no key reduction.
=============================================================================*/ 

#include "Animation/AnimCompress_BitwiseCompressOnly.h"
#include "AnimationCompression.h"
#include "AnimEncoding.h"

UAnimCompress_BitwiseCompressOnly::UAnimCompress_BitwiseCompressOnly(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Description = TEXT("Bitwise Compress Only");
}

#if WITH_EDITOR
void UAnimCompress_BitwiseCompressOnly::DoReduction(const FCompressibleAnimData& CompressibleAnimData, FCompressibleAnimDataResult& OutResult)
{
#if WITH_EDITORONLY_DATA
	// split the raw data into tracks
	TArray<FTranslationTrack> TranslationData;
	TArray<FRotationTrack> RotationData;
	TArray<FScaleTrack> ScaleData;
	SeparateRawDataIntoTracks( CompressibleAnimData.RawAnimationData, CompressibleAnimData.SequenceLength, TranslationData, RotationData, ScaleData );

	// remove obviously redundant keys from the source data
	FilterTrivialKeys(TranslationData, RotationData, ScaleData, TRANSLATION_ZEROING_THRESHOLD, QUATERNION_ZEROING_THRESHOLD, SCALE_ZEROING_THRESHOLD);

	// record the proper runtime decompressor to use
	OutResult.KeyEncodingFormat = AKF_ConstantKeyLerp;
	OutResult.RotationCompressionFormat = RotationCompressionFormat;
	OutResult.TranslationCompressionFormat = TranslationCompressionFormat;
	OutResult.ScaleCompressionFormat = ScaleCompressionFormat;
	AnimationFormat_SetInterfaceLinks(OutResult);

#if USE_SEGMENTING_CONTEXT
	if (bEnableSegmenting)
	{
		TArray<FAnimSegmentContext> RawSegments;
		SeparateRawDataIntoTracks(*AnimSeq, TranslationData, RotationData, ScaleData, IdealNumFramesPerSegment, MaxNumFramesPerSegment, RawSegments);

		BitwiseCompressAnimationTracks(
			*AnimSeq,
			static_cast<AnimationCompressionFormat>(TranslationCompressionFormat),
			static_cast<AnimationCompressionFormat>(RotationCompressionFormat),
			static_cast<AnimationCompressionFormat>(ScaleCompressionFormat),
			RawSegments);

		CoalesceCompressedSegments(*AnimSeq, RawSegments);

		AnimSeq->TranslationCompressionFormat = TranslationCompressionFormat;
		AnimSeq->RotationCompressionFormat = RotationCompressionFormat;
		AnimSeq->ScaleCompressionFormat = ScaleCompressionFormat;
	}
	else
#endif
	{
		// bitwise compress the tracks into the anim sequence buffers
		BitwiseCompressAnimationTracks(
			CompressibleAnimData,
			OutResult,
			static_cast<AnimationCompressionFormat>(TranslationCompressionFormat),
			static_cast<AnimationCompressionFormat>(RotationCompressionFormat),
			static_cast<AnimationCompressionFormat>(ScaleCompressionFormat),
			TranslationData,
			RotationData,
			ScaleData);
	}

	// We could be invalid, set the links again
	AnimationFormat_SetInterfaceLinks(OutResult);
#endif // WITH_EDITORONLY_DATA
}

#endif // WITH_EDITOR
