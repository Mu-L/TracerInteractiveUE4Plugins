// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraCommon.h"
#include "VectorVM.h"
#include "NiagaraDataInterfaceTexture.generated.h"

/** Data Interface allowing sampling of a texture */
UCLASS(EditInlineNew, Category = "Texture", meta = (DisplayName = "Texture Sample"))
class NIAGARA_API UNiagaraDataInterfaceTexture : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()
public:

	DECLARE_NIAGARA_DI_PARAMETER();

	UPROPERTY(EditAnywhere, Category = "Texture")
	UTexture* Texture;

	//UObject Interface
	virtual void PostInitProperties()override;
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void Serialize(FArchive& Ar) override;
	//UObject Interface End

	//UNiagaraDataInterface Interface
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target)const override { return Target==ENiagaraSimTarget::GPUComputeSim; }
	//UNiagaraDataInterface Interface End

	void SampleTexture(FVectorVMContext& Context);
	void GetTextureDimensions(FVectorVMContext& Context);
	void SamplePseudoVolumeTexture(FVectorVMContext& Context);

	virtual bool Equals(const UNiagaraDataInterface* Other) const override;

	// GPU sim functionality
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;

	void SetTexture(UTexture* InTexture);

	//FRWBuffer& GetGPUBuffer();
	static const FString TextureName;
	static const FString SamplerName;
	static const FString DimensionsBaseName;
protected:
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;

	void PushToRenderThread();


	static const FName SampleTexture2DName;
	static const FName SampleVolumeTextureName;
	static const FName SamplePseudoVolumeTextureName;
	static const FName TextureDimsName;
};

struct FNiagaraDataInterfaceProxyTexture : public FNiagaraDataInterfaceProxy
{
	FSamplerStateRHIRef SamplerStateRHI;
	FTextureRHIRef	TextureRHI;
	FVector2D TexDims;

	virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance) override { check(false); }
	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override
	{
		return 0;
	}
};