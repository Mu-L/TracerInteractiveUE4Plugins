// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class ITableRow;
class STableViewBase;

class FSkeletonNotifyDetails : public IDetailCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** IDetailCustomization interface */
	virtual void CustomizeDetails( IDetailLayoutBuilder& DetailBuilder ) override;

	/** Delegate to handle creating rows for the animations slate list */
	TSharedRef< ITableRow > MakeAnimationRow( TSharedPtr<FString> Item, const TSharedRef< STableViewBase >& OwnerTable );
};
