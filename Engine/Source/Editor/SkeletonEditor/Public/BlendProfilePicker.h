// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UBlendProfile;

DECLARE_DELEGATE_OneParam(FOnBlendProfileSelected, UBlendProfile*);

/** Argument used to set up the blend profile picker */
struct FBlendProfilePickerArgs
{
	/** Initial blend profile selected */
	UBlendProfile* InitialProfile;

	/** Delegate to call when the picker selection is changed */
	FOnBlendProfileSelected  OnBlendProfileSelected;

	/** Allow the option to create new profiles in the picker */
	bool bAllowNew;

	/** Allow the option to clear the profile selection */
	bool bAllowClear;

	/** Allow the option to delete blend profiles from the skeleton */
	bool bAllowRemove;

	FBlendProfilePickerArgs()
		: InitialProfile(nullptr)
		, OnBlendProfileSelected()
		, bAllowNew(false)
		, bAllowClear(false)
		, bAllowRemove(false)
	{}
};
