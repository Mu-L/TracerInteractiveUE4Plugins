// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Analyzer.h"

class FAnimationProvider;
namespace Trace { class IAnalysisSession; }

class FAnimationAnalyzer : public Trace::IAnalyzer
{
public:
	FAnimationAnalyzer(Trace::IAnalysisSession& InSession, FAnimationProvider& InAnimationProvider);

	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual void OnAnalysisEnd() override {}
	virtual bool OnEvent(uint16 RouteId, const FOnEventContext& Context) override;

private:
	enum : uint16
	{
		RouteId_TickRecord,
		RouteId_SkeletalMesh,
		RouteId_SkeletalMesh2,
		RouteId_SkeletalMeshComponent,
		RouteId_SkeletalMeshComponent2,
		RouteId_SkeletalMeshFrame,
		RouteId_AnimGraph,
		RouteId_AnimNodeStart,
		RouteId_AnimNodeValueBool,
		RouteId_AnimNodeValueInt,
		RouteId_AnimNodeValueFloat,
		RouteId_AnimNodeValueVector2D,
		RouteId_AnimNodeValueVector,
		RouteId_AnimNodeValueString,
		RouteId_AnimNodeValueObject,
		RouteId_AnimNodeValueClass,
		RouteId_AnimSequencePlayer,
		RouteId_BlendSpacePlayer,
		RouteId_StateMachineState,
		RouteId_Name,
		RouteId_Notify,
		RouteId_SyncMarker,
		RouteId_Montage,
	};

	Trace::IAnalysisSession& Session;
	FAnimationProvider& AnimationProvider;
};