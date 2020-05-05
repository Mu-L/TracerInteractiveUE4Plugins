// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintCompilerCppBackendGatherDependencies.h"
#include "Misc/CoreMisc.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Class.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/UserDefinedEnum.h"
#include "Engine/UserDefinedStruct.h"
#include "EdGraphSchema_K2.h"
#include "IBlueprintCompilerCppBackendModule.h"
#include "K2Node.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_EnumLiteral.h"
#include "KismetCompiler.h" // For LogK2Compiler

class FGatherConvertedClassDependenciesArchive : public FReferenceCollectorArchive
{
public:
	FGatherConvertedClassDependenciesArchive(UObject* InSerializingObject, FReferenceCollector& InCollector)
		: FReferenceCollectorArchive(InSerializingObject, InCollector)
	{
		ArIsObjectReferenceCollector = true;
		SetIsPersistent(InCollector.IsIgnoringTransient());
		ArIgnoreArchetypeRef = InCollector.IsIgnoringArchetypeRef();
	}

protected:
	virtual FArchive& operator<<(UObject*& Object) override
	{
		if (Object)
		{
			FReferenceCollector& CurrentCollector = GetCollector();
			FProperty* OldCollectorSerializedProperty = CurrentCollector.GetSerializedProperty();
			CurrentCollector.SetSerializedProperty(GetSerializedProperty());
			CurrentCollector.AddReferencedObject(Object, GetSerializingObject(), GetSerializedProperty());
			CurrentCollector.SetSerializedProperty(OldCollectorSerializedProperty);
		}
		return *this;
	}

	virtual FArchive& operator<<(FField*& Field) override
	{
		if (Field)
		{
			FReferenceCollector& CurrentCollector = GetCollector();
			Field->AddReferencedObjects(CurrentCollector);
		}
		return *this;
	}
};

struct FGatherConvertedClassDependenciesHelperBase : public FReferenceCollector
{
	TSet<UObject*> SerializedObjects;
	FGatherConvertedClassDependencies& Dependencies;

	FGatherConvertedClassDependenciesHelperBase(FGatherConvertedClassDependencies& InDependencies)
		: Dependencies(InDependencies)
		, Archive(nullptr, *this)
	{ }

	virtual bool IsIgnoringArchetypeRef() const override { return false; }
	virtual bool IsIgnoringTransient() const override { return true; }

	void FindReferences(UObject* Object)
	{
		{
			FVerySlowReferenceCollectorArchiveScope CollectorScope(Archive, Object, nullptr);
			const bool bOldFilterEditorOnly = CollectorScope.GetArchive().IsFilterEditorOnly();
			CollectorScope.GetArchive().SetFilterEditorOnly(true);
			if (UClass* AsBPGC = Cast<UBlueprintGeneratedClass>(Object))
			{
				Object = Dependencies.FindOriginalClass(AsBPGC);
			}
			Object->Serialize(CollectorScope.GetArchive());
			CollectorScope.GetArchive().SetFilterEditorOnly(bOldFilterEditorOnly);

			// Class/struct fields (FFields) are no longer serialized, but we still need to find and collect property object references as part of dependency gathering.
			// Note: Cannot rely on UStruct::PropertyObjectReferences (cached at link time) because that's GC-specific and excludes native references that we need here.
			if (UStruct* ObjAsStruct = Cast<UStruct>(Object))
			{
				for (FField* ChildProperty = ObjAsStruct->ChildProperties; ChildProperty; ChildProperty = ChildProperty->Next)
				{
					ChildProperty->AddReferencedObjects(*this);
				}
			}
		}
	}

	void FindReferencesForNewObject(UObject* Object)
	{
		bool AlreadyAdded = false;
		SerializedObjects.Add(Object, &AlreadyAdded);
		if (!AlreadyAdded)
		{
			FindReferences(Object);
		}
	}

	void IncludeTheHeaderInBody(UField* InField)
	{
		if (InField && !Dependencies.IncludeInHeader.Contains(InField))
		{
			Dependencies.IncludeInBody.Add(InField);
		}
	}

	void AddAssetDependency(UObject* InAsset)
	{
		if (InAsset)
		{
			Dependencies.Assets.Add(InAsset);
		}
	}

	void AddConvertedClassDependency(UBlueprintGeneratedClass* InBPGC)
	{
		if (InBPGC && !Dependencies.ConvertedClasses.Contains(InBPGC))
		{
			Dependencies.ConvertedClasses.Add(InBPGC);
		}
	}

	void AddConvertedStructDependency(UUserDefinedStruct* InUDS)
	{
		if (InUDS && !Dependencies.ConvertedStructs.Contains(InUDS))
		{
			Dependencies.ConvertedStructs.Add(InUDS);
		}
	}

	void AddConvertedEnumDependency(UUserDefinedEnum* InUDE)
	{
		if (InUDE && !Dependencies.ConvertedEnum.Contains(InUDE))
		{
			Dependencies.ConvertedEnum.Add(InUDE);
		}
	}

private:
	FGatherConvertedClassDependenciesArchive Archive;
};

struct FFindAssetsToInclude : public FGatherConvertedClassDependenciesHelperBase
{
	FFindAssetsToInclude(FGatherConvertedClassDependencies& InDependencies)
		: FGatherConvertedClassDependenciesHelperBase(InDependencies)
	{
		FindReferences(Dependencies.GetActualStruct());
	}

	void MaybeIncludeObjectAsDependency(UObject* Object, UStruct* CurrentlyConvertedStruct)
	{
		if (Object->HasAnyFlags(RF_ClassDefaultObject))
		{
			// Static functions from libraries are called on CDO. (The functions is stored as a name not an object).
			UClass* OwnerClass = Object->GetClass();
			if (OwnerClass && (OwnerClass != CurrentlyConvertedStruct))
			{
				// First, see if we need to add the class as a dependency. The CDO itself will then be added below.
				MaybeIncludeObjectAsDependency(OwnerClass, CurrentlyConvertedStruct);
			}
		}

		const bool bUseZConstructorInGeneratedCode = false;
		UField* AsField = Cast<UField>(Object);
		UBlueprintGeneratedClass* ObjAsBPGC = Cast<UBlueprintGeneratedClass>(Object);
		const bool bWillBeConvertedAsBPGC = ObjAsBPGC && Dependencies.WillClassBeConverted(ObjAsBPGC);
		if (bWillBeConvertedAsBPGC)
		{
			if (ObjAsBPGC != CurrentlyConvertedStruct)
			{
				AddConvertedClassDependency(ObjAsBPGC);
				if (!bUseZConstructorInGeneratedCode)
				{
					IncludeTheHeaderInBody(ObjAsBPGC);
				}
			}
			return;
		}
		else if (UUserDefinedStruct* UDS = Cast<UUserDefinedStruct>(Object))
		{
			if (!UDS->HasAnyFlags(RF_ClassDefaultObject))
			{
				AddConvertedStructDependency(UDS);
				if (!bUseZConstructorInGeneratedCode)
				{
					IncludeTheHeaderInBody(UDS);
				}
			}
		}
		else if (UUserDefinedEnum* UDE = Cast<UUserDefinedEnum>(Object))
		{
			if (!UDE->HasAnyFlags(RF_ClassDefaultObject))
			{
				AddConvertedEnumDependency(UDE);
			}
		}
		else if ((Object->IsAsset() || AsField) && !Object->IsIn(CurrentlyConvertedStruct))
		{
			if (AsField)
			{
				if (UClass* OwnerClass = AsField->GetOwnerClass())
				{
					if (OwnerClass != AsField)
					{
						// This is a field owned by a class, so attempt to add the class as a dependency.
						MaybeIncludeObjectAsDependency(OwnerClass, CurrentlyConvertedStruct);
					}
					else
					{
						// Add the class itself as a dependency.
						AddAssetDependency(OwnerClass);

						if (ObjAsBPGC)
						{
							// For BPGC types, we also include the CDO as a dependency (since it will be serialized).
							// Note that if we get here, we already know from above that the BPGC is not being converted.
							AddAssetDependency(ObjAsBPGC->GetDefaultObject());
						}
					}
				}
				else if (UStruct* OwnerStruct = AsField->GetOwnerStruct())
				{
					if (OwnerStruct != AsField)
					{
						// This is a field that's owned by a struct, so attempt to add the struct as a dependency.
						MaybeIncludeObjectAsDependency(OwnerStruct, CurrentlyConvertedStruct);
					}
					else
					{
						// Add the struct itself as a dependency.
						AddAssetDependency(OwnerStruct);
					}
				}
				else
				{
					// UFUNCTION, UENUM, etc.
					AddAssetDependency(Object);
				}
			}
			else
			{
				// Include the asset as a dependency.
				AddAssetDependency(Object);
			}

			// No need to traverse these objects any further, so we just return.
			return;
		}

		// Recursively add references from this object.
		FindReferencesForNewObject(Object);
	}

	virtual void HandleObjectReference(UObject*& InObject, const UObject* InReferencingObject, const FProperty* InReferencingProperty) override
	{
		UObject* Object = InObject;
		if (!Object)
		{
			return;
		}

		if (Object->IsA<UBlueprint>())
		{
			Object = CastChecked<UBlueprint>(Object)->GeneratedClass;
		}

		UClass* ActualClass = Cast<UClass>(Dependencies.GetActualStruct());
		UStruct* CurrentlyConvertedStruct = ActualClass ? Dependencies.FindOriginalClass(ActualClass) : Dependencies.GetActualStruct();
		ensure(CurrentlyConvertedStruct);
		if (Object == CurrentlyConvertedStruct)
		{
			return;
		}

		// Attempt to add the referenced object as a dependency.
		MaybeIncludeObjectAsDependency(Object, CurrentlyConvertedStruct);
	}
};

struct FFindHeadersToInclude : public FGatherConvertedClassDependenciesHelperBase
{
	FFindHeadersToInclude(FGatherConvertedClassDependencies& InDependencies)
		: FGatherConvertedClassDependenciesHelperBase(InDependencies)
	{
		FindReferences(Dependencies.GetActualStruct());

		// special case - literal enum
		UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(Dependencies.GetActualStruct());
		UBlueprint* BP = BPGC ? Cast<UBlueprint>(BPGC->ClassGeneratedBy) : nullptr;
		if (BP)
		{
			TArray<UEdGraph*> Graphs;
			BP->GetAllGraphs(Graphs);
			for (UEdGraph* Graph : Graphs)
			{
				if (Graph)
				{
					TArray<UK2Node*> AllNodes;
					Graph->GetNodesOfClass<UK2Node>(AllNodes);
					for (UK2Node* K2Node : AllNodes)
					{
						if (UK2Node_EnumLiteral* LiteralEnumNode = Cast<UK2Node_EnumLiteral>(K2Node))
						{
							UEnum* Enum = LiteralEnumNode->Enum;
							IncludeTheHeaderInBody(Enum);
						}
						// HACK FOR LITERAL ENUMS:
						else if(K2Node)
						{
							for (UEdGraphPin* Pin : K2Node->Pins)
							{
								if (Pin && (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte))
								{
									if (UEnum* Enum = Cast<UEnum>(Pin->PinType.PinSubCategoryObject.Get()))
									{
										IncludeTheHeaderInBody(Enum);
									}
								}
							}
						}
					}
				}
			}
		}

		// Include classes of native subobjects
		if (BPGC)
		{
			UClass* NativeSuperClass = BPGC->GetSuperClass();
			for (; NativeSuperClass && !NativeSuperClass->HasAnyClassFlags(CLASS_Native); NativeSuperClass = NativeSuperClass->GetSuperClass())
			{}
			UObject* NativeCDO = NativeSuperClass ? NativeSuperClass->GetDefaultObject(false) : nullptr;
			if (NativeCDO)
			{
				TArray<UObject*> DefaultSubobjects;
				NativeCDO->GetDefaultSubobjects(DefaultSubobjects);
				for (UObject* DefaultSubobject : DefaultSubobjects)
				{
					IncludeTheHeaderInBody(DefaultSubobject ? DefaultSubobject->GetClass() : nullptr);
				}
			}
		}
	}

	virtual void HandleObjectReference(UObject*& InObject, const UObject* InReferencingObject, const FProperty* InReferencingProperty) override
	{
		UObject* Object = InObject;
		if (!Object || Object->IsA<UBlueprint>())
		{
			return;
		}

		UClass* ActualClass = Cast<UClass>(Dependencies.GetActualStruct());
		UStruct* CurrentlyConvertedStruct = ActualClass ? Dependencies.FindOriginalClass(ActualClass) : Dependencies.GetActualStruct();
		ensure(CurrentlyConvertedStruct);
		if (Object == CurrentlyConvertedStruct)
		{
			return;
		}

		{
			UField* ObjAsField = Cast<UField>(Object);
			if (!ObjAsField)
			{
				const bool bTransientObject = (Object->HasAnyFlags(RF_Transient) && !Object->IsIn(CurrentlyConvertedStruct)) || Object->IsIn(GetTransientPackage());
				if (bTransientObject)
				{
					return;
				}

				ObjAsField = Object->GetClass();
			}

			if (ObjAsField && !ObjAsField->HasAnyFlags(RF_ClassDefaultObject))
			{
				if (ObjAsField->IsA<UFunction>())
				{
					ObjAsField = ObjAsField->GetOwnerClass();
				}

				UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(ObjAsField);
				if (!BPGC || Dependencies.WillClassBeConverted(BPGC))
				{
					IncludeTheHeaderInBody(ObjAsField);
				}
				else
				{
					IncludeTheHeaderInBody(Dependencies.GetFirstNativeOrConvertedClass(BPGC));
					// Wrappers for unconverted BP will be included only when thay are directly used. See usage of FEmitterLocalContext::MarkUnconvertedClassAsNecessary.
				}
			}
		}

		if ((Object->IsAsset() || Object->IsA<UBlueprintGeneratedClass>()) && !Object->IsIn(CurrentlyConvertedStruct))
		{
			return;
		}

		auto OwnedByAnythingInHierarchy = [this, Object, CurrentlyConvertedStruct]()->bool
		{
			for (UStruct* IterStruct = CurrentlyConvertedStruct; IterStruct; IterStruct = IterStruct->GetSuperStruct())
			{
				if (Object->IsIn(IterStruct))
				{
					return true;
				}
				UClass* IterClass = Cast<UClass>(IterStruct);
				UObject* CDO = IterClass ? IterClass->GetDefaultObject(false) : nullptr;
				if (CDO && Object->IsIn(CDO))
				{
					return true;
				}
			}
			return false;
		};
		if (!Object->IsA<UField>() && !Object->HasAnyFlags(RF_ClassDefaultObject) && !OwnedByAnythingInHierarchy())
		{
			Object = Object->GetClass();
		}
		else
		{
			UObject* OuterObj = Object->GetOuter();
			if (OuterObj && !OuterObj->IsA<UPackage>())
			{
				FindReferencesForNewObject(OuterObj);
			}
		}
		FindReferencesForNewObject(Object);
	}
};

bool FGatherConvertedClassDependencies::IsFieldFromExcludedPackage(const UField* Field, const TSet<FName>& InExcludedModules)
{
	if (Field && (0 != InExcludedModules.Num()))
	{
		const UPackage* Package = Field->GetOutermost();
		if (Package->HasAnyPackageFlags(PKG_CompiledIn))
		{
			const FName ShortPkgName = *FPackageName::GetShortName(Package);
			if (InExcludedModules.Contains(ShortPkgName))
			{
				return true;
			}
		}
	}
	return false;
}

FGatherConvertedClassDependencies::FGatherConvertedClassDependencies(UStruct* InStruct, const FCompilerNativizationOptions& InNativizationOptions)
	: OriginalStruct(InStruct)
	, NativizationOptions(InNativizationOptions)
{
	check(OriginalStruct);

	// Gather headers and type declarations for header.
	DependenciesForHeader();
	// Gather headers (only from the class hierarchy) to include in body.
	FFindHeadersToInclude FindHeadersToInclude(*this);
	// Gather assets that must be referenced.
	FFindAssetsToInclude FindAssetsToInclude(*this);

	static const FBoolConfigValueHelper DontNativizeDataOnlyBP(TEXT("BlueprintNativizationSettings"), TEXT("bDontNativizeDataOnlyBP"));
	if (DontNativizeDataOnlyBP)
	{
		auto RemoveFieldsFromDataOnlyBP = [this](TSet<UField*>& FieldSet)
		{
			TSet<UField*> FieldsToAdd;
			for (auto Iter = FieldSet.CreateIterator(); Iter; ++Iter)
			{
				const UClass* CurrentClass = (*Iter) ? (*Iter)->GetOwnerClass() : nullptr;
				const UBlueprint* CurrentBP = CurrentClass ? Cast<UBlueprint>(CurrentClass->ClassGeneratedBy) : nullptr;
				if (CurrentBP && FBlueprintEditorUtils::IsDataOnlyBlueprint(CurrentBP) && !WillClassBeConverted(Cast<const UBlueprintGeneratedClass>(CurrentClass)))
				{
					Iter.RemoveCurrent();
					FieldsToAdd.Add(GetFirstNativeOrConvertedClass(CurrentClass->GetSuperClass()));
				}
			}

			FieldSet.Append(FieldsToAdd);
		};
		RemoveFieldsFromDataOnlyBP(IncludeInHeader);
		RemoveFieldsFromDataOnlyBP(DeclareInHeader);
		RemoveFieldsFromDataOnlyBP(IncludeInBody);
	}

	{
		TSet<FName> ExcludedModules(NativizationOptions.ExcludedModules);
		auto RemoveFieldsDependentOnExcludedModules = [&ExcludedModules, InStruct](TSet<UField*>& FieldSet)
		{
			for (auto Iter = FieldSet.CreateIterator(); Iter; ++Iter)
			{
				if (IsFieldFromExcludedPackage(*Iter, ExcludedModules))
				{
					UE_LOG(LogK2Compiler, Verbose, TEXT("Struct %s depends on an excluded package."), *GetPathNameSafe(InStruct));
					Iter.RemoveCurrent();
				}
			}
		};
		RemoveFieldsDependentOnExcludedModules(IncludeInHeader);
		RemoveFieldsDependentOnExcludedModules(DeclareInHeader);
		RemoveFieldsDependentOnExcludedModules(IncludeInBody);
	}

	auto GatherRequiredModules = [this](const TSet<UField*>& Fields)
	{
		for (UField* Field : Fields)
		{
			const UPackage* Package = Field ? Field->GetOutermost() : nullptr;
			if (Package && Package->HasAnyPackageFlags(PKG_CompiledIn))
			{
				RequiredModuleNames.Add(Package);
			}
		}
	};

	GatherRequiredModules(IncludeInHeader);
	GatherRequiredModules(IncludeInBody);
}

UClass* FGatherConvertedClassDependencies::GetFirstNativeOrConvertedClass(UClass* InClass) const
{
	check(InClass);
	for (UClass* ItClass = InClass; ItClass; ItClass = ItClass->GetSuperClass())
	{
		UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(ItClass);
		if (ItClass->HasAnyClassFlags(CLASS_Native) || WillClassBeConverted(BPGC))
		{
			return ItClass;
		}
	}
	check(false);
	return nullptr;
};

UClass* FGatherConvertedClassDependencies::FindOriginalClass(const UClass* InClass) const
{
	if (InClass)
	{
		IBlueprintCompilerCppBackendModule& BackEndModule = (IBlueprintCompilerCppBackendModule&)IBlueprintCompilerCppBackendModule::Get();
		TWeakObjectPtr<UClass>* ClassWeakPtrPtr = BackEndModule.GetOriginalClassMap().Find(MakeWeakObjectPtr(const_cast<UClass*>(InClass)));
		UClass* OriginalClass = ClassWeakPtrPtr ? ClassWeakPtrPtr->Get() : nullptr;
		return OriginalClass ? OriginalClass : const_cast<UClass*>(InClass);
	}
	return nullptr;
}

bool FGatherConvertedClassDependencies::WillClassBeConverted(const UBlueprintGeneratedClass* InClass) const
{
	if (InClass && !InClass->HasAnyFlags(RF_ClassDefaultObject))
	{
		const UClass* ClassToCheck = FindOriginalClass(InClass);

		IBlueprintCompilerCppBackendModule& BackEndModule = (IBlueprintCompilerCppBackendModule&)IBlueprintCompilerCppBackendModule::Get();
		const auto& WillBeConvertedQuery = BackEndModule.OnIsTargetedForConversionQuery();

		if (WillBeConvertedQuery.IsBound())
		{
			return WillBeConvertedQuery.Execute(ClassToCheck, NativizationOptions);
		}
		return true;
	}
	return false;
}


class FFieldCollectorArchive : public FArchive
{
	TSet<FField*> VisitedFields;
	TArray<FFieldVariant>& Fields;

public:

	FFieldCollectorArchive(TArray<FFieldVariant>& OutFields)
		: Fields(OutFields)
	{
		this->SetIsSaving(true);
		this->SetIsPersistent(false);
	}

	virtual FArchive& operator << (FField*& InField) override
	{
		if (InField && !VisitedFields.Contains(InField))
		{
			VisitedFields.Add(InField);
			Fields.Add(InField);
			InField->Serialize(*this);
		}
		return *this;
	}
};

class FSimpleArrayReferenceCollector : public FReferenceCollector
{
	TArray<UObject*>& Objects;
public:
	FSimpleArrayReferenceCollector(TArray<UObject*>& OutObjects)
		: Objects(OutObjects)
	{
	}
	virtual bool IsIgnoringArchetypeRef() const override { return false; }
	virtual bool IsIgnoringTransient() const override { return false; }
	virtual void HandleObjectReference(UObject*& InObject, const UObject* InReferencingObject, const FProperty* InReferencingProperty)
	{
		if (InObject)
		{
			Objects.Add(InObject);
		}
	}
};

void GetStructProperties(UStruct* InStruct, TArray<FFieldVariant>& OutFields)
{
	FFieldCollectorArchive Ar(OutFields);

	for (FField* Prop = InStruct->ChildProperties; Prop; Prop = Prop->Next)
	{
		Ar << Prop;
	}
}
void FGatherConvertedClassDependencies::DependenciesForHeader()
{
	TArray<FFieldVariant> ObjectsToCheck;
	{
		TArray<UObject*> StructSubobjects;
		GetObjectsWithOuter(OriginalStruct, StructSubobjects, true);
		GetStructProperties(OriginalStruct, ObjectsToCheck);
		for (UObject* SubObj : StructSubobjects)
		{
			ObjectsToCheck.Add(SubObj);
			if (UStruct* SubStruct = Cast<UStruct>(SubObj))
			{
				GetStructProperties(SubStruct, ObjectsToCheck);
			}
		}
	}

	TArray<UObject*> NeededObjects;
	FSimpleArrayReferenceCollector HeaderReferenceFinder(NeededObjects);

	auto ShouldIncludeHeaderFor = [this](UObject* InObj)->bool
	{
		if (InObj
			&& (InObj->IsA<UClass>() || InObj->IsA<UEnum>() || InObj->IsA<UScriptStruct>())
			&& !InObj->HasAnyFlags(RF_ClassDefaultObject))
		{
			UBlueprintGeneratedClass* ObjAsBPGC = Cast<UBlueprintGeneratedClass>(InObj);
			const bool bWillBeConvetedAsBPGC = ObjAsBPGC && WillClassBeConverted(ObjAsBPGC);
			const bool bRemainAsUnconvertedBPGC = ObjAsBPGC && !bWillBeConvetedAsBPGC;
			if (!bRemainAsUnconvertedBPGC && (InObj->GetOutermost() != OriginalStruct->GetOutermost()))
			{
				return true;
			}
		}
		return false;
	};

	for (FFieldVariant& Obj : ObjectsToCheck)
	{
		const FProperty* Property = Obj.Get<FProperty>();
		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			Property = ArrayProperty->Inner;
		}
		const FProperty* OwnerProperty = Property ? Property->GetOwnerProperty() : nullptr;
		const bool bIsParam = OwnerProperty && (0 != (OwnerProperty->PropertyFlags & CPF_Parm)) && OwnerProperty->IsIn(OriginalStruct);
		const bool bIsMemberVariable = OwnerProperty && (OwnerProperty->GetOwner<UObject>() == OriginalStruct);
		if (bIsParam || bIsMemberVariable)
		{
			if (const FSoftClassProperty* SoftClassProperty = CastField<const FSoftClassProperty>(Property))
			{
				DeclareInHeader.Add(GetFirstNativeOrConvertedClass(SoftClassProperty->MetaClass));
			}
			if (const FClassProperty* ClassProperty = CastField<const FClassProperty>(Property))
			{
				DeclareInHeader.Add(GetFirstNativeOrConvertedClass(ClassProperty->MetaClass));
			}
			if (const FObjectPropertyBase* ObjectProperty = CastField<const FObjectPropertyBase>(Property))
			{
				DeclareInHeader.Add(GetFirstNativeOrConvertedClass(ObjectProperty->PropertyClass));
			}
			else if (const FInterfaceProperty* InterfaceProperty = CastField<const FInterfaceProperty>(Property))
			{
				IncludeInHeader.Add(InterfaceProperty->InterfaceClass);
			}
			else if (const FDelegateProperty* DelegateProperty = CastField<const FDelegateProperty>(Property))
			{
				IncludeInHeader.Add(DelegateProperty->SignatureFunction ? DelegateProperty->SignatureFunction->GetOwnerStruct() : nullptr);
			}
			/* MC Delegate signatures are recreated in local scope anyway.
			else if (const FMulticastDelegateProperty* MulticastDelegateProperty = CastField<const FMulticastDelegateProperty>(Property))
			{
				IncludeInHeader.Add(MulticastDelegateProperty->SignatureFunction ? MulticastDelegateProperty->SignatureFunction->GetOwnerClass() : nullptr);
			}
			*/
			else if (const FByteProperty* ByteProperty = CastField<const FByteProperty>(Property))
			{ 
				// HeaderReferenceFinder.FindReferences(Obj); cannot find this enum..
				IncludeInHeader.Add(ByteProperty->Enum);
			}
			else if (const FEnumProperty* EnumProperty = CastField<const FEnumProperty>(Property))
			{ 
				// HeaderReferenceFinder.FindReferences(Obj); cannot find this enum..
				IncludeInHeader.Add(EnumProperty->GetEnum());
			}
			else if (const FStructProperty* StructProperty = CastField<const FStructProperty>(Property))
			{
				IncludeInHeader.Add(StructProperty->Struct);
			}
			else
			{
				FField* Field = Obj.Get<FField>();	
				check(Field);
				Field->AddReferencedObjects(HeaderReferenceFinder);
			}
		}
	}

	if (UStruct* SuperStruct = OriginalStruct->GetSuperStruct())
	{
		IncludeInHeader.Add(SuperStruct);
	}

	if (UClass* SourceClass = Cast<UClass>(OriginalStruct))
	{
		for (auto& ImplementedInterface : SourceClass->Interfaces)
		{
			IncludeInHeader.Add(ImplementedInterface.Class);
		}
	}

	for (UObject* Obj : NeededObjects)
	{
		if (ShouldIncludeHeaderFor(Obj))
		{
			IncludeInHeader.Add(CastChecked<UField>(Obj));
		}
	}

	// DEFAULT VALUES FROM UDS:
	UUserDefinedStruct* UDS = Cast<UUserDefinedStruct>(OriginalStruct);
	if (UDS)
	{
		FStructOnScope StructOnScope(UDS);
		UDS->InitializeDefaultValue(StructOnScope.GetStructMemory());
		for (TFieldIterator<FObjectPropertyBase> PropertyIt(UDS); PropertyIt; ++PropertyIt)
		{
			UObject* DefaultValueObject = ((FObjectPropertyBase*)*PropertyIt)->GetObjectPropertyValue_InContainer(StructOnScope.GetStructMemory());
			if (ShouldIncludeHeaderFor(DefaultValueObject))
			{
				UField* ObjAsField = Cast<UField>(DefaultValueObject);
				if (UField* FieldForHeader = ObjAsField ? ObjAsField : (DefaultValueObject ? DefaultValueObject->GetClass() : nullptr))
				{
					DeclareInHeader.Add(FieldForHeader);
				}
			}
		}
	}

	// REMOVE UNNECESSARY HEADERS
	UClass* AsBPGC = Cast<UBlueprintGeneratedClass>(OriginalStruct);
	UClass* OriginalClassFromOriginalPackage = AsBPGC ? FindOriginalClass(AsBPGC) : nullptr;
	const UPackage* OriginalStructPackage = OriginalStruct ? OriginalStruct->GetOutermost() : nullptr;
	for (auto Iter = IncludeInHeader.CreateIterator(); Iter; ++Iter)
	{
		UField* CurrentField = *Iter;
		if (CurrentField)
		{
			if (CurrentField->GetOutermost() == OriginalStructPackage)
			{
				Iter.RemoveCurrent();
			}
			else if (CurrentField == OriginalStruct)
			{
				Iter.RemoveCurrent();
			}
			else if (OriginalClassFromOriginalPackage && (CurrentField == OriginalClassFromOriginalPackage))
			{
				Iter.RemoveCurrent();
			}
		}
	}
}

TSet<const UObject*> FGatherConvertedClassDependencies::AllDependencies() const
{
	TSet<const UObject*> All;

	UBlueprintGeneratedClass* SuperClass = Cast<UBlueprintGeneratedClass>(OriginalStruct->GetSuperStruct());
	if (OriginalStruct->GetSuperStruct() && (!SuperClass || WillClassBeConverted(SuperClass)))
	{
		All.Add(SuperClass);
	}

	if (UClass* SourceClass = Cast<UClass>(OriginalStruct))
	{
		for (const FImplementedInterface& ImplementedInterface : SourceClass->Interfaces)
		{
			UBlueprintGeneratedClass* InterfaceClass = Cast<UBlueprintGeneratedClass>(ImplementedInterface.Class);
			if (ImplementedInterface.Class && (!InterfaceClass || WillClassBeConverted(InterfaceClass)))
			{
				All.Add(InterfaceClass);
			}
		}
	}
	
	TSet<UObject*> NestedAssets;
	GatherAssetsReferencedByConvertedTypes(NestedAssets);

	for (auto It : Assets)
	{
		All.Add(It);
	}
	for (auto It : NestedAssets)
	{
		All.Add(It);
	}
	for (auto It : ConvertedClasses)
	{
		All.Add(It);
	}
	for (auto It : ConvertedStructs)
	{
		All.Add(It);
	}
	for (auto It : ConvertedEnum)
	{
		All.Add(It);
	}
	return All;
}

TMap<UStruct*, TSharedPtr<FGatherConvertedClassDependencies>> FGatherConvertedClassDependencies::CachedConvertedClassDependencies;

TSharedPtr<FGatherConvertedClassDependencies> FGatherConvertedClassDependencies::Get(UStruct* InStruct, const FCompilerNativizationOptions& InNativizationOptions)
{
	TSharedPtr<FGatherConvertedClassDependencies> ConvertedClassDependenciesPtr;
	if (InStruct)
	{
		ConvertedClassDependenciesPtr = CachedConvertedClassDependencies.FindOrAdd(InStruct);
		if (!ConvertedClassDependenciesPtr.IsValid())
		{
			ConvertedClassDependenciesPtr = MakeShared<FGatherConvertedClassDependencies>(InStruct, InNativizationOptions);
			check(ConvertedClassDependenciesPtr.IsValid());
		}
	}

	return ConvertedClassDependenciesPtr;
}

class FArchiveReferencesInStructInstance : public FArchive
{
public:

	using FArchive::operator<<; // For visibility of the overloads we don't override

	TSet<UObject*> References;

	//~ Begin FArchive Interface
	virtual FArchive& operator<<(struct FLazyObjectPtr& Value) override { return *this; }
	virtual FArchive& operator<<(struct FSoftObjectPtr& Value) override { return *this; }
	virtual FArchive& operator<<(struct FSoftObjectPath& Value) override { return *this; }

	virtual FArchive& operator<<(UObject*& Object) override
	{
		if (Object)
		{
			References.Add(Object);
		}
		return *this;
	}
	//~ End FArchive Interface

	FArchiveReferencesInStructInstance()
	{
		ArIsObjectReferenceCollector = true;
		ArIsFilterEditorOnly = true;
	}
};

void FGatherConvertedClassDependencies::GatherAssetsReferencedByConvertedTypes(TSet<UObject*>& Dependencies) const
{
	TSet<UStruct*> VisitedTypes = { GetActualStruct() };

	TArray<UStruct*> ConvertedTypeStack;
	ConvertedTypeStack.Append(ConvertedStructs.Array());
	ConvertedTypeStack.Append(ConvertedClasses.Array());

	while(ConvertedTypeStack.Num() > 0)
	{
		UStruct* ConvertedType = ConvertedTypeStack.Pop();
		TSharedPtr<FGatherConvertedClassDependencies> ConvertedTypeDependenciesPtr = Get(ConvertedType, NativizationOptions);

		VisitedTypes.Add(ConvertedType);
		Dependencies.Append(ConvertedTypeDependenciesPtr->Assets);

		for (UUserDefinedStruct* ConvertedStruct : ConvertedTypeDependenciesPtr->ConvertedStructs)
		{
			if (!VisitedTypes.Contains(ConvertedStruct))
			{
				ConvertedTypeStack.Push(ConvertedStruct);
			}
		}
		
		for (UBlueprintGeneratedClass* ConvertedClass : ConvertedTypeDependenciesPtr->ConvertedClasses)
		{
			if (!VisitedTypes.Contains(ConvertedClass))
			{
				ConvertedTypeStack.Push(ConvertedClass);
			}
		}
	}
}

void FGatherConvertedClassDependencies::GatherAssetsReferencedByUDSDefaultValue(TSet<UObject*>& Dependencies, UUserDefinedStruct* Struct)
{
	if (Struct)
	{
		FStructOnScope StructOnScope(Struct);
		Struct->InitializeDefaultValue(StructOnScope.GetStructMemory());
		FArchiveReferencesInStructInstance ArchiveReferencesInStructInstance;
		Struct->SerializeItem(ArchiveReferencesInStructInstance, StructOnScope.GetStructMemory(), nullptr);
		Dependencies.Append(ArchiveReferencesInStructInstance.References);
	}
}