// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EdGraphUtilities.h"

class FPhysicsAssetGraphPanelNodeFactory : public FGraphPanelNodeFactory
{
	virtual TSharedPtr<class SGraphNode> CreateNode(UEdGraphNode* Node) const override;
};
