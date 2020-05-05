// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Android/AndroidPlatform.h"

#if USE_ANDROID_OPENGL

#include "OpenGLDrvPrivate.h"
#include "AndroidOpenGL.h"
#include "OpenGLDrvPrivate.h"
#include "OpenGLES.h"
#include "Android/AndroidWindow.h"
#include "AndroidOpenGLPrivate.h"
#include "Android/AndroidPlatformMisc.h"
#include "Android/AndroidPlatformFramePacer.h"


PFNeglPresentationTimeANDROID eglPresentationTimeANDROID_p = NULL;
PFNeglGetNextFrameIdANDROID eglGetNextFrameIdANDROID_p = NULL;
PFNeglGetCompositorTimingANDROID eglGetCompositorTimingANDROID_p = NULL;
PFNeglGetFrameTimestampsANDROID eglGetFrameTimestampsANDROID_p = NULL;
PFNeglQueryTimestampSupportedANDROID eglQueryTimestampSupportedANDROID_p = NULL;
PFNeglQueryTimestampSupportedANDROID eglGetCompositorTimingSupportedANDROID_p = NULL;
PFNeglQueryTimestampSupportedANDROID eglGetFrameTimestampsSupportedANDROID_p = NULL;

PFNGLFRAMEBUFFERFETCHBARRIERQCOMPROC glFramebufferFetchBarrierQCOM = NULL;

int32 FAndroidOpenGL::GLMajorVerion = 0;
int32 FAndroidOpenGL::GLMinorVersion = 0;

bool FAndroidOpenGL::bSupportsImageExternal = false;
bool FAndroidOpenGL::bRequiresAdrenoTilingHint = false;

static TAutoConsoleVariable<int32> CVarEnableAdrenoTilingHint(
	TEXT("r.Android.EnableAdrenoTilingHint"),
	1,
	TEXT("Whether Adreno-based Android devices should hint to the driver to use tiling mode for the mobile base pass.\n")
	TEXT("  0 = hinting disabled\n")
	TEXT("  1 = hinting enabled for Adreno devices running Andorid 8 or earlier [default]\n")
	TEXT("  2 = hinting always enabled for Adreno devices\n"));

static TAutoConsoleVariable<int32> CVarDisableEarlyFragmentTests(
	TEXT("r.Android.DisableEarlyFragmentTests"),
	0,
	TEXT("Whether to disable early_fragment_tests if any \n"),
	ECVF_ReadOnly);

static TAutoConsoleVariable<int32> CVarDisableFBFNonCoherent(
	TEXT("r.Android.DisableFBFNonCoherent"),
	0,
	TEXT("Whether to disable usage of QCOM_shader_framebuffer_fetch_noncoherent extension\n"),
	ECVF_ReadOnly);

struct FPlatformOpenGLDevice
{

	void SetCurrentSharedContext();
	void SetCurrentRenderingContext();
	void SetupCurrentContext();
	void SetCurrentNULLContext();

	FPlatformOpenGLDevice();
	~FPlatformOpenGLDevice();
	void Init();
	void LoadEXT();
	void Terminate();
	void ReInit();
};


FPlatformOpenGLDevice::~FPlatformOpenGLDevice()
{
	FPlatformRHIFramePacer::Destroy();

	FAndroidAppEntry::ReleaseEGL();
}

FPlatformOpenGLDevice::FPlatformOpenGLDevice()
{
}

// call out to JNI to see if the application was packaged for Oculus Mobile
extern bool AndroidThunkCpp_IsOculusMobileApplication();


// RenderDoc
#define GL_DEBUG_TOOL_EXT	0x6789
static bool bRunningUnderRenderDoc = false;

void FPlatformOpenGLDevice::Init()
{
	// Initialize frame pacer
	FPlatformRHIFramePacer::Init(new FAndroidOpenGLFramePacer());

	extern void InitDebugContext();

	bRunningUnderRenderDoc = glIsEnabled(GL_DEBUG_TOOL_EXT) != GL_FALSE;

	FPlatformMisc::LowLevelOutputDebugString(TEXT("FPlatformOpenGLDevice:Init"));
	bool bCreateSurface = !AndroidThunkCpp_IsOculusMobileApplication();
	AndroidEGL::GetInstance()->InitSurface(false, bCreateSurface);

	LoadEXT();
	PlatformRenderingContextSetup(this);

	InitDefaultGLContextState();
	InitDebugContext();

	PlatformSharedContextSetup(this);
	InitDefaultGLContextState();
	InitDebugContext();

	AndroidEGL::GetInstance()->InitBackBuffer(); //can be done only after context is made current.
}

FPlatformOpenGLDevice* PlatformCreateOpenGLDevice()
{
	FPlatformOpenGLDevice* Device = new FPlatformOpenGLDevice();
	Device->Init();
	return Device;
}

bool PlatformCanEnableGPUCapture()
{
	return bRunningUnderRenderDoc;
}

void PlatformReleaseOpenGLContext(FPlatformOpenGLDevice* Device, FPlatformOpenGLContext* Context)
{
}

void* PlatformGetWindow(FPlatformOpenGLContext* Context, void** AddParam)
{
	check(Context);

	return (void*)&Context->eglContext;
}

bool PlatformBlitToViewport(FPlatformOpenGLDevice* Device, const FOpenGLViewport& Viewport, uint32 BackbufferSizeX, uint32 BackbufferSizeY, bool bPresent,bool bLockToVsync )
{
	if (bPresent && Viewport.GetCustomPresent())
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FAndroidOpenGL_PlatformBlitToViewport_CustomPresent);
		int32 SyncInterval = FAndroidPlatformRHIFramePacer::GetLegacySyncInterval();
		bPresent = Viewport.GetCustomPresent()->Present(SyncInterval);
	}
	if (bPresent)
	{
		FAndroidPlatformRHIFramePacer::SwapBuffers(bLockToVsync);
	}
	static IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("a.UseFrameTimeStampsForPacing"));
	const bool bForceGPUFence = CVar ? CVar->GetInt() != 0 : false;



	return bPresent && ShouldUseGPUFencesToLimitLatency();
}

void PlatformRenderingContextSetup(FPlatformOpenGLDevice* Device)
{
	Device->SetCurrentRenderingContext();
	Device->SetupCurrentContext();
}

void PlatformFlushIfNeeded()
{
}

void PlatformRebindResources(FPlatformOpenGLDevice* Device)
{
}

void PlatformSharedContextSetup(FPlatformOpenGLDevice* Device)
{
	Device->SetCurrentSharedContext();
	Device->SetupCurrentContext();
}

void PlatformNULLContextSetup()
{
	AndroidEGL::GetInstance()->ReleaseContextOwnership();
}

EOpenGLCurrentContext PlatformOpenGLCurrentContext(FPlatformOpenGLDevice* Device)
{
	return (EOpenGLCurrentContext)AndroidEGL::GetInstance()->GetCurrentContextType();
}

void* PlatformOpenGLCurrentContextHandle(FPlatformOpenGLDevice* Device)
{
	return AndroidEGL::GetInstance()->GetCurrentContext();
}

void PlatformRestoreDesktopDisplayMode()
{
}

bool PlatformInitOpenGL()
{
	check(!FAndroidMisc::ShouldUseVulkan());

	{
		// determine ES version. PlatformInitOpenGL happens before ProcessExtensions and therefore FAndroidOpenGL::bES31Support.
		FString FullVersionString, VersionString, SubVersionString;
		FAndroidGPUInfo::Get().GLVersion.Split(TEXT("OpenGL ES "), nullptr, &FullVersionString);
		FullVersionString.Split(TEXT(" "), &FullVersionString, nullptr);
		FullVersionString.Split(TEXT("."), &VersionString, &SubVersionString);
		FAndroidOpenGL::GLMajorVerion = FCString::Atoi(*VersionString);
		FAndroidOpenGL::GLMinorVersion = FCString::Atoi(*SubVersionString);

		bool bES31Supported = FAndroidOpenGL::GLMajorVerion == 3 && FAndroidOpenGL::GLMinorVersion >= 1;
		static const auto CVarDisableES31 = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Android.DisableOpenGLES31Support"));

		bool bBuildForES31 = false;
		GConfig->GetBool(TEXT("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings"), TEXT("bBuildForES31"), bBuildForES31, GEngineIni);

		const bool bSupportsFloatingPointRTs = FAndroidMisc::SupportsFloatingPointRenderTargets();

		if (bBuildForES31 && bES31Supported)
		{
			FOpenGLES::CurrentFeatureLevelSupport = FAndroidOpenGL::GLMinorVersion >= 2 ? FOpenGLES::EFeatureLevelSupport::ES32 : FOpenGLES::EFeatureLevelSupport::ES31;
			UE_LOG(LogRHI, Log, TEXT("App is packaged for OpenGL ES 3.1 and an ES %d.%d-capable device was detected. Reinitializing OpenGL ES with a %d.%d context."),
				FAndroidOpenGL::GLMajorVerion, FAndroidOpenGL::GLMinorVersion, FAndroidOpenGL::GLMajorVerion, FAndroidOpenGL::GLMinorVersion);

			FAndroidAppEntry::ReleaseEGL();
			// Re-init gles for 3.1/3.2
			AndroidEGL::GetInstance()->Init(AndroidEGL::AV_OpenGLES, FAndroidOpenGL::GLMajorVerion, FAndroidOpenGL::GLMinorVersion, false);
		}
		else
		{
			FString Message = TEXT("");

			if (bES31Supported)
			{
				Message.Append(TEXT("This device does not support Vulkan but the app was not packaged with ES 3.1 support."));
				if (FAndroidMisc::GetAndroidBuildVersion() < 26)
				{
					Message.Append(TEXT(" Updating to a newer Android version may resolve this issue."));
				}
				FPlatformMisc::LowLevelOutputDebugString(*Message);
				FAndroidMisc::MessageBoxExt(EAppMsgType::Ok, *Message, TEXT("Unable to run on this device!"));
			}
			else
			{
				Message.Append(TEXT("This device only supports OpenGL ES 2/3 which is not supported, only supports ES 3.1+ "));
				FPlatformMisc::LowLevelOutputDebugString(*Message);
				FAndroidMisc::MessageBoxExt(EAppMsgType::Ok, *Message, TEXT("Unable to run on this device!"));
			}
		}
	}
	return true;
}

bool PlatformOpenGLContextValid()
{
	return AndroidEGL::GetInstance()->IsCurrentContextValid();
}

void PlatformGetBackbufferDimensions( uint32& OutWidth, uint32& OutHeight )
{
	AndroidEGL::GetInstance()->GetDimensions(OutWidth, OutHeight);
}

// =============================================================

void PlatformGetNewOcclusionQuery( GLuint* OutQuery, uint64* OutQueryContext )
{
}

bool PlatformContextIsCurrent( uint64 QueryContext )
{
	return true;
}

void FPlatformOpenGLDevice::LoadEXT()
{
	glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)((void*)eglGetProcAddress("glGenVertexArrays"));
	glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)((void*)eglGetProcAddress("glBindVertexArray"));
	eglGetSystemTimeNV_p = (PFNEGLGETSYSTEMTIMENVPROC)((void*)eglGetProcAddress("eglGetSystemTimeNV"));
	eglCreateSyncKHR_p = (PFNEGLCREATESYNCKHRPROC)((void*)eglGetProcAddress("eglCreateSyncKHR"));
	eglDestroySyncKHR_p = (PFNEGLDESTROYSYNCKHRPROC)((void*)eglGetProcAddress("eglDestroySyncKHR"));
	eglClientWaitSyncKHR_p = (PFNEGLCLIENTWAITSYNCKHRPROC)((void*)eglGetProcAddress("eglClientWaitSyncKHR"));
	eglGetSyncAttribKHR_p = (PFNEGLGETSYNCATTRIBKHRPROC)((void*)eglGetProcAddress("eglGetSyncAttribKHR"));

	eglPresentationTimeANDROID_p = (PFNeglPresentationTimeANDROID)((void*)eglGetProcAddress("eglPresentationTimeANDROID"));
	eglGetNextFrameIdANDROID_p = (PFNeglGetNextFrameIdANDROID)((void*)eglGetProcAddress("eglGetNextFrameIdANDROID"));
	eglGetCompositorTimingANDROID_p = (PFNeglGetCompositorTimingANDROID)((void*)eglGetProcAddress("eglGetCompositorTimingANDROID"));
	eglGetFrameTimestampsANDROID_p = (PFNeglGetFrameTimestampsANDROID)((void*)eglGetProcAddress("eglGetFrameTimestampsANDROID"));
	eglQueryTimestampSupportedANDROID_p = (PFNeglQueryTimestampSupportedANDROID)((void*)eglGetProcAddress("eglQueryTimestampSupportedANDROID"));
	eglGetCompositorTimingSupportedANDROID_p = (PFNeglQueryTimestampSupportedANDROID)((void*)eglGetProcAddress("eglGetCompositorTimingSupportedANDROID"));
	eglGetFrameTimestampsSupportedANDROID_p = (PFNeglQueryTimestampSupportedANDROID)((void*)eglGetProcAddress("eglGetFrameTimestampsSupportedANDROID"));

	const TCHAR* NotAvailable = TEXT("NOT Available");
	const TCHAR* Present = TEXT("Present");

	UE_LOG(LogRHI, Log, TEXT("Extension %s %s"), TEXT("eglPresentationTimeANDROID"), eglPresentationTimeANDROID_p  ? Present : NotAvailable);
	UE_LOG(LogRHI, Log, TEXT("Extension %s %s"), TEXT("eglGetNextFrameIdANDROID"), eglGetNextFrameIdANDROID_p ? Present : NotAvailable);
	UE_LOG(LogRHI, Log, TEXT("Extension %s %s"), TEXT("eglGetCompositorTimingANDROID"), eglGetCompositorTimingANDROID_p  ? Present : NotAvailable);
	UE_LOG(LogRHI, Log, TEXT("Extension %s %s"), TEXT("eglGetFrameTimestampsANDROID"), eglGetFrameTimestampsANDROID_p  ? Present : NotAvailable);
	UE_LOG(LogRHI, Log, TEXT("Extension %s %s"), TEXT("eglQueryTimestampSupportedANDROID"), eglQueryTimestampSupportedANDROID_p ? Present : NotAvailable);
	UE_LOG(LogRHI, Log, TEXT("Extension %s %s"), TEXT("eglGetCompositorTimingSupportedANDROID"), eglGetCompositorTimingSupportedANDROID_p ? Present : NotAvailable);
	UE_LOG(LogRHI, Log, TEXT("Extension %s %s"), TEXT("eglGetFrameTimestampsSupportedANDROID"), eglGetFrameTimestampsSupportedANDROID_p ? Present : NotAvailable);

	glDebugMessageControlKHR = (PFNGLDEBUGMESSAGECONTROLKHRPROC)((void*)eglGetProcAddress("glDebugMessageControlKHR"));

	// Some PowerVR drivers (Rogue Han and Intel-based devices) are crashing using glDebugMessageControlKHR (causes signal 11 crash)
	if (glDebugMessageControlKHR != NULL && FAndroidMisc::GetGPUFamily().Contains(TEXT("PowerVR")))
	{
		glDebugMessageControlKHR = NULL;
	}

	glDebugMessageInsertKHR = (PFNGLDEBUGMESSAGEINSERTKHRPROC)((void*)eglGetProcAddress("glDebugMessageInsertKHR"));
	glDebugMessageCallbackKHR = (PFNGLDEBUGMESSAGECALLBACKKHRPROC)((void*)eglGetProcAddress("glDebugMessageCallbackKHR"));
	glDebugMessageLogKHR = (PFNGLGETDEBUGMESSAGELOGKHRPROC)((void*)eglGetProcAddress("glDebugMessageLogKHR"));
	glGetPointervKHR = (PFNGLGETPOINTERVKHRPROC)((void*)eglGetProcAddress("glGetPointervKHR"));
	glPushDebugGroupKHR = (PFNGLPUSHDEBUGGROUPKHRPROC)((void*)eglGetProcAddress("glPushDebugGroupKHR"));
	glPopDebugGroupKHR = (PFNGLPOPDEBUGGROUPKHRPROC)((void*)eglGetProcAddress("glPopDebugGroupKHR"));
	glObjectLabelKHR = (PFNGLOBJECTLABELKHRPROC)((void*)eglGetProcAddress("glObjectLabelKHR"));
	glGetObjectLabelKHR = (PFNGLGETOBJECTLABELKHRPROC)((void*)eglGetProcAddress("glGetObjectLabelKHR"));
	glObjectPtrLabelKHR = (PFNGLOBJECTPTRLABELKHRPROC)((void*)eglGetProcAddress("glObjectPtrLabelKHR"));
	glGetObjectPtrLabelKHR = (PFNGLGETOBJECTPTRLABELKHRPROC)((void*)eglGetProcAddress("glGetObjectPtrLabelKHR"));

	glGetProgramBinary = (PFNGLGETPROGRAMBINARYOESPROC)((void*)eglGetProcAddress("glGetProgramBinaryOES"));
	glProgramBinary = (PFNGLPROGRAMBINARYOESPROC)((void*)eglGetProcAddress("glProgramBinaryOES"));
}

FPlatformOpenGLContext* PlatformGetOpenGLRenderingContext(FPlatformOpenGLDevice* Device)
{
	return AndroidEGL::GetInstance()->GetRenderingContext();
}

FPlatformOpenGLContext* PlatformCreateOpenGLContext(FPlatformOpenGLDevice* Device, void* InWindowHandle)
{
	//Assumes Device is already initialized and context already created.
	return AndroidEGL::GetInstance()->GetRenderingContext();
}

void PlatformDestroyOpenGLContext(FPlatformOpenGLDevice* Device, FPlatformOpenGLContext* Context)
{
}

FRHITexture* PlatformCreateBuiltinBackBuffer(FOpenGLDynamicRHI* OpenGLRHI, uint32 SizeX, uint32 SizeY)
{
	uint32 Flags = TexCreate_RenderTargetable;
	FOpenGLTexture2D* Texture2D = new FOpenGLTexture2D(OpenGLRHI, AndroidEGL::GetInstance()->GetOnScreenColorRenderBuffer(), GL_RENDERBUFFER, GL_COLOR_ATTACHMENT0, SizeX, SizeY, 0, 1, 1, 1, 1, PF_B8G8R8A8, false, false, Flags, nullptr, FClearValueBinding::Transparent);
	OpenGLTextureAllocated(Texture2D, Flags);

	return Texture2D;
}

void PlatformResizeGLContext( FPlatformOpenGLDevice* Device, FPlatformOpenGLContext* Context, uint32 SizeX, uint32 SizeY, bool bFullscreen, bool bWasFullscreen, GLenum BackBufferTarget, GLuint BackBufferResource)
{
	check(Context);

	glViewport(0, 0, SizeX, SizeY);
	VERIFY_GL(glViewport);
}

void PlatformGetSupportedResolution(uint32 &Width, uint32 &Height)
{
}

bool PlatformGetAvailableResolutions(FScreenResolutionArray& Resolutions, bool bIgnoreRefreshRate)
{
	return true;
}

int32 PlatformGlGetError()
{
	return glGetError();
}

// =============================================================

void PlatformReleaseOcclusionQuery( GLuint Query, uint64 QueryContext )
{
}

void FPlatformOpenGLDevice::SetCurrentSharedContext()
{
	AndroidEGL::GetInstance()->SetCurrentSharedContext();
}

void PlatformDestroyOpenGLDevice(FPlatformOpenGLDevice* Device)
{
	delete Device;
}

void FPlatformOpenGLDevice::SetCurrentRenderingContext()
{
	AndroidEGL::GetInstance()->AcquireCurrentRenderingContext();
}

void FPlatformOpenGLDevice::SetupCurrentContext()
{
	GLuint* DefaultVao = nullptr;
	EOpenGLCurrentContext ContextType = (EOpenGLCurrentContext)AndroidEGL::GetInstance()->GetCurrentContextType();

	if (ContextType == CONTEXT_Rendering)
	{
		DefaultVao = &AndroidEGL::GetInstance()->GetRenderingContext()->DefaultVertexArrayObject;
	}
	else if (ContextType == CONTEXT_Shared)
	{
		DefaultVao = &AndroidEGL::GetInstance()->GetSharedContext()->DefaultVertexArrayObject;
	}
	else
	{
		//Invalid or Other return
		return;
	}
	
	if (*DefaultVao == 0)
	{
		glGenVertexArrays(1, DefaultVao);
		glBindVertexArray(*DefaultVao);
	}
}

void PlatformLabelObjects()
{
	// @todo: Check that there is a valid id (non-zero) as LabelObject will fail otherwise
	GLuint RenderBuffer = AndroidEGL::GetInstance()->GetOnScreenColorRenderBuffer();
	if (RenderBuffer != 0)
	{
		FOpenGL::LabelObject(GL_RENDERBUFFER, RenderBuffer, "OnScreenColorRB");
	}

	GLuint FrameBuffer = AndroidEGL::GetInstance()->GetResolveFrameBuffer();
	if (FrameBuffer != 0)
	{
		FOpenGL::LabelObject(GL_FRAMEBUFFER, FrameBuffer, "ResolveFB");
	}
}

//--------------------------------
#define VIRTUALIZE_QUERIES (1)

static int32 GMaxmimumOcclusionQueries = 4000;

#if VIRTUALIZE_QUERIES
// These data structures could be better, but it would be tricky. InFlightVirtualQueries.Remove(QueryId) is a drag
TArray<GLuint> UsableRealQueries;
TArray<int32> InFlightVirtualQueries;
TArray<GLuint> VirtualToRealMap;
TArray<GLuint64> VirtualResults;
TArray<int32> FreeVirtuals;
TArray<GLuint> QueriesBeganButNotEnded;
#endif

#define QUERY_CHECK(x) check(x)
//#define QUERY_CHECK(x) if (!(x)) { FPlatformMisc::LocalPrint(TEXT("Failed a check on line:\n")); FPlatformMisc::LocalPrint(TEXT( PREPROCESSOR_TO_STRING(__LINE__))); FPlatformMisc::LocalPrint(TEXT("\n")); *((int*)3) = 13; }

#define CHECK_QUERY_ERRORS (DO_CHECK)

void PlatformGetNewRenderQuery(GLuint* OutQuery, uint64* OutQueryContext)
{
#if CHECK_QUERY_ERRORS
	GLenum Err = glGetError();
	while (Err != GL_NO_ERROR)
	{
		Err = glGetError();
	}
#endif

	*OutQueryContext = 0;
	VERIFY_GL_SCOPE();

#if !VIRTUALIZE_QUERIES
	FOpenGLES::GenQueries(1, OutQuery);
	//glGenQueriesEXT(1, OutQuery);
#if CHECK_QUERY_ERRORS
	Err = glGetError();
	if (Err != GL_NO_ERROR)
	{
		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("GenQueries Failed, glError %d (0x%x)"), Err, Err);
		*(char*)3 = 0;
	}
#endif
#else


	if (!UsableRealQueries.Num() && !InFlightVirtualQueries.Num())
	{
		GRHIMaximumReccommendedOustandingOcclusionQueries = GMaxmimumOcclusionQueries;
		UE_LOG(LogRHI, Log, TEXT("AndroidOpenGL: Using a maximum of %d occlusion queries."), GMaxmimumOcclusionQueries);

		UsableRealQueries.AddDefaulted(GMaxmimumOcclusionQueries);
		glGenQueriesEXT(GMaxmimumOcclusionQueries, &UsableRealQueries[0]);
#if CHECK_QUERY_ERRORS
		Err = glGetError();
		if (Err != GL_NO_ERROR)
		{
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("GenQueries Failed, glError %d (0x%x)"), Err, Err);
			*(char*)3 = 0;
		}
#endif
		VirtualToRealMap.Add(0); // this is null, it is not a real query and never will be
		VirtualResults.Add(0); // this is null, it is not a real query and never will be
	}

	if (FreeVirtuals.Num())
	{
		*OutQuery = FreeVirtuals.Pop();
		return;
	}
	*OutQuery = VirtualToRealMap.Num();
	VirtualToRealMap.Add(0);
	VirtualResults.Add(0);
#endif
}

void PlatformReleaseRenderQuery(GLuint Query, uint64 QueryContext)
{
#if !VIRTUALIZE_QUERIES
	glDeleteQueriesEXT(1, &Query);
#else
	GLuint RealIndex = VirtualToRealMap[Query];
	if (RealIndex)
	{
		GLuint OutResult;
		// still in use, wait for it now.
		FAndroidOpenGL::GetQueryObject(Query, FAndroidOpenGL::QM_Result, &OutResult);
		QUERY_CHECK(!VirtualToRealMap[Query]);
	}
	FreeVirtuals.Add(Query);
#endif
}

void FAndroidOpenGL::GetQueryObject(GLuint QueryId, EQueryMode QueryMode, GLuint *OutResult)
{
	GLuint64 Result;
	GetQueryObject(QueryId, QueryMode, &Result);
	*OutResult = GLuint(Result);
}

static void GetQueryObjectui64(GLuint QueryId, GLenum QueryName, GLuint64* OutResult)
{
	if (glGetQueryObjectui64vEXT)
	{
		glGetQueryObjectui64vEXT(QueryId, QueryName, OutResult);
	}
	else
	{
		GLuint TempResult = 0;
		glGetQueryObjectuivEXT(QueryId, QueryName, &TempResult);
		*OutResult = TempResult;
	}
}

void FAndroidOpenGL::GetQueryObject(GLuint QueryId, EQueryMode QueryMode, GLuint64* OutResult)
{
	GLenum QueryName = (QueryMode == QM_Result) ? GL_QUERY_RESULT_EXT : GL_QUERY_RESULT_AVAILABLE_EXT;
	VERIFY_GL_SCOPE();
	uint32 IdleStart = (QueryName == GL_QUERY_RESULT_EXT) ? FPlatformTime::Cycles() : 0;

#if !VIRTUALIZE_QUERIES
#if CHECK_QUERY_ERRORS
	GLenum Err = glGetError();
	while (Err != GL_NO_ERROR)
	{
		Err = glGetError();
	}
#endif

	GetQueryObjectui64(QueryId, QueryName, OutResult);
#else
	GLuint RealIndex = VirtualToRealMap[QueryId];
	if (!RealIndex)
	{
		if (QueryName == GL_QUERY_RESULT_AVAILABLE_EXT)
		{
			*OutResult = GL_TRUE;
		}
		else
		{
			*OutResult = VirtualResults[QueryId];
		}
		return;
	}

	if (QueryName == GL_QUERY_RESULT_EXT)
	{
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_FAndroidOpenGL_GetQueryObject_Remove);
			int NumRem = InFlightVirtualQueries.Remove(QueryId);
			QUERY_CHECK(NumRem == 1);
		}
		UsableRealQueries.Add(RealIndex);
		VirtualToRealMap[QueryId] = 0;
	}

#if CHECK_QUERY_ERRORS
	GLenum Err = glGetError();
	while (Err != GL_NO_ERROR)
	{
		Err = glGetError();
	}
#endif

	GetQueryObjectui64(RealIndex, QueryName, OutResult);

	if (QueryName == GL_QUERY_RESULT_EXT)
	{
		VirtualResults[QueryId] = *OutResult;
	}
#endif
	if (QueryName == GL_QUERY_RESULT_EXT)
	{
		uint32 ThisCycles = FPlatformTime::Cycles() - IdleStart;
		if (IsInRHIThread())
		{
			GWorkingRHIThreadStallTime += ThisCycles;
		}
		else
		{
			GRenderThreadIdle[ERenderThreadIdleTypes::WaitingForGPUQuery] += ThisCycles;
			GRenderThreadNumIdle[ERenderThreadIdleTypes::WaitingForGPUQuery]++;
		}
	}

#if CHECK_QUERY_ERRORS
	Err = glGetError();
	QUERY_CHECK(Err == GL_NO_ERROR);
#endif
}

GLuint FAndroidOpenGL::MakeVirtualQueryReal(GLuint Query)
{
#if !VIRTUALIZE_QUERIES
	return Query;
#else
	GLuint RealIndex = VirtualToRealMap[Query];
	if (RealIndex)
	{
		GLuint OutResult;
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FAndroidOpenGL_BeginQuery_RecycleWait);
		// still in use, wait for it now.
		FAndroidOpenGL::GetQueryObject(Query, QM_Result, &OutResult);
		QUERY_CHECK(!VirtualToRealMap[Query]);
	}
	if (!UsableRealQueries.Num())
	{
		QUERY_CHECK(InFlightVirtualQueries.Num() + QueriesBeganButNotEnded.Num() == GMaxmimumOcclusionQueries);
		QUERY_CHECK(InFlightVirtualQueries.Num()); // if this fires, then it means the nesting of begins is too deep.
		GLuint OutResult;
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FAndroidOpenGL_BeginQuery_FreeWait);
		FAndroidOpenGL::GetQueryObject(InFlightVirtualQueries[0], QM_Result, &OutResult);
		QUERY_CHECK(UsableRealQueries.Num());
	}
	RealIndex = UsableRealQueries.Pop();
	VirtualToRealMap[Query] = RealIndex;
	VirtualResults[Query] = 0;

	return RealIndex;
#endif
}


void FAndroidOpenGL::QueryTimestampCounter(GLuint Query)
{
	QUERY_CHECK(SupportsDisjointTimeQueries());
	VERIFY_GL_SCOPE();
#if CHECK_QUERY_ERRORS
	GLenum Err = glGetError();
	while (Err != GL_NO_ERROR)
	{
		Err = glGetError();
	}
#endif
#if !VIRTUALIZE_QUERIES
	glQueryCounterEXT(Query, GL_TIMESTAMP_EXT);
#else

	GLuint RealIndex = MakeVirtualQueryReal(Query);

	InFlightVirtualQueries.Add(Query);

	glQueryCounterEXT(RealIndex, GL_TIMESTAMP_EXT);
#endif
#if CHECK_QUERY_ERRORS

	Err = glGetError();

	QUERY_CHECK(Err == GL_NO_ERROR);
#endif

}

bool FAndroidOpenGL::SupportsFramebufferSRGBEnable()
{	
	static auto* MobileUseHWsRGBEncodingCVAR = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Mobile.UseHWsRGBEncoding"));
	const bool bMobileUseHWsRGBEncoding = (MobileUseHWsRGBEncodingCVAR && MobileUseHWsRGBEncodingCVAR->GetValueOnAnyThread() == 1);
	return bMobileUseHWsRGBEncoding;
}

void FAndroidOpenGL::BeginQuery(GLenum QueryType, GLuint Query)
{
	QUERY_CHECK(QueryType == UGL_ANY_SAMPLES_PASSED || SupportsDisjointTimeQueries());
#if CHECK_QUERY_ERRORS
	GLenum Err = glGetError();
	while (Err != GL_NO_ERROR)
	{
		Err = glGetError();
	}
#endif

	VERIFY_GL_SCOPE();

#if !VIRTUALIZE_QUERIES
	glBeginQueryEXT(QueryType, Query);
#else
	GLuint RealIndex = MakeVirtualQueryReal(Query);
	QueriesBeganButNotEnded.Add(Query);
	glBeginQueryEXT(QueryType, RealIndex);
#endif
#if CHECK_QUERY_ERRORS
	Err = glGetError();

	QUERY_CHECK(Err == GL_NO_ERROR);
#endif
}

void FAndroidOpenGL::EndQuery(GLenum QueryType)
{
	QUERY_CHECK(QueryType == UGL_ANY_SAMPLES_PASSED || SupportsDisjointTimeQueries());

#if CHECK_QUERY_ERRORS
	GLenum Err = glGetError();
	while (Err != GL_NO_ERROR)
	{
		Err = glGetError();
	}
#endif

	VERIFY_GL_SCOPE();

	if (QueryType == UGL_ANY_SAMPLES_PASSED)
	{
		//return;
	}

#if VIRTUALIZE_QUERIES
	InFlightVirtualQueries.Add(QueriesBeganButNotEnded.Pop());
#endif
	glEndQueryEXT(QueryType);

#if CHECK_QUERY_ERRORS
	Err = glGetError();

	QUERY_CHECK(Err == GL_NO_ERROR);
#endif
}

FAndroidOpenGL::EImageExternalType FAndroidOpenGL::ImageExternalType = FAndroidOpenGL::EImageExternalType::None;

extern bool AndroidThunkCpp_GetMetaDataBoolean(const FString& Key);
extern FString AndroidThunkCpp_GetMetaDataString(const FString& Key);

void FAndroidOpenGL::SetupDefaultGLContextState(const FString& ExtensionsString)
{
	// Enable QCOM non-coherent framebuffer fetch if supported
	if (CVarDisableFBFNonCoherent.GetValueOnAnyThread() == 0 &&
		ExtensionsString.Contains(TEXT("GL_QCOM_shader_framebuffer_fetch_noncoherent")) && 
		ExtensionsString.Contains(TEXT("GL_EXT_shader_framebuffer_fetch")))
	{
		glEnable(GL_FRAMEBUFFER_FETCH_NONCOHERENT_QCOM);
	}
}

bool FAndroidOpenGL::RequiresAdrenoTilingModeHint()
{
	return bRequiresAdrenoTilingHint;
}

void FAndroidOpenGL::EnableAdrenoTilingModeHint(bool bEnable)
{
	if(bEnable && CVarEnableAdrenoTilingHint.GetValueOnAnyThread() != 0)
	{
		glEnable(GL_BINNING_CONTROL_HINT_QCOM);
		glHint(GL_BINNING_CONTROL_HINT_QCOM, GL_GPU_OPTIMIZED_QCOM);
	}
	else
	{
		glDisable(GL_BINNING_CONTROL_HINT_QCOM);
	}
}

void FAndroidOpenGL::ProcessExtensions(const FString& ExtensionsString)
{
	FString VersionString = FString(ANSI_TO_TCHAR((const ANSICHAR*)glGetString(GL_VERSION)));
	FString SubVersionString;
	bool hasES3 = VersionString.Split(TEXT("OpenGL ES 3."), nullptr, &SubVersionString);
	bES31Support = hasES3 && FCString::Atoi(*SubVersionString) >= 1;

	FOpenGLES::ProcessExtensions(ExtensionsString);

	FString RendererString = FString(ANSI_TO_TCHAR((const ANSICHAR*)glGetString(GL_RENDERER)));

	if (RendererString.Contains(TEXT("SGX 540")))
	{
		UE_LOG(LogRHI, Warning, TEXT("Disabling support for GL_OES_packed_depth_stencil on SGX 540"));
		bSupportsPackedDepthStencil = false;
		bRequiresTexture2DPrecisionHack = true;
	}

	// Common GPU types
	const bool bIsNvidiaBased = RendererString.Contains(TEXT("NVIDIA"));
	const bool bIsPoverVRBased = RendererString.Contains(TEXT("PowerVR"));
	const bool bIsAdrenoBased = RendererString.Contains(TEXT("Adreno"));
	const bool bIsMaliBased = RendererString.Contains(TEXT("Mali"));

	if (bIsPoverVRBased)
	{
		bHasHardwareHiddenSurfaceRemoval = true;
		UE_LOG(LogRHI, Log, TEXT("Enabling support for Hidden Surface Removal on PowerVR"));
	}

	if (bIsAdrenoBased)
	{
		GMaxmimumOcclusionQueries = 510;
		// This is to avoid a bug in Adreno drivers that define GL_ARM_shader_framebuffer_fetch_depth_stencil even when device does not support this extension
		// OpenGL ES 3.1 V@127.0 (GIT@I1af360237c)
		bRequiresARMShaderFramebufferFetchDepthStencilUndef = !bSupportsShaderDepthStencilFetch;

		// Adreno 2xx doesn't work with packed depth stencil enabled
		if (RendererString.Contains(TEXT("Adreno (TM) 2")))
		{
			UE_LOG(LogRHI, Warning, TEXT("Disabling support for GL_OES_packed_depth_stencil on Adreno 2xx"));
			bSupportsPackedDepthStencil = false;
		}

		// FORT-221329's broken adreno driver not common on Android 9 and above. TODO: check adreno driver version instead.
		bRequiresAdrenoTilingHint = FAndroidMisc::GetAndroidBuildVersion() < 28 || CVarEnableAdrenoTilingHint.GetValueOnAnyThread() == 2;
		UE_CLOG(bRequiresAdrenoTilingHint, LogRHI, Log, TEXT("Enabling Adreno tiling hint."));
	}

	// Disable ASTC if requested by device profile
	static const auto CVarDisableASTC = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Android.DisableASTCSupport"));
	if (bSupportsASTC && CVarDisableASTC->GetValueOnAnyThread())
	{
		bSupportsASTC = false;
		FAndroidGPUInfo::Get().RemoveTargetPlatform(TEXT("Android_ASTC"));
		UE_LOG(LogRHI, Log, TEXT("ASTC was disabled via r.OpenGL.DisableASTCSupport"));
	}

	//On Android, there are problems compiling shaders with textureCubeLodEXT calls in the glsl code,
	// so we set this to false to modify the glsl manually at compile-time.
	bSupportsTextureCubeLodEXT = false;

	// disable swizzled render targets on Android
	bSupportsBGRA8888RenderTarget = false;

	// Check for external image support for different ES versions
	ImageExternalType = EImageExternalType::None;

	static const auto CVarOverrideExternalTextureSupport = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.Android.OverrideExternalTextureSupport"));
	const int32 OverrideExternalTextureSupport = CVarOverrideExternalTextureSupport->GetValueOnAnyThread();
	switch (OverrideExternalTextureSupport)
	{
	case 1:
		ImageExternalType = EImageExternalType::None;
		break;

	case 2:
		ImageExternalType = EImageExternalType::ImageExternal100;
		break;

	case 3:
		ImageExternalType = EImageExternalType::ImageExternal300;
		break;

	case 4:
		ImageExternalType = EImageExternalType::ImageExternalESSL300;
		break;

	case 0:
	default:
		// auto-detect by extensions (default)
		bool bHasImageExternal = ExtensionsString.Contains(TEXT("GL_OES_EGL_image_external ")) || ExtensionsString.EndsWith(TEXT("GL_OES_EGL_image_external"));
		bool bHasImageExternalESSL3 = ExtensionsString.Contains(TEXT("OES_EGL_image_external_essl3"));
		if (bHasImageExternal || bHasImageExternalESSL3)
		{
			ImageExternalType = EImageExternalType::ImageExternal100;
			if (bUseES30ShadingLanguage)
			{
				if (bHasImageExternalESSL3)
				{
					ImageExternalType = EImageExternalType::ImageExternalESSL300;
				}
				else
				{
					// Adreno 5xx can do essl3 even without extension in list
					if (bIsAdrenoBased && RendererString.Contains(TEXT("(TM) 5")))
					{
						ImageExternalType = EImageExternalType::ImageExternalESSL300;
					}
				}
			}
			if (bIsNvidiaBased)
			{
				// Nvidia needs version 100 even though it supports ES3
				ImageExternalType = EImageExternalType::ImageExternal100;
			}
		}
		break;
	}
	switch (ImageExternalType)
	{
	case EImageExternalType::None:
		UE_LOG(LogRHI, Log, TEXT("Image external disabled"));
		break;

	case EImageExternalType::ImageExternal100:
		UE_LOG(LogRHI, Log, TEXT("Image external enabled: ImageExternal100"));
		break;

	case EImageExternalType::ImageExternal300:
		UE_LOG(LogRHI, Log, TEXT("Image external enabled: ImageExternal300"));
		break;

	case EImageExternalType::ImageExternalESSL300:
		UE_LOG(LogRHI, Log, TEXT("Image external enabled: ImageExternalESSL300"));
		break;

	default:
		ImageExternalType = EImageExternalType::None;
		UE_LOG(LogRHI, Log, TEXT("Image external disabled; unknown type"));
	}
	bSupportsImageExternal = ImageExternalType != EImageExternalType::None;

	// check for supported texture formats if enabled
	bool bCookOnTheFly = false;
#if !UE_BUILD_SHIPPING
	FString FileHostIP;
	bCookOnTheFly = FParse::Value(FCommandLine::Get(), TEXT("filehostip"), FileHostIP);
#endif
	if (!bCookOnTheFly && AndroidThunkCpp_GetMetaDataBoolean(TEXT("com.epicgames.ue4.GameActivity.bValidateTextureFormats")))
	{
		FString CookedFlavorsString = AndroidThunkCpp_GetMetaDataString(TEXT("com.epicgames.ue4.GameActivity.CookedFlavors"));
		if (!CookedFlavorsString.IsEmpty())
		{
			TArray<FString> CookedFlavors;
			CookedFlavorsString.ParseIntoArray(CookedFlavors, TEXT(","), true);

			// check each cooked flavor for support (only need one to be supported)
			bool bFoundSupported = false;
			for (FString Flavor : CookedFlavors)
			{
				if (Flavor.Equals(TEXT("ETC2")))
				{
					if (FOpenGL::SupportsETC2())
					{
						bFoundSupported = true;
						break;
					}
				}
				if (Flavor.Equals(TEXT("DXT")))
				{
					if (FOpenGL::SupportsDXT())
					{
						bFoundSupported = true;
						break;
					}
				}
				if (Flavor.Equals(TEXT("ASTC")))
				{
					if (FOpenGL::SupportsASTC())
					{
						bFoundSupported = true;
						break;
					}
				}
			}

			if (!bFoundSupported)
			{
				FString Message = TEXT("Cooked Flavors: ") + CookedFlavorsString + TEXT("\n\nSupported: ETC2") +
					(FOpenGL::SupportsDXT() ? TEXT(",DXT") : TEXT("")) +
					(FOpenGL::SupportsASTC() ? TEXT(",ASTC") : TEXT(""));

				FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Error: Unsupported Texture Format\n%s"), *Message);
				FAndroidMisc::MessageBoxExt(EAppMsgType::Ok, *Message, TEXT("Unsupported Texture Format"));
			}
		}
	}

	// test for glCopyImageSubData functionality
	// if device supports GLES 3.2 or higher get api function address otherwise search for glCopyImageSubDataEXT extension
	{
		if (GLMajorVerion >= 3 && GLMinorVersion >= 2)
		{
			glCopyImageSubData = (PFNGLCOPYIMAGESUBDATAPROC)((void*)eglGetProcAddress("glCopyImageSubData"));
		}
		else
		{
			// search for extension name first because a non-null eglGetProcAddress() result does not necessarily imply the presence of the extension
			if (ExtensionsString.Contains(TEXT("GL_EXT_copy_image")))
			{
				glCopyImageSubData = (PFNGLCOPYIMAGESUBDATAPROC)((void*)eglGetProcAddress("glCopyImageSubDataEXT"));
			}
		}
		bSupportsCopyImage = (glCopyImageSubData != nullptr);
	}

	// Qualcomm non-coherent framebuffer_fetch
	if (CVarDisableFBFNonCoherent.GetValueOnAnyThread() == 0 &&
		ExtensionsString.Contains(TEXT("GL_QCOM_shader_framebuffer_fetch_noncoherent")) && 
		ExtensionsString.Contains(TEXT("GL_EXT_shader_framebuffer_fetch")))
	{
		glFramebufferFetchBarrierQCOM = (PFNGLFRAMEBUFFERFETCHBARRIERQCOMPROC)((void*)eglGetProcAddress("glFramebufferFetchBarrierQCOM"));
		if (glFramebufferFetchBarrierQCOM != nullptr)
		{
			UE_LOG(LogRHI, Log, TEXT("Using QCOM_shader_framebuffer_fetch_noncoherent"));
		}
	}

	if (CVarDisableEarlyFragmentTests.GetValueOnAnyThread() != 0)
	{
		bRequiresDisabledEarlyFragmentTests = true;
		UE_LOG(LogRHI, Log, TEXT("Disabling early_fragment_tests"));
	}
}

FString FAndroidMisc::GetGPUFamily()
{
	return FAndroidGPUInfo::Get().GPUFamily;
}

FString FAndroidMisc::GetGLVersion()
{
	return FAndroidGPUInfo::Get().GLVersion;
}

bool FAndroidMisc::SupportsFloatingPointRenderTargets()
{
	return FAndroidGPUInfo::Get().bSupportsFloatingPointRenderTargets;
}

bool FAndroidMisc::SupportsShaderFramebufferFetch()
{
	return FAndroidGPUInfo::Get().bSupportsFrameBufferFetch;
}

bool FAndroidMisc::SupportsES30()
{
	return true;
}

void FAndroidMisc::GetValidTargetPlatforms(TArray<FString>& TargetPlatformNames)
{
	TargetPlatformNames = FAndroidGPUInfo::Get().TargetPlatformNames;
}

void FAndroidAppEntry::PlatformInit()
{
	// create an ES2 EGL here for gpu queries.
	AndroidEGL::GetInstance()->Init(AndroidEGL::AV_OpenGLES, 2, 0, false);
}

void FAndroidAppEntry::ReleaseEGL()
{
	AndroidEGL* EGL = AndroidEGL::GetInstance();
	if (EGL->IsInitialized())
	{
		EGL->DestroyBackBuffer();
		EGL->Terminate();
	}
}

#endif
