// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_TimedNiagaraEffect.generated.h"

class UNiagaraSystem;
class USkeletalMeshComponent;
class UFXSystemAsset;
class UFXSystemComponent;

// Timed Niagara Effect Notify
// Allows a looping Niagara effect to be played in an animation that will activate
// at the beginning of the notify and deactivate at the end.
UCLASS(Blueprintable, meta = (DisplayName = "Timed Niagara Effect"))
class NIAGARAANIMNOTIFIES_API UAnimNotifyState_TimedNiagaraEffect : public UAnimNotifyState
{
	GENERATED_UCLASS_BODY()

	// The niagara system template to use when spawning the niagara component
	UPROPERTY(EditAnywhere, Category = NiagaraSystem, meta = (ToolTip = "The niagara system to spawn for the notify state"))
	UNiagaraSystem* Template;

	// The socket within our mesh component to attach to when we spawn the Niagara component
	UPROPERTY(EditAnywhere, Category = NiagaraSystem, meta = (ToolTip = "The socket or bone to attach the system to", AnimNotifyBoneName = "true"))
	FName SocketName;

	// Offset from the socket / bone location
	UPROPERTY(EditAnywhere, Category = NiagaraSystem, meta = (ToolTip = "Offset from the socket or bone to place the Niagara system"))
	FVector LocationOffset;

	// Offset from the socket / bone rotation
	UPROPERTY(EditAnywhere, Category = NiagaraSystem, meta = (ToolTip = "Rotation offset from the socket or bone for the Niagara system"))
	FRotator RotationOffset;

	// Whether or not we destroy the component at the end of the notify or instead just stop
	// the emitters.
	UPROPERTY(EditAnywhere, Category = NiagaraSystem, meta = (DisplayName = "Destroy Immediately", ToolTip = "Whether the Niagara system should be immediately destroyed at the end of the notify state or be allowed to finish"))
	bool bDestroyAtEnd;

	virtual void NotifyBegin(class USkeletalMeshComponent * MeshComp, class UAnimSequenceBase * Animation, float TotalDuration) override;
	virtual void NotifyEnd(class USkeletalMeshComponent * MeshComp, class UAnimSequenceBase * Animation) override;

	// Overridden from UAnimNotifyState to provide custom notify name.
	FString GetNotifyName_Implementation() const override;

protected:
	// Spawns the NiagaraSystemComponent. Called from Notify.
	virtual UFXSystemComponent* SpawnEffect(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation) const;

private:
	bool ValidateParameters(USkeletalMeshComponent* MeshComp) const;
};
