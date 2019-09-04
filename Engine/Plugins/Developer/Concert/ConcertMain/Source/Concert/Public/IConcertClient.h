// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "IConcertSession.h"
#include "ConcertMessages.h"
#include "ConcertTransportMessages.h"

class UConcertClientConfig;

class IConcertClient;
class IConcertClientConnectionTask;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnConcertClientSessionStartupOrShutdown, TSharedRef<IConcertClientSession>);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnConcertClientSessionGetPreConnectionTasks, const IConcertClient&, TArray<TUniquePtr<IConcertClientConnectionTask>>&);

/** Interface for tasks executed during the Concert client connection flow (eg, validation, creation, connection) */
class IConcertClientConnectionTask
{
public:
	virtual ~IConcertClientConnectionTask() = default;

	/**
	 * Execute this task.
	 * Typically this puts the task into a pending state, however it is possible for the task to immediately complete once executed. Ideally this should not block for a long time!
	 */
	virtual void Execute() = 0;

	/**
	 * Abort this task immediately, and discard any pending work.
	 * @note It is expected that GetStatus and GetError will return some kind of error state after this has been called.
	 */
	virtual void Abort() = 0;

	/**
	 * Tick this task, optionally requesting that it should gracefully cancel.
	 */
	virtual void Tick(const bool bShouldCancel) = 0;

	/**
	 * Get whether this task can be gracefully cancelled.
	 */
	virtual bool CanCancel() const = 0;

	/**
	 * Get the current status of this task.
	 * @note It is required that the task return Pending while it is in-progress, and Success when it has finished successfully. Any other status is treated as an error state, and GetError will be called.
	 */
	virtual EConcertResponseCode GetStatus() const = 0;

	/**
	 * Get the extended error status of this task that can be used in the error notification (if any).
	 */
	virtual FText GetError() const = 0;

	/**
	 * Get a description of this task that can be used in the progress notification (if any).
	 */
	virtual FText GetDescription() const = 0;
};

struct FConcertCreateSessionArgs
{
	/** The desired name for the session */
	FString SessionName;

	/** The override for the name used when archiving this session */
	FString ArchiveNameOverride;
};

struct FConcertRestoreSessionArgs
{
	/** True to auto-connect to the session after restoring it */
	bool bAutoConnect = true;

	/** The ID of the archived session to restore */
	FGuid SessionId;

	/** The desired name for new session */
	FString SessionName;

	/** The override for the name used when archiving this session */
	FString ArchiveNameOverride;

	/** The filter controlling which activities from the session should be restored */
	FConcertSessionFilter SessionFilter;
};

struct FConcertArchiveSessionArgs
{
	/** The ID of the archived session to archive */
	FGuid SessionId;

	/** The override for the name used when archiving the session */
	FString ArchiveNameOverride;

	/** The filter controlling which activities from the session should be archived */
	FConcertSessionFilter SessionFilter;
};

/** Interface for Concert client */
class IConcertClient
{
public:
	virtual ~IConcertClient() = default;

	/**
	 * Get the role of this client (eg, MultiUser, DisasterRecovery, etc)
	 */
	virtual const FString& GetRole() const = 0;

	/** 
	 * Configure the client settings and its information.
	 * @note If Configure() is called while the client is in a session, some settings may be applied only once the client leave the session.
	 */
	virtual void Configure(const UConcertClientConfig* InSettings) = 0;

	/**
	 * Return true if the client has been configured.
	 */
	virtual bool IsConfigured() const = 0;

	/**
	 * Return The configuration of this client, or null if it hasn't been configured.
	 */
	virtual const UConcertClientConfig* GetConfiguration() const = 0;

	/**
	 * Get the client information passed to Configure() if the client is not in a session, otherwise, returns
	 * the current session client info as returned by IConcertClientSession::GetLocalClientInfo().
	 */
	virtual const FConcertClientInfo& GetClientInfo() const = 0;

	/**
	 * Returns if the client has already been started up.
	 */
	virtual bool IsStarted() const = 0;

	/**
	 * Startup the client, this can be called multiple time
	 * Configure needs to be called before startup
	 */
	virtual void Startup() = 0;

	/**
	 * Shutdown the client, its discovery and session, if any.
	 * This can be called multiple time with no ill effect.
	 * However it depends on the UObject system so need to be called before its exit.
	 */
	virtual void Shutdown() = 0;
	
	/**
	 * Returns true if server discovery is enabled.
	 */
	virtual bool IsDiscoveryEnabled() const = 0;

	/**
	 * Start the discovery service for the client
	 * This will look for Concert server and populate the known servers list
	 * @see GetKnownServers
	 */
	virtual void StartDiscovery() = 0;

	/**
	 * Stop the discovery service for the client
	 */
	virtual void StopDiscovery() = 0;

	/**
	 * Returns true if the client is configured for auto connection.
	 */
	virtual bool CanAutoConnect() const = 0;

	/**
	 * Returns true if the client has an active auto connection routine.
	 */
	virtual bool IsAutoConnecting() const = 0;

	/**
	 * Start attempting to auto connect the client to the default session on the default server.
	 */
	virtual void StartAutoConnect() = 0;

	/**
	 * Stop the current auto connection if currently enabled.
	 */
	virtual void StopAutoConnect() = 0;

	/**
	 * Get the list of discovered server information
	 */
	virtual TArray<FConcertServerInfo> GetKnownServers() const = 0;

	/**
	 * Get the delegate callback for when the known server list is updated
	 */
	virtual FSimpleMulticastDelegate& OnKnownServersUpdated() = 0;

	/**
	 * Get the delegate that is called right before the client session startup
	 */
	virtual FOnConcertClientSessionStartupOrShutdown& OnSessionStartup() = 0;

	/**
	 * Get the delegate that is called right before the client session shutdown
	 */
	virtual FOnConcertClientSessionStartupOrShutdown& OnSessionShutdown() = 0;

	/**
	 * Get the delegate that is called to get the pre-connection tasks for a client session
	 */
	virtual FOnConcertClientSessionGetPreConnectionTasks& OnGetPreConnectionTasks() = 0;

	/**
	 * Get the delegate that is called when the session connection state changes
	 */
	virtual FOnConcertClientSessionConnectionChanged& OnSessionConnectionChanged() = 0;

	/**
	 * Get the connection status of client session or disconnected if no session is present
	 * @see EConcertConnectionStatus
	 */
	virtual EConcertConnectionStatus GetSessionConnectionStatus() const = 0;

	/** 
	 * Create a session on the server, matching the client configured settings.
	 * This also initiates the connection handshake for that session with the client.
	 * @param ServerAdminEndpointId	The Id of the Concert Server query endpoint
	 * @param CreateSessionArgs		The arguments that will be use for the creation of the session
	 * @return A future that will contains the final response code of the request
	 */
	virtual TFuture<EConcertResponseCode> CreateSession(const FGuid& ServerAdminEndpointId, const FConcertCreateSessionArgs& CreateSessionArgs) = 0;

	/**
	 * Join a session on the server, the settings of the sessions needs to be compatible with the client settings
	 * or the connection will be refused.
	 * @param ServerAdminEndpointId	The Id of the Concert Server query endpoint
	 * @param SessionId				The Id of the session
	 * @return  A future that will contains the final response code of the request
	 */
	virtual TFuture<EConcertResponseCode> JoinSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId) = 0;

	/**
	 * Restore an archived session on the server, matching the client configured settings.
	 * This also initiates the connection handshake for that session with the client when bAutoConnect is true in the RestoreSessionArgs.
	 * @param ServerAdminEndpointId	The Id of the Concert Server query endpoint
	 * @param RestoreSessionArgs	The arguments that will be use for the restoration of the session
	 * @return A future that will contains the final response code of the request
	 */
	virtual TFuture<EConcertResponseCode> RestoreSession(const FGuid& ServerAdminEndpointId, const FConcertRestoreSessionArgs& RestoreSessionArgs) = 0;

	/**
	 * Archive a live session on the server hosting the session.
	 * @param ServerAdminEndpointId	The Id of the Concert Server hosting the session (and where the archive will be created)
	 * @param ArchiveSessionArgs	The arguments that will be use for the archiving of the session
	 * @return A future that will contains the final response code of the request
	 */
	virtual TFuture<EConcertResponseCode> ArchiveSession(const FGuid& ServerAdminEndpointId, const FConcertArchiveSessionArgs& ArchiveSessionArgs) = 0;

	/**
	 * Rename a live or archived session if the client has the permission. The server automatically detects if the session is live or archived.
	 * If the client is not the owner the request will be refused.
	 * @param ServerAdminEndpointId	The Id of the Concert Server query endpoint
	 * @param SessionId				The Id of the live session to rename
	 * @param NewName				The new session name
	 * @return A future that will contains the final response code of the request
	 */
	virtual TFuture<EConcertResponseCode> RenameSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId, const FString& NewName) = 0;

	/**
	 * Delete a live or archived session from the server if the client is the owner of the session. The server automatically detects if the session is live or archived.
	 * If the client is not the owner the request will be refused.
	 * @param ServerAdminEndpointId	The Id of the Concert Server query endpoint
	 * @param SessionId				The Id of the session to delete
	 * @return A future that will contains the final response code of the request
	 */
	virtual TFuture<EConcertResponseCode> DeleteSession(const FGuid& ServerAdminEndpointId, const FGuid& SessionId) = 0;

	/** 
	 * Disconnect from the current session.
	 */
	virtual void DisconnectSession() = 0;

	/**
	 * Resume live-updates for the current session (must be paired with a call to SuspendSession).
	 */
	virtual void ResumeSession() = 0;

	/**
	 * Suspend live-updates for the current session.
	 */
	virtual void SuspendSession() = 0;

	/**
	 * Does the current session have live-updates suspended?
	 */
	virtual bool IsSessionSuspended() const = 0;

	/**
	 * Does the client think he is the owner of the session?
	 */
	virtual bool IsOwnerOf(const FConcertSessionInfo& InSessionInfo) const = 0;

	/**
	 * Get the current client session (if any).
	 */
	virtual TSharedPtr<IConcertClientSession> GetCurrentSession() const = 0;

	/** 
	 * Get the list of sessions available on a server
	 * @param ServerAdminEndpointId The Id of the Concert server admin endpoint
	 * @return A future for FConcertAdmin_GetAllSessionsResponse which contains a list of sessions
	 */
	virtual TFuture<FConcertAdmin_GetAllSessionsResponse> GetServerSessions(const FGuid& ServerAdminEndpointId) const = 0;

	/**
	 * Get the list of the live sessions data from a server
	 * @param ServerAdminEndpointId	The Id of the concert sever admin endpoint
	 * @return A future for FConcertAdmin_GetSessionsResponse which contains the list of the archived sessions.
	 */
	virtual TFuture<FConcertAdmin_GetSessionsResponse> GetLiveSessions(const FGuid& ServerAdminEndpointId) const = 0;

	/**
	 * Get the list of the archived sessions data from a server
	 * @param ServerAdminEndpointId	The Id of the concert sever admin endpoint
	 * @return A future for FConcertAdmin_GetSessionsResponse which contains the list of the archived sessions.
	 */
	virtual TFuture<FConcertAdmin_GetSessionsResponse> GetArchivedSessions(const FGuid& ServerAdminEndpointId) const = 0;

	/**
	 * Get the list of clients connected to a session on the server
	 * @param ServerAdminEndpointId	The Id of the Concert server admin endpoint
	 * @param SessionId				The Id of the session
	 * @return A future for FConcertAdmin_GetSessionClientsResponse which contains a list of session clients
	 */
	virtual TFuture<FConcertAdmin_GetSessionClientsResponse> GetSessionClients(const FGuid& ServerAdminEndpointId, const FGuid& SessionId) const = 0;

	/**
	 * Get the specified session activities, ordered by Activity ID (ascending) from a live or archived session without being connected to it. The function is used
	 * to explore the history of a session, for example to implement the disaster recovery scenario. It is possible to get the total number of activities in a
	 * session using -1 as ActivityCount. The response will contain the last Activity and its ID. To get the N last activities, set ActivityCount = -N.
	 * @param ServerAdminEndpointId	The Id of the Concert server admin endpoint
	 * @param SessionId				The Id of the session
	 * @param FromActivityId		The first activity ID to fetch (1-based) if ActivityCount is positive. Ignored if ActivityCount is negative.
	 * @param ActivityCount			If positive, request \a ActivityCount starting from \a FromActivityId. If negative, request the Abs(\a ActivityCount) last activities.
	 * @return A future for FConcertAdmin_GetSessionActivitiesResponse which contains up to Abs(ActivityCount) activities or an error if it fails.
	 */
	virtual TFuture<FConcertAdmin_GetSessionActivitiesResponse> GetSessionActivities(const FGuid& ServerAdminEndpointId, const FGuid& SessionId, int64 FromActivityId, int64 ActivityCount) const = 0;
};
