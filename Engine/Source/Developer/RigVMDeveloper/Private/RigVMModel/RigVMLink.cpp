// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMModel/RigVMLink.h"
#include "RigVMModel/RigVMGraph.h"

URigVMGraph* URigVMLink::GetGraph() const
{
	return Cast<URigVMGraph>(GetOuter());
}

void URigVMLink::Serialize(FArchive& Ar)
{
	if (Ar.IsLoading())
	{
		Ar << SourcePinPath;
		Ar << TargetPinPath;
	}
	else
	{
		FString SourcePinPathTemp;
		if (SourcePin)
		{
			if (SourcePin->GetNode())
			{
				SourcePinPathTemp = SourcePin->GetPinPath();
			}
		}
		FString TargetPinPathTemp;
		if (TargetPin)
		{
			if (TargetPin->GetNode())
			{
				TargetPinPathTemp = TargetPin->GetPinPath();
			}
		}
		Ar << SourcePinPathTemp;
		Ar << TargetPinPathTemp;
	}
}

int32 URigVMLink::GetLinkIndex() const
{
	int32 Index = INDEX_NONE;
	URigVMGraph* Graph = GetGraph();
	if (Graph != nullptr)
	{
		Graph->GetLinks().Find((URigVMLink*)this, Index);
	}
	return Index;

}

URigVMPin* URigVMLink::GetSourcePin()
{
	if (SourcePin == nullptr)
	{
		SourcePin = GetGraph()->FindPin(SourcePinPath);
	}
	return SourcePin;
}

URigVMPin* URigVMLink::GetTargetPin()
{
	if (TargetPin == nullptr)
	{
		TargetPin = GetGraph()->FindPin(TargetPinPath);
	}
	return TargetPin;
}

FString URigVMLink::GetPinPathRepresentation()
{
	return FString::Printf(TEXT("%s -> %s"), *GetSourcePin()->GetPinPath(), *GetTargetPin()->GetPinPath());
}

void URigVMLink::PrepareForCopy()
{
	if (URigVMPin* CurrentSourcePin = GetSourcePin())
	{
		SourcePinPath = CurrentSourcePin->GetPinPath();
	}
	if (URigVMPin* CurrenTargetPin = GetTargetPin())
	{
		TargetPinPath = CurrenTargetPin->GetPinPath();
	}
}
