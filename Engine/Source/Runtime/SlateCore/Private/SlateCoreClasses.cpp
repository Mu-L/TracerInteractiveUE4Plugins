// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "SlateGlobals.h"
#include "Types/SlateConstants.h"
#include "Styling/SlateWidgetStyle.h"

/** How much to scroll for each click of the mouse wheel (in Slate Screen Units). */
TAutoConsoleVariable<float> GlobalScrollAmount(
	TEXT("Slate.GlobalScrollAmount"),
	32.0f,
	TEXT("How much to scroll for each click of the mouse wheel (in Slate Screen Units)."));


float GSlateContrast = 1;

FAutoConsoleVariableRef CVarSlateContrast(
	TEXT("Slate.Contrast"),
	GSlateContrast,
	TEXT("The amount of contrast to apply to the UI.")
);



int32 GSlateLayoutCaching = 0;

FAutoConsoleVariableRef CVarSlateLayoutCaching(
	TEXT("Slate.EnableLayoutCaching"),
	GSlateLayoutCaching,
	TEXT("Whether or not dynamic prepass and layout caching is enabled")
);


// Enable fast widget paths outside the editor by default.  Only reason we don't enable them everywhere
// is that the editor is more complex than a game, and there are likely a larger swath of edge cases.
int32 GSlateFastWidgetPath = 0;

FAutoConsoleVariableRef CVarSlateFastWidgetPath(
	TEXT("Slate.EnableFastWidgetPath"),
	GSlateFastWidgetPath,
	TEXT("Whether or not we enable fast widget pathing.  This mode relies on parent pointers to work correctly.")
);



FSlateWidgetStyle::FSlateWidgetStyle()
{ }
