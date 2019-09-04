// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraDataInterface.h"
#include "NiagaraCommon.h"
#include "VectorVM.h"
#include "Components/SplineComponent.h"
#include "NiagaraDataInterfaceSpline.generated.h"


struct FNDISpline_InstanceData
{
	//Cached ptr to component we sample from. 
	TWeakObjectPtr<USplineComponent> Component;

	//Cached ComponentToWorld.
	FMatrix Transform;
	//InverseTranspose of above for transforming normals/tangents.
	FMatrix TransformInverseTransposed;
};

/** Data Interface allowing sampling of in-world spline components. Note that this data interface is very experimental. */
UCLASS(EditInlineNew, Category = "Splines", meta = (DisplayName = "Spline"))
class NIAGARA_API UNiagaraDataInterfaceSpline : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:
	
	/** The source actor from which to sample.  */
	UPROPERTY(EditAnywhere, Category = "Spline")
	AActor* Source;
	
	//UObject Interface
	virtual void PostInitProperties()override;
	//UObject Interface End

	//UNiagaraDataInterface Interface
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)override;
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual int32 PerInstanceDataSize()const override;
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target)const override { return Target == ENiagaraSimTarget::CPUSim; }
	//UNiagaraDataInterface Interface End

	template<typename TransformHandlerType, typename SplineSampleType>
	void SampleSplinePositionByUnitDistance(FVectorVMContext& Context);
	template<typename TransformHandlerType, typename SplineSampleType>
	void SampleSplineUpVectorByUnitDistance(FVectorVMContext& Context);
	template<typename TransformHandlerType, typename SplineSampleType>
	void SampleSplineRightVectorByUnitDistance(FVectorVMContext& Context);
	template<typename TransformHandlerType, typename SplineSampleType>
	void SampleSplineDirectionByUnitDistance(FVectorVMContext& Context);
	template<typename TransformHandlerType, typename SplineSampleType>
	void SampleSplineTangentByUnitDistance(FVectorVMContext& Context);
	template<typename PosXType, typename PosYType, typename PosZType>
	void FindClosestUnitDistanceFromPositionWS(FVectorVMContext& Context);
	
	void GetLocalToWorld(FVectorVMContext& Context);
	void GetLocalToWorldInverseTransposed(FVectorVMContext& Context);

	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override
	{
		return 0;
	}

protected:
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;

private:
	
	void WriteTransform(const FMatrix& ToWrite, FVectorVMContext& Context);
	//Cached ComponentToWorld.
};
