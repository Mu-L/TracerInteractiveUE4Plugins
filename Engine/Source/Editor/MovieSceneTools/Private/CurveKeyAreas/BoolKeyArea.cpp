// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "BoolKeyArea.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "CurveKeyEditors/SBoolCurveKeyEditor.h"


/* IKeyArea interface
 *****************************************************************************/

bool FBoolKeyArea::CanCreateKeyEditor()
{
	return true;
}


TSharedRef<SWidget> FBoolKeyArea::CreateKeyEditor(ISequencer* Sequencer)
{
	return SNew(SBoolCurveKeyEditor)
		.Sequencer(Sequencer)
		.OwningSection(OwningSection)
		.Curve(&Curve)
		.ExternalValue(ExternalValue);
};

bool FBoolKeyArea::ConvertCurveValueToIntegralType(int32 CurveValue) const
{
	return CurveValue != 0;
}

