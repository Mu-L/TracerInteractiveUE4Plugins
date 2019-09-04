// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OnlineKeyValuePair.h"
#include "UObject/CoreOnline.h"
#include "OnlineSubsystemTypes.h"
#include "OnlineDelegateMacros.h"

typedef FString FChatRoomId;
struct FOnlineError;

ONLINESUBSYSTEM_API DECLARE_LOG_CATEGORY_EXTERN(LogOnlineParty, Log, All);
#define UE_LOG_ONLINE_PARTY(Verbosity, Format, ...) \
{ \
	UE_LOG(LogOnlineParty, Verbosity, TEXT("%s%s"), ONLINE_LOG_PREFIX, *FString::Printf(Format, ##__VA_ARGS__)); \
}

#define UE_CLOG_ONLINE_PARTY(Conditional, Verbosity, Format, ...) \
{ \
	UE_CLOG(Conditional, LogOnlineParty, Verbosity, TEXT("%s%s"), ONLINE_LOG_PREFIX, *FString::Printf(Format, ##__VA_ARGS__)); \
}

#define F_PREFIX(TypeToPrefix) F##TypeToPrefix
#define PARTY_DECLARE_DELEGATETYPE(Type) typedef F##Type::FDelegate F##Type##Delegate

enum class EAcceptPartyInvitationCompletionResult : int8;
enum class ECreatePartyCompletionResult : int8;
enum class EJoinPartyCompletionResult : int8;
enum class EKickMemberCompletionResult : int8;
enum class ELeavePartyCompletionResult : int8;
enum class EPromoteMemberCompletionResult : int8;
enum class ERejectPartyInvitationCompletionResult : int8;
enum class ERequestPartyInvitationCompletionResult : int8;
enum class ESendPartyInvitationCompletionResult : int8;
enum class EUpdateConfigCompletionResult : int8;

enum class EInvitationResponse : uint8;

struct FAnalyticsEventAttribute;

enum class EMemberConnectionStatus : uint8
{
	Uninitialized,
	Disconnected,
	Initializing,
	Connected
};

/**
 * Party member user info returned by IOnlineParty interface
 */
class FOnlinePartyMember
	: public FOnlineUser
{
public:
	EMemberConnectionStatus MemberConnectionStatus = EMemberConnectionStatus::Uninitialized;
	EMemberConnectionStatus PreviousMemberConnectionStatus = EMemberConnectionStatus::Uninitialized;

	/**
	 * Event when a party member's attribute has changed
	 * @see FOnlineUser::GetUserAttribute
	 * @param ChangedUserId id associated with this notification
	 * @param Attribute attribute that changed
	 * @param NewValue the new value for the attribute
	 * @param PreviousValue the previous value for the attribute
	 */
	DECLARE_EVENT_FourParams(FOnlinePartyMember, FOnMemberAttributeChanged, const FUniqueNetId& /*ChangedUserId*/, const FString& /*Attribute*/, const FString& /*NewValue*/, const FString& /*OldValue*/);
	FOnMemberAttributeChanged& OnMemberAttributeChanged() const { return OnMemberAttributeChangedEvent; }

	/**
	 * Event when a party member's connection status has changed
	 * @param ChangedUserId - id associated with this notification
	 * @param NewMemberConnectionStatus - new member data status
	 * @param PreviousMemberConnectionStatus - previous member data status
	 */
	DECLARE_EVENT_ThreeParams(FOnlinePartyMember, FOnMemberConnectionStatusChanged, const FUniqueNetId& /*ChangedUserId*/, const EMemberConnectionStatus /*NewMemberConnectionStatus*/, const EMemberConnectionStatus /*PreviousMemberConnectionStatus*/);
	FOnMemberConnectionStatusChanged& OnMemberConnectionStatusChanged() const { return OnMemberConnectionStatusChangedEvent; }

	void SetMemberConnectionStatus(EMemberConnectionStatus NewMemberConnectionStatus)
	{
		if (NewMemberConnectionStatus != MemberConnectionStatus)
		{
			PreviousMemberConnectionStatus = MemberConnectionStatus;
			MemberConnectionStatus = NewMemberConnectionStatus;
			OnMemberConnectionStatusChangedEvent.Broadcast(*GetUserId(), MemberConnectionStatus, PreviousMemberConnectionStatus);
		}
	}

private:
	/** Event fired when connection status changes */
	mutable FOnMemberConnectionStatusChanged OnMemberConnectionStatusChangedEvent;
	/** Event fired when an attribute changes */
	mutable FOnMemberAttributeChanged OnMemberAttributeChangedEvent;
};

typedef TSharedRef<const FOnlinePartyMember> FOnlinePartyMemberConstRef;
typedef TSharedPtr<const FOnlinePartyMember> FOnlinePartyMemberConstPtr;

/**
 * Data associated with the entire party
 */
class ONLINESUBSYSTEM_API FOnlinePartyData
	: public TSharedFromThis<FOnlinePartyData>
{
public:
	FOnlinePartyData() = default;
	virtual ~FOnlinePartyData() = default;

	/**
	 * Equality operator
	 *
	 * @param Other the FOnlinePartyData to compare against
	 * @return true if considered equal, false otherwise
	 */
	bool operator==(const FOnlinePartyData& Other) const;
	/**
	 * Inequality operator
	 *
	 * @param Other the FOnlinePartyData to compare against
	 * @return true if considered not equal, false otherwise
	 */
	bool operator!=(const FOnlinePartyData& Other) const;

	/**
	 * Get an attribute from the party data
	 *
	 * @param AttrName - key for the attribute
	 * @param OutAttrValue - [out] value for the attribute if found
	 *
	 * @return true if the attribute was found
	 */
	bool GetAttribute(const FString& AttrName, FVariantData& OutAttrValue) const
	{
		bool bResult = false;

		const FVariantData* FoundValuePtr = KeyValAttrs.Find(AttrName);
		if (FoundValuePtr != nullptr)
		{
			OutAttrValue = *FoundValuePtr;
			return true;
		}

		return bResult;
	}

	/**
	 * Set an attribute from the party data
	 *
	 * @param AttrName - key for the attribute
	 * @param AttrValue - value to set the attribute to
	 */
	void SetAttribute(const FString& AttrName, const FVariantData& AttrValue)
	{
		FVariantData& NewAttrValue = KeyValAttrs.FindOrAdd(AttrName);
		if (NewAttrValue != AttrValue)
		{
			NewAttrValue = AttrValue;
			DirtyKeys.Emplace(AttrName);
		}
	}

	/**
	 * Set an attribute from the party data
	 *
	 * @param AttrName - key for the attribute
	 * @param AttrValue - value to set the attribute to
	 */
	void SetAttribute(FString&& AttrName, FVariantData&& AttrValue)
	{
		FVariantData& NewAttrValue = KeyValAttrs.FindOrAdd(AttrName);
		if (NewAttrValue != AttrValue)
		{
			NewAttrValue = MoveTemp(AttrValue);
			DirtyKeys.Emplace(MoveTemp(AttrName));
		}
	}

	/**
	 * Remove an attribute from the party data
	 *
	 * @param AttrName - key for the attribute
	 */
	void RemoveAttribute(FString&& AttrName)
	{
		if (KeyValAttrs.Remove(AttrName) > 0)
		{
			DirtyKeys.Emplace(MoveTemp(AttrName));
		}
	}

	/**
	 * Remove an attribute from the party data
	 *
	 * @param AttrName - key for the attribute
	 */
	void RemoveAttribute(const FString& AttrName)
	{
		if (KeyValAttrs.Remove(AttrName) > 0)
		{
			DirtyKeys.Emplace(AttrName);
		}
	}

	/**
	 * Mark an attribute as dirty so it can be rebroadcasted
	 *
	 * @param AttrName - key for the attribute to mark dirty
	 */
	void MarkAttributeDirty(FString&& AttrName)
	{
		DirtyKeys.Emplace(MoveTemp(AttrName));
	}

	/**
	 * Check if there are any dirty keys
	 *
	 * @return true if there are any dirty keys
	 */
	bool HasDirtyKeys() const
	{
		return DirtyKeys.Num() > 0;
	}

	/**
	 * Get the dirty and removed key-value attributes
	 *
	 * @param OutDirtyAttrs the dirty attributes
	 * @param OutRemovedAttrs the removed attributes
	 */
	void GetDirtyKeyValAttrs(FOnlineKeyValuePairs<FString, FVariantData>& OutDirtyAttrs, TArray<FString>& OutRemovedAttrs) const;

	/**
	 * Clear the attributes map
	 */
	void ClearAttributes()
	{
		KeyValAttrs.Empty();
		DirtyKeys.Empty();
	}

	/** 
	 * Clear the dirty keys set, called after successfully sending an update of the dirty elements
	 */
	void ClearDirty()
	{
		DirtyKeys.Empty();
	}

	/** 
	 * Increment the stat tracking variables on packet sent
	 * 
	 * @param PacketSize - size of the packet generated
	 * @param NumRecipients - number of recipients the packet was sent to
	 * @param bIncrementRevision - this packet was a dirty packet update so we should increment the revision
	 */
	void OnPacketSent(int32 PacketSize, int32 NumRecipients, bool bIncrementRevision) const
	{
		TotalPackets++;
		TotalBytes += PacketSize;
		TotalEffectiveBytes += PacketSize * NumRecipients;
		if (bIncrementRevision)
		{
			++RevisionCount;
		}
	}

	/** 
	 * Generate a JSON packet containing all key-value attributes
	 * 
	 * @param JsonString - [out] string containing the resulting JSON output
	 */
	void ToJsonFull(FString& JsonString) const;
	
	/** 
	 * Generate a JSON packet containing only the dirty key-value attributes for a delta update
	 *
	 * @param JsonString - [out] string containing the resulting JSON output
	 */
	void ToJsonDirty(FString& JsonString) const;

	/**
	 * Create a JSON object containing all key-value attributes
	 * @return a JSON object containing all key-value attributes
	 */
	TSharedRef<FJsonObject> GetAllAttributesAsJsonObject() const;

	/**
	 * Create a string representing a JSON object containing all key-value attributes
	 * @return a string representing a JSON object containing all key-value attributes
	 */
	FString GetAllAttributesAsJsonObjectString() const;

	/** 
	 * Update attributes from a JSON packet
	 *
	 * @param JsonString - string containing the JSON packet
	 */
	void FromJson(const FString& JsonString);

	/** Accessor functions for KeyValAttrs map */
	FOnlineKeyValuePairs<FString, FVariantData>& GetKeyValAttrs() { return KeyValAttrs; }
	const FOnlineKeyValuePairs<FString, FVariantData>& GetKeyValAttrs() const { return KeyValAttrs; }

	/** Stat tracking variables */
	/** Total number of bytes generated by calls to ToJsonFull and ToJsonDirty */
	mutable int32 TotalBytes = 0;
	/** Total number of bytes generated by calls to ToJsonFull and ToJsonDirty, multiplied by the number of recipients the packet was sent to */
	mutable int32 TotalEffectiveBytes = 0;
	/** Total number of packets generated by calls to ToJsonFull and ToJsonDirty */
	mutable int32 TotalPackets = 0;

	/** Id representing number of updates sent, useful for determining if a client has missed an update */
	mutable int32 RevisionCount = 0;

private:
	/** map of key/val attributes that represents the data */
	FOnlineKeyValuePairs<FString, FVariantData> KeyValAttrs;

	/** set of which fields are dirty and need to transmitted */
	TSet<FString> DirtyKeys;

};

typedef TSharedRef<FOnlinePartyData> FOnlinePartyDataRef;
typedef TSharedPtr<FOnlinePartyData> FOnlinePartyDataPtr;
typedef TSharedRef<const FOnlinePartyData> FOnlinePartyDataConstRef;
typedef TSharedPtr<const FOnlinePartyData> FOnlinePartyDataConstPtr;

/**
* Info needed to join a party
*/
class IOnlinePartyPendingJoinRequestInfo
{
public:
	IOnlinePartyPendingJoinRequestInfo() {}
	virtual ~IOnlinePartyPendingJoinRequestInfo() {}

	/**
	 * @return id of the sender of this join request
	 */
	virtual const TSharedRef<const FUniqueNetId>& GetSenderId() const = 0;

	/**
	 * @return display name of the sender of this join request
	 */
	virtual const FString& GetSenderDisplayName() const = 0;

	/**
	 * @return platform of the sender of this join request
	 */
	virtual const FString& GetSenderPlatform() const = 0;

	/**
	 * @return join data provided by the sender for htis join request
	 */
	virtual TSharedRef<const FOnlinePartyData> GetSenderJoinData() const = 0;
};

typedef TSharedRef<const IOnlinePartyPendingJoinRequestInfo> IOnlinePartyPendingJoinRequestInfoConstRef;
typedef TSharedPtr<const IOnlinePartyPendingJoinRequestInfo> IOnlinePartyPendingJoinRequestInfoConstPtr;

/**
 * Info needed to join a party
 */
class IOnlinePartyJoinInfo
	: public TSharedFromThis<IOnlinePartyJoinInfo>
{
public:
	IOnlinePartyJoinInfo() {}
	virtual ~IOnlinePartyJoinInfo() {}

	virtual bool IsValid() const = 0;

	/**
	 * @return party id of party associated with this join invite
	 */
	virtual TSharedRef<const FOnlinePartyId> GetPartyId() const = 0;

	/**
	 * @return party id of party associated with this join invite
	 */
	virtual FOnlinePartyTypeId GetPartyTypeId() const = 0;

	/**
	 * @return user id of where this join info came from
	 */
	virtual TSharedRef<const FUniqueNetId> GetSourceUserId() const = 0;

	/**
	 * @return user id of where this join info came from
	 */
	virtual const FString& GetSourceDisplayName() const = 0;

	/** 
	 * @return source platform string
	 */
	virtual const FString& GetSourcePlatform() const = 0;

	/**
	 * @return true if the join info has some form of key(does not guarantee the validity of that key)
	 */
	virtual bool HasKey() const = 0;

	/**
	 * @return true if a password can be used to bypass generated access key
     */
	virtual bool HasPassword() const = 0;

	/**
	 * @return true if the party is known to be accepting members
     */
	virtual bool IsAcceptingMembers() const = 0;

	/**
	 * @return true if this is a party of one
	 */
	virtual bool IsPartyOfOne() const = 0;

	/**
	 * @return why the party is not accepting members
	 */
	virtual int32 GetNotAcceptingReason() const = 0;

	/**
	 * @return id of the client app associated with the sender of the party invite
	 */
	virtual const FString& GetAppId() const = 0;

	/**
	* @return id of the build associated with the sender of the party invite
	*/
	virtual const FString& GetBuildId() const = 0;

	/**
	 * @return whether or not the join info can be used to join
	 */
	virtual bool CanJoin() const = 0;

	/**
	 * @return whether or not the join info can be used to join with a password
	 */
	virtual bool CanJoinWithPassword() const = 0;

	/**
	 * @return whether or not the join info has the info to request an invite
	 */
	virtual bool CanRequestAnInvite() const = 0;
};

typedef TSharedRef<const IOnlinePartyJoinInfo> IOnlinePartyJoinInfoConstRef;
typedef TSharedPtr<const IOnlinePartyJoinInfo> IOnlinePartyJoinInfoConstPtr;

/**
 * Permissions for party features
 */
namespace PartySystemPermissions
{
	/**
	 * Who has permissions to perform party actions
	 */
	enum class EPermissionType : uint8
	{
		/** Noone has access to do that action */
		Noone,
		/** Available to the leader only */
		Leader,
		/** Available to the leader and friends of the leader only */
		Friends,
		/** Available to anyone */
		Anyone
	};
}

enum class EJoinRequestAction : uint8
{
	Manual,
	AutoApprove,
	AutoReject
};
/**
 * Options for configuring a new party or for updating an existing party
 */
struct ONLINESUBSYSTEM_API FPartyConfiguration
	: public TSharedFromThis<FPartyConfiguration>
{
	FPartyConfiguration()
		: JoinRequestAction(EJoinRequestAction::Manual)
		, PresencePermissions(PartySystemPermissions::EPermissionType::Anyone)
		, InvitePermissions(PartySystemPermissions::EPermissionType::Leader)
		, bChatEnabled(true)
		, bShouldRemoveOnDisconnection(false)
		, bIsAcceptingMembers(false)
		, NotAcceptingMembersReason(0)
		, MaxMembers(0)
	{}

	/**
	 * Equality operator
	 *
	 * @param Other the FPartyConfiguration to compare against
	 * @return true if considered equal, false otherwise
	 */
	bool operator==(const FPartyConfiguration& Other) const;
	/**
	 * Inequality operator
	 *
	 * @param Other the FPartyConfiguration to compare against
	 * @return true if considered not equal, false otherwise
	 */
	bool operator!=(const FPartyConfiguration& Other) const;

	/** should publish info to presence */
	EJoinRequestAction JoinRequestAction;
	/** Permission for how the party can be  */
	PartySystemPermissions::EPermissionType PresencePermissions;
	/** Permission who can send invites */
	PartySystemPermissions::EPermissionType InvitePermissions;
	/** should have a muc room */
	bool bChatEnabled;
	/** should remove on disconnection */
	bool bShouldRemoveOnDisconnection;
	/** is accepting members */
	bool bIsAcceptingMembers;
	/** not accepting members reason */
	int32 NotAcceptingMembersReason;
	/** Maximum active members allowed. 0 means no maximum. */
	int32 MaxMembers;
	/** Human readable nickname */
	FString Nickname;
	/** Human readable description */
	FString Description;
	/** Human readable password for party. */
	FString Password;
};

typedef TSharedRef<const FPartyConfiguration> FPartyConfigurationConstRef;

enum class EPartyState : uint8
{
	None,
	CreatePending,
	JoinPending,
	RejoinPending, 
	LeavePending,
	Active,
	Disconnected,
	CleanUp
};

/**
 * Current state associated with a party
 */
class FOnlineParty
	: public TSharedFromThis<FOnlineParty>
{
	FOnlineParty() = delete;
protected:
		FOnlineParty(const TSharedRef<const FOnlinePartyId>& InPartyId, const FOnlinePartyTypeId InPartyTypeId)
		: PartyId(InPartyId)
		, PartyTypeId(InPartyTypeId)
		, State(EPartyState::None)
		, PreviousState(EPartyState::None)
	{}

public:
	virtual ~FOnlineParty() = default;

	/**
	 * Check if the local user has invite permissions in this party. Based on configuration permissions and party state.
	 *
	 * @param LocalUserId the local user's id
	 * @return true if the local user can invite, false if not
	 */
	virtual bool CanLocalUserInvite(const FUniqueNetId& LocalUserId) const = 0;

	/**
	 * Is this party joinable?
	 *
	 * @return true if this party is joinable, false if not
	 */
	virtual bool IsJoinable() const = 0;
	virtual void SetState(EPartyState InState)
	{
		PreviousState = State;
		State = InState;
	}

	/**
	 * Get the party's configuration
	 *
	 * @return the party's configuration
	 */
	virtual TSharedRef<const FPartyConfiguration> GetConfiguration() const = 0;

	/** Unique id of the party */
	TSharedRef<const FOnlinePartyId> PartyId;
	/** Type of party (e.g., Primary) */
	const FOnlinePartyTypeId PartyTypeId;
	/** Unique id of the leader */
	TSharedPtr<const FUniqueNetId> LeaderId;
	/** The current state of the party */
	EPartyState State;
	/** The current state of the party */
	EPartyState PreviousState;
	/** id of chat room associated with the party */
	FChatRoomId RoomId;
};

typedef TSharedRef<const FOnlineParty> FOnlinePartyConstRef;
typedef TSharedPtr<const FOnlineParty> FOnlinePartyConstPtr;

enum class EMemberExitedReason : uint8
{
	Unknown,
	Left,
	Removed,
	Kicked
};

enum class EPartyInvitationRemovedReason : uint8
{
	/** Unknown or undefined reason */
	Unknown,
	/** User accepted the invitation */
	Accepted,
	/** User declined the invitation */
	Declined,
	/** ClearInvitations was called, the invitation should no longer be displayed */
	Cleared,
	/** Expired */
	Expired,
	/** Became invalid (for example, party was destroyed) */
	Invalidated,
};

/** Recipient information for SendInvitation */
struct FPartyInvitationRecipient
{
	/** Constructor */
	FPartyInvitationRecipient(const TSharedRef<const FUniqueNetId>& InId)
		: Id(InId)
	{}

	/** Constructor */
	FPartyInvitationRecipient(const FUniqueNetId& InId)
		: Id(InId.AsShared())
	{}

	/** Id of the user to send the invitation to */
	TSharedRef<const FUniqueNetId> Id;
	/** Additional data to provide context for the invitee */
	FString PlatformData;

	/** Get a string representation suitable for logging */
	FString ONLINESUBSYSTEM_API ToDebugString() const;
};

enum class EPartySystemState : uint8
{
	Initializing = 0,
	Initialized,
	RequestingShutdown,
	ShutDown,
};

///////////////////////////////////////////////////////////////////
// Completion delegates
///////////////////////////////////////////////////////////////////
/**
 * Restore parties async task completed callback
 *
 * @param LocalUserId id of user that initiated the request
 * @param Result Result of the operation
 */
DECLARE_DELEGATE_TwoParams(FOnRestorePartiesComplete, const FUniqueNetId& /*LocalUserId*/, const FOnlineError& /*Result*/);
/**
 * Cleanup parties async task completed callback
 *
 * @param LocalUserId id of user that initiated the request
 * @param Result Result of the operation
 */
DECLARE_DELEGATE_TwoParams(FOnCleanupPartiesComplete, const FUniqueNetId& /*LocalUserId*/, const FOnlineError& /*Result*/);
/**
 * Party creation async task completed callback
 *
 * @param LocalUserId - id of user that initiated the request
 * @param PartyId - id associated with the party
 * @param Result - result of the operation
 */
DECLARE_DELEGATE_ThreeParams(FOnCreatePartyComplete, const FUniqueNetId& /*LocalUserId*/, const TSharedPtr<const FOnlinePartyId>& /*PartyId*/, const ECreatePartyCompletionResult /*Result*/);
/**
 * Party join async task completed callback
 *
 * @param LocalUserId - id of user that initiated the request
 * @param PartyId - id associated with the party
 * @param Result - result of the operation
 * @param NotApprovedReason - client defined value describing why you were not approved
 */
DECLARE_DELEGATE_FourParams(FOnJoinPartyComplete, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const EJoinPartyCompletionResult /*Result*/, const int32 /*NotApprovedReason*/);
/**
 * Party query joinability async task completed callback
 *
 * @param LocalUserId - id of user that initiated the request
 * @param PartyId - id associated with the party
 * @param Result - result of the operation
 * @param NotApprovedReason - client defined value describing why you were not approved
 */
DECLARE_DELEGATE_FourParams(FOnQueryPartyJoinabilityComplete, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const EJoinPartyCompletionResult /*Result*/, const int32 /*NotApprovedReason*/);
/**
 * Party leave async task completed callback
 *
 * @param LocalUserId - id of user that initiated the request
 * @param PartyId - id associated with the party
 * @param Result - result of the operation
 */
DECLARE_DELEGATE_ThreeParams(FOnLeavePartyComplete, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const ELeavePartyCompletionResult /*Result*/);
/**
 * Party update async task completed callback
 *
 * @param LocalUserId - id of user that initiated the request
 * @param PartyId - id associated with the party
 * @param Result - result of the operation
 */
DECLARE_DELEGATE_ThreeParams(FOnUpdatePartyComplete, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const EUpdateConfigCompletionResult /*Result*/);
/**
 * Party update async task completed callback
 *
 * @param LocalUserId - id of user that initiated the request
 * @param PartyId - id associated with the party
 * @param Result - result of the operation
 */
DECLARE_DELEGATE_ThreeParams(FOnRequestPartyInvitationComplete, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const ERequestPartyInvitationCompletionResult /*Result*/);
/**
 * Party invitation sent completed callback
 *
 * @param LocalUserId - id of user that initiated the request
 * @param PartyId - id associated with the party
 * @param RecipientId - user invite was sent to
 * @param Result - result of the send operation
 */
DECLARE_DELEGATE_FourParams(FOnSendPartyInvitationComplete, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FUniqueNetId& /*RecipientId*/, const ESendPartyInvitationCompletionResult /*Result*/);
/**
 * Accepting an invite to a user to join party async task completed callback
 *
 * @param LocalUserId - id of user that initiated the request
 * @param PartyId - id associated with the party
 * @param Result - string with error info if any
 */
DECLARE_DELEGATE_ThreeParams(FOnAcceptPartyInvitationComplete, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const EAcceptPartyInvitationCompletionResult /*Result*/);
/**
 * Rejecting an invite to a user to join party async task completed callback
 *
 * @param LocalUserId - id of user that initiated the request
 * @param bWasSuccessful - true if successfully sent invite
 * @param PartyId - id associated with the party
 * @param Result - string with error info if any
 */
DECLARE_DELEGATE_ThreeParams(FOnRejectPartyInvitationComplete, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const ERejectPartyInvitationCompletionResult /*Result*/);
/**
 * Kicking a member of a party async task completed callback
 *
 * @param LocalUserId - id of user that initiated the request
 * @param PartyId - id associated with the party
 * @param MemberId - id of member being kicked
 * @param Result - string with error info if any
 */
DECLARE_DELEGATE_FourParams(FOnKickPartyMemberComplete, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FUniqueNetId& /*LocalUserId*/, const EKickMemberCompletionResult /*Result*/);
/**
 * Promoting a member of a party async task completed callback
 *
 * @param LocalUserId - id of user that initiated the request
 * @param PartyId - id associated with the party
 * @param MemberId - id of member being promoted to leader
 * @param Result - string with error info if any
 */
DECLARE_DELEGATE_FourParams(FOnPromotePartyMemberComplete, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FUniqueNetId& /*LocalUserId*/, const EPromoteMemberCompletionResult /*Result*/);






///////////////////////////////////////////////////////////////////
// Notification delegates
///////////////////////////////////////////////////////////////////

/**
* notification when a party is joined
* @param LocalUserId - id associated with this notification
* @param PartyId - id associated with the party
*/
DECLARE_MULTICAST_DELEGATE_TwoParams(F_PREFIX(OnPartyJoined), const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/);
PARTY_DECLARE_DELEGATETYPE(OnPartyJoined);

/**
 * notification when a party is joined
 * @param LocalUserId - id associated with this notification
 * @param PartyId - id associated with the party
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(F_PREFIX(OnPartyExited), const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/);
PARTY_DECLARE_DELEGATETYPE(OnPartyExited);

/**
 * Notification when a party's state has changed
 * @param LocalUserId - id associated with this notification
 * @param PartyId - id associated with the party
 * @param State - state of the party
 * @param PreviousState - previous state of the party
 */
DECLARE_MULTICAST_DELEGATE_FourParams(F_PREFIX(OnPartyStateChanged), const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, EPartyState /*State*/, EPartyState /*PreviousState*/);
PARTY_DECLARE_DELEGATETYPE(OnPartyStateChanged);

/**
* Notification when a player has been approved for JIP
* @param LocalUserId - id associated with this notification
* @param PartyId - id associated with the party
*/
DECLARE_MULTICAST_DELEGATE_ThreeParams(F_PREFIX(OnPartyJIP), const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, bool /*Success*/);
PARTY_DECLARE_DELEGATETYPE(OnPartyJIP);

/**
 * Notification when player promotion is locked out.
 * @param LocalUserId - id associated with this notification
 * @param PartyId - id associated with the party
 * @param bLockoutState - if promotion is currently locked out
 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(F_PREFIX(OnPartyPromotionLockoutChanged), const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const bool /*bLockoutState*/);
PARTY_DECLARE_DELEGATETYPE(OnPartyPromotionLockoutChanged);

/**
 * Notification when party config is updated
 * Deprecated - Use OnPartyConfigChangedConst
 * @param LocalUserId - id associated with this notification
 * @param PartyId - id associated with the party
 * @param PartyConfig - party whose config was updated
 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(F_PREFIX(OnPartyConfigChanged), const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const TSharedRef<FPartyConfiguration>& /*PartyConfig*/);
PARTY_DECLARE_DELEGATETYPE(OnPartyConfigChanged);

/**
 * Notification when party config is updated
 * @param LocalUserId - id associated with this notification
 * @param PartyId - id associated with the party
 * @param PartyConfig - party whose config was updated
 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(F_PREFIX(OnPartyConfigChangedConst), const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FPartyConfiguration& /*PartyConfig*/);
PARTY_DECLARE_DELEGATETYPE(OnPartyConfigChangedConst);

/**
 * Notification when party data is updated
 * Deprecated - Use OnPartyDataReceivedConst
 * @param LocalUserId - id associated with this notification
 * @param PartyId - id associated with the party
 * @param PartyData - party data that was updated
 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(F_PREFIX(OnPartyDataReceived), const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const TSharedRef<FOnlinePartyData>& /*PartyData*/);
PARTY_DECLARE_DELEGATETYPE(OnPartyDataReceived);

/**
 * Notification when party data is updated
 * @param LocalUserId - id associated with this notification
 * @param PartyId - id associated with the party
 * @param PartyData - party data that was updated
 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(F_PREFIX(OnPartyDataReceivedConst), const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FOnlinePartyData& /*PartyData*/);
PARTY_DECLARE_DELEGATETYPE(OnPartyDataReceivedConst);

/**
 * Notification when a member is promoted in a party
 * @param LocalUserId - id associated with this notification
 * @param PartyId - id associated with the party
 * @param NewLeaderId - id of member that was promoted
 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(F_PREFIX(OnPartyMemberPromoted), const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FUniqueNetId& /*NewLeaderId*/);
PARTY_DECLARE_DELEGATETYPE(OnPartyMemberPromoted);

/**
 * Notification when a member exits a party
 * @param LocalUserId - id associated with this notification
 * @param PartyId - id associated with the party
 * @param MemberId - id of member that joined
 * @param Reason - why the member was removed
 */
DECLARE_MULTICAST_DELEGATE_FourParams(F_PREFIX(OnPartyMemberExited), const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FUniqueNetId& /*MemberId*/, const EMemberExitedReason /*Reason*/);
PARTY_DECLARE_DELEGATETYPE(OnPartyMemberExited);

/**
 * Notification when a member joins the party
 * @param LocalUserId - id associated with this notification
 * @param PartyId - id associated with the party
 * @param MemberId - id of member that joined
 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(F_PREFIX(OnPartyMemberJoined), const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FUniqueNetId& /*MemberId*/);
PARTY_DECLARE_DELEGATETYPE(OnPartyMemberJoined);

/**
 * Notification when party member data is updated
 * Deprecated - Use OnPartyMemberDataReceivedConst
 * @param LocalUserId - id associated with this notification
 * @param PartyId - id associated with the party
 * @param MemberId - id of member that had updated data
 * @param PartyMemberData - party member data that was updated
 */
DECLARE_MULTICAST_DELEGATE_FourParams(F_PREFIX(OnPartyMemberDataReceived), const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FUniqueNetId& /*MemberId*/, const TSharedRef<FOnlinePartyData>& /*PartyMemberData*/);
PARTY_DECLARE_DELEGATETYPE(OnPartyMemberDataReceived);

/**
 * Notification when party member data is updated
 * @param LocalUserId - id associated with this notification
 * @param PartyId - id associated with the party
 * @param MemberId - id of member that had updated data
 * @param PartyMemberData - party member data that was updated
 */
DECLARE_MULTICAST_DELEGATE_FourParams(F_PREFIX(OnPartyMemberDataReceivedConst), const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FUniqueNetId& /*MemberId*/, const FOnlinePartyData& /*PartyMemberData*/);
PARTY_DECLARE_DELEGATETYPE(OnPartyMemberDataReceivedConst);

/**
 * Notification when an invite list has changed for a party the user is in
 * @param LocalUserId - user that is associated with this notification
 */
DECLARE_MULTICAST_DELEGATE_OneParam(F_PREFIX(OnPartyInvitesChanged), const FUniqueNetId& /*LocalUserId*/);
PARTY_DECLARE_DELEGATETYPE(OnPartyInvitesChanged);

/**
 * Notification when a request for an invite has been received
 * @param LocalUserId id associated with this notification
 * @param PartyId id associated with the party
 * @param SenderId id of user that sent the invite
 * @param RequestForId id of user that sender is requesting the invite for - invalid if the sender is requesting the invite
 */
DECLARE_MULTICAST_DELEGATE_FourParams(F_PREFIX(OnPartyInviteRequestReceived), const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FUniqueNetId& /*SenderId*/, const FUniqueNetId& /*RequestForId*/);
PARTY_DECLARE_DELEGATETYPE(OnPartyInviteRequestReceived);

/**
 * Notification when a new invite is received
 * @param LocalUserId id associated with this notification
 * @param PartyId id associated with the party
 * @param SenderId id of member that sent the invite
 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(F_PREFIX(OnPartyInviteReceived), const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FUniqueNetId& /*SenderId*/);
PARTY_DECLARE_DELEGATETYPE(OnPartyInviteReceived);

/**
 * Notification when an invite has been removed
 * @param LocalUserId id associated with this notification
 * @param PartyId id associated with the party
 * @param SenderId id of member that sent the invite
 * @param Reason reason the invite has been removed
 */
DECLARE_MULTICAST_DELEGATE_FourParams(F_PREFIX(OnPartyInviteRemoved), const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FUniqueNetId& /*SenderId*/, EPartyInvitationRemovedReason /*Reason*/);
PARTY_DECLARE_DELEGATETYPE(OnPartyInviteRemoved);

/**
 * Notification when a new invite is received
 * @param LocalUserId id associated with this notification
 * @param PartyId id associated with the party
 * @param SenderId id of user that sent the invite
 * @param bWasAccepted whether or not the invite was accepted
 */
DECLARE_MULTICAST_DELEGATE_FourParams(F_PREFIX(OnPartyInviteResponseReceived), const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FUniqueNetId& /*SenderId*/, const EInvitationResponse /*Response*/);
PARTY_DECLARE_DELEGATETYPE(OnPartyInviteResponseReceived);

/**
 * Notification when a new reservation request is received
 * @param LocalUserId - id associated with this notification
 * @param PartyId - id associated with the party
 * @param SenderId - id of member that sent the request
 * @param Platform - platform of member that sent the request
 * @param PartyData - data provided by the sender for the leader to use to determine joinability
 */
DECLARE_MULTICAST_DELEGATE_FiveParams(F_PREFIX(OnPartyJoinRequestReceived), const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FUniqueNetId& /*SenderId*/, const FString& /*Platform*/, const FOnlinePartyData& /*PartyData*/);
PARTY_DECLARE_DELEGATETYPE(OnPartyJoinRequestReceived);

/**
* Notification when a new reservation request is received
* @param LocalUserId - id associated with this notification
* @param PartyId - id associated with the party
* @param SenderId - id of member that sent the request
* @param Platform - platform of member that sent the request
* @param PartyData - data provided by the sender for the leader to use to determine joinability
*/
DECLARE_MULTICAST_DELEGATE_ThreeParams(F_PREFIX(OnPartyJIPRequestReceived), const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FUniqueNetId& /*SenderId*/);
PARTY_DECLARE_DELEGATETYPE(OnPartyJIPRequestReceived);

/**
 * Notification when a player wants to know if the party is in a joinable state
 * @param LocalUserId - id associated with this notification
 * @param PartyId - id associated with the party
 * @param SenderId - id of member that sent the request
 * @param Platform - platform of member that sent the request
 * @param PartyData - data provided by the sender for the leader to use to determine joinability
 */
DECLARE_MULTICAST_DELEGATE_FiveParams(F_PREFIX(OnQueryPartyJoinabilityReceived), const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FUniqueNetId& /*SenderId*/, const FString& /*Platform*/, const FOnlinePartyData& /*PartyData*/);
PARTY_DECLARE_DELEGATETYPE(OnQueryPartyJoinabilityReceived);

/**
 * Request for the game to fill in data to be sent with the join request for the leader to make an informed decision based on the joiner's state
 * @param LocalUserId - id associated with this notification
 * @param PartyId - id associated with the party
 * @param PartyData - data for the game to populate
 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(F_PREFIX(OnFillPartyJoinRequestData), const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, FOnlinePartyData& /*PartyData*/);
PARTY_DECLARE_DELEGATETYPE(OnFillPartyJoinRequestData);

/**
 * Notification when an analytics event needs to be recorded
 * @param LocalUserId - id associated with this notification
 * @param PartyId - id associated with the party
 * @param PartyData - data for the game to populate
 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(F_PREFIX(OnPartyAnalyticsEvent), const FUniqueNetId& /*LocalUserId*/, const FString& /*EventName*/, const TArray<FAnalyticsEventAttribute>& /*Attributes*/);
PARTY_DECLARE_DELEGATETYPE(OnPartyAnalyticsEvent);

/**
* Notification of party system state change
* @param NewState - new state this partysystem is in
*/
DECLARE_MULTICAST_DELEGATE_OneParam(F_PREFIX(OnPartySystemStateChange), EPartySystemState /*NewState*/);
PARTY_DECLARE_DELEGATETYPE(OnPartySystemStateChange);

// Helper macro to aid in the deprecation of delegates that have non-const values
#define DEFINE_ONLINE_DELEGATE_BASE_DEPRECATION_HELPER(DelegateName, NewDelegateName) \
public: \
	F##NewDelegateName DelegateName##Delegates; \
public: \
	virtual FDelegateHandle Add##DelegateName##Delegate_Handle(const F##NewDelegateName##Delegate& Delegate) \
	{ \
		DelegateName##Delegates.Add(Delegate); \
		return Delegate.GetHandle(); \
	} \
	virtual void Clear##DelegateName##Delegate_Handle(FDelegateHandle& Handle) \
	{ \
		DelegateName##Delegates.Remove(Handle); \
		Handle.Reset(); \
	} \
	virtual void Clear##DelegateName##Delegates(void* Object) \
	{ \
		DelegateName##Delegates.RemoveAll(Object); \
	}

#define DEFINE_ONLINE_DELEGATE_THREE_PARAM_DEPRECATION_HELPER(DelegateName, NewDelegateName, Param1Type, Param2Type, Param3Type) \
DEFINE_ONLINE_DELEGATE_BASE_DEPRECATION_HELPER(DelegateName, NewDelegateName) \
virtual void Trigger##DelegateName##Delegates(Param1Type Param1, Param2Type Param2, Param3Type Param3) \
{ \
	DelegateName##Delegates.Broadcast(Param1, Param2, Param3); \
}

#define DEFINE_ONLINE_DELEGATE_FOUR_PARAM_DEPRECATION_HELPER(DelegateName, DelegateNameConst, Param1Type, Param2Type, Param3Type, Param4Type) \
DEFINE_ONLINE_DELEGATE_BASE_DEPRECATION_HELPER(DelegateName, DelegateNameConst) \
virtual void Trigger##DelegateName##Delegates(Param1Type Param1, Param2Type Param2, Param3Type Param3, Param4Type Param4) \
{ \
	DelegateName##Delegates.Broadcast(Param1, Param2, Param3, Param4); \
}

/**
 * Interface definition for the online party services 
 * Allows for forming a party and communicating with party members
 */
class ONLINESUBSYSTEM_API IOnlinePartySystem
{
protected:
	IOnlinePartySystem() {};

public:
	virtual ~IOnlinePartySystem() {};

	/**
	 * Restore party memberships. Intended to be called once during login to restore state from other running instances.
	 *
	 * @param LocalUserId the user to restore the party membership for
	 * @param CompletionDelegate the delegate to trigger on completion
	 */
	virtual void RestoreParties(const FUniqueNetId& LocalUserId, const FOnRestorePartiesComplete& CompletionDelegate) = 0;
	
	/**
	 * Cleanup party state. This will cleanup the local party state and attempt to cleanup party memberships on an external service if possible.  Intended to be called for development purposes.
	 *
	 * @param LocalUserId the user to cleanup the parties for
	 * @param CompletionDelegate the delegate to trigger on completion
	 */
	virtual void CleanupParties(const FUniqueNetId& LocalUserId, const FOnCleanupPartiesComplete& CompletionDelegate) = 0;
	
	/**
	 * Create a new party
	 *
	 * @param LocalUserId - user making the request
	 * @param PartyConfig - configuration for the party (can be updated later)
	 * @param Delegate - called on completion
	 * @param UserRoomId - this forces the name of the room to be this value
	 *
	 * @return true if task was started
	 */
	virtual bool CreateParty(const FUniqueNetId& LocalUserId, const FOnlinePartyTypeId PartyTypeId, const FPartyConfiguration& PartyConfig, const FOnCreatePartyComplete& Delegate = FOnCreatePartyComplete()) = 0;

	/**
	 * Update an existing party with new configuration
	 *
	 * @param LocalUserId - user making the request
	 * @param PartyConfig - configuration for the party
	 * @param Delegate - called on completion
	 *
	 * @return true if task was started
	 */
	virtual bool UpdateParty(const FUniqueNetId& LocalUserId, const FOnlinePartyId& PartyId, const FPartyConfiguration& PartyConfig, bool bShouldRegenerateReservationKey = false, const FOnUpdatePartyComplete& Delegate = FOnUpdatePartyComplete()) = 0;

	/**
	 * Join an existing party
	 *
	 * @param LocalUserId - user making the request
	 * @param OnlinePartyJoinInfo - join information containing data such as party id, leader id
	 * @param Delegate - called on completion
	 *
	 * @return true if task was started
	 */
	virtual bool JoinParty(const FUniqueNetId& LocalUserId, const IOnlinePartyJoinInfo& OnlinePartyJoinInfo, const FOnJoinPartyComplete& Delegate = FOnJoinPartyComplete()) = 0;

	/**
	* Join an existing game session from within a party
	*
	* @param LocalUserId - user making the request
	* @param OnlinePartyJoinInfo - join information containing data such as party id, leader id
	* @param Delegate - called on completion
	*
	* @return true if task was started
	*/
	virtual bool JIPFromWithinParty(const FUniqueNetId& LocalUserId, const FOnlinePartyId& PartyId, const FUniqueNetId& PartyLeaderId) = 0;

	/**
	 * Query a party to check it's current joinability
	 * Intended to be used before a call to LeaveParty (to leave your existing party, which would then be followed by JoinParty)
	 * Note that the party's joinability can change from moment to moment so a successful response for this does not guarantee a successful JoinParty
	 *
	 * @param LocalUserId - user making the request
	 * @param OnlinePartyJoinInfo - join information containing data such as party id, leader id
	 * @param Delegate - called on completion
	 */
	virtual void QueryPartyJoinability(const FUniqueNetId& LocalUserId, const IOnlinePartyJoinInfo& OnlinePartyJoinInfo, const FOnQueryPartyJoinabilityComplete& Delegate = FOnQueryPartyJoinabilityComplete()) = 0;

	/**
	 * Attempt to rejoin a former party
	 *
	 * @param LocalUserId - user making the request
	 * @param PartyId - id of the party you want to rejoin
	 * @param PartyTypeId - type id of the party you want to rejoin
	 * @param FormerMembers - array of former member ids that we can contact to try to rejoin the party
	 * @param Delegate - called on completion
	 *
	 * @return true if task was started
	 */
	virtual bool RejoinParty(const FUniqueNetId& LocalUserId, const FOnlinePartyId& PartyId, const FOnlinePartyTypeId& PartyTypeId, const TArray<TSharedRef<const FUniqueNetId>>& FormerMembers, const FOnJoinPartyComplete& Delegate = FOnJoinPartyComplete()) = 0;

	/**
	 * Leave an existing party
	 * All existing party members notified of member leaving (see FOnPartyMemberLeft)
	 *
	 * @param LocalUserId - user making the request
	 * @param PartyId - id of an existing party
	 * @param Delegate - called on completion
	 *
	 * @return true if task was started
	 */
	virtual bool LeaveParty(const FUniqueNetId& LocalUserId, const FOnlinePartyId& PartyId, const FOnLeavePartyComplete& Delegate = FOnLeavePartyComplete()) = 0;

	/**
	* Approve a request to join a party
	*
	* @param LocalUserId - user making the request
	* @param PartyId - id of an existing party
	* @param RecipientId - id of the user being invited
	* @param bIsApproved - whether the join request was approved or not
	* @param DeniedResultCode - used when bIsApproved is false - client defined value to return when leader denies approval
	*
	* @return true if task was started
	*/
	virtual bool ApproveJoinRequest(const FUniqueNetId& LocalUserId, const FOnlinePartyId& PartyId, const FUniqueNetId& RecipientId, bool bIsApproved, int32 DeniedResultCode = 0) = 0;

	/**
	* Approve a request to join the JIP match a party is in.
	*
	* @param LocalUserId - user making the request
	* @param PartyId - id of an existing party
	* @param RecipientId - id of the user being invited
	* @param bIsApproved - whether the join request was approved or not
	* @param DeniedResultCode - used when bIsApproved is false - client defined value to return when leader denies approval
	*
	* @return true if task was started
	*/
	virtual bool ApproveJIPRequest(const FUniqueNetId& LocalUserId, const FOnlinePartyId& PartyId, const FUniqueNetId& RecipientId, bool bIsApproved, int32 DeniedResultCode = 0) = 0;

	/**
	 * Respond to a query joinability request.  This reflects the current party's joinability state and can change from moment to moment, and therefore does not guarantee a successful join.
	 *
	 * @param LocalUserId - user making the request
	 * @param PartyId - id of an existing party
	 * @param RecipientId - id of the user being invited
	 * @param bCanJoin - whether the player can attempt to join or not
	 * @param DeniedResultCode - used when bCanJoin is false - client defined value to return when leader denies approval
	 */
	virtual void RespondToQueryJoinability(const FUniqueNetId& LocalUserId, const FOnlinePartyId& PartyId, const FUniqueNetId& RecipientId, bool bCanJoin, int32 DeniedResultCode = 0) = 0;

	/**
	 * sends an invitation to a user that could not otherwise join a party
	 * if the player accepts the invite they will be sent the data needed to trigger a call to RequestReservation
	 *
	 * @param LocalUserId - user making the request
	 * @param PartyId - id of an existing party
	 * @param Recipient - structure specifying the recipient of the invitation
	 * @param Delegate - called on completion
	 *
	 * @return true if task was started
	 */
	virtual bool SendInvitation(const FUniqueNetId& LocalUserId, const FOnlinePartyId& PartyId, const FPartyInvitationRecipient& Recipient, const FOnSendPartyInvitationComplete& Delegate = FOnSendPartyInvitationComplete()) = 0;

	/**
	 * Accept an invite to a party. NOTE this does not initiate a join.
	 *
	 * @param LocalUserId - user making the request
	 * @param SenderId - id of the sender
	 *
	 * @return true if task was started
	 */
	UE_DEPRECATED(4.23, "Use JoinParty instead of AcceptInvitation")
	virtual bool AcceptInvitation(const FUniqueNetId& LocalUserId, const FUniqueNetId& SenderId) { return false; }

	/**
	 * Reject an invite to a party
	 *
	 * @param LocalUserId - user making the request
	 * @param SenderId - id of the sender
	 *
	 * @return true if task was started
	 */
	virtual bool RejectInvitation(const FUniqueNetId& LocalUserId, const FUniqueNetId& SenderId) = 0;

	/**
	 * Clear invitations from a user because the invitations were handled by the application
	 *
	 * @param LocalUserId - user making the request
	 * @param SenderId - id of the sender
	 * @param PartyId - optional, if specified will clear only the one invitation, if blank all invitations from the sender will be cleared
	 *
	 * @return true if task was started
	 */
	virtual void ClearInvitations(const FUniqueNetId& LocalUserId, const FUniqueNetId& SenderId, const FOnlinePartyId* PartyId = nullptr) = 0;

	/** 
	 * Mark a user as approved to attempt to rejoin our party
	 *
	 * @param LocalUserId - user making the request
	 * @param PartyId - party the player is approved to rejoin the party
	 * @param ApprovedUserId - the user that has been approved to attempt to rejoin the party (does not need to be in the party now)
	 */
	UE_DEPRECATED(4.23, "Marking users for rejoins in the public interface is deprecated. This functionality should be implemented internal to your party implementation.")
	virtual void ApproveUserForRejoin(const FUniqueNetId& LocalUserId, const FOnlinePartyId& PartyId, const FUniqueNetId& ApprovedUserId) {}

	/** 
	 * Unmark a user as approved to attempt to rejoin our party
	 *
	 * @param LocalUserId - user making the request
	 * @param PartyId - party the player is approved to rejoin the party
	 * @param RemovedUserId - the user that has lost approval to attempt to rejoin the party
	 */
	UE_DEPRECATED(4.23, "Marking users for rejoins in the public interface is deprecated. This functionality should be implemented internal to your party implementation.")
	virtual void RemoveUserForRejoin(const FUniqueNetId& LocalUserId, const FOnlinePartyId& PartyId, const FUniqueNetId& RemovedUserId) {}

	/** 
	 * Get a list of users that have been approved for rejoining
	 *
	 * @param LocalUserId - user making the request
	 * @param PartyId - party the player is approved to rejoin the party
	 * @param OutApprovedUserIds - list of users that have been approved
	 */
	UE_DEPRECATED(4.23, "Marking users for rejoins in the public interface is deprecated. This functionality should be implemented internal to your party implementation.")
	virtual void GetUsersApprovedForRejoin(const FUniqueNetId& LocalUserId, const FOnlinePartyId& PartyId, TArray<TSharedRef<const FUniqueNetId>>& OutApprovedUserIds) {}

	/**
	 * Kick a user from an existing party
	 * Only admin can kick a party member
	 *
	 * @param LocalUserId - user making the request
	 * @param PartyId - id of an existing party
	 * @param MemberId - id of the user being kicked
	 * @param Delegate - called on completion
	 *
	 * @return true if task was started
	 */
	virtual bool KickMember(const FUniqueNetId& LocalUserId, const FOnlinePartyId& PartyId, const FUniqueNetId& TargetMemberId, const FOnKickPartyMemberComplete& Delegate = FOnKickPartyMemberComplete()) = 0;

	/**
	 * Promote a user from an existing party to be admin
	 * All existing party members notified of promoted member (see FOnPartyMemberPromoted)
	 *
	 * @param LocalUserId - user making the request
	 * @param PartyId - id of an existing party
	 * @param MemberId - id of the user being promoted
	 * @param Delegate - called on completion
	 *
	 * @return true if task was started
	 */
	virtual bool PromoteMember(const FUniqueNetId& LocalUserId, const FOnlinePartyId& PartyId, const FUniqueNetId& TargetMemberId, const FOnPromotePartyMemberComplete& Delegate = FOnPromotePartyMemberComplete()) = 0;

	/**
	 * Set party data and broadcast to all members
	 * Only current data can be set and no history of past party data is preserved
	 * Party members notified of new data (see FOnPartyDataReceived)
	 *
	 * @param LocalUserId - user making the request
	 * @param PartyId - id of an existing party
	 * @param PartyData - data to send to all party members
	 * @param Delegate - called on completion
	 *
	 * @return true if task was started
	 */
	virtual bool UpdatePartyData(const FUniqueNetId& LocalUserId, const FOnlinePartyId& PartyId, const FOnlinePartyData& PartyData) = 0;

	/**
	 * Set party data for a single party member and broadcast to all members
	 * Only current data can be set and no history of past party member data is preserved
	 * Party members notified of new data (see FOnPartyMemberDataReceived)
	 *
	 * @param LocalUserId - user making the request
	 * @param PartyId - id of an existing party
	 * @param PartyMemberData - member data to send to all party members
	 * @param Delegate - called on completion
	 *
	 * @return true if task was started
	 */
	virtual bool UpdatePartyMemberData(const FUniqueNetId& LocalUserId, const FOnlinePartyId& PartyId, const FOnlinePartyData& PartyMemberData) = 0;

	/**
	 * returns true if the user specified by MemberId is the leader of the party specified by PartyId
	 *
	 * @param LocalUserId - user making the request
	 * @param PartyId     - id of an existing party
	 * @param MemberId    - id of member to test
	 *
	 */
	virtual bool IsMemberLeader(const FUniqueNetId& LocalUserId, const FOnlinePartyId& PartyId, const FUniqueNetId& MemberId) const = 0;

	/**
	 * returns the number of players in a given party
	 *
	 * @param LocalUserId - user making the request
	 * @param PartyId     - id of an existing party
	 *
	 */
	virtual uint32 GetPartyMemberCount(const FUniqueNetId& LocalUserId, const FOnlinePartyId& PartyId) const = 0;

	/**
	 * Get info associated with a party
	 *
	 * @param LocalUserId - user making the request
	 * @param PartyId     - id of an existing party
	 *
	 * @return party info or nullptr if not found
	 */
	virtual FOnlinePartyConstPtr GetParty(const FUniqueNetId& LocalUserId, const FOnlinePartyId& PartyId) const = 0;

	/**
	 * Get info associated with a party
	 *
	 * @param LocalUserId - user making the request
	 * @param PartyTypeId - type of an existing party
	 *
	 * @return party info or nullptr if not found
	 */
	virtual FOnlinePartyConstPtr GetParty(const FUniqueNetId& LocalUserId, const FOnlinePartyTypeId& PartyTypeId) const = 0;

	/**
	 * Get a party member by id
	 *
	 * @param LocalUserId - user making the request
	 * @param PartyId     - id of an existing party
	 * @param MemberId    - id of member to find
	 *
	 * @return party member info or nullptr if not found
	 */
	virtual FOnlinePartyMemberConstPtr GetPartyMember(const FUniqueNetId& LocalUserId, const FOnlinePartyId& PartyId, const FUniqueNetId& MemberId) const = 0;

	/**
	 * Get current cached data associated with a party
	 * FOnPartyDataReceived notification called whenever this data changes
	 *
	 * @param LocalUserId - user making the request
	 * @param PartyId     - id of an existing party
	 *
	 * @return party data or nullptr if not found
	 */
	virtual FOnlinePartyDataConstPtr GetPartyData(const FUniqueNetId& LocalUserId, const FOnlinePartyId& PartyId) const = 0;

	/**
	 * Get current cached data associated with a party member
	 * FOnPartyMemberDataReceived notification called whenever this data changes
	 *
	 * @param LocalUserId - user making the request
	 * @param PartyId     - id of an existing party
	 * @param MemberId    - id of member to find data for
	 *
	 * @return party member data or nullptr if not found
	 */
	virtual FOnlinePartyDataConstPtr GetPartyMemberData(const FUniqueNetId& LocalUserId, const FOnlinePartyId& PartyId, const FUniqueNetId& MemberId) const = 0;

	/**
	 * Get the join info of the specified user and party type
	 *
	 * @param LocalUserId       - user making the request
	 * @param UserId            - user to check
	 * @param PartyTypeId       - type of party to query
	 *
	 * @return shared pointer to the join info if the user is advertising for that party type
	 */
	virtual IOnlinePartyJoinInfoConstPtr GetAdvertisedParty(const FUniqueNetId& LocalUserId, const FUniqueNetId& UserId, const FOnlinePartyTypeId PartyTypeId) const = 0;

	/**
	 * Get a list of currently joined parties for the user
	 *
	 * @param LocalUserId     - user making the request
	 * @param OutPartyIdArray - list of party ids joined by the current user
	 *
	 * @return true if entries found
	 */
	virtual bool GetJoinedParties(const FUniqueNetId& LocalUserId, TArray<TSharedRef<const FOnlinePartyId>>& OutPartyIdArray) const = 0;

	/**
	 * Get list of current party members
	 *
	 * @param LocalUserId          - user making the request
	 * @param PartyId              - id of an existing party
	 * @param OutPartyMembersArray - list of party members currently in the party
	 *
	 * @return true if entries found
	 */
	virtual bool GetPartyMembers(const FUniqueNetId& LocalUserId, const FOnlinePartyId& PartyId, TArray<FOnlinePartyMemberConstRef>& OutPartyMembersArray) const = 0;
	UE_DEPRECATED(4.23, "Use GetPartyMembers that gives const FOnlinePartyMember")
	virtual bool GetPartyMembers(const FUniqueNetId& LocalUserId, const FOnlinePartyId& PartyId, TArray<TSharedRef<FOnlinePartyMember>>& OutPartyMembersArray) const;

	/**
	 * Get a list of parties the user has been invited to
	 *
	 * @param LocalUserId            - user making the request
	 * @param OutPendingInvitesArray - list of party info needed to join the party
	 *
	 * @return true if entries found
	 */
	virtual bool GetPendingInvites(const FUniqueNetId& LocalUserId, TArray<IOnlinePartyJoinInfoConstRef>& OutPendingInvitesArray) const = 0;
	UE_DEPRECATED(4.23, "Use GetPendingInvites that gives const IOnlinePartyJoinInfo")
	virtual bool GetPendingInvites(const FUniqueNetId& LocalUserId, TArray<TSharedRef<IOnlinePartyJoinInfo>>& OutPendingInvitesArray) const;

	/**
	 * Get list of users requesting to join the party
	 *
	 * @param LocalUserId           - user making the request
	 * @param PartyId               - id of an existing party
	 * @param OutPendingUserIdArray - list of pending party members
	 *
	 * @return true if entries found
	 */
	virtual bool GetPendingJoinRequests(const FUniqueNetId& LocalUserId, const FOnlinePartyId& PartyId, TArray<IOnlinePartyPendingJoinRequestInfoConstRef>& OutPendingJoinRequestArray) const = 0;
	UE_DEPRECATED(4.23, "Use GetPendingJoinRequests that gives const IOnlinePartyPendingJoinRequestInfo")
	virtual bool GetPendingJoinRequests(const FUniqueNetId& LocalUserId, const FOnlinePartyId& PartyId, TArray<TSharedRef<IOnlinePartyPendingJoinRequestInfo>>& OutPendingJoinRequestArray) const;

	/**
	 * Get list of users invited to a party that have not yet responded
	 *
	 * @param LocalUserId                - user making the request
	 * @param PartyId                    - id of an existing party
	 * @param OutPendingInvitedUserArray - list of user that have pending invites
	 *
	 * @return true if entries found
	 */
	virtual bool GetPendingInvitedUsers(const FUniqueNetId& LocalUserId, const FOnlinePartyId& PartyId, TArray<TSharedRef<const FUniqueNetId>>& OutPendingInvitedUserArray) const = 0;

	static const FOnlinePartyTypeId::TInternalType PrimaryPartyTypeIdValue = 0x11111111;
	/**
	 * @return party type id for the primary party - the primary party is the party that will be addressable via the social panel
	 */
	static const FOnlinePartyTypeId GetPrimaryPartyTypeId() { return FOnlinePartyTypeId(PrimaryPartyTypeIdValue); }

	/**
	 * @return party type id for the specified party
	 */
	static const FOnlinePartyTypeId MakePartyTypeId(const FOnlinePartyTypeId::TInternalType InTypeId) { ensure(InTypeId != PrimaryPartyTypeIdValue); return FOnlinePartyTypeId(InTypeId); }

	/**
	 * returns the json version of a join info for a current party
	 *
	 * @param LocalUserId       - user making the request
	 * @param PartyId           - party to make the json from
	 *
 	 */
	virtual FString MakeJoinInfoJson(const FUniqueNetId& LocalUserId, const FOnlinePartyId& PartyId) = 0;

	/**
	 * returns a valid join info object from a json blob
	 *
	 * @param JoinInfoJson       - json blob to convert
	 *
	 */
	virtual IOnlinePartyJoinInfoConstPtr MakeJoinInfoFromJson(const FString& JoinInfoJson) = 0;

	/**
	 * Creates a command line token from a IOnlinePartyJoinInfo object
	 *
	 * @param JoinInfo - the IOnlinePartyJoinInfo object to convert
	 *
	 * return the new IOnlinePartyJoinInfo object
	 */
	virtual FString MakeTokenFromJoinInfo(const IOnlinePartyJoinInfo& JoinInfo) const = 0;

	/**
	 * Creates a IOnlinePartyJoinInfo object from a command line token
	 *
	 * @param Token - the token string
	 *
	 * return the new IOnlinePartyJoinInfo object
	 */
	virtual IOnlinePartyJoinInfoConstPtr MakeJoinInfoFromToken(const FString& Token) const = 0;

	/**
	 * Checks to see if there is a pending command line invite and consumes it
	 *
	 * return the pending IOnlinePartyJoinInfo object
	 */
	virtual IOnlinePartyJoinInfoConstPtr ConsumePendingCommandLineInvite() = 0;

	/**
	 * List of all subscribe-able notifications
	 *
	 * OnPartyJoined
	 * OnPartyExited
	 * OnPartyStateChanged
	 * OnPartyPromotionLockoutStateChanged
	 * OnPartyConfigChanged
	 * OnPartyDataReceived
	 * OnPartyMemberPromoted
	 * OnPartyMemberExited
	 * OnPartyMemberJoined
	 * OnPartyMemberDataReceived
	 * OnPartyInvitesChanged
	 * OnPartyInviteRequestReceived
	 * OnPartyInviteReceived
	 * OnPartyInviteRemoved
	 * OnPartyInviteResponseReceived
	 * OnPartyJoinRequestReceived
	 * OnPartyQueryJoinabilityReceived
	 * OnFillPartyJoinRequestData
	 * OnPartyAnalyticsEvent
	 */

	/**
	 * notification of when a party is joined
	 * @param LocalUserId - id associated with this notification
	 * @param PartyId - id associated with the party
	 */
	DEFINE_ONLINE_DELEGATE_TWO_PARAM(OnPartyJoined, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/);

	/**
	 * notification of when a party is exited
	 * @param LocalUserId - id associated with this notification
	 * @param PartyId - id associated with the party
	 */
	DEFINE_ONLINE_DELEGATE_TWO_PARAM(OnPartyExited, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/);

	/**
	 * Notification when a party's state has changed
	 * @param LocalUserId - id associated with this notification
	 * @param PartyId - id associated with the party
	 * @param State - state of the party
	 * @param PreviousState - previous state of the party
	*/
	DEFINE_ONLINE_DELEGATE_FOUR_PARAM(OnPartyStateChanged, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, EPartyState /*State*/, EPartyState /*PreviousState*/);

	/**
	* notification of when a player had been approved to Join In Progress
	* @param LocalUserId - id associated with this notification
	* @param PartyId - id associated with the party
	*/
	DEFINE_ONLINE_DELEGATE_THREE_PARAM(OnPartyJIP, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, bool /*Success*/);

	/**
	 * Notification when player promotion is locked out.
	 *
	 * @param PartyId - id associated with the party
	 * @param bLockoutState - if promotion is currently locked out
	 */
	DEFINE_ONLINE_DELEGATE_THREE_PARAM(OnPartyPromotionLockoutChanged, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const bool /*bLockoutState*/);

	/**
	 * Notification when party data is updated
	 * @param LocalUserId - id associated with this notification
	 * @param PartyId - id associated with the party
	 * @param PartyConfig - party whose config was updated
	 */
	DEFINE_ONLINE_DELEGATE_THREE_PARAM_DEPRECATION_HELPER(OnPartyConfigChanged, OnPartyConfigChangedConst, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FPartyConfiguration& /*PartyConfig*/);
	UE_DEPRECATED(4.23, "Use OnPartyConfigChangedConst instead of OnPartyConfigChanged")
	virtual FDelegateHandle AddOnPartyConfigChangedDelegate_Handle(const FOnPartyConfigChangedDelegate& Delegate);

	/**
	 * Notification when party data is updated
	 * @param LocalUserId - id associated with this notification
	 * @param PartyId - id associated with the party
	 * @param PartyData - party data that was updated
	 */
	DEFINE_ONLINE_DELEGATE_THREE_PARAM_DEPRECATION_HELPER(OnPartyDataReceived, OnPartyDataReceivedConst, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FOnlinePartyData& /*PartyData*/);
	UE_DEPRECATED(4.23, "Use OnPartyDataReceivedConst instead of OnPartyDataReceived")
	virtual FDelegateHandle AddOnPartyDataReceivedDelegate_Handle(const FOnPartyDataReceivedDelegate& Delegate);

	/**
	* Notification when a member is promoted in a party
	* @param LocalUserId - id associated with this notification
	* @param PartyId - id associated with the party
	* @param NewLeaderId - id of member that was promoted
	*/
	DEFINE_ONLINE_DELEGATE_THREE_PARAM(OnPartyMemberPromoted, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FUniqueNetId& /*NewLeaderId*/);

	/**
	* Notification when a member exits a party
	* @param LocalUserId - id associated with this notification
	* @param PartyId - id associated with the party
	* @param MemberId - id of member that joined
	* @param Reason - why the member was removed
	*/
	DEFINE_ONLINE_DELEGATE_FOUR_PARAM(OnPartyMemberExited, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FUniqueNetId& /*MemberId*/, const EMemberExitedReason /*Reason*/);

	/**
	 * Notification when a member joins the party
	 * @param LocalUserId - id associated with this notification
	 * @param PartyId - id associated with the party
	 * @param MemberId - id of member that joined
	 */
	DEFINE_ONLINE_DELEGATE_THREE_PARAM(OnPartyMemberJoined, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FUniqueNetId& /*MemberId*/);

	/**
	 * Notification when party member data is updated
	 * @param LocalUserId - id associated with this notification
	 * @param PartyId - id associated with the party
	 * @param MemberId - id of member that had updated data
	 * @param PartyMemberData - party member data that was updated
	 */
	DEFINE_ONLINE_DELEGATE_FOUR_PARAM_DEPRECATION_HELPER(OnPartyMemberDataReceived, OnPartyMemberDataReceivedConst, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FUniqueNetId& /*MemberId*/, const FOnlinePartyData& /*PartyData*/);
	UE_DEPRECATED(4.23, "Use OnPartyMemberDataReceivedConst instead of OnPartyMemberDataReceived")
	virtual FDelegateHandle AddOnPartyMemberDataReceivedDelegate_Handle(const FOnPartyMemberDataReceivedDelegate& Delegate);

	/**
	 * Notification when an invite list has changed for a party
	 * @param LocalUserId - user that is associated with this notification
	 */
	DEFINE_ONLINE_DELEGATE_ONE_PARAM(OnPartyInvitesChanged, const FUniqueNetId& /*LocalUserId*/);

	/**
	* Notification when a request for an invite has been received
	* @param LocalUserId - id associated with this notification
	* @param PartyId - id associated with the party
	* @param SenderId - id of user that sent the invite
	* @param RequestForId - id of user that sender is requesting the invite for - invalid if the sender is requesting the invite
	*/
	DEFINE_ONLINE_DELEGATE_FOUR_PARAM(OnPartyInviteRequestReceived, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FUniqueNetId& /*SenderId*/, const FUniqueNetId& /*RequestForId*/);

	/**
	 * Notification when a new invite is received
	 * @param LocalUserId - id associated with this notification
	 * @param PartyId - id associated with the party
	 * @param SenderId - id of member that sent the invite
	 */
	DEFINE_ONLINE_DELEGATE_THREE_PARAM(OnPartyInviteReceived, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FUniqueNetId& /*SenderId*/);

	/**
	 * Notification when an invite has been removed
	 * @param LocalUserId id associated with this notification
	 * @param PartyId id associated with the party
	 * @param SenderId id of member that sent the invite
	 * @param Reason reason the invitation was removed
	 */
	DEFINE_ONLINE_DELEGATE_FOUR_PARAM(OnPartyInviteRemoved, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FUniqueNetId& /*SenderId*/, EPartyInvitationRemovedReason /*Reason*/);

	/**
	 * Notification when an invitation response is received
	 * @param LocalUserId - id associated with this notification
	 * @param PartyId - id associated with the party
	 * @param SenderId - id of user that responded to an invite
	 * @param Response - the response
	 */
	DEFINE_ONLINE_DELEGATE_FOUR_PARAM(OnPartyInviteResponseReceived, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FUniqueNetId& /*SenderId*/, const EInvitationResponse /*Response*/);

	/**
	 * Notification when a new reservation request is received
	 * Subscriber is expected to call ApproveJoinRequest
	 * @param LocalUserId - id associated with this notification
	 * @param PartyId - id associated with the party
	 * @param SenderId - id of member that sent the request
	 * @param Platform - platform of member that sent the request
	 * @param PartyData - data provided by the sender for the leader to use to determine joinability
	 */
	DEFINE_ONLINE_DELEGATE_FIVE_PARAM(OnPartyJoinRequestReceived, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FUniqueNetId& /*SenderId*/, const FString& /*Platform*/, const FOnlinePartyData& /*PartyData*/);

	/**
	* Notification when a new reservation request is received
	* Subscriber is expected to call ApproveJoinRequest
	* @param LocalUserId - id associated with this notification
	* @param PartyId - id associated with the party
	* @param SenderId - id of member that sent the request
	* @param Platform - platform of member that sent the request
	* @param PartyData - data provided by the sender for the leader to use to determine joinability
	*/
	DEFINE_ONLINE_DELEGATE_THREE_PARAM(OnPartyJIPRequestReceived, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FUniqueNetId& /*SenderId*/);

	/**
	 * Notification when a player wants to know if the party is in a joinable state
	 * Subscriber is expected to call RespondToQueryJoinability
	 * @param LocalUserId - id associated with this notification
	 * @param PartyId - id associated with the party
	 * @param SenderId - id of member that sent the request
	 * @param Platform - platform of member that sent the request
	 * @param PartyData - data provided by the sender for the leader to use to determine joinability
	 */
	DEFINE_ONLINE_DELEGATE_FIVE_PARAM(OnQueryPartyJoinabilityReceived, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, const FUniqueNetId& /*SenderId*/, const FString& /*Platform*/, const FOnlinePartyData& /*PartyData*/);

	/**
	 * Notification when a player wants to know if the party is in a joinable state
	 * Subscriber is expected to call RespondToQueryJoinability
	 * @param LocalUserId - id associated with this notification
	 * @param PartyId - id associated with the party
	 * @param PartyData - data provided by the sender for the leader to use to determine joinability
	 */
	DEFINE_ONLINE_DELEGATE_THREE_PARAM(OnFillPartyJoinRequestData, const FUniqueNetId& /*LocalUserId*/, const FOnlinePartyId& /*PartyId*/, FOnlinePartyData& /*PartyData*/);

	/**
	 * Notification when an analytics event needs to be recorded
	 * Subscriber is expected to record the event
	 * @param LocalUserId - id associated with this event
	 * @param EventName - name of the event
	 * @param Attributes - attributes for the event
	 */
	DEFINE_ONLINE_DELEGATE_THREE_PARAM(OnPartyAnalyticsEvent, const FUniqueNetId& /*LocalUserId*/, const FString& /*EventName*/, const TArray<FAnalyticsEventAttribute>& /*Attributes*/);

	/**
	* Notification of party system state change
	* @param NewState - new state this partysystem is in
	*/
	DEFINE_ONLINE_DELEGATE_ONE_PARAM(OnPartySystemStateChange, EPartySystemState /*NewState*/);

	/**
	 * Dump out party state for all known parties
	 */
	virtual void DumpPartyState() = 0;

};

enum class ECreatePartyCompletionResult : int8
{
	UnknownClientFailure = -100,
	AlreadyInPartyOfSpecifiedType,
	AlreadyCreatingParty,
	AlreadyInParty,
	FailedToCreateMucRoom,
	NoResponse,
	LoggedOut,
	UnknownInternalFailure = 0,
	Succeeded = 1
};

enum class EJoinPartyCompletionResult : int8
{
	/** Unspecified error.  No message sent to party leader. */
	UnknownClientFailure = -100,
	/** Your build id does not match the build id of the party */
	BadBuild,
	/** Your provided access key does not match the party's access key */
	InvalidAccessKey,
	/** The party leader already has you in the joining players list */
	AlreadyInLeadersJoiningList,
	/** The party leader already has you in the party members list */
	AlreadyInLeadersPartyRoster,
	/** The party leader rejected your join request because the party is full*/
	NoSpace,
	/** The party leader rejected your join request for a game specific reason, indicated by the NotApprovedReason parameter */
	NotApproved,
	/** The player you send the join request to is not a member of the specified party */
	RequesteeNotMember,
	/** The player you send the join request to is not the leader of the specified party */
	RequesteeNotLeader,
	/** A response was not received from the party leader in a timely manner, the join attempt is considered failed */
	NoResponse,
	/** You were logged out while attempting to join the party */
	LoggedOut,
	/** You were unable to rejoin the party */
	UnableToRejoin,
	/** Your platform is not compatible with the party */
	IncompatiblePlatform,

	/** We are currently waiting for a response for a previous join request for the specified party.  No message sent to party leader. */
	AlreadyJoiningParty,
	/** We are already in the party that you are attempting to join.  No message sent to the party leader. */
	AlreadyInParty,
	/** The party join info is invalid.  No message sent to the party leader. */
	JoinInfoInvalid,
	/** We are already in a party of the specified type.  No message sent to the party leader. */
	AlreadyInPartyOfSpecifiedType,
	/** Failed to send a message to the party leader.  No message sent to the party leader. */
	MessagingFailure,

	/** Game specific reason, indicated by the NotApprovedReason parameter.  Message might or might not have been sent to party leader. */
	GameSpecificReason,

	/** DEPRECATED */
	UnknownInternalFailure = 0,

	/** Successully joined the party */
	Succeeded = 1
};

enum class ELeavePartyCompletionResult : int8
{
	/** Unspecified error.  No message sent. */
	UnknownClientFailure = -100,
	/** Timed out waiting for a response to the message.  Party has been left. */
	NoResponse,
	/** You were logged out while attempting to leave the party.  Party has been left. */
	LoggedOut,

	/** You are not in the specified party.  No message sent. */
	UnknownParty,
	/** You are already leaving the party.  No message sent. */
	LeavePending,

	/** DEPRECATED! */
	UnknownLocalUser,
	/** DEPRECATED! */
	NotMember,
	/** DEPRECATED! */
	MessagingFailure,
	/** DEPRECATED! */
	UnknownTransportFailure,
	/** DEPRECATED! */
	UnknownInternalFailure = 0,

	/** Successfully left the party */
	Succeeded = 1
};

enum class EUpdateConfigCompletionResult : int8
{
	UnknownClientFailure = -100,
	UnknownParty,
	LocalMemberNotMember,
	LocalMemberNotLeader,
	RemoteMemberNotMember,
	MessagingFailure,
	NoResponse,
	UnknownInternalFailure = 0,
	Succeeded = 1
};

enum class ERequestPartyInvitationCompletionResult : int8
{
	NotLoggedIn = -100,
	InvitePending,
	AlreadyInParty,
	PartyFull,
	NoPermission,
	UnknownInternalFailure = 0,
	Succeeded = 1
};

enum class ESendPartyInvitationCompletionResult : int8
{
	NotLoggedIn = -100,
	InvitePending,
	AlreadyInParty,
	PartyFull,
	NoPermission,
	UnknownInternalFailure = 0,
	Succeeded = 1
};

enum class EAcceptPartyInvitationCompletionResult : int8
{
	NotLoggedIn = -100,
	InvitePending,
	AlreadyInParty,
	PartyFull,
	NoPermission,
	UnknownInternalFailure = 0,
	Succeeded = 1
};

enum class ERejectPartyInvitationCompletionResult : int8
{
	NotLoggedIn = -100,
	InvitePending,
	AlreadyInParty,
	PartyFull,
	NoPermission,
	UnknownInternalFailure = 0,
	Succeeded = 1
};

enum class EKickMemberCompletionResult : int8
{
	UnknownClientFailure = -100,
	UnknownParty,
	LocalMemberNotMember,
	LocalMemberNotLeader,
	RemoteMemberNotMember,
	MessagingFailure,
	NoResponse,
	LoggedOut,
	UnknownInternalFailure = 0,
	Succeeded = 1
};

enum class EPromoteMemberCompletionResult : int8
{
	UnknownClientFailure = -100,
	UnknownServiceFailure,
	UnknownParty,
	LocalMemberNotMember,
	LocalMemberNotLeader,
	PromotionAlreadyPending,
	TargetIsSelf,
	TargetNotMember,
	MessagingFailure,
	NoResponse,
	LoggedOut,
	UnknownInternalFailure = 0,
	Succeeded = 1
};

enum class EInvitationResponse : uint8
{
	UnknownFailure,
	BadBuild,
	Rejected,
	Accepted
};

/** @return the stringified version of the enum passed in */
ONLINESUBSYSTEM_API const TCHAR* ToString(const EPartyState Value);
/** @return the enum version of the string passed in */
ONLINESUBSYSTEM_API EPartyState EPartyStateFromString(const TCHAR* Value);

/** @return the stringified version of the enum passed in */
ONLINESUBSYSTEM_API const TCHAR* ToString(const EMemberExitedReason Value);
/** @return the stringified version of the enum passed in */
ONLINESUBSYSTEM_API const TCHAR* ToString(const EPartyInvitationRemovedReason Value);
/** @return the stringified version of the enum passed in */
ONLINESUBSYSTEM_API const TCHAR* ToString(const ECreatePartyCompletionResult Value);
/** @return the stringified version of the enum passed in */
ONLINESUBSYSTEM_API const TCHAR* ToString(const ESendPartyInvitationCompletionResult Value);
/** @return the stringified version of the enum passed in */
ONLINESUBSYSTEM_API const TCHAR* ToString(const EJoinPartyCompletionResult Value);
/** @return the stringified version of the enum passed in */
ONLINESUBSYSTEM_API const TCHAR* ToString(const ELeavePartyCompletionResult Value);
/** @return the stringified version of the enum passed in */
ONLINESUBSYSTEM_API const TCHAR* ToString(const EUpdateConfigCompletionResult Value);
/** @return the stringified version of the enum passed in */
ONLINESUBSYSTEM_API const TCHAR* ToString(const EKickMemberCompletionResult Value);
/** @return the stringified version of the enum passed in */
ONLINESUBSYSTEM_API const TCHAR* ToString(const EPromoteMemberCompletionResult Value);
/** @return the stringified version of the enum passed in */
ONLINESUBSYSTEM_API const TCHAR* ToString(const EInvitationResponse Value);

/** @return the stringified version of the enum passed in */
ONLINESUBSYSTEM_API const TCHAR* ToString(const PartySystemPermissions::EPermissionType Value);
/** @return the enum version of the string passed in */
ONLINESUBSYSTEM_API PartySystemPermissions::EPermissionType PartySystemPermissionTypeFromString(const TCHAR* Value);

/** @return the stringified version of the enum passed in */
ONLINESUBSYSTEM_API const TCHAR* ToString(const EJoinRequestAction Value);
/** @return the enum version of the string passed in */
ONLINESUBSYSTEM_API EJoinRequestAction JoinRequestActionFromString(const TCHAR* Value);

/** Dump party configuration for debugging */
ONLINESUBSYSTEM_API FString ToDebugString(const FPartyConfiguration& PartyConfiguration);
/** Dump join info for debugging */
ONLINESUBSYSTEM_API FString ToDebugString(const IOnlinePartyJoinInfo& JoinInfo);
/** Dump key/value pairs for debugging */
ONLINESUBSYSTEM_API FString ToDebugString(const FOnlineKeyValuePairs<FString, FVariantData>& KeyValAttrs);
/** Dump state about the party data for debugging */
ONLINESUBSYSTEM_API FString ToDebugString(const FOnlinePartyData& PartyData);