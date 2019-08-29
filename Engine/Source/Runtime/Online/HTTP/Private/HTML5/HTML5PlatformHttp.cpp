// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "HTML5/HTML5PlatformHttp.h"
#include "HTML5HTTP.h"


void FHTML5PlatformHttp::Init()
{
}


void FHTML5PlatformHttp::Shutdown()
{
}


IHttpRequest* FHTML5PlatformHttp::ConstructRequest()
{
	return new FHTML5HttpRequest();
}
