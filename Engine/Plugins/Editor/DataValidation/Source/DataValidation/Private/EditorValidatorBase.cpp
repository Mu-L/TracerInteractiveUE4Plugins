// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EditorValidatorBase.h"

#include "Editor.h"
#include "AssetToolsModule.h"
#include "ObjectTools.h"
#include "Dialogs/Dialogs.h"
#include "EditorValidatorSubsystem.h"
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#include "Logging/TokenizedMessage.h"

DEFINE_LOG_CATEGORY_STATIC(LogContentValidation, Log, Log);
#define LOCTEXT_NAMESPACE "AssetValidation"

UEditorValidatorBase::UEditorValidatorBase()
	: Super()
{
	bIsEnabled = true;
}

bool UEditorValidatorBase::CanValidateAsset_Implementation(UObject* InAsset) const
{
	return false;
}

EDataValidationResult UEditorValidatorBase::ValidateLoadedAsset_Implementation(UObject* InAsset, TArray<FText>& ValidationErrors)
{
	return EDataValidationResult::NotValidated;
}

void UEditorValidatorBase::AssetFails(UObject* InAsset, const FText& InMessage, TArray<FText>& ValidationErrors)
{
	FFormatNamedArguments Arguments;
	if (InAsset)
	{
		Arguments.Add(TEXT("AssetName"), FText::FromName(InAsset->GetFName()));	
	}
	Arguments.Add(TEXT("CustomMessage"), InMessage);
	Arguments.Add(TEXT("ValidatorName"), FText::FromString(GetClass()->GetName()));

	FText FailureMessage = FText::Format(LOCTEXT("AssetCheck_Message_Display", "{AssetName} failed: {CustomMessage}. ({ValidatorName})"), Arguments);

	if(LogContentValidation.GetVerbosity() >= ELogVerbosity::Verbose)
	{
		LogElapsedTime(Arguments);

	}

	ValidationErrors.Add(FailureMessage);
	ValidationResult = EDataValidationResult::Invalid;
}

void UEditorValidatorBase::LogElapsedTime(FFormatNamedArguments &Arguments)
{
	FDateTime CurrentTime = FDateTime::Now();
	FTimespan ElapsedTimeSpan = (CurrentTime - ValidationTime);
	float ElapsedTimeMS = ElapsedTimeSpan.GetTotalMilliseconds();
	FNumberFormattingOptions TimeFormat;
	TimeFormat.MinimumFractionalDigits = 5;
	Arguments.Add(TEXT("ElapsedTime"), FText::AsNumber(ElapsedTimeMS, &TimeFormat));
	FText ElapsedTimeMessage = FText::Format(LOCTEXT("ElapsedTime", "Checking {AssetName} with {ValidatorName} took {ElapsedTime} ms."), Arguments);
	UE_LOG(LogContentValidation, Verbose, TEXT("%s"), *ElapsedTimeMessage.ToString());
}

void UEditorValidatorBase::AssetPasses(UObject* InAsset)
{

	if (LogContentValidation.GetVerbosity() >= ELogVerbosity::Verbose)
	{
		FFormatNamedArguments Arguments;
		if (InAsset)
		{
			Arguments.Add(TEXT("AssetName"), FText::FromName(InAsset->GetFName()));
		}
		Arguments.Add(TEXT("ValidatorName"), FText::FromString(GetClass()->GetName()));

		LogElapsedTime(Arguments);
	}

	ValidationResult = EDataValidationResult::Valid;
}

void UEditorValidatorBase::ResetValidationState()
{
	ValidationResult = EDataValidationResult::NotValidated;
	ValidationTime = FDateTime::Now();
}

#undef LOCTEXT_NAMESPACE