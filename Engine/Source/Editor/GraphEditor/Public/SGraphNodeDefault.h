// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "SGraphNode.h"

class GRAPHEDITOR_API SGraphNodeDefault : public SGraphNode
{
public:

	SLATE_BEGIN_ARGS( SGraphNodeDefault )
		: _GraphNodeObj( static_cast<UEdGraphNode*>(NULL) )
		{}

		SLATE_ARGUMENT( UEdGraphNode*, GraphNodeObj )
	SLATE_END_ARGS()

	void Construct( const FArguments& InArgs );
};
