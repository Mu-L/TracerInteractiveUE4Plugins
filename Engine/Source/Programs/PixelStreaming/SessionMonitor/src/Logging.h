// Copyright Epic Games, Inc. All Rights Reserved.

/**
 *
 * Logging framework very similar to UE4's own logging framework
 *
 */
#pragma once

#include "SessionMonitorCommon.h"

enum class ELogVerbosity : uint8_t
{
	None,
	Fatal,
	Error,
	Warning,
	Log,
	Verbose,
	VeryVerbose
};

const char* LogVerbosityToString(ELogVerbosity v);

class FLogCategoryBase
{
public:
	FLogCategoryBase(const char* Name, ELogVerbosity Verbosity, ELogVerbosity CompileTimeVerbosity);

	//! Tells if a log message of the specified verbosity should be suppressed or logged
	bool IsSuppressed(ELogVerbosity V) const;

	//! Set runtime verbosity
	void SetVerbosity(ELogVerbosity V);

	ELogVerbosity Verbosity;
	ELogVerbosity CompileTimeVerbosity;
	std::string Name;
};

template <ELogVerbosity DEFAULT_VERBOSITY, ELogVerbosity COMPILETIME_VERBOSITY>
class FLogCategory : public FLogCategoryBase
{
public:
	FLogCategory(const char* Name)
	    : FLogCategoryBase(Name, DEFAULT_VERBOSITY, COMPILETIME_VERBOSITY)
	{
	}

	enum
	{
		CompileTimeVerbosity = (int)COMPILETIME_VERBOSITY
	};
};

/**
 * Interface for log outputs.
 * NOTE: Classes that implement this interface get automatically registered as
 * a log output, and unregistered when destroyed
 */
class ILogOutput
{
public:
	ILogOutput();
	virtual ~ILogOutput();

	static void LogToAll(
	    const char* File, int Line, const FLogCategoryBase* Category, ELogVerbosity Verbosity,
	    _Printf_format_string_ const char* Fmt, ...);

	virtual void
	Log(const char* File, int Line, const FLogCategoryBase* Category, ELogVerbosity Verbosity, const char* Msg) = 0;

private:
	struct FSharedData
	{
		std::mutex Mtx;
		std::vector<ILogOutput*> Outputs;
	};
	static FSharedData* GetSharedData();
};

#define EG_LOG_MINIMUM_VERBOSITY VeryVerbose

#define EG_DECLARE_LOG_CATEGORY(NAME, DEFAULT_VERBOSITY, COMPILETIME_VERBOSITY)                             \
	extern class FLogCategory##NAME                                                                         \
	    : public ::FLogCategory<::ELogVerbosity::DEFAULT_VERBOSITY, ::ELogVerbosity::COMPILETIME_VERBOSITY> \
	{                                                                                                       \
	public:                                                                                                 \
		FLogCategory##NAME()                                                                                \
		    : FLogCategory(#NAME)                                                                           \
		{                                                                                                   \
		}                                                                                                   \
	} NAME;

#define EG_DEFINE_LOG_CATEGORY(NAME) FLogCategory##NAME NAME;

#define EG_LOG_CHECK_COMPILETIME_VERBOSITY(NAME, VERBOSITY)                           \
	(((int)::ELogVerbosity::VERBOSITY <= FLogCategory##NAME::CompileTimeVerbosity) && \
	 ((int)::ELogVerbosity::VERBOSITY <= (int)::ELogVerbosity::EG_LOG_MINIMUM_VERBOSITY))

#define EG_LOG(NAME, VERBOSITY, Fmt, ...)                                                                          \
	{                                                                                                              \
		if constexpr (EG_LOG_CHECK_COMPILETIME_VERBOSITY(NAME, VERBOSITY))                                         \
		{                                                                                                          \
			if (!NAME.IsSuppressed(::ELogVerbosity::VERBOSITY))                                                    \
			{                                                                                                      \
				::ILogOutput::LogToAll(__FILE__, __LINE__, &NAME, ::ELogVerbosity::VERBOSITY, Fmt, ##__VA_ARGS__); \
				if (::ELogVerbosity::VERBOSITY == ::ELogVerbosity::Fatal)                                          \
				{                                                                                                  \
					::DoAssert(__FILE__, __LINE__, Fmt, ##__VA_ARGS__);                                            \
				}                                                                                                  \
			}                                                                                                      \
		}                                                                                                          \
	}

EG_DECLARE_LOG_CATEGORY(LogDefault, Log, VeryVerbose)
