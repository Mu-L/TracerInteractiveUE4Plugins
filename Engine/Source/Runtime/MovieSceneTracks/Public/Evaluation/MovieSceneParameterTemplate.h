// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "UObject/ObjectMacros.h"
#include "MovieSceneFwd.h"
#include "MovieSceneExecutionToken.h"
#include "Evaluation/MovieSceneEvalTemplate.h"
#include "Sections/MovieSceneParameterSection.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "IMovieScenePlayer.h"

#include "MovieSceneParameterTemplate.generated.h"

class UMovieSceneComponentMaterialTrack;

DECLARE_CYCLE_STAT(TEXT("Parameter Track Token Execute"), MovieSceneEval_ParameterTrack_TokenExecute, STATGROUP_MovieSceneEval);

/** Evaluation structure that holds evaluated values */
struct FEvaluatedParameterSectionValues
{
	FEvaluatedParameterSectionValues() = default;

	FEvaluatedParameterSectionValues(FEvaluatedParameterSectionValues&&) = default;
	FEvaluatedParameterSectionValues& operator=(FEvaluatedParameterSectionValues&&) = default;

	// Non-copyable
	FEvaluatedParameterSectionValues(const FEvaluatedParameterSectionValues&) = delete;
	FEvaluatedParameterSectionValues& operator=(const FEvaluatedParameterSectionValues&) = delete;

	/** Array of evaluated scalar values */
	TArray<FScalarParameterNameAndValue, TInlineAllocator<2>> ScalarValues;
	/** Array of evaluated bool values */
	TArray<FBoolParameterNameAndValue, TInlineAllocator<2>> BoolValues;
	/** Array of evaluated vector2D values */
	TArray<FVector2DParameterNameAndValue, TInlineAllocator<2>> Vector2DValues;
	/** Array of evaluated vector values */
	TArray<FVectorParameterNameAndValue, TInlineAllocator<2>> VectorValues;
	/** Array of evaluated color values */
	TArray<FColorParameterNameAndValue, TInlineAllocator<2>> ColorValues;
	/** Array of evaluated transform values */
	TArray<FTransformParameterNameAndValue, TInlineAllocator<2>> TransformValues;
};

/** Template that performs evaluation of parameter sections */
USTRUCT()
struct FMovieSceneParameterSectionTemplate : public FMovieSceneEvalTemplate
{
	GENERATED_BODY()

	FMovieSceneParameterSectionTemplate() {}

protected:

	/** Protected constructor to initialize from a parameter section */
	MOVIESCENETRACKS_API FMovieSceneParameterSectionTemplate(const UMovieSceneParameterSection& Section);

	/** Evaluate our curves, outputting evaluated values into the specified container */
	MOVIESCENETRACKS_API void EvaluateCurves(const FMovieSceneContext& Context, FEvaluatedParameterSectionValues& OutValues) const;

protected:

	/** The scalar parameter names and their associated curves. */
	UPROPERTY()
	TArray<FScalarParameterNameAndCurve> Scalars;

	/** The bool parameter names and their associated curves. */
	UPROPERTY()
	TArray<FBoolParameterNameAndCurve> Bools;

	/** The vector parameter names and their associated curves. */
	UPROPERTY()
	TArray<FVector2DParameterNameAndCurves> Vector2Ds;

	/** The vector parameter names and their associated curves. */
	UPROPERTY()
	TArray<FVectorParameterNameAndCurves> Vectors;

	/** The color parameter names and their associated curves. */
	UPROPERTY()
	TArray<FColorParameterNameAndCurves> Colors;

	UPROPERTY()
	TArray<FTransformParameterNameAndCurves> Transforms;
};

/** Default accessor type for use with TMaterialTrackExecutionToken */
struct FDefaultMaterialAccessor
{
	// Implement in derived classes:

	/** Get the anim type ID for the evaluation token */
	// FMovieSceneAnimTypeID 	GetAnimTypeID();

	/** Get the material from the specified object */
	// UMaterialInterface* 		GetMaterialForObject(UObject& Object);

	/** Set the material for the specified object */
	// void 					SetMaterialForObject(UObject& Object, UMaterialInterface& Material);

	/** Apply the specified values onto the specified material */
	MOVIESCENETRACKS_API void Apply(UMaterialInstanceDynamic& Material, const FEvaluatedParameterSectionValues& Values);
};

/**
 * Material track execution token
 * Templated on accessor type to allow for copyable accessors into pre animated state
 */
template<typename AccessorType>
struct TMaterialTrackExecutionToken : IMovieSceneExecutionToken
{
	TMaterialTrackExecutionToken(AccessorType InAccessor)
		: Accessor(MoveTemp(InAccessor))
	{
	}

	TMaterialTrackExecutionToken(TMaterialTrackExecutionToken&&) = default;
	TMaterialTrackExecutionToken& operator=(TMaterialTrackExecutionToken&&) = default;

	// Non-copyable
	TMaterialTrackExecutionToken(const TMaterialTrackExecutionToken&) = delete;
	TMaterialTrackExecutionToken& operator=(const TMaterialTrackExecutionToken&) = delete;

	virtual void Execute(const FMovieSceneContext& Context, const FMovieSceneEvaluationOperand& Operand, FPersistentEvaluationData& PersistentData, IMovieScenePlayer& Player)
	{
		MOVIESCENE_DETAILED_SCOPE_CYCLE_COUNTER(MovieSceneEval_ParameterTrack_TokenExecute)
		
		for (TWeakObjectPtr<>& WeakObject : Player.FindBoundObjects(Operand))
		{
			UObject* Object = WeakObject.Get();
			UMaterialInterface* Material = Object ? Accessor.GetMaterialForObject(*Object) : nullptr;

			if (!Material)
			{
				continue;
			}

			// Save the old instance
			Player.SavePreAnimatedState(*Object, Accessor.GetAnimTypeID(), FPreAnimatedTokenProducer(Accessor));

			UMaterialInstanceDynamic* DynamicMaterialInstance = Cast<UMaterialInstanceDynamic>(Material);
			if (!DynamicMaterialInstance)
			{
				FString DynamicName = Material->GetName() + "_Animated";
				FName UniqueDynamicName = MakeUniqueObjectName( Object, UMaterialInstanceDynamic::StaticClass() , *DynamicName );
				DynamicMaterialInstance = Accessor.CreateMaterialInstanceDynamic(*Object, *Material, UniqueDynamicName);

				Accessor.SetMaterialForObject(*Object, *DynamicMaterialInstance);
			}

			Accessor.Apply(*DynamicMaterialInstance, Values);
		}
	}

	AccessorType Accessor;
	FEvaluatedParameterSectionValues Values;

private:

	struct FPreAnimatedTokenProducer : IMovieScenePreAnimatedTokenProducer
	{
		struct FPreAnimatedToken : IMovieScenePreAnimatedToken
		{
			FPreAnimatedToken(UObject& Object, const AccessorType& InAccessor)
				: Accessor(InAccessor)
				, Material(Accessor.GetMaterialForObject(Object))
			{}

			virtual void RestoreState(UObject& Object, IMovieScenePlayer& Player) override
			{
				if (UMaterialInterface* PinnedMaterial = Material.Get())
				{
					Accessor.SetMaterialForObject(Object, *PinnedMaterial);
				}
			}

			AccessorType Accessor;
			TWeakObjectPtr<UMaterialInterface> Material;
		};

		FPreAnimatedTokenProducer(const AccessorType& InAccessor) : Accessor(InAccessor) {}

		virtual IMovieScenePreAnimatedTokenPtr CacheExistingState(UObject& Object) const override
		{
			return FPreAnimatedToken(Object, Accessor);
		}

		const AccessorType& Accessor;
	};
};

/** Evaluation template for primitive component materials */
USTRUCT()
struct FMovieSceneComponentMaterialSectionTemplate : public FMovieSceneParameterSectionTemplate
{
	GENERATED_BODY()

	FMovieSceneComponentMaterialSectionTemplate() : MaterialIndex(0) {}
	FMovieSceneComponentMaterialSectionTemplate(const UMovieSceneParameterSection& Section, const UMovieSceneComponentMaterialTrack& Track);

private:

	virtual UScriptStruct& GetScriptStructImpl() const override { return *StaticStruct(); }
	virtual void Evaluate(const FMovieSceneEvaluationOperand& Operand, const FMovieSceneContext& Context, const FPersistentEvaluationData& PersistentData, FMovieSceneExecutionTokens& ExecutionTokens) const override;

	UPROPERTY()
	int32 MaterialIndex;
};
