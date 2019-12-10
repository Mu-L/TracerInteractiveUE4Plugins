// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ToolBuilderUtil.h"
#include "CoreMinimal.h"
#include "Algo/Accumulate.h"
#include "GameFramework/Actor.h"
#include "Engine/Selection.h"
#include "Components/StaticMeshComponent.h"

int ToolBuilderUtil::CountComponents(const FToolBuilderState& InputState, const TFunction<bool(UActorComponent*)>& Predicate)
{
	int nTypedComponents{};

	if (InputState.SelectedComponents.Num() > 0)
	{
		nTypedComponents = Algo::CountIf(InputState.SelectedComponents, Predicate);
	}
	else
	{
		nTypedComponents =
			Algo::TransformAccumulate(InputState.SelectedActors,
									  [&Predicate](AActor* Actor)
									  {
										  return Algo::CountIf(Actor->GetComponents(), Predicate);
									  },
									  0);
	}
	return nTypedComponents;
}




UActorComponent* ToolBuilderUtil::FindFirstComponent(const FToolBuilderState& InputState, const TFunction<bool(UActorComponent*)>& Predicate)
{
	if (InputState.SelectedComponents.Num() > 0)
	{
		return *InputState.SelectedComponents.FindByPredicate(Predicate);
	}
	else
	{
		for ( AActor* Actor : InputState.SelectedActors )
		{
			UActorComponent* Component = *Algo::FindByPredicate(Actor->GetComponents(), Predicate);
			if ( Component )
			{
				return Component;
			}
		}
	}
	return nullptr;
}




TArray<UActorComponent*> ToolBuilderUtil::FindAllComponents(const FToolBuilderState& InputState, const TFunction<bool(UActorComponent*)>& Predicate)
{
	if (InputState.SelectedComponents.Num() > 0)
	{
		return InputState.SelectedComponents.FilterByPredicate(Predicate);
	}
	else
	{
		return Algo::TransformAccumulate(InputState.SelectedActors,
										 [&Predicate](AActor* Actor)
										 {
											 TInlineComponentArray<UActorComponent*> ActorComponents;
											 Actor->GetComponents(ActorComponents);
											 return ActorComponents.FilterByPredicate(Predicate);
										 },
										 TArray<UActorComponent*>{},
										 [](TArray<UActorComponent*> FoundComponents, TArray<UActorComponent*> ActorComponents)
										 {
											 FoundComponents.Insert(MoveTemp(ActorComponents), FoundComponents.Num());
											 return FoundComponents;
										 });
	}
}
