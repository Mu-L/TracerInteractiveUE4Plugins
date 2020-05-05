// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class IPropertyUtilities;
struct FPropertyChangedEvent;

class FMovieSceneCaptureCustomization : public IDetailCustomization
{
public:
	FMovieSceneCaptureCustomization();
	~FMovieSceneCaptureCustomization();

	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	
private:

	void OnObjectPostEditChange( UObject* Object, FPropertyChangedEvent& PropertyChangedEvent );
	void OnObjectsReplaced(const TMap<UObject*, UObject*>& OldToNewInstanceMap);

	TArray<TWeakObjectPtr<>> ObjectsBeingCustomized;
	TSharedPtr<IPropertyUtilities> PropertyUtilities;

	FDelegateHandle PropertyChangedHandle;
	FDelegateHandle ObjectsReplacedHandle;
};
