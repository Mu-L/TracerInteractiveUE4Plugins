// Copyright Epic Games, Inc. All Rights Reserved.
#include "NiagaraRendererProperties.h"
#include "NiagaraTypes.h"
#include "NiagaraCommon.h"
#include "NiagaraDataSet.h"
#include "NiagaraConstants.h"
#include "NiagaraEmitter.h"
#include "Interfaces/ITargetPlatform.h"
#include "Styling/SlateIconFinder.h"
#include "NiagaraScriptSourceBase.h"
#include "NiagaraSystem.h"

void FNiagaraRendererLayout::Initialize(int32 NumVariables)
{
	VFVariables_GT.Reset(NumVariables);
	VFVariables_GT.AddDefaulted(NumVariables);
	TotalFloatComponents_GT = 0;
	TotalHalfComponents_GT = 0;
}

bool FNiagaraRendererLayout::SetVariable(const FNiagaraDataSetCompiledData* CompiledData, const FNiagaraVariable& Variable, int32 VFVarOffset)
{
	// No compiled data, nothing to bind
	if (CompiledData == nullptr)
	{
		return false;
	}

	// use the DataSetVariable to figure out the information about the data that we'll be sending to the renderer
	const int32 VariableIndex = CompiledData->Variables.IndexOfByPredicate(
		[&](const FNiagaraVariable& InVariable)
		{
			return InVariable.GetName() == Variable.GetName();
		}
	);
	if (VariableIndex == INDEX_NONE)
	{
		VFVariables_GT[VFVarOffset] = FNiagaraRendererVariableInfo();
		return false;
	}

	const FNiagaraVariable& DataSetVariable = CompiledData->Variables[VariableIndex];
	const FNiagaraTypeDefinition& VarType = DataSetVariable.GetType();

	const bool bHalfVariable = VarType == FNiagaraTypeDefinition::GetHalfDef()
		|| VarType == FNiagaraTypeDefinition::GetHalfVec2Def()
		|| VarType == FNiagaraTypeDefinition::GetHalfVec3Def()
		|| VarType == FNiagaraTypeDefinition::GetHalfVec4Def();


	const FNiagaraVariableLayoutInfo& DataSetVariableLayout = CompiledData->VariableLayouts[VariableIndex];
	const int32 VarSize = bHalfVariable ? sizeof(FFloat16) : sizeof(float);
	const int32 NumComponents = DataSetVariable.GetSizeInBytes() / VarSize;
	const int32 Offset = bHalfVariable ? DataSetVariableLayout.HalfComponentStart : DataSetVariableLayout.FloatComponentStart;
	int32& TotalVFComponents = bHalfVariable ? TotalHalfComponents_GT : TotalFloatComponents_GT;

	int32 GPULocation = INDEX_NONE;
	bool bUpload = true;
	if (Offset != INDEX_NONE)
	{
		if (FNiagaraRendererVariableInfo* ExistingVarInfo = VFVariables_GT.FindByPredicate([&](const FNiagaraRendererVariableInfo& VarInfo) { return VarInfo.DatasetOffset == Offset && VarInfo.bHalfType == bHalfVariable; }))
		{
			//Don't need to upload this var again if it's already been uploaded for another var info. Just point to that.
			//E.g. when custom sorting uses age.
			GPULocation = ExistingVarInfo->GPUBufferOffset;
			bUpload = false;
		}
		else
		{
			//For CPU Sims we pack just the required data tightly in a GPU buffer we upload. For GPU sims the data is there already so we just provide the real data location.
			GPULocation = CompiledData->SimTarget == ENiagaraSimTarget::CPUSim ? TotalVFComponents : Offset;
			TotalVFComponents += NumComponents;
		}
	}

	VFVariables_GT[VFVarOffset] = FNiagaraRendererVariableInfo(Offset, GPULocation, NumComponents, bUpload, bHalfVariable);

	return Offset != INDEX_NONE;
}


bool FNiagaraRendererLayout::SetVariableFromBinding(const FNiagaraDataSetCompiledData* CompiledData, const FNiagaraVariableAttributeBinding& VariableBinding, int32 VFVarOffset)
{
	if (VariableBinding.IsParticleBinding())
		return SetVariable(CompiledData, VariableBinding.GetDataSetBindableVariable(), VFVarOffset);
	return false;
}

void FNiagaraRendererLayout::Finalize()
{
	ENQUEUE_RENDER_COMMAND(NiagaraFinalizeLayout)
	(
		[this, VFVariables=VFVariables_GT,TotalFloatComponents=TotalFloatComponents_GT, TotalHalfComponents=TotalHalfComponents_GT](FRHICommandListImmediate& RHICmdList) mutable
		{
			VFVariables_RT = MoveTemp(VFVariables);
			TotalFloatComponents_RT = TotalFloatComponents;
			TotalHalfComponents_RT = TotalHalfComponents;
		}
	);
}

#if WITH_EDITORONLY_DATA
bool UNiagaraRendererProperties::IsSupportedVariableForBinding(const FNiagaraVariableBase& InSourceForBinding, const FName& InTargetBindingName) const
{
	if (InSourceForBinding.IsInNameSpace(FNiagaraConstants::ParticleAttributeNamespace))
	{
		return true;
	}
	return false;
}

void UNiagaraRendererProperties::RenameEmitter(const FName& InOldName, const UNiagaraEmitter* InRenamedEmitter)
{
	const ENiagaraRendererSourceDataMode SourceMode = GetCurrentSourceMode();
	UpdateSourceModeDerivates(SourceMode);
}

const TArray<FNiagaraVariable>& UNiagaraRendererProperties::GetBoundAttributes()
{
	CurrentBoundAttributes.Reset();

	for (const FNiagaraVariableAttributeBinding* AttributeBinding : AttributeBindings)
	{
		if (AttributeBinding->GetParamMapBindableVariable().IsValid())
		{
			CurrentBoundAttributes.Add(AttributeBinding->GetParamMapBindableVariable());
		}
		/*
		else if (AttributeBinding->DataSetVariable.IsValid())
		{
			CurrentBoundAttributes.Add(AttributeBinding->DataSetVariable);
		}
		else
		{
			CurrentBoundAttributes.Add(AttributeBinding->DefaultValueIfNonExistent);
		}*/
	}

	return CurrentBoundAttributes;
}

void UNiagaraRendererProperties::GetRendererFeedback(UNiagaraEmitter* InEmitter,	TArray<FNiagaraRendererFeedback>& OutErrors, TArray<FNiagaraRendererFeedback>& OutWarnings,	TArray<FNiagaraRendererFeedback>& OutInfo) const
{
	TArray<FText> Errors;
	TArray<FText> Warnings;
	TArray<FText> Infos;
	GetRendererFeedback(InEmitter, Errors, Warnings, Infos);
	for (FText ErrorText : Errors)
	{
		OutErrors.Add(FNiagaraRendererFeedback( ErrorText));
	}
	for (FText WarningText : Warnings)
	{
		OutWarnings.Add(FNiagaraRendererFeedback( WarningText));
	}
	for (FText InfoText : Infos)
	{
		OutInfo.Add(FNiagaraRendererFeedback(InfoText));
	}
}

const FSlateBrush* UNiagaraRendererProperties::GetStackIcon() const
{
	return FSlateIconFinder::FindIconBrushForClass(GetClass());
}

FText UNiagaraRendererProperties::GetWidgetDisplayName() const
{
	return GetClass()->GetDisplayNameText();
}

void UNiagaraRendererProperties::RenameVariable(const FNiagaraVariableBase& OldVariable, const FNiagaraVariableBase& NewVariable, const UNiagaraEmitter* InEmitter)
{
	// Handle the renaming of generic renderer bindings...
	for (const FNiagaraVariableAttributeBinding* AttributeBinding : AttributeBindings)
	{
		FNiagaraVariableAttributeBinding* Binding = const_cast<FNiagaraVariableAttributeBinding*>(AttributeBinding);
		if (Binding)
			Binding->RenameVariableIfMatching(OldVariable, NewVariable, InEmitter, GetCurrentSourceMode());
	}
}
void UNiagaraRendererProperties::RemoveVariable(const FNiagaraVariableBase& OldVariable,const UNiagaraEmitter* InEmitter)
{
	// Handle the reset to defaults of generic renderer bindings
	for (const FNiagaraVariableAttributeBinding* AttributeBinding : AttributeBindings)
	{
		FNiagaraVariableAttributeBinding* Binding = const_cast<FNiagaraVariableAttributeBinding*>(AttributeBinding);
		if (Binding && Binding->Matches(OldVariable, InEmitter, GetCurrentSourceMode()))
		{
			// Reset to default but first we have to find the default value!
			for (TFieldIterator<FProperty> PropertyIterator(GetClass()); PropertyIterator; ++PropertyIterator)
			{
				if (PropertyIterator->ContainerPtrToValuePtr<void>(this) == Binding)
				{
					FNiagaraVariableAttributeBinding* DefaultBinding = static_cast<FNiagaraVariableAttributeBinding*>(PropertyIterator->ContainerPtrToValuePtr<void>(GetClass()->GetDefaultObject()));
					if (DefaultBinding)
					{
						Binding->ResetToDefault(*DefaultBinding, InEmitter, GetCurrentSourceMode());
					}
					break;
				}
			}		
		}
			
	}
}

#endif

uint32 UNiagaraRendererProperties::ComputeMaxUsedComponents(const FNiagaraDataSetCompiledData* CompiledDataSetData) const
{
	enum BaseType
	{
		BaseType_Int,
		BaseType_Float,
		BaseType_Half,
		BaseType_NUM
	};

	TArray<int32, TInlineAllocator<32>> SeenOffsets[BaseType_NUM];
	uint32 NumComponents[BaseType_NUM] = { 0 };

	auto AccumulateUniqueComponents = [&](BaseType Type, uint32 ComponentCount, int32 ComponentOffset)
	{
		if (!SeenOffsets[Type].Contains(ComponentOffset))
		{
			SeenOffsets[Type].Add(ComponentOffset);
			NumComponents[Type] += ComponentCount;
		}
	};

	for (const FNiagaraVariableAttributeBinding* Binding : AttributeBindings)
	{
		const FNiagaraVariable& Var = Binding->GetDataSetBindableVariable();

		const int32 VariableIndex = CompiledDataSetData->Variables.IndexOfByKey(Var);
		if ( VariableIndex != INDEX_NONE )
		{
			const FNiagaraVariableLayoutInfo& DataSetVarLayout = CompiledDataSetData->VariableLayouts[VariableIndex];

			if (const uint32 FloatCount = DataSetVarLayout.GetNumFloatComponents())
			{
				AccumulateUniqueComponents(BaseType_Float, FloatCount, DataSetVarLayout.FloatComponentStart);
			}

			if (const uint32 IntCount = DataSetVarLayout.GetNumInt32Components())
			{
				AccumulateUniqueComponents(BaseType_Int, IntCount, DataSetVarLayout.Int32ComponentStart);
			}

			if (const uint32 HalfCount = DataSetVarLayout.GetNumHalfComponents())
			{
				AccumulateUniqueComponents(BaseType_Half, HalfCount, DataSetVarLayout.HalfComponentStart);
			}
		}
	}

	uint32 MaxNumComponents = 0;

	for (uint32 ComponentCount : NumComponents)
	{
		MaxNumComponents = FMath::Max(MaxNumComponents, ComponentCount);
	}

	return MaxNumComponents;
}

bool UNiagaraRendererProperties::NeedsLoadForTargetPlatform(const ITargetPlatform* TargetPlatform) const
{
	// only keep enabled renderers that are parented to valid emitters
	if (const UNiagaraEmitter* OwnerEmitter = GetTypedOuter<const UNiagaraEmitter>())
	{
		if (OwnerEmitter->NeedsLoadForTargetPlatform(TargetPlatform))
		{
			return bIsEnabled && Platforms.IsEnabledForPlatform(TargetPlatform->IniPlatformName());
		}
	}

	return false;
}

void UNiagaraRendererProperties::PostLoadBindings(ENiagaraRendererSourceDataMode InSourceMode)
{
	for (int32 i = 0; i < AttributeBindings.Num(); i++)
	{
		FNiagaraVariableAttributeBinding* Binding = const_cast<FNiagaraVariableAttributeBinding*>(AttributeBindings[i]);
		Binding->PostLoad(InSourceMode);
	}
}

void UNiagaraRendererProperties::PostInitProperties()
{
	Super::PostInitProperties();
#if WITH_EDITOR
	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		SetFlags(RF_Transactional);
	}
#endif
}

void UNiagaraRendererProperties::SetIsEnabled(bool bInIsEnabled)
{
	if (bIsEnabled != bInIsEnabled)
	{
#if WITH_EDITORONLY_DATA
		// Changing the enabled state will add or remove its renderer binding data stored on the emitters RenderBindings
		// parameter store, so we need to reset to clear any binding references or add new ones
		if (UNiagaraEmitter* SrcEmitter = GetTypedOuter<UNiagaraEmitter>())
		{
			FNiagaraSystemUpdateContext(SrcEmitter, true);
		}
#endif
	}

	bIsEnabled = bInIsEnabled;
}

void UNiagaraRendererProperties::UpdateSourceModeDerivates(ENiagaraRendererSourceDataMode InSourceMode, bool bFromPropertyEdit)
{
	UNiagaraEmitter* SrcEmitter = GetTypedOuter<UNiagaraEmitter>();
	if (SrcEmitter)
	{
		for (const FNiagaraVariableAttributeBinding* Binding : AttributeBindings)
		{
			((FNiagaraVariableAttributeBinding*)Binding)->CacheValues(SrcEmitter, InSourceMode);
		}

#if WITH_EDITORONLY_DATA
		// If we added or removed any valid bindings to a non-particle source during editing, we need to reset to prevent hazards and
		// to ensure new ones get bound by the simulation
		if (bFromPropertyEdit)
		{
			if (SrcEmitter)
			{
				// We may need to refresh internal variables because this may be the first binding to it, so request a recompile as that will pull data 
				// into the right place.
				UNiagaraSystem::RequestCompileForEmitter(SrcEmitter);
			}
			FNiagaraSystemUpdateContext Context(SrcEmitter, true);
		}
#endif
	}
}