// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "SslModule.h"
#include "SslCertificateManager.h"
#include "SslManager.h"
#include "Ssl.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY(LogSsl);

// FHttpModule

IMPLEMENT_MODULE(FSslModule, SSL);

FSslModule* FSslModule::Singleton = NULL;

FSslModule::FSslModule()
	: CertificateManagerPtr(nullptr)
	, SslManagerPtr(nullptr)
{
}

bool FSslModule::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	bool bResult = false;

	// Ignore any execs that don't start with HTTP
	if (FParse::Command(&Cmd, TEXT("SSL")))
	{
		bResult = false;
	}

	return bResult;
}

void FSslModule::StartupModule()
{	
	Singleton = this;

#if WITH_SSL
	CertificateManagerPtr = new FSslCertificateManager();
	static_cast<FSslCertificateManager*>(CertificateManagerPtr)->BuildRootCertificateArray();

	SslManagerPtr = new FSslManager();
#endif //#if WITH_SSL
}

void FSslModule::ShutdownModule()
{
#if WITH_SSL
	static_cast<FSslCertificateManager*>(CertificateManagerPtr)->EmptyRootCertificateArray();
	delete CertificateManagerPtr;

	delete SslManagerPtr;
#endif // #if WITH_SSL

	Singleton = nullptr;
}

FSslModule& FSslModule::Get()
{
	if (Singleton == NULL)
	{
		check(IsInGameThread());
		FModuleManager::LoadModuleChecked<FSslModule>("SSL");
	}
	check(Singleton != NULL);
	return *Singleton;
}
