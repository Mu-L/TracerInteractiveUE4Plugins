// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAssetDropTarget.h"
#include "AssetData.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "DragAndDrop/ActorDragDropOp.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "AssetSelection.h"

#define LOCTEXT_NAMESPACE "EditorWidgets"

void SAssetDropTarget::Construct(const FArguments& InArgs )
{
	OnAssetDropped = InArgs._OnAssetDropped;
	OnIsAssetAcceptableForDrop = InArgs._OnIsAssetAcceptableForDrop;
	OnIsAssetAcceptableForDropWithReason = InArgs._OnIsAssetAcceptableForDropWithReason;

	SDropTarget::Construct(
		SDropTarget::FArguments()
		.OnDrop(this, &SAssetDropTarget::OnDropped)
		[
			InArgs._Content.Widget
		]);
}

FReply SAssetDropTarget::OnDropped(TSharedPtr<FDragDropOperation> DragDropOperation)
{
	bool bUnused;
	UObject* Object = GetDroppedObject(DragDropOperation, bUnused);

	if ( Object )
	{
		OnAssetDropped.ExecuteIfBound(Object);
	}

	return FReply::Handled();
}

bool SAssetDropTarget::OnAllowDrop(TSharedPtr<FDragDropOperation> DragDropOperation) const
{
	bool bUnused = false;
	UObject* Object = GetDroppedObject(DragDropOperation, bUnused);

	if ( Object )
	{
		// Check and see if its valid to drop this object
		if (OnIsAssetAcceptableForDropWithReason.IsBound())
		{
			FText FailureReason;
			if (OnIsAssetAcceptableForDropWithReason.Execute(Object, FailureReason))
			{
				return true;
			}
			else
			{
				if (IsDragOver() && !FailureReason.IsEmpty())
				{
					if (DragDropOperation.IsValid() && DragDropOperation->IsOfType<FDecoratedDragDropOp>())
					{
						TSharedPtr<FDecoratedDragDropOp> DragDropOp = StaticCastSharedPtr<FDecoratedDragDropOp>(DragDropOperation);
						if (DragDropOp.IsValid())
						{
							DragDropOp->SetToolTip(FailureReason, FEditorStyle::GetBrush(TEXT("Graph.ConnectorFeedback.Error")));
						}
					}
				}

				return false;
			}
		}
		else if ( OnIsAssetAcceptableForDrop.IsBound() )
		{
			return OnIsAssetAcceptableForDrop.Execute(Object);
		}
		else
		{
			// If no delegate is bound assume its always valid to drop this object
			return true;
		}
	}

	return false;
}

bool SAssetDropTarget::OnIsRecognized(TSharedPtr<FDragDropOperation> DragDropOperation) const
{
	bool bRecognizedEvent = false;
	UObject* Object = GetDroppedObject(DragDropOperation, bRecognizedEvent);

	return bRecognizedEvent;
}

void SAssetDropTarget::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	SDropTarget::OnDragLeave(DragDropEvent);

	TSharedPtr<FDragDropOperation> Operation = DragDropEvent.GetOperation();
	if (Operation.IsValid() && Operation->IsOfType<FDecoratedDragDropOp>())
	{
		TSharedPtr<FDecoratedDragDropOp> DragDropOp = StaticCastSharedPtr<FDecoratedDragDropOp>(Operation);
		DragDropOp->ResetToDefaultToolTip();
	}
}

UObject* SAssetDropTarget::GetDroppedObject(TSharedPtr<FDragDropOperation> DragDropOperation, bool& bOutRecognizedEvent) const
{
	bOutRecognizedEvent = false;
	UObject* DroppedObject = NULL;

	// Asset being dragged from content browser
	if ( DragDropOperation->IsOfType<FAssetDragDropOp>() )
	{
		bOutRecognizedEvent = true;
		TSharedPtr<FAssetDragDropOp> DragDropOp = StaticCastSharedPtr<FAssetDragDropOp>(DragDropOperation);
		const TArray<FAssetData>& DroppedAssets = DragDropOp->GetAssets();

		bool bCanDrop = DroppedAssets.Num() == 1;

		if( bCanDrop )
		{
			const FAssetData& AssetData = DroppedAssets[0];

			// Make sure the asset is loaded
			DroppedObject = AssetData.GetAsset();
		}
	}
	// Asset being dragged from some external source
	else if ( DragDropOperation->IsOfType<FExternalDragOperation>() )
	{
		TArray<FAssetData> DroppedAssetData = AssetUtil::ExtractAssetDataFromDrag(DragDropOperation);

		if (DroppedAssetData.Num() == 1)
		{
			bOutRecognizedEvent = true;
			DroppedObject = DroppedAssetData[0].GetAsset();
		}
	}
	// Actor being dragged?
	else if ( DragDropOperation->IsOfType<FActorDragDropOp>() )
	{
		bOutRecognizedEvent = true;
		TSharedPtr<FActorDragDropOp> ActorDragDrop = StaticCastSharedPtr<FActorDragDropOp>(DragDropOperation);

		if (ActorDragDrop->Actors.Num() == 1)
		{
			DroppedObject = ActorDragDrop->Actors[0].Get();
		}
	}

	return DroppedObject;
}

#undef LOCTEXT_NAMESPACE
