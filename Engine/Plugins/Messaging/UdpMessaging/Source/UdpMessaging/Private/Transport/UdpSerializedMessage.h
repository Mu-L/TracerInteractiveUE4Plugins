// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Delegates/Delegate.h"
#include "Serialization/Archive.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"


/**
 * Enumerates possibly states of a serialized message.
 *
 * @see FUdpSerializedMessage
 */
enum class EUdpSerializedMessageState
{
	/** The message data is complete. */
	Complete,

	/** The message data is incomplete. */
	Incomplete,

	/** The message data is invalid. */
	Invalid
};


/**
 * Holds serialized message data.
 */
class FUdpSerializedMessage
	: public FMemoryWriter
{
public:

	/** Default constructor. */
	FUdpSerializedMessage()
		: FMemoryWriter(DataArray, true)
		, State(EUdpSerializedMessageState::Incomplete)
	{ }

public:

	/**
	 * Creates an archive reader to the data.
	 *
	 * The caller is responsible for deleting the returned object.
	 *
	 * @return An archive reader.
	 */
	FArchive* CreateReader()
	{
		return new FMemoryReader(DataArray, true);
	}

	/**
	 * Get the serialized message data.
	 *
	 * @return Byte array of message data.
	 * @see GetState
	 */
	const TArray<uint8>& GetDataArray()
	{
		return DataArray;
	}

	/**
	 * Gets the state of the message data.
	 *
	 * @return Message data state.
	 * @see GetData, UpdateState
	 */
	EUdpSerializedMessageState GetState() const
	{
		return State;
	}

	/**
	 * Updates the state of this message data.
	 *
	 * @param InState The state to set.
	 * @see GetState
	 */
	void UpdateState(EUdpSerializedMessageState InState)
	{
		State = InState;
	}

private:

	/** Holds the serialized data. */
	TArray<uint8> DataArray;

	/** Holds the message data state. */
	EUdpSerializedMessageState State;
};
