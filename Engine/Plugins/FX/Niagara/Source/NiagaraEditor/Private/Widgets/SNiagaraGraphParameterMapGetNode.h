// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "SNiagaraGraphNode.h"

class SNiagaraGraphParameterMapGetNode : public SNiagaraGraphNode
{
public:
	SLATE_BEGIN_ARGS(SNiagaraGraphParameterMapGetNode) {}
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UEdGraphNode* InGraphNode);

	virtual void AddPin(const TSharedRef<SGraphPin>& PinToAdd) override;
	virtual TSharedRef<SWidget> CreateNodeContentArea();
	virtual void CreatePinWidgets() override;

protected:
	const FSlateBrush* GetBackgroundBrush(TSharedPtr<SWidget> Border) const;
	TSharedPtr<SVerticalBox> PinContainerRoot;

	const FSlateBrush* BackgroundBrush;
	const FSlateBrush* BackgroundHoveredBrush;

};