// Copyright Epic Games, Inc. All Rights Reserved.

#include "SoundBaseDetails.h"
#include "Sound/SoundBase.h"
#include "Sound/AudioSettings.h"
#include "PropertyHandle.h"
#include "DetailLayoutBuilder.h"

TSharedRef<IDetailCustomization> FSoundBaseDetails::MakeInstance()
{
	return MakeShareable(new FSoundBaseDetails);
}

void FSoundBaseDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	if (!GetDefault<UAudioSettings>()->IsAudioMixerEnabled())
	{
		TSharedRef<IPropertyHandle> Property = DetailBuilder.GetProperty("SoundSubmixObject", USoundBase::StaticClass());
		Property->MarkHiddenByCustomization();

		Property = DetailBuilder.GetProperty("SourceEffectChain", USoundBase::StaticClass());
		Property->MarkHiddenByCustomization();

		Property = DetailBuilder.GetProperty("OutputToBusOnly", USoundBase::StaticClass());
		Property->MarkHiddenByCustomization();

		Property = DetailBuilder.GetProperty("BusSends", USoundBase::StaticClass());
		Property->MarkHiddenByCustomization();

		Property = DetailBuilder.GetProperty("PreEffectBusSends", USoundBase::StaticClass());
		Property->MarkHiddenByCustomization();
	}
}
