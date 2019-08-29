// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "SGraphNodeDefault.h"

void SGraphNodeDefault::Construct( const FArguments& InArgs )
{
	this->GraphNode = InArgs._GraphNodeObj;

	this->SetCursor( EMouseCursor::CardinalCross );

	this->UpdateGraphNode();
}
