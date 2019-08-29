// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EditorUndoClient.h"
#include "Editor.h"

FEditorUndoClient::~FEditorUndoClient()
{
	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}
}
