// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class FMobilityCustomization;
class IDetailLayoutBuilder;

class FSceneComponentDetails : public IDetailCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** IDetailCustomization interface */
	virtual void CustomizeDetails( IDetailLayoutBuilder& DetailBuilder ) override;
	
private:
	void MakeTransformDetails( IDetailLayoutBuilder& DetailBuilder );

	TSharedPtr<FMobilityCustomization> MobilityCustomization;
};
