// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraEvents.h"
#include "WorldCollision.h"

UENUM()
enum class ENiagaraCollisionMode : uint8
{
	None = 0,
	SceneGeometry,
	DepthBuffer,
	DistanceField
};


struct FNiagaraCollisionTrace
{
	FTraceHandle CollisionTraceHandle;
	uint32 SourceParticleIndex;
	float CollisionSize;
	float DeltaSeconds;
};


class FNiagaraCollisionBatch
{
public:
	FNiagaraCollisionBatch()
		: CollisionEventDataSet(nullptr)
		, EmitterName(NAME_None)
	{
	}

	~FNiagaraCollisionBatch()
	{
		FNiagaraEventDataSetMgr::Reset(OwnerSystemInstanceName, EmitterName);
	}

	void Reset()
	{
		if (CollisionEventDataSet)
		{
			CollisionEventDataSet->ResetBuffers();
		}
	}

	void Init(FName InOwnerSystemInstanceName, FName InEmitterName)
	{
		if (CollisionEventDataSet)
		{
			CollisionEventDataSet->Init(FNiagaraDataSetID(), ENiagaraSimTarget::CPUSim, TEXT("NiagaraCollisionEventDataset"));
		}
		EmitterName = InEmitterName;
		OwnerSystemInstanceName = InOwnerSystemInstanceName;
		CollisionEventDataSet = FNiagaraEventDataSetMgr::CreateEventDataSet(OwnerSystemInstanceName, EmitterName, NIAGARA_BUILTIN_EVENTNAME_COLLISION);
		// TODO: this should go away once we can use the FNiagaraCollisionEventPayload
		// UStruct to create the data set
		//FNiagaraVariable ValidVar(FNiagaraTypeDefinition::GetIntDef(), "Valid");
		FNiagaraVariable PosVar(FNiagaraTypeDefinition::GetVec3Def(), "CollisionLocation");
		FNiagaraVariable NrmVar(FNiagaraTypeDefinition::GetVec3Def(), "CollisionNormal");
		FNiagaraVariable PhysMatIdxVar(FNiagaraTypeDefinition::GetIntDef(), "PhysicalMaterialIndex");
		FNiagaraVariable VelVar(FNiagaraTypeDefinition::GetVec3Def(), "CollisionVelocity");
		FNiagaraVariable ParticleIndexVar(FNiagaraTypeDefinition::GetIntDef(), "ParticleIndex");
		//CollisionEventDataSet->AddVariable(ValidVar);
		CollisionEventDataSet->AddVariable(PosVar);
		CollisionEventDataSet->AddVariable(NrmVar);
		CollisionEventDataSet->AddVariable(PhysMatIdxVar);
		CollisionEventDataSet->AddVariable(VelVar);
		CollisionEventDataSet->AddVariable(ParticleIndexVar);
		CollisionEventDataSet->Finalize();
	}
	
	void KickoffNewBatch(class FNiagaraEmitterInstance *Sim, float DeltaSeconds);

	void GenerateEventsFromResults(class FNiagaraEmitterInstance *Sim);

	const FNiagaraDataSet *GetDataSet() const { return CollisionEventDataSet; }
private:
	TArray<FTraceHandle> CollisionTraceHandles;
	TArray<FNiagaraCollisionTrace> CollisionTraces;
	TArray<FNiagaraCollisionEventPayload> CollisionEvents;
	FNiagaraDataSet *CollisionEventDataSet;
	FName EmitterName;
	FName OwnerSystemInstanceName;
};



struct FNiagaraDICollsionQueryResult
{
	uint32 TraceID;
	FVector CollisionPos;
	FVector CollisionNormal;
	FVector CollisionVelocity;
	int32 PhysicalMaterialIdx;
	float Friction;
	float Restitution;
	bool IsInsideMesh;
};

class FNiagaraDICollisionQueryBatch
{
public:
	FNiagaraDICollisionQueryBatch()
		: BatchID(NAME_None)
		, CurrBuffer(0)
	{
	}

	~FNiagaraDICollisionQueryBatch()
	{
	}

	int32 GetWriteBufferIdx()
	{
		return CurrBuffer;
	}

	int32 GetReadBufferIdx()
	{
		return CurrBuffer ^ 0x1;
	}

	void Tick(ENiagaraSimTarget Target)
	{
		CurrBuffer = CurrBuffer ^ 0x1;
	}

	void ClearWrite()
	{
		uint32 PrevNum = CollisionTraces[CurrBuffer].Num();
		CollisionTraces[CurrBuffer].Empty(PrevNum);
		IdToTraceIdx[CurrBuffer].Empty(PrevNum);
	}


	void Init(FName InBatchID, UWorld *InCollisionWorld)
	{
		BatchID = InBatchID;
		CollisionWorld = InCollisionWorld;
		CollisionTraces[0].Empty();
		CollisionTraces[1].Empty();
		IdToTraceIdx[0].Empty();
		IdToTraceIdx[1].Empty();
		CurrBuffer = 0;
		TraceID = 0;
	}

	int32 SubmitQuery(FVector Position, FVector Direction, float CollisionSize, float DeltaSeconds);
	int32 SubmitQuery(FVector StartPos, FVector EndPos, ECollisionChannel TraceChannel);
	bool PerformQuery(FVector StartPos, FVector EndPos, FNiagaraDICollsionQueryResult &Result, ECollisionChannel TraceChannel);
	bool GetQueryResult(uint32 TraceID, FNiagaraDICollsionQueryResult &Result);

private:
	//TArray<FTraceHandle> CollisionTraceHandles;

	TArray<FNiagaraCollisionEventPayload> CollisionEvents;
	FNiagaraDataSet *CollisionEventDataSet;

	FName BatchID;
	TArray<FNiagaraCollisionTrace> CollisionTraces[2];
	TMap<uint32, int32> IdToTraceIdx[2];
	uint32 CurrBuffer;
	uint32 TraceID;
	UWorld *CollisionWorld;
};