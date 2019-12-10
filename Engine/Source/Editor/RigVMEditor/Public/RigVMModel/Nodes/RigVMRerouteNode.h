// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMModel/RigVMNode.h"
#include "RigVMRerouteNode.generated.h"

/**
 * A reroute node is used to visually improve the 
 * data flow within a Graph. Reroutes are purely 
 * cosmetic and have no impact on the resulting
 * VM whatsoever. Reroutes can furthermore be
 * displayed as full nodes or as small circles.
 */
UCLASS(BlueprintType)
class RIGVMEDITOR_API URigVMRerouteNode : public URigVMNode
{
	GENERATED_BODY()

public:

	// Default constructor
	URigVMRerouteNode();

	// Override of node title
	virtual FString GetNodeTitle() const override { return RerouteName; }

	// Returns true if this node should be shown as a full node,
	// or false if this node should be shown as a small circle.
	UFUNCTION(BlueprintCallable, Category = RigVMRerouteNode)
	bool GetShowsAsFullNode() const;

private:

	static const FString RerouteName;
	static const FString ValueName;

	UPROPERTY()
	bool bShowAsFullNode;

	friend class URigVMController;
};

