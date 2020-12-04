// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraParameterCollection.h"
#include "NiagaraDataInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/SecureHash.h"
#if WITH_EDITORONLY_DATA
	#include "IAssetTools.h"
#endif
#include "AssetRegistryModule.h"

//////////////////////////////////////////////////////////////////////////

UNiagaraParameterCollectionInstance::UNiagaraParameterCollectionInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, SourceInstanceDirtied(false)
{
	ParameterStorage.SetOwner(this);
	//Bind(ParameterStorage);
}

UNiagaraParameterCollectionInstance::~UNiagaraParameterCollectionInstance()
{
	//Unbind(ParameterStorage);
}

void UNiagaraParameterCollectionInstance::PostLoad()
{
	Super::PostLoad();

	ParameterStorage.PostLoad();

	//Ensure we're synced up with our collection. TODO: Do conditionally via a version number/guid?
	SyncWithCollection();
}

void UNiagaraParameterCollectionInstance::SetParent(UNiagaraParameterCollection* InParent)
{
	Collection = InParent;
	SyncWithCollection();
}

bool UNiagaraParameterCollectionInstance::IsDefaultInstance()const
{
	return GetParent() && GetParent()->GetDefaultInstance() == this; 
}

bool UNiagaraParameterCollectionInstance::AddParameter(const FNiagaraVariable& Parameter)
{
	Modify();
	return ParameterStorage.AddParameter(Parameter);
}

bool UNiagaraParameterCollectionInstance::RemoveParameter(const FNiagaraVariable& Parameter)
{
	Modify();
	return ParameterStorage.RemoveParameter(Parameter); 
}

void UNiagaraParameterCollectionInstance::RenameParameter(const FNiagaraVariable& Parameter, FName NewName)
{
	Modify();
	ParameterStorage.RenameParameter(Parameter, NewName); 
}

void UNiagaraParameterCollectionInstance::Empty()
{
	Modify();
	ParameterStorage.Empty();
}

void UNiagaraParameterCollectionInstance::GetParameters(TArray<FNiagaraVariable>& OutParameters)
{
	ParameterStorage.GetParameters(OutParameters);
}

void UNiagaraParameterCollectionInstance::Bind(UWorld* World)
{
	if (const UMaterialParameterCollection* SourceCollection = Collection ? Collection->GetSourceCollection() : nullptr)
	{
		if (UMaterialParameterCollectionInstance* SourceInstance = World->GetParameterCollectionInstance(SourceCollection))
		{
			SourceInstance->OnParametersUpdated().AddLambda([this]()
			{
				SourceInstanceDirtied = true;
			});

			RefreshSourceParameters(World);
		}
	}

}

void UNiagaraParameterCollectionInstance::RefreshSourceParameters(UWorld* World)
{
	// if the NPC uses any MPC as sources, the make those bindings now
	if (const UMaterialParameterCollection* SourceCollection = Collection ? Collection->GetSourceCollection() : nullptr)
	{
		if (UMaterialParameterCollectionInstance* SourceInstance = World->GetParameterCollectionInstance(SourceCollection))
		{
			const FNiagaraTypeDefinition& ScalarDef = FNiagaraTypeDefinition::GetFloatDef();
			const FNiagaraTypeDefinition& ColorDef = FNiagaraTypeDefinition::GetColorDef();

			for (const auto& Variable : ParameterStorage.ReadParameterVariables())
			{
				const FNiagaraTypeDefinition& VariableType = Variable.GetType();

				// TODO - we should probably store an array of the FriendlyNames as FNames rather than doing any string work
				const FName ParameterName = *Collection->FriendlyNameFromParameterName(Variable.GetName().ToString());

				if (ParameterName != NAME_None)
				{
					if (VariableType == ScalarDef)
					{
						float ScalarValue;
						if (SourceInstance->GetScalarParameterValue(ParameterName, ScalarValue))
						{
							ParameterStorage.SetParameterValue(ScalarValue, Variable);
						}
					}
					else if (VariableType == ColorDef)
					{
						FLinearColor VectorValue;
						if (SourceInstance->GetVectorParameterValue(ParameterName, VectorValue))
						{
							ParameterStorage.SetParameterValue(VectorValue, Variable);
						}
					}
				}
			}
		}
	}
}

void UNiagaraParameterCollectionInstance::Tick(UWorld* World)
{
	if (SourceInstanceDirtied)
	{
		RefreshSourceParameters(World);
		SourceInstanceDirtied = false;
	}

	//Push our parameter changes to any bound stores.
	ParameterStorage.Tick();
}

void UNiagaraParameterCollectionInstance::SyncWithCollection()
{
	FNiagaraParameterStore OldStore = ParameterStorage;
	ParameterStorage.Empty(false);

	for (FNiagaraVariable& Param : Collection->GetParameters())
	{
		int32 Offset = OldStore.IndexOf(Param);
		if (Offset != INDEX_NONE && OverridesParameter(Param))
		{
			//If this parameter is in the old store and we're overriding it, use the existing value in the store.
			int32 ParameterStorageOffset = INDEX_NONE;
			ParameterStorage.AddParameter(Param, false, true, &ParameterStorageOffset);
			if (Param.IsDataInterface())
			{
				ParameterStorage.SetDataInterface(OldStore.GetDataInterface(Offset), Param);
			}
			else
			{
				ParameterStorage.SetParameterData(OldStore.GetParameterData(Offset), ParameterStorageOffset, Param.GetSizeInBytes());
			}
		}
		else
		{
			//If the parameter did not exist in the old store or we don't override this parameter, sync it up to the parent collection.
			FNiagaraParameterStore& DefaultStore = Collection->GetDefaultInstance()->GetParameterStore();
			Offset = DefaultStore.IndexOf(Param);
			check(Offset != INDEX_NONE);

			int32 ParameterStorageOffset = INDEX_NONE;
			ParameterStorage.AddParameter(Param, false, true, &ParameterStorageOffset);
			if (Param.IsDataInterface())
			{
				ParameterStorage.SetDataInterface(CastChecked<UNiagaraDataInterface>(StaticDuplicateObject(DefaultStore.GetDataInterface(Offset), this)), Param);
			}
			else
			{
				ParameterStorage.SetParameterData(DefaultStore.GetParameterData(Offset), ParameterStorageOffset, Param.GetSizeInBytes());
			}
		}
	}

	ParameterStorage.Rebind();
}

bool UNiagaraParameterCollectionInstance::OverridesParameter(const FNiagaraVariable& Parameter)const
{ 
	return IsDefaultInstance() || OverridenParameters.Contains(Parameter); 
}

void UNiagaraParameterCollectionInstance::SetOverridesParameter(const FNiagaraVariable& Parameter, bool bOverrides)
{
	if (bOverrides)
	{
		OverridenParameters.AddUnique(Parameter);
	}
	else
	{
		OverridenParameters.Remove(Parameter);
	}
}

//Blueprint Accessors
bool UNiagaraParameterCollectionInstance::GetBoolParameter(const FString& InVariableName)
{
	return ParameterStorage.GetParameterValue<int32>(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), *Collection->ParameterNameFromFriendlyName(InVariableName))) == FNiagaraBool::True;
}

float UNiagaraParameterCollectionInstance::GetFloatParameter(const FString& InVariableName)
{
	return ParameterStorage.GetParameterValue<float>(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), *Collection->ParameterNameFromFriendlyName(InVariableName)));
}

int32 UNiagaraParameterCollectionInstance::GetIntParameter(const FString& InVariableName)
{
	return ParameterStorage.GetParameterValue<int32>(FNiagaraVariable(FNiagaraTypeDefinition::GetIntStruct(), *Collection->ParameterNameFromFriendlyName(InVariableName)));
}

FVector2D UNiagaraParameterCollectionInstance::GetVector2DParameter(const FString& InVariableName)
{
	return ParameterStorage.GetParameterValue<FVector2D>(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), *Collection->ParameterNameFromFriendlyName(InVariableName)));
}

FVector UNiagaraParameterCollectionInstance::GetVectorParameter(const FString& InVariableName)
{
	return ParameterStorage.GetParameterValue<FVector>(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), *Collection->ParameterNameFromFriendlyName(InVariableName)));
}

FVector4 UNiagaraParameterCollectionInstance::GetVector4Parameter(const FString& InVariableName)
{
	return ParameterStorage.GetParameterValue<FVector4>(FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), *Collection->ParameterNameFromFriendlyName(InVariableName)));
}


FQuat UNiagaraParameterCollectionInstance::GetQuatParameter(const FString& InVariableName)
{
	return ParameterStorage.GetParameterValue<FQuat>(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), *Collection->ParameterNameFromFriendlyName(InVariableName)));
}

FLinearColor UNiagaraParameterCollectionInstance::GetColorParameter(const FString& InVariableName)
{
	return ParameterStorage.GetParameterValue<FLinearColor>(FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), *Collection->ParameterNameFromFriendlyName(InVariableName)));
}

template<typename T>
static bool CheckConflictWithSourceMpc(FName ParameterName, FString FunctionCall, const T& Value, const UNiagaraParameterCollection* Collection)
{
	if (const UMaterialParameterCollection* SourceCollection = Collection ? Collection->GetSourceCollection() : nullptr)
	{
		if (SourceCollection->GetParameterId(ParameterName).IsValid())
		{
#if !UE_BUILD_SHIPPING
			static bool LogWrittenOnce = false;

			if (!LogWrittenOnce)
			{
				UE_LOG(LogNiagara, Warning, TEXT("Skipping attempt to %s for parameter %s of %s because it is driven by MPC %s"),
					*ParameterName.ToString(), *FunctionCall, *Collection->GetFullName(), *Collection->GetSourceCollection()->GetFullName());

				LogWrittenOnce = true;
			}
#endif

			return true;
		}
	}

	return false;
}

void UNiagaraParameterCollectionInstance::SetBoolParameter(const FString& InVariableName, bool InValue)
{
	const FName ParameterName = *Collection->ParameterNameFromFriendlyName(InVariableName);
	if (!CheckConflictWithSourceMpc(ParameterName, __FUNCTION__, InValue, Collection))
	{
		ParameterStorage.SetParameterValue(InValue ? FNiagaraBool::True : FNiagaraBool::False, FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), ParameterName));
	}
}

void UNiagaraParameterCollectionInstance::SetFloatParameter(const FString& InVariableName, float InValue)
{
	const FName ParameterName = *Collection->ParameterNameFromFriendlyName(InVariableName);
	if (!CheckConflictWithSourceMpc(ParameterName, __FUNCTION__, InValue, Collection))
	{
		ParameterStorage.SetParameterValue(InValue, FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), ParameterName));
	}
}

void UNiagaraParameterCollectionInstance::SetIntParameter(const FString& InVariableName, int32 InValue)
{
	const FName ParameterName = *Collection->ParameterNameFromFriendlyName(InVariableName);
	if (!CheckConflictWithSourceMpc(ParameterName, __FUNCTION__, InValue, Collection))
	{
		ParameterStorage.SetParameterValue(InValue, FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), ParameterName));
	}
}

void UNiagaraParameterCollectionInstance::SetVector2DParameter(const FString& InVariableName, FVector2D InValue)
{
	const FName ParameterName = *Collection->ParameterNameFromFriendlyName(InVariableName);
	if (!CheckConflictWithSourceMpc(ParameterName, __FUNCTION__, InValue, Collection))
	{
		ParameterStorage.SetParameterValue(InValue, FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), ParameterName));
	}
}

void UNiagaraParameterCollectionInstance::SetVectorParameter(const FString& InVariableName, FVector InValue)
{
	const FName ParameterName = *Collection->ParameterNameFromFriendlyName(InVariableName);
	if (!CheckConflictWithSourceMpc(ParameterName, __FUNCTION__, InValue, Collection))
	{
		ParameterStorage.SetParameterValue(InValue, FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), ParameterName));
	}
}

void UNiagaraParameterCollectionInstance::SetVector4Parameter(const FString& InVariableName, const FVector4& InValue)
{
	const FName ParameterName = *Collection->ParameterNameFromFriendlyName(InVariableName);
	if (!CheckConflictWithSourceMpc(ParameterName, __FUNCTION__, InValue, Collection))
	{
		ParameterStorage.SetParameterValue(InValue, FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), ParameterName));
	}
}

void UNiagaraParameterCollectionInstance::SetColorParameter(const FString& InVariableName, FLinearColor InValue)
{
	const FName ParameterName = *Collection->ParameterNameFromFriendlyName(InVariableName);
	if (!CheckConflictWithSourceMpc(ParameterName, __FUNCTION__, InValue, Collection))
	{
		ParameterStorage.SetParameterValue(InValue, FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), ParameterName));
	}
}

void UNiagaraParameterCollectionInstance::SetQuatParameter(const FString& InVariableName, const FQuat& InValue)
{
	const FName ParameterName = *Collection->ParameterNameFromFriendlyName(InVariableName);
	if (!CheckConflictWithSourceMpc(ParameterName, __FUNCTION__, InValue, Collection))
	{
		ParameterStorage.SetParameterValue(InValue, FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), ParameterName));
	}
}

//////////////////////////////////////////////////////////////////////////

UNiagaraParameterCollection::UNiagaraParameterCollection(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Namespace = *GetName();
	DefaultInstance = ObjectInitializer.CreateDefaultSubobject<UNiagaraParameterCollectionInstance>(this, TEXT("Default Instance"));
	DefaultInstance->SetParent(this);
}

#if WITH_EDITORONLY_DATA
void UNiagaraParameterCollection::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	MakeNamespaceNameUnique();

	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraParameterCollection, SourceMaterialCollection))
	{
		AddDefaultSourceParameters();
		OnChangedDelegate.Broadcast();
	}
}
#endif

int32 UNiagaraParameterCollection::IndexOfParameter(const FNiagaraVariable& Var)
{
	return Parameters.IndexOfByPredicate([&](const FNiagaraVariable& Other)
	{
		return Var.IsEquivalent(Other);
	});
}

int32 UNiagaraParameterCollection::AddParameter(const FNiagaraVariable& Parameter)
{
	// go through the existing elements to see if we already have an entry for the supplied parameter
	int32 Idx = IndexOfParameter(Parameter);
	if (Idx == INDEX_NONE)
	{
		Modify();

		Idx = Parameters.Add(Parameter);
		DefaultInstance->AddParameter(Parameter);
	}

	return Idx;
}

int32 UNiagaraParameterCollection::AddParameter(FName Name, FNiagaraTypeDefinition Type)
{
	return AddParameter(FNiagaraVariable(Type, Name));
}

//void UNiagaraParameterCollection::RemoveParameter(int32 ParamIdx)
void UNiagaraParameterCollection::RemoveParameter(const FNiagaraVariable& Parameter)
{
	Modify();
	CompileId = FGuid::NewGuid();  // Any scripts depending on this parameter name will likely need to be changed.
	DefaultInstance->RemoveParameter(Parameter);
	Parameters.Remove(Parameter);
}

void UNiagaraParameterCollection::RenameParameter(FNiagaraVariable& Parameter, FName NewName)
{
	Modify();
	CompileId = FGuid::NewGuid(); // Any scripts depending on this parameter name will likely need to be changed.
	int32 ParamIdx = Parameters.IndexOfByKey(Parameter);
	check(ParamIdx != INDEX_NONE);

	Parameters[ParamIdx].SetName(NewName);
	DefaultInstance->RenameParameter(Parameter, NewName);
}

FString UNiagaraParameterCollection::GetFullNamespace()const
{
	return TEXT("NPC.") + Namespace.ToString() + TEXT(".");
}

FNiagaraCompileHash UNiagaraParameterCollection::GetCompileHash() const
{
	// TODO - Implement an actual hash for parameter collections instead of just hashing a change id.
	FSHA1 CompileHash;
	CompileHash.Update((const uint8*)&CompileId, sizeof(FGuid));
	CompileHash.Final();

	TArray<uint8> DataHash;
	DataHash.AddUninitialized(FSHA1::DigestSize);
	CompileHash.GetHash(DataHash.GetData());

	return FNiagaraCompileHash(DataHash);
}

void UNiagaraParameterCollection::RefreshCompileId()
{
	CompileId = FGuid::NewGuid();
}

FNiagaraVariable UNiagaraParameterCollection::CollectionParameterFromFriendlyParameter(const FNiagaraVariable& FriendlyParameter)const
{
	return FNiagaraVariable(FriendlyParameter.GetType(), *ParameterNameFromFriendlyName(FriendlyParameter.GetName().ToString()));
}

FNiagaraVariable UNiagaraParameterCollection::FriendlyParameterFromCollectionParameter(const FNiagaraVariable& CollectionParameter)const
{
	return FNiagaraVariable(CollectionParameter.GetType(), *FriendlyNameFromParameterName(CollectionParameter.GetName().ToString()));
}

FString UNiagaraParameterCollection::FriendlyNameFromParameterName(FString ParameterName)const
{
	ParameterName.RemoveFromStart(GetFullNamespace());
	return ParameterName;
}

FString UNiagaraParameterCollection::ParameterNameFromFriendlyName(const FString& FriendlyName)const
{
	return FString::Printf(TEXT("%s%s"), *GetFullNamespace(), *FriendlyName);
}

#if WITH_EDITORONLY_DATA
void UNiagaraParameterCollection::MakeNamespaceNameUnique()
{
 	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
 	TArray<FAssetData> CollectionAssets;
 	AssetRegistryModule.Get().GetAssetsByClass(UNiagaraParameterCollection::StaticClass()->GetFName(), CollectionAssets);
	TArray<FName> ExistingNames;
 	for (FAssetData& CollectionAsset : CollectionAssets)
 	{
		if (CollectionAsset.GetFullName() != GetFullName())
		{
			ExistingNames.Add(CollectionAsset.GetTagValueRef<FName>(GET_MEMBER_NAME_CHECKED(UNiagaraParameterCollection, Namespace)));
		}
	}

	if (ExistingNames.Contains(Namespace))
	{
		FString CandidateNameString = Namespace.ToString();
		FString BaseNameString = CandidateNameString;
		if (CandidateNameString.Len() >= 3 && CandidateNameString.Right(3).IsNumeric())
		{
			BaseNameString = CandidateNameString.Left(CandidateNameString.Len() - 3);
		}

		FName UniqueName = FName(*BaseNameString);
		int32 NameIndex = 1;
		while (ExistingNames.Contains(UniqueName))
		{
			UniqueName = FName(*FString::Printf(TEXT("%s%03i"), *BaseNameString, NameIndex));
			NameIndex++;
		}

		UE_LOG(LogNiagara, Warning, TEXT("Parameter collection namespace conflict found. \"%s\" is already in use!"), *Namespace.ToString());
		Namespace = UniqueName;
	}
}

void UNiagaraParameterCollection::AddDefaultSourceParameters()
{
	if (SourceMaterialCollection)
	{
		TArray<FName> ScalarParameterNames;
		TArray<FName> VectorParameterNames;

		SourceMaterialCollection->GetParameterNames(ScalarParameterNames, false /* bVectorParameters */);
		SourceMaterialCollection->GetParameterNames(VectorParameterNames, true /* bVectorParameters */);

		const FNiagaraTypeDefinition& ScalarDef = FNiagaraTypeDefinition::GetFloatDef();
		const FNiagaraTypeDefinition& ColorDef = FNiagaraTypeDefinition::GetColorDef();

		for (const FName& ScalarParameterName : ScalarParameterNames)
		{
			FNiagaraVariable ScalarParameter(ScalarDef, *ParameterNameFromFriendlyName(ScalarParameterName.ToString()));
			ScalarParameter.SetValue(SourceMaterialCollection->GetScalarParameterByName(ScalarParameterName)->DefaultValue);

			AddParameter(ScalarParameter);
		}

		for (const FName& VectorParameterName : VectorParameterNames)
		{
			FNiagaraVariable VectorParameter(ColorDef, *ParameterNameFromFriendlyName(VectorParameterName.ToString()));
			VectorParameter.SetValue(SourceMaterialCollection->GetVectorParameterByName(VectorParameterName)->DefaultValue);
			AddParameter(VectorParameter);
		}
	}
}

#endif

void UNiagaraParameterCollection::PostLoad()
{
	Super::PostLoad();

	DefaultInstance->ConditionalPostLoad();

	if (CompileId.IsValid() == false)
	{
		CompileId = FGuid::NewGuid();
	}

	if (SourceMaterialCollection)
	{
		SourceMaterialCollection->ConditionalPostLoad();

#if WITH_EDITOR
		// catch up with any changes that may have been made to the MPC
		AddDefaultSourceParameters();
#endif
	}
}
