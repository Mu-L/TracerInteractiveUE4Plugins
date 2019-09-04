// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ControlRigGraphPanelPinFactory.h"
#include "Graph/ControlRigGraphSchema.h"
#include "Graph/ControlRigGraphNode.h"
#include "Graph/SGraphPinBoneName.h"
#include "Graph/SGraphPinCurveFloat.h"
#include "KismetPins/SGraphPinExec.h"
#include "ControlRig.h"
#include "NodeFactory.h"
#include "EdGraphSchema_K2.h"
#include "Curves/CurveFloat.h"

TSharedPtr<SGraphPin> FControlRigGraphPanelPinFactory::CreatePin(UEdGraphPin* InPin) const
{
	if (InPin)
	{
		UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(InPin->GetOwningNode());
		if (RigNode)
		{
			// use a bone name widget in case we are looking at a name with appropriate metadata
			if (InPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Name)
			{
				UStruct* Struct = nullptr;
				if (InPin->ParentPin != nullptr)
				{
					Struct = Cast<UStruct>(InPin->ParentPin->PinType.PinSubCategoryObject);
				}
				if (Struct == nullptr)
				{
					Struct = RigNode->GetUnitScriptStruct();
				}
				if (Struct)
				{
					FString NodeName, PropertyName;
					if (InPin->GetName().Split(TEXT("."), &NodeName, &PropertyName, ESearchCase::IgnoreCase, ESearchDir::FromEnd))
					{
						UProperty* Property = Struct->FindPropertyByName(*PropertyName);
						if (Property)
						{
							if (Property->HasMetaData(UControlRig::BoneNameMetaName))
							{
								return SNew(SGraphPinBoneName, InPin);
							}
						}
					}
				}
			}

			else if (InPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
			{
				if (InPin->PinType.PinSubCategoryObject == FControlRigExecuteContext::StaticStruct())
				{
					return SNew(SGraphPinExec, InPin);
				}
				else if (InPin->PinType.PinSubCategoryObject == FRuntimeFloatCurve::StaticStruct())
				{
					return SNew(SGraphPinCurveFloat, InPin);
				}
			}
		}
	}

	TSharedPtr<SGraphPin> K2PinWidget = FNodeFactory::CreateK2PinWidget(InPin);
	if(K2PinWidget.IsValid())
	{
		return K2PinWidget;
	}

	return nullptr;
}