// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Components/Widget.h"
#include "TableViewBase.generated.h"

/** The base class for all wrapped table views */
UCLASS(Abstract)
class UMG_API UTableViewBase : public UWidget
{
	GENERATED_UCLASS_BODY()

	/** Delegate for constructing a UWidget based on a UObject */
	DECLARE_DYNAMIC_DELEGATE_RetVal_OneParam(UWidget*, FOnGenerateRowUObject, UObject*, Item);
};
