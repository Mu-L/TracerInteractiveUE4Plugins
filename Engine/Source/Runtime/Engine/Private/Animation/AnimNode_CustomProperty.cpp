// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Animation/AnimNode_CustomProperty.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNode_SubInput.h"

FAnimNode_CustomProperty::FAnimNode_CustomProperty()
	: TargetInstance(nullptr)
#if WITH_EDITOR
	, bReinitializeProperties(false)
#endif // WITH_EDITOR
{

}

void FAnimNode_CustomProperty::SetTargetInstance(UObject* InInstance)
{
	TargetInstance = InInstance;
}

void FAnimNode_CustomProperty::PropagateInputProperties(const UObject* InSourceInstance)
{
	if(TargetInstance)
	{
		// First copy properties
		check(SourceProperties.Num() == DestProperties.Num());
		for(int32 PropIdx = 0; PropIdx < SourceProperties.Num(); ++PropIdx)
		{
			UProperty* CallerProperty = SourceProperties[PropIdx];
			UProperty* SubProperty = DestProperties[PropIdx];

			check(CallerProperty && SubProperty);

#if WITH_EDITOR
			if (ensure(CallerProperty->SameType(SubProperty)))
#endif
			{
				const uint8* SrcPtr = CallerProperty->ContainerPtrToValuePtr<uint8>(InSourceInstance);
				uint8* DestPtr = SubProperty->ContainerPtrToValuePtr<uint8>(TargetInstance);

				CallerProperty->CopyCompleteValue(DestPtr, SrcPtr);
			}
		}
	}
}

void FAnimNode_CustomProperty::PreUpdate(const UAnimInstance* InAnimInstance) 
{
	FAnimNode_Base::PreUpdate(InAnimInstance);

#if WITH_EDITOR
	if (bReinitializeProperties)
	{
		InitializeProperties(InAnimInstance, GetTargetClass());
		bReinitializeProperties = false;
	}
#endif// WITH_EDITOR
}

void FAnimNode_CustomProperty::InitializeProperties(const UObject* InSourceInstance, UClass* InTargetClass)
{
	if(InTargetClass)
	{
		// Build property lists
		SourceProperties.Reset(SourcePropertyNames.Num());
		DestProperties.Reset(SourcePropertyNames.Num());

		check(SourcePropertyNames.Num() == DestPropertyNames.Num());

		for(int32 Idx = 0; Idx < SourcePropertyNames.Num(); ++Idx)
		{
			FName& SourceName = SourcePropertyNames[Idx];
			FName& DestName = DestPropertyNames[Idx];

			UClass* SourceClass = InSourceInstance->GetClass();

			UProperty* SourceProperty = FindField<UProperty>(SourceClass, SourceName);
			UProperty* DestProperty = FindField<UProperty>(InTargetClass, DestName);

			if (SourceProperty && DestProperty
#if WITH_EDITOR
				// This type check can fail when anim blueprints are in an error state:
				&& SourceProperty->SameType(DestProperty)
#endif
				)
			{
				SourceProperties.Add(SourceProperty);
				DestProperties.Add(DestProperty);
			}
		}
		
	}
}

