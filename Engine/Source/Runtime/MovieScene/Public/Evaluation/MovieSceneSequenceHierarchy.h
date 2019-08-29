// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/FrameRate.h"
#include "UObject/ObjectMacros.h"
#include "Evaluation/MovieSceneSequenceTransform.h"
#include "Evaluation/MovieSceneSectionParameters.h"
#include "UObject/SoftObjectPath.h"
#include "MovieSceneSequenceID.h"
#include "Evaluation/MovieSceneSequenceInstanceData.h"
#include "Containers/ArrayView.h"
#include "MovieSceneFrameMigration.h"
#include "MovieSceneSequenceHierarchy.generated.h"

class UMovieSceneSequence;
class UMovieSceneSubSection;
struct FMovieSceneSequenceID;

/**
 * Sub sequence data that is stored within an evaluation template as a backreference to the originating sequence, and section
 */
USTRUCT()
struct FMovieSceneSubSequenceData
{
	GENERATED_BODY()

	/**
	 * Default constructor for serialization
	 */
	MOVIESCENE_API FMovieSceneSubSequenceData();

	/**
	 * Construction from a movie scene sequence, and a sub section name, and its valid play range
	 */
	MOVIESCENE_API FMovieSceneSubSequenceData(const UMovieSceneSubSection& InSubSection);

	/**
	 * Get this sub sequence's sequence asset, potentially loading it through its soft object path
	 */
	MOVIESCENE_API UMovieSceneSequence* GetSequence() const;

	/**
	 * Get this sub sequence's sequence asset if it is already loaded, will not attempt to load the sequence if not
	 */
	MOVIESCENE_API UMovieSceneSequence* GetLoadedSequence() const;

	/**
	 * Check whether this structure is dirty and should be reconstructed
	 */
	MOVIESCENE_API bool IsDirty(const UMovieSceneSubSection& InSubSection) const;

	/** The sequence that the sub section references */
	UPROPERTY(meta=(AllowedClasses="MovieSceneSequence"))
	FSoftObjectPath Sequence;

	/** Transform that transforms a given time from the sequences outer space, to its authored space. */
	UPROPERTY()
	FMovieSceneSequenceTransform RootToSequenceTransform;

	/** The tick resolution of the inner sequence. */
	UPROPERTY()
	FFrameRate TickResolution;

	/** This sequence's deterministic sequence ID. Used in editor to reduce the risk of collisions on recompilation. */ 
	UPROPERTY()
	FMovieSceneSequenceID DeterministicSequenceID;

	/** This sub sequence's playback range according to its parent sub section. Clamped recursively during template generation */
	UPROPERTY()
	FMovieSceneFrameRange PlayRange;

	/** The sequence preroll range considering the start offset */
	UPROPERTY()
	FMovieSceneFrameRange PreRollRange;

	/** The sequence postroll range considering the start offset */
	UPROPERTY()
	FMovieSceneFrameRange PostRollRange;

	/** The accumulated hierarchical bias of this sequence. Higher bias will take precedence */
	UPROPERTY()
	int32 HierarchicalBias;

	/** Instance data that should be used for any tracks contained immediately within this sub sequence */
	UPROPERTY()
	FMovieSceneSequenceInstanceDataPtr InstanceData;

#if WITH_EDITORONLY_DATA

	/** This sequence's path within its movie scene */
	UPROPERTY()
	FName SectionPath;
#endif

private:

	/** Cached version of the sequence to avoid resolving it every time */
	mutable TWeakObjectPtr<UMovieSceneSequence> CachedSequence;

	/** The sub section's signature at the time this structure was populated. */
	UPROPERTY()
	FGuid SubSectionSignature;

	/** The transform from this sub sequence's parent to its own play space. */
	UPROPERTY()
	FMovieSceneSequenceTransform OuterToInnerTransform;
};

/**
 * Simple structure specifying parent and child sequence IDs for any given sequences
 */
USTRUCT()
struct FMovieSceneSequenceHierarchyNode
{
	GENERATED_BODY()

	/**
	 * Default construction used by serialization
	 */
	FMovieSceneSequenceHierarchyNode()
	{}

	/**
	 * Construct this hierarchy node from the sequence's parent ID
	 */
	FMovieSceneSequenceHierarchyNode(FMovieSceneSequenceIDRef InParentID)
		: ParentID(InParentID)
	{}

	/** Movie scene sequence ID of this node's parent sequence */
	UPROPERTY()
	FMovieSceneSequenceID ParentID;

	/** Array of child sequences contained within this sequence */
	UPROPERTY()
	TArray<FMovieSceneSequenceID> Children;
};

/**
 * Structure that stores hierarchical information pertaining to all sequences contained within a master sequence
 */
USTRUCT()
struct FMovieSceneSequenceHierarchy
{
	GENERATED_BODY()

	FMovieSceneSequenceHierarchy()
	{
		Hierarchy.Add(MovieSceneSequenceID::Root, FMovieSceneSequenceHierarchyNode(MovieSceneSequenceID::Invalid));
	}

	/**
	 * Find the structural information for the specified sequence ID
	 *
	 * @param SequenceID 			The ID of the sequence to lookup
	 * @return pointer to the structural information, or nullptr if the sequence ID does not exist in this hierarchy 
	 */
	const FMovieSceneSequenceHierarchyNode* FindNode(FMovieSceneSequenceIDRef SequenceID) const
	{
		return Hierarchy.Find(SequenceID);
	}

	/**
	 * Find the structural information for the specified sequence ID
	 *
	 * @param SequenceID 			The ID of the sequence to lookup
	 * @return pointer to the structural information, or nullptr if the sequence ID does not exist in this hierarchy 
	 */
	FMovieSceneSequenceHierarchyNode* FindNode(FMovieSceneSequenceIDRef SequenceID)
	{
		return Hierarchy.Find(SequenceID);
	}

	/**
	 * Find the sub sequence and section information for the specified sequence ID
	 *
	 * @param SequenceID 			The ID of the sequence to lookup
	 * @return pointer to the sequence/section information, or nullptr if the sequence ID does not exist in this hierarchy 
	 */
	const FMovieSceneSubSequenceData* FindSubData(FMovieSceneSequenceIDRef SequenceID) const
	{
		return SubSequences.Find(SequenceID);
	}

	/**
	 * Find the sub sequence and section information for the specified sequence ID
	 *
	 * @param SequenceID 			The ID of the sequence to lookup
	 * @return pointer to the sequence/section information, or nullptr if the sequence ID does not exist in this hierarchy 
	 */
	FMovieSceneSubSequenceData* FindSubData(FMovieSceneSequenceIDRef SequenceID)
	{
		return SubSequences.Find(SequenceID);
	}

	/**
	 * Add the specified sub sequence data to the hierarchy
	 *
	 * @param Data 					The data to add
	 * @param ThisSequenceID 		The sequence ID of the sequence the data relates to
	 * @param ParentID 				The parent ID of this sequence data
	 */
	void Add(const FMovieSceneSubSequenceData& Data, FMovieSceneSequenceIDRef ThisSequenceID, FMovieSceneSequenceIDRef ParentID);

	void Remove(TArrayView<const FMovieSceneSequenceID> SequenceIDs);

	/** Access to all the subsequence data */
	const TMap<FMovieSceneSequenceID, FMovieSceneSubSequenceData>& AllSubSequenceData() const
	{
		return (const TMap<FMovieSceneSequenceID, FMovieSceneSubSequenceData>&)SubSequences;
	}

private:

	/** Map of all (recursive) sub sequences found in this template, keyed on sequence ID */
	UPROPERTY()
	TMap<FMovieSceneSequenceID, FMovieSceneSubSequenceData> SubSequences;

	/** Structural information describing the structure of the sequence */
	UPROPERTY()
	TMap<FMovieSceneSequenceID, FMovieSceneSequenceHierarchyNode> Hierarchy;
};
