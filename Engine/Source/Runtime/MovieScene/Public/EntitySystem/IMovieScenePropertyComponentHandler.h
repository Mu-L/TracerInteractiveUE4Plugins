// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EntitySystem/MovieSceneEntityIDs.h"

class UMovieSceneBlenderSystem;
class UMovieSceneEntitySystemLinker;

namespace UE
{
namespace MovieScene
{

struct FPropertyStats;
struct FPropertyDefinition;
struct FSystemSubsequentTasks;
struct FSystemTaskPrerequisites;
struct FFloatDecompositionParams;
struct FPropertyCompositeDefinition;

/** Type-erased view of a component. Used for passing typed data through the IPropertyComponentHandler interface */
struct FPropertyComponentView
{
	/** Construction from a specific piece of data. Specified data must outlive this view */
	template<typename T>
	FPropertyComponentView(T& InData) : Data(&InData), DataSizeof(sizeof(T)) {}

	/** Construction from a pointer to a piece of data, and its type's size. Specified data must outlive this view */
	FPropertyComponentView(void* InData, int32 InDataSizeof) : Data(InData), DataSizeof(InDataSizeof) {}

	/**
	 * Retrieve the size of this component
	 */
	int32 Sizeof() const { return DataSizeof; }

	/**
	 * Cast this type-erased view to a known data type. Only crude size checking is performned - user is responsible for ensuring that the cast is valid.
	 */
	template<typename T>
	T& ReinterpretCast() const { check(sizeof(T) <= DataSizeof); return *static_cast<T*>(Data); }

private:
	void* Data;
	int32 DataSizeof;
};

/** Type-erased view of a constant component. Used for passing typed data through the IPropertyComponentHandler interface */
struct FConstPropertyComponentView
{
	/** Construction from a specific piece of data. Specified data must outlive this view */
	template<typename T>
	FConstPropertyComponentView(const T& InData) : Data(&InData), DataSizeof(sizeof(T)) {}

	/** Construction from a pointer to a piece of data, and its type's size. Specified data must outlive this view */
	FConstPropertyComponentView(const void* InData, int32 InDataSizeof) : Data(InData), DataSizeof(InDataSizeof) {}

	/**
	 * Retrieve the size of this component
	 */
	int32 Sizeof() const { return DataSizeof; }

	/**
	 * Cast this type-erased view to a known data type. Only crude size checking is performned - user is responsible for ensuring that the cast is valid.
	 */
	template<typename T>
	const T& ReinterpretCast() const { check(sizeof(T) <= DataSizeof); return *static_cast<const T*>(Data); }

private:
	const void* Data;
	int32 DataSizeof;
};


/** Type-erased view of an array of components. Used for passing typed arrays of data through the IPropertyComponentHandler interface */
struct FPropertyComponentArrayView
{
	/** Construction from an array */
	template<typename T, typename Allocator>
	FPropertyComponentArrayView(TArray<T, Allocator>& InRange)
		: Data(InRange.GetData())
		, DataSizeof(sizeof(T))
		, ArrayNum(InRange.Num())
	{}

	/** Access the number of items in the array */
	int32 Num() const
	{
		return ArrayNum;
	}

	/** Access the sizeof a single item in the array view, in bytes */
	int32 Sizeof() const
	{
		return DataSizeof;
	}

	/** Cast this view to a typed array view. Only crude size checking is performed - the user is responsible for ensuring the cast is valid */
	template<typename T>
	TArrayView<T> ReinterpretCast() const
	{
		check(sizeof(T) == DataSizeof);
		return MakeArrayView(static_cast<T*>(Data), ArrayNum);
	}

	/** Access an element in the array */
	FPropertyComponentView operator[](int32 Index)
	{
		check(Index < ArrayNum);
		return FPropertyComponentView(static_cast<uint8*>(Data) + DataSizeof*Index, DataSizeof);
	}

	/** Access an element in the array */
	FConstPropertyComponentView operator[](int32 Index) const
	{
		check(Index < ArrayNum);
		return FConstPropertyComponentView(static_cast<uint8*>(Data) + DataSizeof*Index, DataSizeof);
	}

private:
	void* Data;
	int32 DataSizeof;
	int32 ArrayNum;
};


/** Interface for a property type handler that is able to interact with properties in sequencer */
struct IPropertyComponentHandler
{
	virtual ~IPropertyComponentHandler(){}

	/**
	 * Dispatch tasks that apply any entity that matches this property type to their final values
	 *
	 * @param Definition       The property definition this handler was registered for
	 * @param Composites       The composite channels that this property type comprises
	 * @param Stats            Stats pertaining to the entities that currently exist in the entity manager
	 * @param InPrerequisites  Task prerequisites for any entity system tasks that are dispatched
	 * @param Subsequents      Subsequents to add any dispatched tasks to
	 * @param Linker           The linker that owns the entity manager to dispatch tasks for
	 */
	virtual void DispatchSetterTasks(const FPropertyDefinition& Definition, TArrayView<const FPropertyCompositeDefinition> Composites, const FPropertyStats& Stats, FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents, UMovieSceneEntitySystemLinker* Linker) = 0;

	/**
	 * Dispatch tasks that cache a pre-animated value for any entities that have the CachePreAnimatedState tag
	 *
	 * @param Definition       The property definition this handler was registered for
	 * @param InPrerequisites  Task prerequisites for any entity system tasks that are dispatched
	 * @param Subsequents      Subsequents to add any dispatched tasks to
	 * @param Linker           The linker that owns the entity manager to dispatch tasks for
	 */
	virtual void DispatchCachePreAnimatedTasks(const FPropertyDefinition& Definition, FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents, UMovieSceneEntitySystemLinker* Linker) = 0;

	/**
	 * Dispatch tasks that restore a pre-animated value for any entities that have the NeedsUnlink tag
	 *
	 * @param Definition       The property definition this handler was registered for
	 * @param InPrerequisites  Task prerequisites for any entity system tasks that are dispatched
	 * @param Subsequents      Subsequents to add any dispatched tasks to
	 * @param Linker           The linker that owns the entity manager to dispatch tasks for
	 */
	virtual void DispatchRestorePreAnimatedStateTasks(const FPropertyDefinition& Definition, FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents, UMovieSceneEntitySystemLinker* Linker) = 0;

	/**
	 * Dispatch tasks that cache an initial unblended value for any entities that have the NeedsLink tag
	 *
	 * @param Definition       The property definition this handler was registered for
	 * @param InPrerequisites  Task prerequisites for any entity system tasks that are dispatched
	 * @param Subsequents      Subsequents to add any dispatched tasks to
	 * @param Linker           The linker that owns the entity manager to dispatch tasks for
	 */
	virtual void DispatchCacheInitialValueTasks(const FPropertyDefinition& Definition, FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents, UMovieSceneEntitySystemLinker* Linker) = 0;

	/**
	 * Run a recomposition using the specified params and values. The current value and result views must be of type PropertyType
	 *
	 * @param Definition       The property definition this handler was registered for
	 * @param Composites       The composite channels that this property type comprises
	 * @param Params           The decomposition parameters
	 * @param Blender          The blender system to recompose from
	 * @param InCurrentValue   The current value (of type PropertyType) to recompose using. For instance, if a property comprises 3 additive values (a:1, b:2, c:3), and we recompose 'a' with an InCurrentValue of 10, the result for 'a' would be 5.
	 * @param OutResult        The result to receieve recomposed values, one for every entitiy in Params.Query.Entities. Must be of type PropertyType.
	 */
	virtual void RecomposeBlendFinal(const FPropertyDefinition& Definition, TArrayView<const FPropertyCompositeDefinition> Composites, const FFloatDecompositionParams& Params, UMovieSceneBlenderSystem* Blender, FConstPropertyComponentView InCurrentValue, FPropertyComponentArrayView OutResult) = 0;

	/**
	 * Run a recomposition using the specified params and values. The current value and result views must be of type OperationalType
	 *
	 * @param Definition       The property definition this handler was registered for
	 * @param Composites       The composite channels that this property type comprises
	 * @param Params           The decomposition parameters
	 * @param Blender          The blender system to recompose from
	 * @param InCurrentValue   The current value (of type OperationalType) to recompose using. For instance, if a property comprises 3 additive values (a:1, b:2, c:3), and we recompose 'a' with an InCurrentValue of 10, the result for 'a' would be 5.
	 * @param OutResult        The result to receieve recomposed values, one for every entitiy in Params.Query.Entities. Must be of type OperationalType.
	 */
	virtual void RecomposeBlendOperational(const FPropertyDefinition& Definition, TArrayView<const FPropertyCompositeDefinition> Composites, const FFloatDecompositionParams& Params, UMovieSceneBlenderSystem* Blender, FConstPropertyComponentView InCurrentValue, FPropertyComponentArrayView OutResult) = 0;

	/**
	 * Run a recomposition using the specified params and values.
	 *
	 * @param Definition       The property definition this handler was registered for
	 * @param Composite        The composite channel of the property type that we want to decompose
	 * @param Params           The decomposition parameters
	 * @param Blender          The blender system to recompose from
	 * @param InCurrentValue   The current value (of type OperationalType) to recompose using. For instance, if a property comprises 3 additive values (a:1, b:2, c:3), and we recompose 'a' with an InCurrentValue of 10, the result for 'a' would be 5.
	 * @param OutResults       The result to receieve recomposed values, one for every entitiy in Params.Query.Entities.
	 */
	virtual void RecomposeBlendChannel(const FPropertyDefinition& Definition, const FPropertyCompositeDefinition& Composite, const FFloatDecompositionParams& Params, UMovieSceneBlenderSystem* Blender, float InCurrentValue, TArrayView<float> OutResults) = 0;

	/**
	 * Dispatch tasks that apply any entity that matches this property type to their final values
	 *
	 * @param Definition       The property definition this handler was registered for
	 * @param Linker           The linker that owns the entity manager to dispatch tasks for
	 */
	virtual void SaveGlobalPreAnimatedState(const FPropertyDefinition& Definition, UMovieSceneEntitySystemLinker* Linker) = 0;
};


} // namespace MovieScene
} // namespace UE


