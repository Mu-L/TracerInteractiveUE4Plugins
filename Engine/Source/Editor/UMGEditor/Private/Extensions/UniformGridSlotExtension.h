// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "WidgetReference.h"
#include "DesignerExtension.h"

class FUniformGridSlotExtension : public FDesignerExtension
{
public:
	FUniformGridSlotExtension();

	virtual ~FUniformGridSlotExtension() {}

	virtual bool CanExtendSelection(const TArray< FWidgetReference >& Selection) const override;
	
	virtual void ExtendSelection(const TArray< FWidgetReference >& Selection, TArray< TSharedRef<FDesignerSurfaceElement> >& SurfaceElements) override;

private:

	FReply HandleShiftRow(int32 ShiftAmount);
	FReply HandleShiftColumn(int32 ShiftAmount);

	void ShiftRow(UWidget* Widget, int32 ShiftAmount);
	void ShiftColumn(UWidget* Widget, int32 ShiftAmount);
};
