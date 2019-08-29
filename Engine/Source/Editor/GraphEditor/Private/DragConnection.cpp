// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.


#include "DragConnection.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Images/SImage.h"
#include "EdGraph/EdGraph.h"
#include "SGraphPanel.h"
#include "ScopedTransaction.h"

TSharedRef<FDragConnection> FDragConnection::New(const TSharedRef<SGraphPanel>& GraphPanel, const FDraggedPinTable& DraggedPins)
{
	TSharedRef<FDragConnection> Operation = MakeShareable(new FDragConnection(GraphPanel, DraggedPins));
	Operation->Construct();

	return Operation;
}

void FDragConnection::OnDrop( bool bDropWasHandled, const FPointerEvent& MouseEvent )
{
	GraphPanel->OnStopMakingConnection();

	Super::OnDrop(bDropWasHandled, MouseEvent);
}

void FDragConnection::OnDragged(const class FDragDropEvent& DragDropEvent)
{
	FVector2D TargetPosition = DragDropEvent.GetScreenSpacePosition();

	// Reposition the info window wrt to the drag
	CursorDecoratorWindow->MoveWindowTo(DragDropEvent.GetScreenSpacePosition() + DecoratorAdjust);
	// Request the active panel to scroll if required
	GraphPanel->RequestDeferredPan(TargetPosition);
}

void FDragConnection::HoverTargetChanged()
{
	TArray<FPinConnectionResponse> UniqueMessages;

	if (UEdGraphPin* TargetPinObj = GetHoveredPin())
	{
		TArray<UEdGraphPin*> ValidSourcePins;
		ValidateGraphPinList(/*out*/ ValidSourcePins);

		// Check the schema for connection responses
		for (UEdGraphPin* StartingPinObj : ValidSourcePins)
		{
			if (TargetPinObj != StartingPinObj)
			{
				// The Graph object in which the pins reside.
				UEdGraph* GraphObj = StartingPinObj->GetOwningNode()->GetGraph();

				// Determine what the schema thinks about the wiring action
				const FPinConnectionResponse Response = GraphObj->GetSchema()->CanCreateConnection( StartingPinObj, TargetPinObj );

				if (Response.Response == ECanCreateConnectionResponse::CONNECT_RESPONSE_DISALLOW)
				{
					TSharedPtr<SGraphNode> NodeWidget = TargetPinObj->GetOwningNode()->DEPRECATED_NodeWidget.Pin();
					 if (NodeWidget.IsValid())
					 {
						 NodeWidget->NotifyDisallowedPinConnection(StartingPinObj, TargetPinObj);
					 }
				}

				UniqueMessages.AddUnique(Response);
			}
		}
	}
	else if(UEdGraphNode* TargetNodeObj = GetHoveredNode())
	{
		TArray<UEdGraphPin*> ValidSourcePins;
		ValidateGraphPinList(/*out*/ ValidSourcePins);

		// Check the schema for connection responses
		for (UEdGraphPin* StartingPinObj : ValidSourcePins)
		{
			FPinConnectionResponse Response;
			FText ResponseText;
			if (StartingPinObj->GetOwningNode() != TargetNodeObj && StartingPinObj->GetSchema()->SupportsDropPinOnNode(TargetNodeObj, StartingPinObj->PinType, StartingPinObj->Direction, ResponseText))
			{
				Response.Response = ECanCreateConnectionResponse::CONNECT_RESPONSE_MAKE;
			}
			else
			{
				Response.Response = ECanCreateConnectionResponse::CONNECT_RESPONSE_DISALLOW;
			}
			
			// Do not display an error if there is no message
			if (!ResponseText.IsEmpty())
			{
				Response.Message = ResponseText;
				UniqueMessages.AddUnique(Response);
			}
		}
	}
	else if(UEdGraph* CurrentHoveredGraph = GetHoveredGraph())
	{
		TArray<UEdGraphPin*> ValidSourcePins;
		ValidateGraphPinList(/*out*/ ValidSourcePins);

		for (UEdGraphPin* StartingPinObj : ValidSourcePins)
		{
			// Let the schema describe the connection we might make
			FPinConnectionResponse Response = CurrentHoveredGraph->GetSchema()->CanCreateNewNodes(StartingPinObj);
			if(!Response.Message.IsEmpty())
			{
				UniqueMessages.AddUnique(Response);
			}
		}
	}

	// Let the user know the status of dropping now
	if (UniqueMessages.Num() == 0)
	{
		// Display the place a new node icon, we're not over a valid pin and have no message from the schema
		SetSimpleFeedbackMessage(
			FEditorStyle::GetBrush(TEXT("Graph.ConnectorFeedback.NewNode")),
			FLinearColor::White,
			NSLOCTEXT("GraphEditor.Feedback", "PlaceNewNode", "Place a new node."));
	}
	else
	{
		// Take the unique responses and create visual feedback for it
		TSharedRef<SVerticalBox> FeedbackBox = SNew(SVerticalBox);
		for (auto ResponseIt = UniqueMessages.CreateConstIterator(); ResponseIt; ++ResponseIt)
		{
			// Determine the icon
			const FSlateBrush* StatusSymbol = NULL;

			switch (ResponseIt->Response)
			{
			case CONNECT_RESPONSE_MAKE:
			case CONNECT_RESPONSE_BREAK_OTHERS_A:
			case CONNECT_RESPONSE_BREAK_OTHERS_B:
			case CONNECT_RESPONSE_BREAK_OTHERS_AB:
				StatusSymbol = FEditorStyle::GetBrush(TEXT("Graph.ConnectorFeedback.OK"));
				break;

			case CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE:
				StatusSymbol = FEditorStyle::GetBrush(TEXT("Graph.ConnectorFeedback.ViaCast"));
				break;

			case CONNECT_RESPONSE_DISALLOW:
			default:
				StatusSymbol = FEditorStyle::GetBrush(TEXT("Graph.ConnectorFeedback.Error"));
				break;
			}

			// Add a new message row
			FeedbackBox->AddSlot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(3.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SImage) .Image( StatusSymbol )
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock) .Text( ResponseIt->Message )
				]
			];
		}

		SetFeedbackMessage(FeedbackBox);
	}
}

FDragConnection::FDragConnection(const TSharedRef<SGraphPanel>& GraphPanelIn, const FDraggedPinTable& DraggedPinsIn)
	: GraphPanel(GraphPanelIn)
	, DraggingPins(DraggedPinsIn)
	, DecoratorAdjust(FSlateApplication::Get().GetCursorSize())
{	
	if (DraggingPins.Num() > 0)
	{
		const UEdGraphPin* PinObj = FDraggedPinTable::TConstIterator(DraggedPinsIn)->GetPinObj(*GraphPanelIn);
		if (PinObj && PinObj->Direction == EGPD_Input)
		{
			DecoratorAdjust *= FVector2D(-1.0f, 1.0f);
		}
	}

	for (const FGraphPinHandle& DraggedPin : DraggedPinsIn)
	{
		GraphPanelIn->OnBeginMakingConnection(DraggedPin);
	}
}

FReply FDragConnection::DroppedOnPin(FVector2D ScreenPosition, FVector2D GraphPosition)
{
	TArray<UEdGraphPin*> ValidSourcePins;
	ValidateGraphPinList(/*out*/ ValidSourcePins);

	const FScopedTransaction Transaction( NSLOCTEXT("UnrealEd", "GraphEd_CreateConnection", "Create Pin Link") );

	UEdGraphPin* PinB = GetHoveredPin();
	bool bError = false;
	TSet<UEdGraphNode*> NodeList;

	for (UEdGraphPin* PinA : ValidSourcePins)
	{
		if ((PinA != NULL) && (PinB != NULL))
		{
			UEdGraph* MyGraphObj = PinA->GetOwningNode()->GetGraph();

			if (MyGraphObj->GetSchema()->TryCreateConnection(PinA, PinB))
			{
				NodeList.Add(PinA->GetOwningNode());
				NodeList.Add(PinB->GetOwningNode());
			}
		}
		else
		{
			bError = true;
		}
	}

	// Send all nodes that received a new pin connection a notification
	for (auto It = NodeList.CreateConstIterator(); It; ++It)
	{
		UEdGraphNode* Node = (*It);
		Node->NodeConnectionListChanged();
	}

	if (bError)
	{
		return FReply::Unhandled();
	}

	return FReply::Handled();
}

FReply FDragConnection::DroppedOnNode(FVector2D ScreenPosition, FVector2D GraphPosition)
{
	bool bHandledPinDropOnNode = false;
	UEdGraphNode* NodeOver = GetHoveredNode();

	if (NodeOver)
	{
		// Gather any source drag pins
		TArray<UEdGraphPin*> ValidSourcePins;
		ValidateGraphPinList(/*out*/ ValidSourcePins);


		if (ValidSourcePins.Num())
		{
			for (UEdGraphPin* SourcePin : ValidSourcePins)
			{
				// Check for pin drop support
				FText ResponseText;
				if (SourcePin->GetOwningNode() != NodeOver && SourcePin->GetSchema()->SupportsDropPinOnNode(NodeOver, SourcePin->PinType, SourcePin->Direction, ResponseText))
				{
					bHandledPinDropOnNode = true;

					// Find which pin name to use and drop the pin on the node
					const FName PinName = SourcePin->PinFriendlyName.IsEmpty()? SourcePin->PinName : *SourcePin->PinFriendlyName.ToString();

					const FScopedTransaction Transaction( NSLOCTEXT("UnrealEd", "AddInParam", "Add In Parameter" ) );

					UEdGraphPin* EdGraphPin = NodeOver->GetSchema()->DropPinOnNode(GetHoveredNode(), PinName, SourcePin->PinType, SourcePin->Direction);

					// This can invalidate the source pin due to node reconstruction, abort in that case
					if(SourcePin->GetOwningNodeUnchecked() && EdGraphPin)
					{
						SourcePin->Modify();
						EdGraphPin->Modify();
						SourcePin->GetSchema()->TryCreateConnection(SourcePin, EdGraphPin);
					}
				}

				// If we have not handled the pin drop on node and there is an error message, do not let other actions occur.
				if(!bHandledPinDropOnNode && !ResponseText.IsEmpty())
				{
					bHandledPinDropOnNode = true;
				}
			}
		}
	}
	return bHandledPinDropOnNode? FReply::Handled() : FReply::Unhandled();
}

FReply FDragConnection::DroppedOnPanel( const TSharedRef< SWidget >& Panel, FVector2D ScreenPosition, FVector2D GraphPosition, UEdGraph& Graph)
{
	// Gather any source drag pins
	TArray<UEdGraphPin*> PinObjects;
	ValidateGraphPinList(/*out*/ PinObjects);

	// Create a context menu
	TSharedPtr<SWidget> WidgetToFocus = GraphPanel->SummonContextMenu(ScreenPosition, GraphPosition, NULL, NULL, PinObjects);

	// Give the context menu focus
	return (WidgetToFocus.IsValid())
		? FReply::Handled().SetUserFocus(WidgetToFocus.ToSharedRef(), EFocusCause::SetDirectly)
		: FReply::Handled();
}


void FDragConnection::ValidateGraphPinList(TArray<UEdGraphPin*>& OutValidPins)
{
	OutValidPins.Empty(DraggingPins.Num());
	for (const FGraphPinHandle& PinHandle : DraggingPins)
	{
		if (UEdGraphPin* GraphPin = PinHandle.GetPinObj(*GraphPanel))
		{
			OutValidPins.Add(GraphPin);
		}
	}
}
