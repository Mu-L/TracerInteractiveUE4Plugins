// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

DECLARE_DELEGATE_TwoParams(FNiagaraComponentRemoveFromPool, class UNiagaraComponentPool*, class UNiagaraComponent*);

extern FNiagaraComponentRemoveFromPool GNiagaraComponentRemoveFromPool;
