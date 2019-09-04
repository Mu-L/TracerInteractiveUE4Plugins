// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ObjectTemplates/DatasmithStaticMeshComponentTemplate.h"

#include "Components/StaticMeshComponent.h"

UObject* UDatasmithStaticMeshComponentTemplate::UpdateObject( UObject* Destination, bool bForce )
{
	UStaticMeshComponent* StaticMeshComponent = Cast< UStaticMeshComponent >( Destination );

	if ( !StaticMeshComponent )
	{
		return nullptr;
	}

#if WITH_EDITORONLY_DATA
	UDatasmithStaticMeshComponentTemplate* PreviousStaticMeshTemplate = !bForce ? FDatasmithObjectTemplateUtils::GetObjectTemplate< UDatasmithStaticMeshComponentTemplate >( Destination ) : nullptr;

	if ( !PreviousStaticMeshTemplate || PreviousStaticMeshTemplate->StaticMesh == StaticMeshComponent->GetStaticMesh() )
	{
		StaticMeshComponent->SetStaticMesh( StaticMesh );
	}

	if ( !PreviousStaticMeshTemplate )
	{
		StaticMeshComponent->OverrideMaterials.Empty( OverrideMaterials.Num() );
	}
	
	for ( int32 MaterialIndex = 0; MaterialIndex < OverrideMaterials.Num(); ++MaterialIndex )
	{
		// If it's a new material override, assign it
		if ( !PreviousStaticMeshTemplate || !PreviousStaticMeshTemplate->OverrideMaterials.IsValidIndex( MaterialIndex ) )
		{
			StaticMeshComponent->SetMaterial( MaterialIndex, OverrideMaterials[ MaterialIndex ] );
		}
		else if ( StaticMeshComponent->OverrideMaterials.IsValidIndex( MaterialIndex ) &&
			PreviousStaticMeshTemplate->OverrideMaterials[ MaterialIndex ] == StaticMeshComponent->OverrideMaterials[ MaterialIndex ] )
		{
			StaticMeshComponent->SetMaterial( MaterialIndex, OverrideMaterials[ MaterialIndex ] );
		}
	}

	// Remove materials that aren't in the template anymore
	if ( PreviousStaticMeshTemplate )
	{
		for ( int32 MaterialIndexToRemove = PreviousStaticMeshTemplate->OverrideMaterials.Num() - 1; MaterialIndexToRemove >= OverrideMaterials.Num(); --MaterialIndexToRemove )
		{
			if ( StaticMeshComponent->OverrideMaterials.IsValidIndex( MaterialIndexToRemove ) &&
				StaticMeshComponent->OverrideMaterials[MaterialIndexToRemove] == PreviousStaticMeshTemplate->OverrideMaterials[MaterialIndexToRemove] )
			{
				StaticMeshComponent->OverrideMaterials.RemoveAt( MaterialIndexToRemove );
			}
		}
	}

	StaticMeshComponent->MarkRenderStateDirty();
#endif // #if WITH_EDITORONLY_DATA

	return Destination;
}

void UDatasmithStaticMeshComponentTemplate::Load( const UObject* Source )
{
#if WITH_EDITORONLY_DATA
	const UStaticMeshComponent* SourceComponent = Cast< UStaticMeshComponent >( Source );

	if ( !SourceComponent )
	{
		return;
	}

	StaticMesh = SourceComponent->GetStaticMesh();
	OverrideMaterials = SourceComponent->OverrideMaterials;
#endif // #if WITH_EDITORONLY_DATA
}

bool UDatasmithStaticMeshComponentTemplate::Equals( const UDatasmithObjectTemplate* Other ) const
{
	const UDatasmithStaticMeshComponentTemplate* TypedOther = Cast< UDatasmithStaticMeshComponentTemplate >( Other );

	if ( !TypedOther )
	{
		return false;
	}

	bool bEquals = StaticMesh == TypedOther->StaticMesh;
	bEquals = bEquals && ( OverrideMaterials == TypedOther->OverrideMaterials );

	return bEquals;
}
