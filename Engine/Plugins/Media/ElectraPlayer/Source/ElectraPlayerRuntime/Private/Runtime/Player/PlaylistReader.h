// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"
#include "Player/PlayerSessionServices.h"
#include "StreamTypes.h"
#include "Player/Playlist.h"
#include "ParameterDictionary.h"



namespace Electra
{
	// Forward declarations
	class IManifest;

	class IPlaylistReader
	{
	public:
		virtual ~IPlaylistReader() = default;

		/**
		 * Returns the type of the playlist (eg. "hls", "dash", etc.)
		 */
		virtual const FString& GetPlaylistType() const = 0;

		/**
		 * Loads and parses the playlist.
		 *
		 * @param URL     URL of the playlist to load
		 * @param Preferences
		 *                User preferences (for initial stream selection)
		 * @param Options Options for the playlist reader and parser specific to the format.
		 *                See specifics in the format's implementation.
		 */
		virtual void LoadAndParse(const FString& URL, const FStreamPreferences& Preferences, const FParamDict& Options) = 0;

		/**
		 * Returns the URL from which the playlist was loaded (or supposed to be loaded).
		 *
		 * @return The playlist URL
		 */
		virtual FString GetURL() const = 0;


		/**
		 * Returns the manifest interface to access the playlist in a uniform way.
		 *
		 * @return
		 */
		virtual TSharedPtrTS<IManifest> GetManifest() = 0;


		class PlaylistDownloadMessage : public IPlayerMessage
		{
		public:
			static TSharedPtrTS<IPlayerMessage> Create(const HTTP::FConnectionInfo* ConnectionInfo, Playlist::EListType InListType, Playlist::ELoadType InLoadType)
			{
				TSharedPtrTS<PlaylistDownloadMessage> p(new PlaylistDownloadMessage(ConnectionInfo, InListType, InLoadType));
				return p;
			}

			static const FString& Type()
			{
				static FString TypeName("PlaylistDownload");
				return TypeName;
			}

			virtual const FString& GetType() const
			{
				return Type();
			}

			Playlist::EListType GetListType() const
			{
				return ListType;
			}

			Playlist::ELoadType GetLoadType() const
			{
				return LoadType;
			}

			const HTTP::FConnectionInfo& GetConnectionInfo() const
			{
				return ConnectionInfo;
			}

		private:
			PlaylistDownloadMessage(const HTTP::FConnectionInfo* InConnectionInfo, Playlist::EListType InListType, Playlist::ELoadType InLoadType)
				: ListType(InListType)
				, LoadType(InLoadType)
			{
				if (InConnectionInfo)
				{
					// Have to make a dedicated copy of the connection info in order to get a copy of the retry info at this point in time.
					ConnectionInfo.CopyFrom(*InConnectionInfo);
					int32 y = 0;
				}
			}
			HTTP::FConnectionInfo	ConnectionInfo;
			Playlist::EListType		ListType;
			Playlist::ELoadType		LoadType;
		};


		class PlaylistLoadedMessage : public IPlayerMessage
		{
		public:
			static TSharedPtrTS<IPlayerMessage> Create(const FErrorDetail& PlayerResult, const HTTP::FConnectionInfo* ConnectionInfo, Playlist::EListType InListType, Playlist::ELoadType InLoadType)
			{
				TSharedPtrTS<PlaylistLoadedMessage> p(new PlaylistLoadedMessage(PlayerResult, ConnectionInfo, InListType, InLoadType));
				return p;
			}

			static const FString& Type()
			{
				static FString TypeName("PlaylistLoaded");
				return TypeName;
			}

			virtual const FString& GetType() const
			{
				return Type();
			}

			const FErrorDetail& GetResult() const
			{
				return Result;
			}

			Playlist::EListType GetListType() const
			{
				return ListType;
			}

			Playlist::ELoadType GetLoadType() const
			{
				return LoadType;
			}

			const HTTP::FConnectionInfo& GetConnectionInfo() const
			{
				return ConnectionInfo;
			}

		private:
			PlaylistLoadedMessage(const FErrorDetail& PlayerResult, const HTTP::FConnectionInfo* InConnectionInfo, Playlist::EListType InListType, Playlist::ELoadType InLoadType)
				: Result(PlayerResult)
				, ListType(InListType)
				, LoadType(InLoadType)
			{
				if (InConnectionInfo)
				{
					// Have to make a dedicated copy of the connection info in order to get a copy of the retry info at this point in time.
					ConnectionInfo.CopyFrom(*InConnectionInfo);
				}
			}
			HTTP::FConnectionInfo	ConnectionInfo;
			FErrorDetail			Result;
			Playlist::EListType		ListType;
			Playlist::ELoadType		LoadType;
		};

	};


} // namespace Electra


