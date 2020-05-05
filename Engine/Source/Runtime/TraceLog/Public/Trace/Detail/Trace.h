// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Config.h"

#if UE_TRACE_ENABLED

#include "CoreTypes.h"

#define TRACE_PRIVATE_CHANNEL_DECLARE(LinkageType, ChannelName) \
	LinkageType Trace::FChannel ChannelName;

#define TRACE_PRIVATE_CHANNEL_IMPL(ChannelName) \
	struct F##ChannelName##Registrator \
	{ \
		F##ChannelName##Registrator() \
		{ \
			Trace::FChannel::Register(ChannelName, PREPROCESSOR_TO_STRING(ChannelName)); \
		} \
	}; \
	static F##ChannelName##Registrator ChannelName##Reg = F##ChannelName##Registrator();

#define TRACE_PRIVATE_CHANNEL(ChannelName) \
	TRACE_PRIVATE_CHANNEL_DECLARE(static, ChannelName) \
	TRACE_PRIVATE_CHANNEL_IMPL(ChannelName)

#define TRACE_PRIVATE_CHANNEL_EXTERN(ChannelName) \
	TRACE_PRIVATE_CHANNEL_DECLARE(extern, ChannelName)

#define TRACE_PRIVATE_CHANNEL_DEFINE(ChannelName) \
	TRACE_PRIVATE_CHANNEL_DECLARE(, ChannelName) \
	TRACE_PRIVATE_CHANNEL_IMPL(ChannelName)

#define TRACE_PRIVATE_CHANNELEXPR_IS_ENABLED(ChannelsExpr) \
	bool(ChannelsExpr)

#define TRACE_PRIVATE_EVENT_DEFINE(LoggerName, EventName) \
	Trace::FEventDef LoggerName##EventName##Event;

#define TRACE_PRIVATE_EVENT_BEGIN(LoggerName, EventName, ...) \
	TRACE_PRIVATE_EVENT_BEGIN_IMPL(static, LoggerName, EventName, ##__VA_ARGS__)

#define TRACE_PRIVATE_EVENT_BEGIN_EXTERN(LoggerName, EventName, ...) \
	TRACE_PRIVATE_EVENT_BEGIN_IMPL(extern, LoggerName, EventName, ##__VA_ARGS__)

#define TRACE_PRIVATE_EVENT_BEGIN_IMPL(LinkageType, LoggerName, EventName, ...) \
	LinkageType TRACE_PRIVATE_EVENT_DEFINE(LoggerName, EventName) \
	struct F##LoggerName##EventName##Fields \
	{ \
		enum \
		{ \
			Important			= Trace::FEventDef::Flag_Important, \
			NoSync				= Trace::FEventDef::Flag_NoSync, \
			PartialEventFlags	= (0, ##__VA_ARGS__), \
		}; \
		static void FORCENOINLINE Initialize() \
		{ \
			static const bool bOnceOnly = [] () \
			{ \
				F##LoggerName##EventName##Fields Fields; \
				const auto* Descs = (Trace::FFieldDesc*)(&Fields); \
				uint32 DescCount = uint32(sizeof(Fields) / sizeof(*Descs)); \
				const auto& LoggerLiteral = Trace::FLiteralName(#LoggerName); \
				const auto& EventLiteral = Trace::FLiteralName(#EventName); \
				Trace::FEventDef::Create(&LoggerName##EventName##Event, LoggerLiteral, EventLiteral, Descs, DescCount, EventFlags); \
				return true; \
			}(); \
		} \
		Trace::TField<0 /*Index*/, 0 /*Offset*/,

#define TRACE_PRIVATE_EVENT_FIELD(FieldType, FieldName) \
		FieldType> const FieldName = Trace::FLiteralName(#FieldName); \
		Trace::TField< \
			decltype(FieldName)::Index + 1, \
			decltype(FieldName)::Offset + decltype(FieldName)::Size,

#define TRACE_PRIVATE_EVENT_END() \
		Trace::EventProps> const EventProps_Private = {}; \
		Trace::TField<0, decltype(EventProps_Private)::Size, Trace::Attachment> const Attachment = {}; \
		explicit operator bool () const { return true; } \
		enum { EventFlags = PartialEventFlags|(decltype(EventProps_Private)::MaybeHasAux ? Trace::FEventDef::Flag_MaybeHasAux : 0), }; \
	};

#define TRACE_PRIVATE_EVENT_IS_INITIALIZED(LoggerName, EventName) \
	(LoggerName##EventName##Event.bInitialized || (F##LoggerName##EventName##Fields::Initialize(), true))

#define TRACE_PRIVATE_EVENT_IS_IMPORTANT(LoggerName, EventName) \
	(F##LoggerName##EventName##Fields::EventFlags & F##LoggerName##EventName##Fields::Important)

#define TRACE_PRIVATE_EVENT_SIZE(LoggerName, EventName) \
	decltype(F##LoggerName##EventName##Fields::EventProps_Private)::Size

#define TRACE_PRIVATE_LOG(LoggerName, EventName, ChannelsExpr, ...) \
	if ((TRACE_PRIVATE_CHANNELEXPR_IS_ENABLED(ChannelsExpr) || TRACE_PRIVATE_EVENT_IS_IMPORTANT(LoggerName, EventName)) \
		&& TRACE_PRIVATE_EVENT_IS_INITIALIZED(LoggerName, EventName)) \
		if (const auto& __restrict EventName = (F##LoggerName##EventName##Fields&)LoggerName##EventName##Event) \
			if (auto LogScope = Trace::FEventDef::FLogScope( \
				LoggerName##EventName##Event.Uid, \
				TRACE_PRIVATE_EVENT_SIZE(LoggerName, EventName), \
				F##LoggerName##EventName##Fields::EventFlags, \
				##__VA_ARGS__)) \
					LogScope

#else

#define TRACE_PRIVATE_CHANNEL(ChannelName)

#define TRACE_PRIVATE_CHANNEL_EXTERN(ChannelName)

#define TRACE_PRIVATE_CHANNEL_DEFINE(ChannelName)

#define TRACE_PRIVATE_CHANNELEXPR_IS_ENABLED(ChannelsExpr) \
	false

#define TRACE_PRIVATE_EVENT_DEFINE(LoggerName, EventName)

#define TRACE_PRIVATE_EVENT_BEGIN(LoggerName, EventName, ...) \
	TRACE_PRIVATE_EVENT_BEGIN_IMPL(LoggerName, EventName)

#define TRACE_PRIVATE_EVENT_BEGIN_EXTERN(LoggerName, EventName) \
	TRACE_PRIVATE_EVENT_BEGIN_IMPL(LoggerName, EventName)

#define TRACE_PRIVATE_EVENT_BEGIN_IMPL(LoggerName, EventName) \
	struct F##LoggerName##EventName##Dummy \
	{ \
		struct FTraceDisabled \
		{ \
			const FTraceDisabled& operator () (...) const { return *this; } \
		}; \
		const F##LoggerName##EventName##Dummy& operator << (const FTraceDisabled&) const \
		{ \
			return *this; \
		} \
		explicit operator bool () const { return false; }

#define TRACE_PRIVATE_EVENT_FIELD(FieldType, FieldName) \
		const FTraceDisabled& FieldName;

#define TRACE_PRIVATE_EVENT_END() \
		const FTraceDisabled& Attachment; \
	};

#define TRACE_PRIVATE_LOG(LoggerName, EventName, ...) \
	if (const auto& EventName = *(F##LoggerName##EventName##Dummy*)1) \
		EventName

#endif // UE_TRACE_ENABLED
