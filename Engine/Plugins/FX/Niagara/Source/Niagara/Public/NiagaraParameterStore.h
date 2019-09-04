// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraCommon.h"
#include "NiagaraDataInterface.h"
#include "NiagaraParameterStore.generated.h"

struct FNiagaraParameterStore;

//Binding from one parameter store to another.
//This does no tracking of lifetimes etc so the owner must ensure safe use and rebinding when needed etc.
struct FNiagaraParameterStoreBinding
{
	struct FParameterBinding
	{
		FParameterBinding(int32 InSrcOffset, int32 InDestOffset, int32 InSize)
			: SrcOffset((uint16)InSrcOffset), DestOffset((uint16)InDestOffset), Size((uint16)InSize)
		{
			//No way these offsets could ever be higher but check just in case.
			check(InSrcOffset < (int32)0xFFFF);
			check(InDestOffset < (int32)0xFFFF);
			check(InSize < (int32)0xFFFF);
		}
		FORCEINLINE bool operator==(const FParameterBinding& Other)const
		{
			return SrcOffset == Other.SrcOffset && DestOffset == Other.DestOffset && Size == Other.Size;
		}
		uint16 SrcOffset;
		uint16 DestOffset;
		uint16 Size;
	};
	/** Bindings of parameter data. Src offset, Dest offset and Size. */
	TArray<FParameterBinding> ParameterBindings;

	struct FInterfaceBinding
	{
		FInterfaceBinding(int32 InSrcOffset, int32 InDestOffset) 
			: SrcOffset((uint16)InSrcOffset), DestOffset((uint16)InDestOffset)
		{
			//No way these offsets could ever be higher but check just in case.
			check(InSrcOffset < (int32)0xFFFF);
			check(InDestOffset < (int32)0xFFFF);
		}
		FORCEINLINE bool operator==(const FInterfaceBinding& Other)const
		{
			return SrcOffset == Other.SrcOffset && DestOffset == Other.DestOffset;
		}
		uint16 SrcOffset;
		uint16 DestOffset;
	};
	/** Bindings of data interfaces. Src and Dest offsets.*/
	TArray<FInterfaceBinding> InterfaceBindings;

	FNiagaraParameterStoreBinding()
	{
	}
	
	FORCEINLINE_DEBUGGABLE void Empty(FNiagaraParameterStore* DestStore, FNiagaraParameterStore* SrcStore);
	FORCEINLINE_DEBUGGABLE void Initialize(FNiagaraParameterStore* DestStore, FNiagaraParameterStore* SrcStore);
	FORCEINLINE_DEBUGGABLE bool VerifyBinding(const FNiagaraParameterStore* DestStore, const FNiagaraParameterStore* SrcStore)const;
	FORCEINLINE_DEBUGGABLE void Tick(FNiagaraParameterStore* DestStore, FNiagaraParameterStore* SrcStore, bool bForce = false);
	FORCEINLINE_DEBUGGABLE void Dump(const FNiagaraParameterStore* DestStore, const FNiagaraParameterStore* SrcStore)const;
	/** TODO: Merge contiguous ranges into a single binding? */
	//FORCEINLINE_DEBUGGABLE void Optimize();

private:
	void BindParameters(FNiagaraParameterStore* DestStore, FNiagaraParameterStore* SrcStore);
};

/** Base storage class for Niagara parameter values. */
USTRUCT()
struct NIAGARA_API FNiagaraParameterStore
{
#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE(FOnChanged);
#endif

	GENERATED_USTRUCT_BODY()

private:
	/** Owner of this store. Used to provide an outer to data interfaces in this store. */
	UPROPERTY()
	UObject* Owner;
	
	/** Map from parameter defs to their offset in the data table or the data interface. TODO: Separate out into a layout and instance class to reduce duplicated data for this?  */
	UPROPERTY()
	TMap<FNiagaraVariable, int32> ParameterOffsets;
	
	/** Buffer containing parameter data. Indexed using offsets in ParameterOffsets */
	UPROPERTY()
	TArray<uint8> ParameterData;
	
	/** Data interfaces for this script. Possibly overridden with externally owned interfaces. Also indexed by ParameterOffsets. */
	UPROPERTY()
	TArray<UNiagaraDataInterface*> DataInterfaces;

	/** Bindings between this parameter store and others we push data into when we tick. */
	TMap<FNiagaraParameterStore*, FNiagaraParameterStoreBinding> Bindings;

	/** Parameter stores we've been bound to and are feeding data into us. */
	TArray<FNiagaraParameterStore*> SourceStores;

	/** Marks our parameters as dirty. They will be pushed to any bound stores on tick if true. */
	uint32 bParametersDirty : 1;
	/** Marks our interfaces as dirty. They will be pushed to any bound stores on tick if true. */
	uint32 bInterfacesDirty : 1;

	/** Uniquely identifies the current layout of this parameter store for detecting layout changes. */
	uint32 LayoutVersion;

#if WITH_EDITOR
	FOnChanged OnChangedDelegate;
#endif

public:
	FNiagaraParameterStore();
	FNiagaraParameterStore(UObject* InOwner);
	FNiagaraParameterStore(const FNiagaraParameterStore& Other);
	FNiagaraParameterStore& operator=(const FNiagaraParameterStore& Other);

	virtual ~FNiagaraParameterStore();
	
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FString DebugName;
#endif

	UObject* GetOwner()const { return Owner; }

	void Dump();
	void DumpParameters(bool bDumpBindings = false)const;

	FORCEINLINE uint32 GetParametersDirty() const { return bParametersDirty; }
	FORCEINLINE uint32 GetInterfacesDirty() const { return bInterfacesDirty; }

	FORCEINLINE void MarkParametersDirty() { bParametersDirty = true; }
	FORCEINLINE void MarkInterfacesDirty() { bInterfacesDirty = true; }

	FORCEINLINE uint32 GetLayoutVersion() const { return LayoutVersion; }

	/** Binds this parameter store to another. During Tick the values of this store will be pushed into all bound stores */
	void Bind(FNiagaraParameterStore* DestStore);
	/** Unbinds this store form one it's bound to. */
	void Unbind(FNiagaraParameterStore* DestStore);
	/** Recreates any bindings to reflect a layout change etc. */
	void Rebind();
	/** Recreates any bindings to reflect a layout change etc. */
	void TransferBindings(FNiagaraParameterStore& OtherStore);
	/** Handles any update such as pushing parameters to bound stores etc. */
	void Tick();
	/** Unbinds this store from all stores it's being driven by. */
	void UnbindFromSourceStores();
	
	bool VerifyBinding(const FNiagaraParameterStore* InDestStore) const;

	void CheckForNaNs() const;

	/**
	Adds the passed parameter to this store.
	Does nothing if this parameter is already present.
	Returns true if we added a new parameter.
	*/
	virtual bool AddParameter(const FNiagaraVariable& Param, bool bInitialize=true, bool bTriggerRebind = true);

	/** Removes the passed parameter if it exists in the store. */
	virtual bool RemoveParameter(const FNiagaraVariable& Param);

	/** Renames the passed parameter. */
	void RenameParameter(const FNiagaraVariable& Param, FName NewName);

	/** Removes all parameters from this store and releases any data. */
	virtual void Empty(bool bClearBindings = true);

	/** Removes all parameters from this store but does't change memory allocations. */
	virtual void Reset(bool bClearBindings = true);

	FORCEINLINE TArray<FNiagaraParameterStore*>& GetSourceParameterStores() { return SourceStores; }
	FORCEINLINE const TMap<FNiagaraVariable, int32>& GetParameterOffsets()const { return ParameterOffsets; }
	/** Get the list of FNiagaraVariables referenced by this store. Note that the values will be stale and are not to be trusted directly. Get the Values using the offset specified by IndexOf or GetParameterValue.*/
	FORCEINLINE void GetParameters(TArray<FNiagaraVariable>& OutParameters) const { return ParameterOffsets.GenerateKeyArray(OutParameters); }
	FORCEINLINE int32 GetNumParameters() const { return ParameterOffsets.Num(); }

	FORCEINLINE const TArray<UNiagaraDataInterface*>& GetDataInterfaces()const { return DataInterfaces; }
	FORCEINLINE const TArray<uint8>& GetParameterDataArray()const { return ParameterData; }

	FORCEINLINE void SetParameterDataArray(const TArray<uint8>& InParameterDataArray);

	// Called to initially set up the parameter store to *exactly* match the input store (other than any bindings and the internal name of it).
	virtual void InitFromSource(const FNiagaraParameterStore* SrcStore, bool bNotifyAsDirty);

	/** Gets the index of the passed parameter. If it is a data interface, this is an offset into the data interface table, otherwise a byte offset into he parameter data buffer. */
	virtual int32 IndexOf(const FNiagaraVariable& Parameter) const
	{
		const int32* Off = ParameterOffsets.Find(Parameter);
		if ( Off )
		{
			return *Off;
		}
		return INDEX_NONE;
	}

	/** Gets the typed parameter data. */
	template<typename T>
	FORCEINLINE_DEBUGGABLE void GetParameterValue(T& OutValue, const FNiagaraVariable& Parameter)const
	{
		check(Parameter.GetSizeInBytes() == sizeof(T));
		int32 Offset = IndexOf(Parameter);
		if (Offset != INDEX_NONE)
		{
			OutValue = *(const T*)(GetParameterData(Offset));
		}
	}

	template<typename T>
	FORCEINLINE_DEBUGGABLE T GetParameterValue(const FNiagaraVariable& Parameter)const
	{
		check(Parameter.GetSizeInBytes() == sizeof(T));
		int32 Offset = IndexOf(Parameter);
		if (Offset != INDEX_NONE)
		{
			return *(const T*)(GetParameterData(Offset));
		}
		return T();
	}

	FORCEINLINE const uint8* GetParameterData(int32 Offset)const
	{
		return ParameterData.GetData() + Offset;
	}

	/** Returns the parameter data for the passed parameter if it exists in this store. Null if not. */
	FORCEINLINE_DEBUGGABLE const uint8* GetParameterData(const FNiagaraVariable& Parameter)const
	{
		int32 Offset = IndexOf(Parameter);
		if (Offset != INDEX_NONE)
		{
			return GetParameterData(Offset);
		}
		return nullptr;
	}

	/** Returns the data interface at the passed offset. */
	FORCEINLINE UNiagaraDataInterface* GetDataInterface(int32 Offset)const
	{
		if (DataInterfaces.IsValidIndex(Offset))
		{
			return DataInterfaces[Offset];
		}

		return nullptr;
	}

	/** Returns the data interface for the passed parameter if it exists in this store. */
	FORCEINLINE_DEBUGGABLE UNiagaraDataInterface* GetDataInterface(const FNiagaraVariable& Parameter)const
	{
		int32 Offset = IndexOf(Parameter);
		UNiagaraDataInterface* Interface = GetDataInterface(Offset);
		checkSlow(!Interface || Parameter.GetType().GetClass() == Interface->GetClass());
		return Interface;
	}

	/** Returns the associated FNiagaraVariable for the passed data interface if it exists in the store. Null if not.*/
	const FNiagaraVariable* FindVariable(UNiagaraDataInterface* Interface)const;

	FORCEINLINE_DEBUGGABLE virtual const int32* FindParameterOffset(const FNiagaraVariable& Parameter) const
	{
		return ParameterOffsets.Find(Parameter);
	}

	/** Copies the passed parameter from this parameter store into another. */
	FORCEINLINE_DEBUGGABLE void CopyParameterData(FNiagaraParameterStore& DestStore, const FNiagaraVariable& Parameter) const
	{
		int32 DestIndex = DestStore.IndexOf(Parameter);
		int32 SrcIndex = IndexOf(Parameter);
		if (DestIndex != INDEX_NONE && SrcIndex != INDEX_NONE)
		{
			if (Parameter.IsDataInterface())
			{
				DataInterfaces[SrcIndex]->CopyTo(DestStore.DataInterfaces[DestIndex]);
				DestStore.OnInterfaceChange();
			}
			else
			{
				DestStore.SetParameterData(GetParameterData_Internal(SrcIndex), DestIndex, Parameter.GetSizeInBytes());
			}
		}
	}

	enum class EDataInterfaceCopyMethod
	{
		/** A new data interface will be created and it will be synchronized using the CopyTo method. */
		Value,
		/** A reference to the source data interface will be added to the destination. */
		Reference,
		/** Do not copy data interfaces.  This will cause an assert if there are data interfaces in the source
		  * store, and bOnlyAdd is false. */
		None
	};

	/** Copies all parameters from this parameter store into another.*/
	void CopyParametersTo(FNiagaraParameterStore& DestStore, bool bOnlyAdd, EDataInterfaceCopyMethod DataInterfaceCopyMethod);

	/** Remove all parameters from this parameter store from another.*/
	void RemoveParameters(FNiagaraParameterStore& DestStore);

	FString ToString() const;

	template<typename T>
	FORCEINLINE_DEBUGGABLE bool SetParameterValue(const T& InValue, const FNiagaraVariable& Param, bool bAdd=false)
	{
		check(Param.GetSizeInBytes() == sizeof(T));
		int32 Offset = IndexOf(Param);
		if (Offset != INDEX_NONE)
		{
			//*(T*)(GetParameterData_Internal(Offset)) = InValue;
			//Until we solve our alignment issues, temporarily just doing a memcpy here.
			T* ParamData = reinterpret_cast<T*>( GetParameterData_Internal(Offset) );
			FMemory::Memcpy(ParamData, &InValue, sizeof(T));
			OnParameterChange();
			return true;
		}
		else
		{
			if (bAdd)
			{
				bool bInitInterfaces = false;
				bool bTriggerRebind = false;
				AddParameter(Param, bInitInterfaces, bTriggerRebind);
				Offset = IndexOf(Param);
				check(Offset != INDEX_NONE);
				*(T*)(GetParameterData_Internal(Offset)) = InValue;
				OnLayoutChange();
				return true;
			}
		}
		return false;
	}

	FORCEINLINE_DEBUGGABLE void SetParameterData(const uint8* Data, int32 Offset, int32 Size)
	{
		checkSlow(Data != nullptr);
		checkSlow((Offset + Size) <= ParameterData.Num());
		uint8* Dest = GetParameterData_Internal(Offset);
		if (Dest != Data)
		{
			FMemory::Memcpy(Dest, Data, Size);
		}
		OnParameterChange();
	}

	FORCEINLINE_DEBUGGABLE bool SetParameterData(const uint8* Data, const FNiagaraVariable& Param, bool bAdd = false)
	{
		checkSlow(Data != nullptr);
		int32 Offset = IndexOf(Param);
		if (Offset != INDEX_NONE)
		{
			checkSlow(!Param.IsDataInterface());
			uint8* Dest = GetParameterData_Internal(Offset);
			if (Dest != Data)
			{
				FMemory::Memcpy(Dest, Data, Param.GetSizeInBytes());
			}
			OnParameterChange();
			return true;
		}
		else
		{
			if (bAdd)
			{
				bool bInitInterfaces = false;
				bool bTriggerRebind = false;
				AddParameter(Param, bInitInterfaces, bTriggerRebind);
				Offset = IndexOf(Param);
				check(Offset != INDEX_NONE);
				uint8* Dest = GetParameterData_Internal(Offset);
				if (Dest != Data)
				{
					FMemory::Memcpy(Dest, Data, Param.GetSizeInBytes());
				}
				OnLayoutChange();
				return true;
			}
		}
		return false;
	}

	/**
	* Sets the parameter using the internally stored data in the passed FNiagaraVariable.
	* TODO: Remove this. IMO FNiagaraVariable should be just for data definition and all storage should be done via this class.
	*/
	FORCEINLINE_DEBUGGABLE void SetParameter(const FNiagaraVariable& Param)
	{
		checkSlow(Param.IsDataAllocated());
		int32 Offset = IndexOf(Param);
		if (Offset != INDEX_NONE)
		{
			uint8* Dest = GetParameterData_Internal(Offset);
			const uint8* Src = Param.GetData();
			if (Dest != Src)
			{
				FMemory::Memcpy(Dest, Src, Param.GetSizeInBytes());
			}
			OnParameterChange();
		}
	}

	FORCEINLINE_DEBUGGABLE void SetDataInterface(UNiagaraDataInterface* InInterface, int32 Offset)
	{
		DataInterfaces[Offset] = InInterface;
		OnInterfaceChange();
	}

	FORCEINLINE_DEBUGGABLE void SetDataInterface(UNiagaraDataInterface* InInterface, const FNiagaraVariable& Parameter)
	{
		int32 Offset = IndexOf(Parameter);
		if (Offset != INDEX_NONE)
		{
			DataInterfaces[Offset] = InInterface;
			OnInterfaceChange();
		}
	}

	FORCEINLINE void OnParameterChange() 
	{ 
		bParametersDirty = true;
#if WITH_EDITOR
		OnChangedDelegate.Broadcast();
#endif
	}

	FORCEINLINE void OnInterfaceChange() 
	{ 
		bInterfacesDirty = true;
#if WITH_EDITOR
		OnChangedDelegate.Broadcast();
#endif
	}

#if WITH_EDITOR
	FDelegateHandle AddOnChangedHandler(FOnChanged::FDelegate InOnChanged);
	void RemoveOnChangedHandler(FDelegateHandle DelegateHandle);
	void RemoveAllOnChangedHandlers(const void* InUserObject);
#endif

	void TriggerOnLayoutChanged() { OnLayoutChange(); }

protected:
	void OnLayoutChange();

	/** Returns the parameter data at the passed offset. */
	FORCEINLINE const uint8* GetParameterData_Internal(int32 Offset) const
	{
		return ParameterData.GetData() + Offset;
	}

	FORCEINLINE uint8* GetParameterData_Internal(int32 Offset) 
	{
		return ParameterData.GetData() + Offset;
	}


	template<typename ParamType>
	void SetParameterByOffset(uint32 ParamOffset, const ParamType& Param)
	{
		ParamType* ParamPtr = (ParamType*)(GetParameterDataArray().GetData() + ParamOffset);
		*ParamPtr = Param;
		//SetParameterData((const uint8*)&Param, ParamOffset, sizeof(ParamType)); // TODO why aren't we using this path instead of SetParametersByOffset?
	}

	friend struct FNiagaraParameterStoreToDataSetBinding;    // this should be the only class calling SetParameterByOffset
};

FORCEINLINE_DEBUGGABLE void FNiagaraParameterStoreBinding::Empty(FNiagaraParameterStore* DestStore, FNiagaraParameterStore* SrcStore)
{
	if (DestStore)
	{
		DestStore->GetSourceParameterStores().RemoveSingleSwap(SrcStore, false);
	}
	DestStore = nullptr;
	ParameterBindings.Reset();
	InterfaceBindings.Reset();
}

FORCEINLINE_DEBUGGABLE void FNiagaraParameterStoreBinding::BindParameters(FNiagaraParameterStore* DestStore, FNiagaraParameterStore* SrcStore)
{
	InterfaceBindings.Reset();
	ParameterBindings.Reset();
	for (const TPair<FNiagaraVariable, int32>& ParamOffsetPair : DestStore->GetParameterOffsets())
	{
		const FNiagaraVariable& Parameter = ParamOffsetPair.Key;
		int32 DestOffset = ParamOffsetPair.Value;
		int32 SrcOffset = SrcStore->IndexOf(Parameter);

		if (SrcOffset != INDEX_NONE && DestOffset != INDEX_NONE)
		{
			if (Parameter.IsDataInterface())
			{
				InterfaceBindings.Add(FInterfaceBinding(SrcOffset, DestOffset));
			}
			else
			{
				ParameterBindings.Add(FParameterBinding(SrcOffset, DestOffset, Parameter.GetSizeInBytes()));
			}
		}
	}

	//Force an initial tick to prime our values in the destination store.
	Tick(DestStore, SrcStore, true);
}

FORCEINLINE_DEBUGGABLE void FNiagaraParameterStoreBinding::Initialize(FNiagaraParameterStore* DestStore, FNiagaraParameterStore* SrcStore)
{
	checkSlow(DestStore);
	checkSlow(SrcStore);
	DestStore->GetSourceParameterStores().AddUnique(SrcStore);

	BindParameters(DestStore, SrcStore);
}

FORCEINLINE_DEBUGGABLE bool FNiagaraParameterStoreBinding::VerifyBinding(const FNiagaraParameterStore* DestStore, const FNiagaraParameterStore* SrcStore)const
{
	bool bBindingValid = true;
#if WITH_EDITORONLY_DATA
	TArray<FName, TInlineAllocator<32>> MissingParameterNames;
	for (const TPair<FNiagaraVariable, int32>& ParamOffsetPair : DestStore->GetParameterOffsets())
	{
		const FNiagaraVariable& Parameter = ParamOffsetPair.Key;
		int32 DestOffset = ParamOffsetPair.Value;
		int32 SrcOffset = SrcStore->IndexOf(Parameter);

		if (Parameter.IsDataInterface())
		{
			if (InterfaceBindings.Find(FInterfaceBinding(SrcOffset, DestOffset)) == INDEX_NONE)
			{
				MissingParameterNames.Add(Parameter.GetName());
				bBindingValid = false;
			}
		}
		else
		{
			if (ParameterBindings.Find(FParameterBinding(SrcOffset, DestOffset, Parameter.GetSizeInBytes())) == INDEX_NONE)
			{
				MissingParameterNames.Add(Parameter.GetName());
				bBindingValid = false;
			}
		}
	}

	UE_LOG(LogNiagara, Warning, TEXT("Invalid ParameterStore Binding: Parameters missing from binding between %s and %s. Stores should have been rebound when one changed!"), *SrcStore->DebugName, *DestStore->DebugName);
	for (FName MissingParam : MissingParameterNames)
	{
		UE_LOG(LogNiagara, Warning, TEXT("%s"), *MissingParam.ToString());
	}
#endif
	return bBindingValid;
}

/** Merge contiguous ranges into a single binding. */
// FORCEINLINE_DEBUGGABLE void FNiagaraParameterStoreBinding::Optimize()
// {
// 	//TODO
// }

FORCEINLINE_DEBUGGABLE void FNiagaraParameterStoreBinding::Tick(FNiagaraParameterStore* DestStore, FNiagaraParameterStore* SrcStore, bool bForce)
{
	if (SrcStore->GetParametersDirty() || bForce)
	{
		for (FParameterBinding& Binding : ParameterBindings)
		{
			DestStore->SetParameterData(SrcStore->GetParameterData(Binding.SrcOffset), Binding.DestOffset, Binding.Size);
		}
	}

	if (SrcStore->GetInterfacesDirty() || bForce)
	{
		for (FInterfaceBinding& Binding : InterfaceBindings)
		{
			DestStore->SetDataInterface(SrcStore->GetDataInterface(Binding.SrcOffset), Binding.DestOffset);
		}
	}

#if NIAGARA_NAN_CHECKING
	DestStore->CheckForNaNs();
#endif
}

FORCEINLINE_DEBUGGABLE void FNiagaraParameterStoreBinding::Dump(const FNiagaraParameterStore* DestStore, const FNiagaraParameterStore* SrcStore)const
{
#if WITH_EDITORONLY_DATA
	UE_LOG(LogNiagara, Log, TEXT("\n\nDest Store: %s\n"), *DestStore->DebugName);
	for (const FParameterBinding& Binding : ParameterBindings)
	{
		ensure(Binding.Size != 0);
		ensure(Binding.SrcOffset != -1);
		ensure(Binding.DestOffset != -1);
		FNiagaraVariable Param;
		bool bFound = false;
		for (const TPair<FNiagaraVariable, int32>& ParamOffsetPair : DestStore->GetParameterOffsets())
		{
			if (ParamOffsetPair.Value == Binding.DestOffset && !ParamOffsetPair.Key.IsDataInterface())
			{
				bFound = true;
				Param = ParamOffsetPair.Key;
			}
		}
		if (ensure(bFound))
		{
			UE_LOG(LogNiagara, Log, TEXT("| Param | %s %s: Src:%d - Dest:%d - Size:%d\n"), *Param.GetType().GetName(), *Param.GetName().ToString(), Binding.SrcOffset, Binding.DestOffset, Binding.Size);

			//Also ensure the param has been pushed correctly.
			const uint8* SrcData = SrcStore->GetParameterData(Binding.SrcOffset);
			const uint8* DestData = DestStore->GetParameterData(Binding.DestOffset);
			if (!ensure(FMemory::Memcmp(SrcData, DestData, Binding.Size) == 0))
			{
				UE_LOG(LogNiagara, Log, TEXT("Parameter in dest store has incorrect data!\n"));
			}
		}
		else
		{
			UE_LOG(LogNiagara, Log, TEXT("Failed to find matching param in bound store!\n"));
		}
	}

	for (const FInterfaceBinding& Binding : InterfaceBindings)
	{
		ensure(Binding.SrcOffset != -1);
		ensure(Binding.DestOffset != -1);
		FNiagaraVariable Param;
		bool bFound = false;
		for (const TPair<FNiagaraVariable, int32>& ParamOffsetPair : DestStore->GetParameterOffsets())
		{
			if (ParamOffsetPair.Value == Binding.DestOffset && ParamOffsetPair.Key.IsDataInterface())
			{
				bFound = true;
				Param = ParamOffsetPair.Key;
			}
		}
		if (ensure(bFound))
		{
			UE_LOG(LogNiagara, Log, TEXT("| DI | %s %s: Src:%d - Dest:%d\n"), *Param.GetType().GetName(), *Param.GetName().ToString(), Binding.SrcOffset, Binding.DestOffset);

			//Also ensure the param has been pushed correctly.
			const UNiagaraDataInterface* SrcData = SrcStore->GetDataInterfaces()[Binding.SrcOffset];
			const UNiagaraDataInterface* DestData = DestStore->GetDataInterfaces()[Binding.DestOffset];
			if (!ensure(SrcData == DestData))
			{
				UE_LOG(LogNiagara, Log, TEXT("Data interface parameter in dest store is incorrect!\n"));
			}
		}
		else
		{
			UE_LOG(LogNiagara, Log, TEXT("Failed to find matching data interface param in bound store!\n"));
		}
	}
#endif
}

//////////////////////////////////////////////////////////////////////////

/**
Direct binding to a parameter store to allow efficient gets/sets from code etc. 
Does no tracking of lifetimes etc so users are responsible for safety.
*/
template<typename T>
struct FNiagaraParameterDirectBinding
{
	mutable T* ValuePtr;
	FNiagaraParameterStore* BoundStore;
	FNiagaraVariable BoundVariable;
	uint32 LayoutVersion;

	FNiagaraParameterDirectBinding()
		: ValuePtr(nullptr), BoundStore(nullptr)
	{}

	T* Init(FNiagaraParameterStore& InStore, const FNiagaraVariable& DestVariable)
	{
		BoundStore = &InStore;
		BoundVariable = DestVariable;
		LayoutVersion = BoundStore->GetLayoutVersion();

		check(DestVariable.GetSizeInBytes() == sizeof(T));
		ValuePtr = (T*)InStore.GetParameterData(DestVariable);
		return ValuePtr;
	}

	FORCEINLINE void SetValue(const T& InValue)
	{
		check(BoundVariable.GetSizeInBytes() == sizeof(T));
		checkf(LayoutVersion == BoundStore->GetLayoutVersion(), TEXT("This binding is invalid, its bound parameter store's layout was changed since it was created"));

		if (ValuePtr)
		{
			*ValuePtr = InValue;
		}
	}

	FORCEINLINE T GetValue()const 
	{
		check(BoundVariable.GetSizeInBytes() == sizeof(T));
		checkf(LayoutVersion == BoundStore->GetLayoutVersion(), TEXT("This binding is invalid, its bound parameter store's layout was changed since it was created"));

		if (ValuePtr)
		{
			return *ValuePtr;
		}
		return T();
	}
};

template<>
struct FNiagaraParameterDirectBinding<FMatrix>
{
	mutable FMatrix* ValuePtr;
	FNiagaraParameterStore* BoundStore;
	FNiagaraVariable BoundVariable;
	uint32 LayoutVersion;

	FNiagaraParameterDirectBinding():ValuePtr(nullptr), BoundStore(nullptr) {}

	FMatrix* Init(FNiagaraParameterStore& InStore, const FNiagaraVariable& DestVariable)
	{
		BoundStore = &InStore;
		BoundVariable = DestVariable;
		LayoutVersion = BoundStore->GetLayoutVersion();

		check(DestVariable.GetSizeInBytes() == sizeof(FMatrix));
		ValuePtr = (FMatrix*)InStore.GetParameterData(DestVariable);
		return ValuePtr;
	}

	FORCEINLINE void SetValue(const FMatrix& InValue)
	{
		check(BoundVariable.GetSizeInBytes() == sizeof(FMatrix));
		checkf(LayoutVersion == BoundStore->GetLayoutVersion(), TEXT("This binding is invalid, its bound parameter store's layout was changed since it was created"));

		if (ValuePtr)
		{
			FMemory::Memcpy(ValuePtr, &InValue, sizeof(FMatrix));//Temp annoyance until we fix the alignment issues with parameter stores.
		}
	}

	FORCEINLINE FMatrix GetValue()const
	{
		check(BoundVariable.GetSizeInBytes() == sizeof(FMatrix));
		checkf(LayoutVersion == BoundStore->GetLayoutVersion(), TEXT("This binding is invalid, its bound parameter store's layout was changed since it was created"));

		FMatrix Ret;
		if (ValuePtr)
		{
			FMemory::Memcpy(&Ret, ValuePtr, sizeof(FMatrix));//Temp annoyance until we fix the alignment issues with parameter stores.
		}
		return Ret;
	}
};

template<>
struct FNiagaraParameterDirectBinding<FVector4>
{
	mutable FVector4* ValuePtr;
	FNiagaraParameterStore* BoundStore;
	FNiagaraVariable BoundVariable;
	uint32 LayoutVersion;

	FNiagaraParameterDirectBinding() :ValuePtr(nullptr), BoundStore(nullptr){}

	FVector4* Init(FNiagaraParameterStore& InStore, const FNiagaraVariable& DestVariable)
	{
		BoundStore = &InStore;
		BoundVariable = DestVariable;
		LayoutVersion = BoundStore->GetLayoutVersion();

		check(DestVariable.GetSizeInBytes() == sizeof(FVector4));
		ValuePtr = (FVector4*)InStore.GetParameterData(DestVariable);
		return ValuePtr;
	}

	FORCEINLINE void SetValue(const FVector4& InValue)
	{
		check(BoundVariable.GetSizeInBytes() == sizeof(FVector4));
		checkf(LayoutVersion == BoundStore->GetLayoutVersion(), TEXT("This binding is invalid, its bound parameter store's layout was changed since it was created"));

		if (ValuePtr)
		{
			FMemory::Memcpy(ValuePtr, &InValue, sizeof(FVector4));//Temp annoyance until we fix the alignment issues with parameter stores.
		}
	}

	FORCEINLINE FVector4 GetValue()const
	{
		check(BoundVariable.GetSizeInBytes() == sizeof(FVector4));
		checkf(LayoutVersion == BoundStore->GetLayoutVersion(), TEXT("This binding is invalid, its bound parameter store's layout was changed since it was created"));

		FVector4 Ret;
		if (ValuePtr)
		{
			FMemory::Memcpy(&Ret, ValuePtr, sizeof(FVector4));//Temp annoyance until we fix the alignment issues with parameter stores.
		}
		return Ret;
	}
};

template<>
struct FNiagaraParameterDirectBinding<FQuat>
{
	mutable FQuat* ValuePtr;
	FNiagaraParameterStore* BoundStore;
	FNiagaraVariable BoundVariable;
	uint32 LayoutVersion;

	FNiagaraParameterDirectBinding() :ValuePtr(nullptr), BoundStore(nullptr) {}

	FQuat* Init(FNiagaraParameterStore& InStore, const FNiagaraVariable& DestVariable)
	{
		BoundStore = &InStore;
		BoundVariable = DestVariable;
		LayoutVersion = BoundStore->GetLayoutVersion();

		check(DestVariable.GetSizeInBytes() == sizeof(FQuat));
		ValuePtr = (FQuat*)InStore.GetParameterData(DestVariable);
		return ValuePtr;
	}

	FORCEINLINE void SetValue(const FQuat& InValue)
	{
		check(BoundVariable.GetSizeInBytes() == sizeof(FQuat));
		checkf(LayoutVersion == BoundStore->GetLayoutVersion(), TEXT("This binding is invalid, its bound parameter store's layout was changed since it was created"));

		if (ValuePtr)
		{
			FMemory::Memcpy(ValuePtr, &InValue, sizeof(FQuat));//Temp annoyance until we fix the alignment issues with parameter stores.
		}
	}

	FORCEINLINE FQuat GetValue()const
	{
		check(BoundVariable.GetSizeInBytes() == sizeof(FQuat));
		checkf(LayoutVersion == BoundStore->GetLayoutVersion(), TEXT("This binding is invalid, its bound parameter store's layout was changed since it was created"));

		FQuat Ret;
		if (ValuePtr)
		{
			FMemory::Memcpy(&Ret, ValuePtr, sizeof(FQuat));//Temp annoyance until we fix the alignment issues with parameter stores.
		}
		return Ret;
	}
};

template<>
struct FNiagaraParameterDirectBinding<FNiagaraBool>
{
	mutable uint32* ValuePtr;
	FNiagaraParameterStore* BoundStore;
	FNiagaraVariable BoundVariable;
	uint32 LayoutVersion;

	FNiagaraParameterDirectBinding() :ValuePtr(nullptr), BoundStore(nullptr) {}

	uint32* Init(FNiagaraParameterStore& InStore, const FNiagaraVariable& DestVariable)
	{
		BoundStore = &InStore;
		BoundVariable = DestVariable;
		LayoutVersion = BoundStore->GetLayoutVersion();

		check(DestVariable.GetSizeInBytes() == sizeof(FNiagaraBool));
		check(sizeof(uint32) == sizeof(FNiagaraBool));
		ValuePtr = (uint32*)InStore.GetParameterData(DestVariable);
		return ValuePtr;
	}

	FORCEINLINE void SetValue(const FNiagaraBool& InValue)
	{
		check(BoundVariable.GetSizeInBytes() == sizeof(FNiagaraBool));
		check(sizeof(uint32) == sizeof(FNiagaraBool));
		checkf(LayoutVersion == BoundStore->GetLayoutVersion(), TEXT("This binding is invalid, its bound parameter store's layout was changed since it was created"));

		if (ValuePtr)
		{
			FMemory::Memcpy(ValuePtr, &InValue, sizeof(FNiagaraBool));
		}
	}

	FORCEINLINE void SetValue(const bool& InValue)
	{
		check(BoundVariable.GetSizeInBytes() == sizeof(FNiagaraBool));
		checkf(LayoutVersion == BoundStore->GetLayoutVersion(), TEXT("This binding is invalid, its bound parameter store's layout was changed since it was created"));

		if (ValuePtr)
		{
			if (!InValue)
			{
				*ValuePtr = FNiagaraBool::False;
			}
			else
			{
				*ValuePtr = FNiagaraBool::True;
			}
		}
	}

	FORCEINLINE FNiagaraBool GetValue()const
	{
		check(BoundVariable.GetSizeInBytes() == sizeof(FNiagaraBool));
		checkf(LayoutVersion == BoundStore->GetLayoutVersion(), TEXT("This binding is invalid, its bound parameter store's layout was changed since it was created"));

		FNiagaraBool Ret(false);
		if (ValuePtr)
		{
			if (FNiagaraBool::False == *ValuePtr)
			{
				Ret = FNiagaraBool(false);
			}
			else
			{
				Ret = FNiagaraBool(true);
			}
		}
		return Ret;
	}
};