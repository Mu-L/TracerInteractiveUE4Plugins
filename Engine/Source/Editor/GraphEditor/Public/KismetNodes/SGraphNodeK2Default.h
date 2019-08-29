// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "KismetNodes/SGraphNodeK2Base.h"

class UK2Node;

class GRAPHEDITOR_API SGraphNodeK2Default : public SGraphNodeK2Base
{
public:
	SLATE_BEGIN_ARGS(SGraphNodeK2Default){}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UK2Node* InNode);
};
