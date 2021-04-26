// Engine/Source/Runtime/WebBrowser/Public/WebJSFunction.h

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"
#include "UObject/Class.h"
#include "WebInterfaceJSFunction.generated.h"

class FWebInterfaceJSScripting;

struct WEBBROWSERUI_API FWebInterfaceJSParam
{

	struct IStructWrapper
	{
		virtual ~IStructWrapper() {};
		virtual UStruct* GetTypeInfo() = 0;
		virtual const void* GetData() = 0;
		virtual IStructWrapper* Clone() = 0;
	};

	template <typename T> struct FStructWrapper
		: public IStructWrapper
	{
		T StructValue;
		FStructWrapper(const T& InValue)
			: StructValue(InValue)
		{}
		virtual ~FStructWrapper()
		{}
		virtual UStruct* GetTypeInfo() override
		{
			return T::StaticStruct();
		}
		virtual const void* GetData() override
		{
			return &StructValue;
		}
		virtual IStructWrapper* Clone() override
		{
			return new FStructWrapper<T>(StructValue);
		}
	};

	FWebInterfaceJSParam() : Tag(PTYPE_NULL) {}
	FWebInterfaceJSParam(bool Value) : Tag(PTYPE_BOOL), BoolValue(Value) {}
	FWebInterfaceJSParam(int8 Value) : Tag(PTYPE_INT), IntValue(Value) {}
	FWebInterfaceJSParam(int16 Value) : Tag(PTYPE_INT), IntValue(Value) {}
	FWebInterfaceJSParam(int32 Value) : Tag(PTYPE_INT), IntValue(Value) {}
	FWebInterfaceJSParam(uint8 Value) : Tag(PTYPE_INT), IntValue(Value) {}
	FWebInterfaceJSParam(uint16 Value) : Tag(PTYPE_INT), IntValue(Value) {}
	FWebInterfaceJSParam(uint32 Value) : Tag(PTYPE_DOUBLE), DoubleValue(Value) {}
	FWebInterfaceJSParam(int64 Value) : Tag(PTYPE_DOUBLE), DoubleValue(Value) {}
	FWebInterfaceJSParam(uint64 Value) : Tag(PTYPE_DOUBLE), DoubleValue(Value) {}
	FWebInterfaceJSParam(double Value) : Tag(PTYPE_DOUBLE), DoubleValue(Value) {}
	FWebInterfaceJSParam(float Value) : Tag(PTYPE_DOUBLE), DoubleValue(Value) {}
	FWebInterfaceJSParam(const FString& Value) : Tag(PTYPE_STRING), StringValue(new FString(Value)) {}
	FWebInterfaceJSParam(const FText& Value) : Tag(PTYPE_STRING), StringValue(new FString(Value.ToString())) {}
	FWebInterfaceJSParam(const FName& Value) : Tag(PTYPE_STRING), StringValue(new FString(Value.ToString())) {}
	FWebInterfaceJSParam(const TCHAR* Value) : Tag(PTYPE_STRING), StringValue(new FString(Value)) {}
	FWebInterfaceJSParam(UObject* Value) : Tag(PTYPE_OBJECT), ObjectValue(Value) {}
	template <typename T> FWebInterfaceJSParam(const T& Value,
		typename TEnableIf<!TIsPointer<T>::Value, UStruct>::Type* InTypeInfo=T::StaticStruct())
		: Tag(PTYPE_STRUCT)
		, StructValue(new FStructWrapper<T>(Value))
	{}
	template <typename T> FWebInterfaceJSParam(const TArray<T>& Value)
		: Tag(PTYPE_ARRAY)
	{
		ArrayValue = new TArray<FWebInterfaceJSParam>();
		ArrayValue->Reserve(Value.Num());
		for(T Item : Value)
		{
			ArrayValue->Add(FWebInterfaceJSParam(Item));
		}
	}
	template <typename T> FWebInterfaceJSParam(const TMap<FString, T>& Value)
		: Tag(PTYPE_MAP)
	{
		MapValue = new TMap<FString, FWebInterfaceJSParam>();
		MapValue->Reserve(Value.Num());
		for(const auto& Pair : Value)
		{
			MapValue->Add(Pair.Key, FWebInterfaceJSParam(Pair.Value));
		}
	}
	template <typename K, typename T> FWebInterfaceJSParam(const TMap<K, T>& Value)
		: Tag(PTYPE_MAP)
	{
		MapValue = new TMap<FString, FWebInterfaceJSParam>();
		MapValue->Reserve(Value.Num());
		for(const auto& Pair : Value)
		{
			MapValue->Add(Pair.Key.ToString(), FWebInterfaceJSParam(Pair.Value));
		}
	}
	FWebInterfaceJSParam(const FWebInterfaceJSParam& Other);
	FWebInterfaceJSParam(FWebInterfaceJSParam&& Other);
	~FWebInterfaceJSParam();

	enum { PTYPE_NULL, PTYPE_BOOL, PTYPE_INT, PTYPE_DOUBLE, PTYPE_STRING, PTYPE_OBJECT, PTYPE_STRUCT, PTYPE_ARRAY, PTYPE_MAP } Tag;
	union
	{
		bool BoolValue;
		double DoubleValue;
		int32 IntValue;
		UObject* ObjectValue;
		const FString* StringValue;
		IStructWrapper* StructValue;
		TArray<FWebInterfaceJSParam>* ArrayValue;
		TMap<FString, FWebInterfaceJSParam>* MapValue;
	};

};

class FWebInterfaceJSScripting;

/** Base class for JS callback objects. */
USTRUCT()
struct WEBBROWSERUI_API FWebInterfaceJSCallbackBase
{
	GENERATED_USTRUCT_BODY()
	FWebInterfaceJSCallbackBase()
	{}

	bool IsValid() const
	{
		return ScriptingPtr.IsValid();
	}


protected:
	FWebInterfaceJSCallbackBase(TSharedPtr<FWebInterfaceJSScripting> InScripting, const FGuid& InCallbackId)
		: ScriptingPtr(InScripting)
		, CallbackId(InCallbackId)
	{}

	void Invoke(int32 ArgCount, FWebInterfaceJSParam Arguments[], bool bIsError = false) const;

private:

	TWeakPtr<FWebInterfaceJSScripting> ScriptingPtr;
	FGuid CallbackId;
};


/**
 * Representation of a remote JS function.
 * FWebJSFunction objects represent a JS function and allow calling them from native code.
 * FWebJSFunction objects can also be added to delegates and events using the Bind/AddLambda method.
 */
USTRUCT()
struct WEBBROWSERUI_API FWebInterfaceJSFunction
	: public FWebInterfaceJSCallbackBase
{
	GENERATED_USTRUCT_BODY()

	FWebInterfaceJSFunction()
		: FWebInterfaceJSCallbackBase()
	{}

	FWebInterfaceJSFunction(TSharedPtr<FWebInterfaceJSScripting> InScripting, const FGuid& InFunctionId)
		: FWebInterfaceJSCallbackBase(InScripting, InFunctionId)
	{}

	template<typename ...ArgTypes> void operator()(ArgTypes... Args) const
	{
		FWebInterfaceJSParam ArgArray[sizeof...(Args)] = {FWebInterfaceJSParam(Args)...};
		Invoke(sizeof...(Args), ArgArray);
	}
};

/** 
 *  Representation of a remote JS async response object.
 *  UFUNCTIONs taking a FWebJSResponse will get it passed in automatically when called from a web browser.
 *  Pass a result or error back by invoking Success or Failure on the object.
 *  UFunctions accepting a FWebJSResponse should have a void return type, as any value returned from the function will be ignored.
 *  Calling the response methods does not have to happen before returning from the function, which means you can use this to implement asynchronous functionality.
 *
 *  Note that the remote object will become invalid as soon as a result has been delivered, so you can only call either Success or Failure once.
 */
USTRUCT()
struct WEBBROWSERUI_API FWebInterfaceJSResponse
	: public FWebInterfaceJSCallbackBase
{
	GENERATED_USTRUCT_BODY()

	FWebInterfaceJSResponse()
		: FWebInterfaceJSCallbackBase()
	{}

	FWebInterfaceJSResponse(TSharedPtr<FWebInterfaceJSScripting> InScripting, const FGuid& InCallbackId)
		: FWebInterfaceJSCallbackBase(InScripting, InCallbackId)
	{}

	/**
	 * Indicate successful completion without a return value.
	 * The remote Promise's then() handler will be executed without arguments.
	 */
	void Success() const
	{
		Invoke(0, nullptr, false);
	}

	/**
	 * Indicate successful completion passing a return value back.
	 * The remote Promise's then() handler will be executed with the value passed as its single argument.
	 */
	template<typename T>
	void Success(T Arg) const
	{
		FWebInterfaceJSParam ArgArray[1] = {FWebInterfaceJSParam(Arg)};
		Invoke(1, ArgArray, false);
	}

	/**
	 * Indicate failed completion, passing an error message back to JS.
	 * The remote Promise's catch() handler will be executed with the value passed as the error reason.
	 */
	template<typename T>
	void Failure(T Arg) const
	{
		FWebInterfaceJSParam ArgArray[1] = {FWebInterfaceJSParam(Arg)};
		Invoke(1, ArgArray, true);
	}


};
