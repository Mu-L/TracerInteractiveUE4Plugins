// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	EmptyQuery.cpp: Empty query RHI implementation.
=============================================================================*/

#include "EmptyRHIPrivate.h"


FEmptyRenderQuery::FEmptyRenderQuery(ERenderQueryType InQueryType)
{

}

FEmptyRenderQuery::~FEmptyRenderQuery()
{

}

void FEmptyRenderQuery::Begin()
{

}

void FEmptyRenderQuery::End()
{

}






FRenderQueryRHIRef FEmptyDynamicRHI::RHICreateRenderQuery(ERenderQueryType QueryType)
{
	return new FEmptyRenderQuery(QueryType);
}

bool FEmptyDynamicRHI::RHIGetRenderQueryResult(FRHIRenderQuery* QueryRHI, uint64& OutNumPixels, bool bWait, uint32 GPUIndex)
{
	check(IsInRenderingThread());

	FEmptyRenderQuery* Query = ResourceCast(QueryRHI);

	return false;
}

void FEmptyDynamicRHI::RHISubmitCommandsHint()
{
}
