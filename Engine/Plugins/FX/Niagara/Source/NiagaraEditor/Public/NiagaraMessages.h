// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/Guid.h"
#include "UObject/ObjectKey.h"
#include "NiagaraCommon.h"
#include "NiagaraShared.h"
#include "NiagaraGraph.h"
#include "NiagaraMessageDataBase.h"
#include "NiagaraMessages.generated.h"

struct NIAGARAEDITOR_API FNiagaraMessageTopics
{
	static const FName CompilerTopicName;
	static const FName ObjectTopicName;
};

UENUM()
enum class ENiagaraMessageSeverity : uint8
{
	CriticalError = 0,
	Error = 1,
	PerformanceWarning = 2,
	Warning = 3,
	Info = 4,	// Should be last
};

//Struct for passing around script asset info from compile event message job to message types
struct FNiagaraScriptNameAndAssetPath
{
public:
	FNiagaraScriptNameAndAssetPath(const FString& InScriptNameString, const FString& InScriptAssetPathString)
		: ScriptNameString(InScriptNameString)
		, ScriptAssetPathString(InScriptAssetPathString)
	{};

	const FString ScriptNameString;
	const FString ScriptAssetPathString;
};

// Struct for passing around named simple delegates.
struct NIAGARAEDITOR_API FLinkNameAndDelegate
{
public:
	FLinkNameAndDelegate() = default;

	FLinkNameAndDelegate(const FText& InLinkNameText, const FSimpleDelegate& InLinkDelegate)
		: LinkNameText(InLinkNameText)
		, LinkDelegate(InLinkDelegate)
	{};

	FText LinkNameText;
	FSimpleDelegate LinkDelegate;
};

/** 
 * Interface for view-agnostic message that holds limited lifetime information on a message (e.g. a weak pointer to an asset.)
 */
class INiagaraMessage
{
public:
	INiagaraMessage(const TArray<FObjectKey>& InAssociatedObjectKeys = TArray<FObjectKey>())
		: AssociatedObjectKeys(InAssociatedObjectKeys)
		, MessageTopicBitflag(0)
	{};

	virtual ~INiagaraMessage() {};

	virtual FText GenerateMessageText() const = 0;

	virtual TSharedRef<FTokenizedMessage> GenerateTokenizedMessage() const = 0;

	virtual void GenerateLinks(TArray<FText>& OutLinkDisplayNames, TArray<FSimpleDelegate>& OutLinkNavigationActions) const = 0;

	virtual const FName GetMessageTopic() const = 0;

	const TArray<FObjectKey>& GetAssociatedObjectKeys() const { return AssociatedObjectKeys; };

	const uint32 GetMessageTopicBitflag() const;

protected:
	const TArray<FObjectKey> AssociatedObjectKeys;
	mutable uint32 MessageTopicBitflag;
};

class FNiagaraMessageCompileEvent : public INiagaraMessage
{
public:
	FNiagaraMessageCompileEvent(
		const FNiagaraCompileEvent& InCompileEvent
		, TArray<FNiagaraScriptNameAndAssetPath>& InContextScriptNamesAndAssetPaths
		, TOptional<const FText>& InOwningScriptNameAndUsageText
		, TOptional<const FNiagaraScriptNameAndAssetPath>& InCompiledScriptNameAndAssetPath
		, const TArray<FObjectKey>& InAssociatedObjectKeys
		);

	virtual FText GenerateMessageText() const override;

	virtual TSharedRef<FTokenizedMessage> GenerateTokenizedMessage() const override;

	virtual void GenerateLinks(TArray<FText>& OutLinkDisplayNames, TArray<FSimpleDelegate>& OutLinkNavigationActions) const override;

	virtual const FName GetMessageTopic() const override { return FNiagaraMessageTopics::CompilerTopicName; };

private:
	const FNiagaraCompileEvent CompileEvent;
	const TArray<FNiagaraScriptNameAndAssetPath> ContextScriptNamesAndAssetPaths;
	const TOptional<const FText> OwningScriptNameAndUsageText;
	const TOptional<const FNiagaraScriptNameAndAssetPath> CompiledScriptNameAndAssetPath;
	const TArray<FObjectKey> AssociatedObjectKeys;
};

class FNiagaraMessageText : public INiagaraMessage
{
public:
	FNiagaraMessageText(const FText& InMessageText, const EMessageSeverity::Type& InMessageSeverity, const FName& InTopicName, const TArray<FObjectKey>& InAssociatedObjectKeys = TArray<FObjectKey>())
		: INiagaraMessage(InAssociatedObjectKeys)
		, MessageText(InMessageText)
		, MessageSeverity(InMessageSeverity)
		, TopicName(InTopicName)
	{
	};

	virtual FText GenerateMessageText() const override;

	virtual TSharedRef<FTokenizedMessage> GenerateTokenizedMessage() const override;

	virtual void GenerateLinks(TArray<FText>& OutLinkDisplayNames, TArray<FSimpleDelegate>& OutLinkNavigationActions) const override { }

	virtual const FName GetMessageTopic() const override { return TopicName; };

private:
	const FText MessageText;
	const EMessageSeverity::Type MessageSeverity;
	const FName TopicName;
};

class FNiagaraMessageTextWithLinks : public FNiagaraMessageText
{
public:
	FNiagaraMessageTextWithLinks(
	  const FText& InMessageText
	, const EMessageSeverity::Type& InMessageSeverity
	, const FName& InTopicName
	, const TArray<FLinkNameAndDelegate>& InLinks
	, const TArray<FObjectKey>& InAssociatedObjectKeys = TArray<FObjectKey>())
		: FNiagaraMessageText(InMessageText, InMessageSeverity, InTopicName, InAssociatedObjectKeys)
		, Links(InLinks)
	{
	};

	virtual TSharedRef<FTokenizedMessage> GenerateTokenizedMessage() const override;

	virtual void GenerateLinks(TArray<FText>& OutLinkDisplayNames, TArray<FSimpleDelegate>& OutLinkNavigationActions) const override;

private:
	const TArray<FLinkNameAndDelegate> Links;
};

/**
 * Interface for "slow task" message generation jobs that should be time sliced to avoid stalling the UI.
 */
class INiagaraMessageJob
{
public:
	virtual TSharedRef<const INiagaraMessage> GenerateNiagaraMessage() const = 0;

	virtual ~INiagaraMessageJob() {};
};

class FNiagaraMessageJobCompileEvent : public INiagaraMessageJob
{
public:
	FNiagaraMessageJobCompileEvent(
		const FNiagaraCompileEvent& InCompileEvent
		, const TWeakObjectPtr<const UNiagaraScript>& InOriginatingScriptWeakObjPtr
		, const TOptional<const FString>& InOwningScriptNameString = TOptional<const FString>()
		, const TOptional<const FString>& InSourceScriptAssetPath = TOptional<const FString>()
		);

	virtual TSharedRef<const INiagaraMessage> GenerateNiagaraMessage() const override;

private:

	bool RecursiveGetScriptNamesAndAssetPathsFromContextStack(
		TArray<FGuid>& InContextStackNodeGuids
		, FGuid NodeGuid
		, const UNiagaraGraph* InGraphToSearch
		, TArray<FNiagaraScriptNameAndAssetPath>& OutContextScriptNamesAndAssetPaths
		, TOptional<const FString>& OutEmitterName
		, TOptional<const FText>& OutFailureReason
		, TArray<FObjectKey>& OutContextNodeObjectKeys
		) const;

	const FNiagaraCompileEvent CompileEvent;
	const TWeakObjectPtr<const UNiagaraScript> OriginatingScriptWeakObjPtr;
	TOptional<const FString> OwningScriptNameString;
	TOptional<const FString> SourceScriptAssetPath;
};


struct NIAGARAEDITOR_API FGenerateNiagaraMessageInfo
{
public:
	FGenerateNiagaraMessageInfo() = default;

	void SetAssociatedObjectKeys(const TArray<FObjectKey>& InAssociatedObjectKeys) { AssociatedObjectKeys = InAssociatedObjectKeys; };
	const TArray<FObjectKey>& GetAssociatedObjectKeys() const { return AssociatedObjectKeys; };
	void SetLinks(const TArray<FLinkNameAndDelegate>& InLinks) { Links = InLinks; };
	const TArray<FLinkNameAndDelegate>& GetLinks() const { return Links; };

private:
	TArray<FObjectKey> AssociatedObjectKeys;
	TArray<FLinkNameAndDelegate> Links;
};

UCLASS()
class NIAGARAEDITOR_API UNiagaraMessageData : public UNiagaraMessageDataBase
{
	GENERATED_BODY()

public:
	virtual TSharedRef<const INiagaraMessage> GenerateNiagaraMessage(const FGenerateNiagaraMessageInfo& InGenerateInfo = FGenerateNiagaraMessageInfo()) const PURE_VIRTUAL(UNiagaraMessageData::GenerateNiagaraMessage, return TSharedRef<const INiagaraMessage>(static_cast<INiagaraMessage*>(nullptr)););
};

UCLASS()
class NIAGARAEDITOR_API UNiagaraMessageDataText : public UNiagaraMessageData
{
	GENERATED_BODY()

public:
	void Init(const FText& InMessageText, const ENiagaraMessageSeverity InMessageSeverity, const FName& InTopicName);

	virtual TSharedRef<const INiagaraMessage> GenerateNiagaraMessage(const FGenerateNiagaraMessageInfo& InGenerateInfo = FGenerateNiagaraMessageInfo()) const override;

private:
	UPROPERTY()
	FText MessageText;

	UPROPERTY()
	ENiagaraMessageSeverity MessageSeverity;

	UPROPERTY()
	FName TopicName;
};