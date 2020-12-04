// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceCamera.h"
#include "NiagaraTypes.h"
#include "NiagaraWorldManager.h"
#include "Internationalization/Internationalization.h"
#include "NiagaraSystemInstance.h"
#include "GameFramework/PlayerController.h"
#include "ShaderParameterUtils.h"

#if WITH_EDITORONLY_DATA
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#endif

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceCamera"


IMPLEMENT_TYPE_LAYOUT(FNiagaraDataInterfaceParametersCS_CameraQuery);

void FNiagaraDataInterfaceParametersCS_CameraQuery::Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
{
	PassUniformBuffer.Bind(ParameterMap, FSceneTextureUniformParameters::StaticStructMetadata.GetShaderVariableName());
}

void FNiagaraDataInterfaceParametersCS_CameraQuery::Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
{
	check(IsInRenderingThread());
	FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

	TUniformBufferRef<FSceneTextureUniformParameters> SceneTextureUniformParams = GNiagaraViewDataManager.GetSceneTextureUniformParameters();
	SetUniformBufferParameter(RHICmdList, ComputeShaderRHI, PassUniformBuffer, SceneTextureUniformParams);
}


const FName UNiagaraDataInterfaceCamera::GetViewPropertiesName(TEXT("GetViewPropertiesGPU"));
const FName UNiagaraDataInterfaceCamera::GetClipSpaceTransformsName(TEXT("GetClipSpaceTransformsGPU"));
const FName UNiagaraDataInterfaceCamera::GetViewSpaceTransformsName(TEXT("GetViewSpaceTransformsGPU"));
const FName UNiagaraDataInterfaceCamera::GetCameraPropertiesName(TEXT("GetCameraPropertiesCPU/GPU"));
const FName UNiagaraDataInterfaceCamera::GetFieldOfViewName(TEXT("GetFieldOfView"));
const FName UNiagaraDataInterfaceCamera::CalculateDistancesName(TEXT("CalculateParticleDistancesCPU"));
const FName UNiagaraDataInterfaceCamera::QueryClosestName(TEXT("QueryClosestParticlesCPU"));

UNiagaraDataInterfaceCamera::UNiagaraDataInterfaceCamera(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Proxy.Reset(new FNiagaraDataIntefaceProxyCameraQuery());
}

void UNiagaraDataInterfaceCamera::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), true, false, false);
	}
}

bool UNiagaraDataInterfaceCamera::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FCameraDataInterface_InstanceData* PIData = new (PerInstanceData) FCameraDataInterface_InstanceData;
	return true;
}

bool UNiagaraDataInterfaceCamera::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	FCameraDataInterface_InstanceData* PIData = (FCameraDataInterface_InstanceData*)PerInstanceData;
	if (!PIData)
	{
		return true;
	}

	// calculate the distance for each particle and sort by distance (if required)
	PIData->ParticlesSortedByDistance.Empty();
	FDistanceData DistanceData;
	while (PIData->DistanceSortQueue.Dequeue(DistanceData))
	{
		PIData->ParticlesSortedByDistance.Add(DistanceData);
	}
	PIData->ParticlesSortedByDistance.StableSort([](const FDistanceData& A, const FDistanceData& B) { return A.DistanceSquared < B.DistanceSquared; });

	// grab the current camera data
	UWorld* World = SystemInstance->GetWorldManager()->GetWorld();
	if (World && PlayerControllerIndex < World->GetNumPlayerControllers())
	{
		int32 i = 0;
		for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
		{
			APlayerController* PlayerController = Iterator->Get();
			if (i == PlayerControllerIndex && PlayerController)
			{
				PIData->CameraLocation = PlayerController->PlayerCameraManager->GetCameraLocation();
				PIData->CameraRotation = PlayerController->PlayerCameraManager->GetCameraRotation();
				PIData->CameraFOV = PlayerController->PlayerCameraManager->GetFOVAngle();
				return false;
			}
			i++;
		}
	}
#if WITH_EDITORONLY_DATA
	if (GCurrentLevelEditingViewportClient)
	{
		const FViewportCameraTransform& ViewTransform = GCurrentLevelEditingViewportClient->GetViewTransform();
		PIData->CameraLocation = ViewTransform.GetLocation();
		PIData->CameraRotation = ViewTransform.GetRotation();
		PIData->CameraFOV = GCurrentLevelEditingViewportClient->ViewFOV;
		return false;
	}
#endif

	PIData->CameraLocation = FVector::ZeroVector;
	PIData->CameraRotation = FRotator(0);
	PIData->CameraFOV = 0;
	
	return false;
}

void UNiagaraDataInterfaceCamera::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	FNiagaraFunctionSignature Sig;
	Sig.Name = GetViewPropertiesName;
#if WITH_EDITORONLY_DATA
	Sig.Description = NSLOCTEXT("Niagara", "GetViewPropertiesDescription", "This function returns the properties of the current view. Only valid for gpu particles.");
#endif
	Sig.bMemberFunction = true;
	Sig.bRequiresContext = false;
	Sig.bSupportsCPU = false;
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Camera interface")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("View Position World")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("View Forward Vector")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("View Up Vector")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("View Right Vector")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), TEXT("View Size And Inverse Size")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), TEXT("Screen To View Space")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("Temporal AA Jitter (Current Frame)")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(), TEXT("Temporal AA Jitter (Previous Frame)")));
	OutFunctions.Add(Sig);


	Sig = FNiagaraFunctionSignature();
	Sig.Name = GetClipSpaceTransformsName;
#if WITH_EDITORONLY_DATA
	Sig.Description = NSLOCTEXT("Niagara", "GetClipSpaceTransformsDescription", "This function returns the clip transforms for the current view. Only valid for gpu particles.");
#endif
	Sig.bMemberFunction = true;
	Sig.bRequiresContext = false;
	Sig.bSupportsCPU = false;
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Camera interface")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("World To Clip Transform")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Translated World To Clip Transform")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Clip To World Transform")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Clip To View Transform")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Clip To Translated World Transform")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Screen To World Transform")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Screen To Translated World Transform")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Clip To Previous Clip Transform")));
	OutFunctions.Add(Sig);


	Sig = FNiagaraFunctionSignature();
	Sig.Name = GetViewSpaceTransformsName;
#if WITH_EDITORONLY_DATA
	Sig.Description = NSLOCTEXT("Niagara", "GetViewSpaceTransformsDescription", "This function returns the relevant transforms for the current view. Only valid for gpu particles.");
#endif
	Sig.bMemberFunction = true;
	Sig.bRequiresContext = false;
	Sig.bSupportsCPU = false;
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Camera interface")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Translated World To View Transform")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("View To Translated World Transform")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Translated World To Camera View Transform")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("Camera View To Translated World Transform")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("View To Clip Transform")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("View To ClipNoAA Transform")));
	OutFunctions.Add(Sig);


	Sig = FNiagaraFunctionSignature();
	Sig.Name = GetFieldOfViewName;
#if WITH_EDITORONLY_DATA
	Sig.Description = NSLOCTEXT("Niagara", "GetNiagaraFOVDescription", "This function returns the field of view angle (in degrees) for the active camera. For gpu particles this returns the x axis fov.");
#endif
	Sig.bMemberFunction = true;
	Sig.bRequiresContext = false;
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Camera interface")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Field Of View Angle")));
	OutFunctions.Add(Sig);


	Sig = FNiagaraFunctionSignature();
	Sig.Name = GetCameraPropertiesName;
#if WITH_EDITORONLY_DATA
	Sig.Description = NSLOCTEXT("Niagara", "GetCameraPositionDescription", "This function returns the position of the currently active camera.");
#endif
	Sig.bMemberFunction = true;
	Sig.bRequiresContext = false;
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Camera interface")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Camera Position World")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Forward Vector World")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Up Vector World")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Right Vector World")));
	OutFunctions.Add(Sig);

	Sig = FNiagaraFunctionSignature();
	Sig.Name = QueryClosestName;
#if WITH_EDITORONLY_DATA
	Sig.Description = NSLOCTEXT("Niagara", "QueryClosestDescription", "This function checks the previously calculated distance of each particle and then returns true for the closest particles and false for the other ones.");
#endif
	Sig.bMemberFunction = true;
	Sig.bRequiresContext = false;
	Sig.bSupportsGPU = false;
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Camera interface")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIDDef(), TEXT("Particle ID")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Max Valid Results")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Is Closest")));
	OutFunctions.Add(Sig);

	Sig = FNiagaraFunctionSignature();
	Sig.Name = CalculateDistancesName;
#if WITH_EDITORONLY_DATA
	Sig.Description = NSLOCTEXT("Niagara", "CalculateDistancesDescription", "This function compares the particle position against the camera position and stores the result to be queried in the next frame.");
#endif
	Sig.bMemberFunction = true;
	Sig.bRequiresContext = false;
	Sig.bSupportsGPU = false;
	Sig.bRequiresExecPin = true;
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Camera interface")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIDDef(), TEXT("Particle ID")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Particle Position World")));
	OutFunctions.Add(Sig);
}

bool UNiagaraDataInterfaceCamera::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	TMap<FString, FStringFormatArg> ArgsSample;
	ArgsSample.Add(TEXT("FunctionName"), FunctionInfo.InstanceName);

	if (FunctionInfo.DefinitionName == GetViewPropertiesName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(out float3 Out_ViewPositionWorld, out float3 Out_ViewForwardVector, out float3 Out_ViewUpVector, out float3 Out_ViewRightVector, out float4 Out_ViewSizeAndInverseSize, out float4 Out_ScreenToViewSpace, out float2 Out_Current_TAAJitter, out float2 Out_Previous_TAAJitter)
			{
				Out_ViewPositionWorld.xyz = View.WorldViewOrigin.xyz;
				Out_ViewForwardVector.xyz = View.ViewForward.xyz;
				Out_ViewUpVector.xyz = View.ViewUp.xyz;
				Out_ViewRightVector.xyz = View.ViewRight.xyz;
				Out_ViewSizeAndInverseSize = View.ViewSizeAndInvSize;
				Out_ScreenToViewSpace = View.ScreenToViewSpace;
				Out_Current_TAAJitter = View.TemporalAAJitter.xy;
				Out_Previous_TAAJitter = View.TemporalAAJitter.zw;
			} 
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	if (FunctionInfo.DefinitionName == GetFieldOfViewName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(out float Out_FieldOfViewAngle)
			{
				Out_FieldOfViewAngle = degrees(View.FieldOfViewWideAngles.x);
			}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	if (FunctionInfo.DefinitionName == GetClipSpaceTransformsName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(out float4x4 Out_WorldToClipTransform, out float4x4 Out_TranslatedWorldToClipTransform, out float4x4 Out_ClipToWorldTransform, out float4x4 Out_ClipToViewTransform,
				out float4x4 Out_ClipToTranslatedWorldTransform, out float4x4 Out_ScreenToWorldTransform, out float4x4 Out_ScreenToTranslatedWorldTransform, out float4x4 Out_ClipToPreviousClipTransform)
			{
				Out_WorldToClipTransform = View.WorldToClip;
				Out_TranslatedWorldToClipTransform = View.TranslatedWorldToClip;
				Out_ClipToWorldTransform = View.ClipToWorld;
				Out_ClipToViewTransform = View.ClipToView;
				Out_ClipToTranslatedWorldTransform = View.ClipToTranslatedWorld;
				Out_ScreenToWorldTransform = View.ScreenToWorld;
				Out_ScreenToTranslatedWorldTransform = View.ScreenToTranslatedWorld;
				Out_ClipToPreviousClipTransform = View.ClipToPrevClip;
			}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	if (FunctionInfo.DefinitionName == GetViewSpaceTransformsName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(out float4x4 Out_TranslatedWorldToViewTransform, out float4x4 Out_ViewToTranslatedWorldTransform, out float4x4 Out_TranslatedWorldToCameraViewTransform,
				out float4x4 Out_CameraViewToTranslatedWorldTransform, out float4x4 Out_ViewToClipTransform, out float4x4 Out_ViewToClipNoAATransform)
			{
				Out_TranslatedWorldToViewTransform = View.TranslatedWorldToView;
				Out_ViewToTranslatedWorldTransform = View.ViewToTranslatedWorld;
				Out_TranslatedWorldToCameraViewTransform = View.TranslatedWorldToCameraView;
				Out_CameraViewToTranslatedWorldTransform = View.CameraViewToTranslatedWorld;
				Out_ViewToClipTransform = View.ViewToClip;
				Out_ViewToClipNoAATransform = View.ViewToClipNoAA;
			}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	if (FunctionInfo.DefinitionName == GetCameraPropertiesName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {FunctionName}(out float3 Out_CameraPositionWorld, out float3 Out_ViewForwardVector, out float3 Out_ViewUpVector, out float3 Out_ViewRightVector)
			{				
				Out_CameraPositionWorld.xyz = View.WorldCameraOrigin.xyz;
				Out_ViewForwardVector.xyz = View.ViewForward.xyz;
				Out_ViewUpVector.xyz = View.ViewUp.xyz;
				Out_ViewRightVector.xyz = View.ViewRight.xyz;
			}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	return false;
}

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceCamera, GetClosestParticles);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceCamera, CalculateParticleDistances);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceCamera, GetCameraFOV);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceCamera, GetCameraProperties);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceCamera, GetViewPropertiesGPU);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceCamera, GetClipSpaceTransformsGPU);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceCamera, GetViewSpaceTransformsGPU);
void UNiagaraDataInterfaceCamera::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	if (BindingInfo.Name == GetFieldOfViewName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceCamera, GetCameraFOV)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == CalculateDistancesName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceCamera, CalculateParticleDistances)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == QueryClosestName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceCamera, GetClosestParticles)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetCameraPropertiesName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceCamera, GetCameraProperties)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetViewSpaceTransformsName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceCamera, GetViewSpaceTransformsGPU)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetClipSpaceTransformsName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceCamera, GetClipSpaceTransformsGPU)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetViewPropertiesName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceCamera, GetViewPropertiesGPU)::Bind(this, OutFunc);
	}
	else
	{
		UE_LOG(LogNiagara, Error, TEXT("Could not find data interface external function. Received Name: %s"), *BindingInfo.Name.ToString());
	}
}

void UNiagaraDataInterfaceCamera::GetCameraFOV(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FCameraDataInterface_InstanceData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutFov(Context);

	float Fov = InstData.Get()->CameraFOV;

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*OutFov.GetDestAndAdvance() = Fov;
	}
}

void UNiagaraDataInterfaceCamera::GetCameraProperties(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FCameraDataInterface_InstanceData> InstData(Context);

	VectorVM::FExternalFuncRegisterHandler<float> CamPosX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> CamPosY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> CamPosZ(Context);

	VectorVM::FExternalFuncRegisterHandler<float> CamForwardX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> CamForwardY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> CamForwardZ(Context);

	VectorVM::FExternalFuncRegisterHandler<float> CamUpX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> CamUpY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> CamUpZ(Context);

	VectorVM::FExternalFuncRegisterHandler<float> CamRightX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> CamRightY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> CamRightZ(Context);

	FCameraDataInterface_InstanceData* CamData = InstData.Get();
	float XPos = CamData->CameraLocation.X;
	float YPos = CamData->CameraLocation.Y;
	float ZPos = CamData->CameraLocation.Z;

	FRotationMatrix RotationMatrix(CamData->CameraRotation);
	const FVector Forward = RotationMatrix.GetScaledAxis(EAxis::X);
	const FVector Up = RotationMatrix.GetScaledAxis(EAxis::Z);
	const FVector Right = RotationMatrix.GetScaledAxis(EAxis::Y);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*CamPosX.GetDestAndAdvance() = XPos;
		*CamPosY.GetDestAndAdvance() = YPos;
		*CamPosZ.GetDestAndAdvance() = ZPos;

		*CamForwardX.GetDestAndAdvance() = Forward.X;
		*CamForwardY.GetDestAndAdvance() = Forward.Y;
		*CamForwardZ.GetDestAndAdvance() = Forward.Z;

		*CamUpX.GetDestAndAdvance() = Up.X;
		*CamUpY.GetDestAndAdvance() = Up.Y;
		*CamUpZ.GetDestAndAdvance() = Up.Z;

		*CamRightX.GetDestAndAdvance() = Right.X;
		*CamRightY.GetDestAndAdvance() = Right.Y;
		*CamRightZ.GetDestAndAdvance() = Right.Z;
	}
}

void UNiagaraDataInterfaceCamera::GetClosestParticles(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FCameraDataInterface_InstanceData> InstData(Context);

	FNDIInputParam<FNiagaraID> ParticleIDParam(Context);
	FNDIInputParam<int32> CountParam(Context);
	FNDIOutputParam<FNiagaraBool> ResultOutParam(Context);

	int32 Count = Context.NumInstances > 0 ? CountParam.GetAndAdvance() : 0;
	if (Count == 0 || InstData->ParticlesSortedByDistance.Num() == 0)
	{
		for (int32 i = 0; i < Context.NumInstances; ++i)
		{
			ResultOutParam.SetAndAdvance(false);
		}
		return;
	}

	// grab the IDs of the closest n particles
	TSet<FNiagaraID> ClosestParticleIDs;
	for (int32 i = 0; i < Count; ++i)
	{
		ClosestParticleIDs.Add(InstData->ParticlesSortedByDistance[i].ParticleID);
	}

	// Assign each particles their result
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		FNiagaraID ParticleID = ParticleIDParam.GetAndAdvance();
		ResultOutParam.SetAndAdvance(ClosestParticleIDs.Contains(ParticleID));
	}
}

void UNiagaraDataInterfaceCamera::CalculateParticleDistances(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FCameraDataInterface_InstanceData> InstData(Context);

	FNDIInputParam<FNiagaraID> IDParam(Context);
	FNDIInputParam<FVector> ParticlePosParam(Context);

	FVector CameraPos = InstData->CameraLocation;
	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		FDistanceData DistanceData;
		FVector ParticlePos = ParticlePosParam.GetAndAdvance();
		DistanceData.ParticleID = IDParam.GetAndAdvance();
		DistanceData.DistanceSquared = (ParticlePos - CameraPos).SizeSquared();
		InstData->DistanceSortQueue.Enqueue(DistanceData);
	}
}

ETickingGroup UNiagaraDataInterfaceCamera::CalculateTickGroup(const void* PerInstanceData) const
{
	if (!bRequireCurrentFrameData)
		return NiagaraFirstTickGroup;
	return ETickingGroup::TG_PostUpdateWork;
}

#if WITH_EDITOR	
void UNiagaraDataInterfaceCamera::GetFeedback(UNiagaraSystem* Asset, UNiagaraComponent* Component,
	TArray<FNiagaraDataInterfaceError>& OutErrors, TArray<FNiagaraDataInterfaceFeedback>& Warnings,
	TArray<FNiagaraDataInterfaceFeedback>& Info)
{
	if (Asset == nullptr)
	{
		return;
	}

	// we need to check if the DI is used to access camera properties in a cpu context to warn the user that
	// the Niagara viewport does not support cpu cameras and it only works correctly in the level viewport and PIE

	TArray<UNiagaraScript*> Scripts;
	Scripts.Add(Asset->GetSystemSpawnScript());
	Scripts.Add(Asset->GetSystemUpdateScript());
	for (auto&& EmitterHandle : Asset->GetEmitterHandles())
	{
		if (EmitterHandle.GetInstance()->SimTarget == ENiagaraSimTarget::GPUComputeSim)
		{
			// Ignore gpu emitters
			continue;
		}
		TArray<UNiagaraScript*> OutScripts;
		EmitterHandle.GetInstance()->GetScripts(OutScripts, false);
		Scripts.Append(OutScripts);
	}

	// Check if any CPU script uses camera functions
	//TODO: This is the same as in the skel mesh DI, it doesn't guarantee that the DI used by these functions are THIS DI.
	// Has a possibility of false positives
	bool bHasCameraAccessWarning = [this, &Scripts]()
	{
		for (const auto Script : Scripts)
		{
			for (const auto& Info : Script->GetVMExecutableData().DataInterfaceInfo)
			{
				if (Info.MatchesClass(GetClass()))
				{
					for (const auto& Func : Info.RegisteredFunctions)
					{
						if (Func.Name == GetCameraPropertiesName || Func.Name == GetFieldOfViewName)
						{
							return true;
						}
					}
				}
			}
		}
		return false;
	}();

	if (bHasCameraAccessWarning)
	{
		FNiagaraDataInterfaceFeedback CPUAccessNotAllowedWarning(
         LOCTEXT("CPUCameraAccessWarning", "The cpu camera is bound to a player controller and will therefore not work correctly in the Niagara viewport.\nTo correctly preview the effect, use it in the level editor or switch to a GPU emitter."),
         LOCTEXT("CPUCameraAccessWarningSummary", "Camera properties cannot be previewed on CPU emitters!"),
         FNiagaraDataInterfaceFix());

		Warnings.Add(CPUAccessNotAllowedWarning);
	}
}

#endif

// ------- Dummy implementations for CPU execution ------------

void UNiagaraDataInterfaceCamera::GetViewPropertiesGPU(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FCameraDataInterface_InstanceData> InstData(Context);
	TArray<VectorVM::FExternalFuncRegisterHandler<float>> OutParams;
	OutParams.Reserve(24);
	for (int i = 0; i < 24; i++)
	{
		OutParams.Emplace(Context);
	}

	for (int32 k = 0; k < Context.NumInstances; ++k)
	{
		for (int i = 0; i < 24; i++)
		{
			*OutParams[i].GetDestAndAdvance() = 0;
		}
	}
}

void UNiagaraDataInterfaceCamera::GetClipSpaceTransformsGPU(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FCameraDataInterface_InstanceData> InstData(Context);
	TArray<VectorVM::FExternalFuncRegisterHandler<float>> OutParams;
	OutParams.Reserve(128);
	for (int i = 0; i < 128; i++)
	{
		OutParams.Emplace(Context);
	}

	for (int32 k = 0; k < Context.NumInstances; ++k)
	{
		for (int i = 0; i < 128; i++)
		{
			*OutParams[i].GetDestAndAdvance() = 0;
		}
	}
}

void UNiagaraDataInterfaceCamera::GetViewSpaceTransformsGPU(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FCameraDataInterface_InstanceData> InstData(Context);
	TArray<VectorVM::FExternalFuncRegisterHandler<float>> OutParams;
	OutParams.Reserve(96);
	for (int i = 0; i < 96; i++)
	{
		OutParams.Emplace(Context);
	}

	for (int32 k = 0; k < Context.NumInstances; ++k)
	{
		for (int i = 0; i < 96; i++)
		{
			*OutParams[i].GetDestAndAdvance() = 0;
		}
	}
}


bool UNiagaraDataInterfaceCamera::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceCamera* OtherTyped = CastChecked<UNiagaraDataInterfaceCamera>(Destination);
	OtherTyped->PlayerControllerIndex = PlayerControllerIndex;
	OtherTyped->bRequireCurrentFrameData = bRequireCurrentFrameData;
	return true;
}

bool UNiagaraDataInterfaceCamera::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceCamera* OtherTyped = CastChecked<const UNiagaraDataInterfaceCamera>(Other);
	return OtherTyped->PlayerControllerIndex == PlayerControllerIndex &&
		OtherTyped->bRequireCurrentFrameData == bRequireCurrentFrameData ;
}


IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceCamera, FNiagaraDataInterfaceParametersCS_CameraQuery);

#undef LOCTEXT_NAMESPACE