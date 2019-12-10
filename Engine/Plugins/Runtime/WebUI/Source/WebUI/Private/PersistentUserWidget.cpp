// Copyright 2019 Tracer Interactive, LLC. All Rights Reserved.
#include "PersistentUserWidget.h"

UPersistentUserWidget::UPersistentUserWidget( const FObjectInitializer& ObjectInitializer )
    : Super( ObjectInitializer )
{
    //
}

void UPersistentUserWidget::OnLevelRemovedFromWorld( ULevel* InLevel, UWorld* InWorld )
{
    if ( InLevel == nullptr && InWorld == GetWorld() )
    {
        //RemoveFromParent();
    }
}
