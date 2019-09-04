// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
/*=============================================================================
	UniformExpressions.h: Uniform expression definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialInterface.h"
#include "MaterialShared.h"
#include "TextureResource.h"
#include "Engine/Texture.h"
#include "Materials/MaterialExpressionTextureProperty.h"
#include "Materials/MaterialLayersFunctions.h"
#include "UObject/RenderingObjectVersion.h"

// Temporary flag for toggling experimental material layers functionality
bool AreExperimentalMaterialLayersEnabled();

/**
 */
class FMaterialUniformExpressionConstant: public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionConstant);
public:
	FMaterialUniformExpressionConstant() {}
	FMaterialUniformExpressionConstant(const FLinearColor& InValue,uint8 InValueType):
		Value(InValue),
		ValueType(InValueType)
	{}

	// FMaterialUniformExpression interface.
	virtual void Serialize(FArchive& Ar)
	{
		Ar << Value << ValueType;
	}
	virtual void GetNumberValue(const FMaterialRenderContext& Context,FLinearColor& OutValue) const
	{
		OutValue = Value;
	}
	virtual bool IsConstant() const
	{
		return true;
	}
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const
	{
		if (GetType() != OtherExpression->GetType())
		{
			return false;
		}
		FMaterialUniformExpressionConstant* OtherConstant = (FMaterialUniformExpressionConstant*)OtherExpression;
		return OtherConstant->ValueType == ValueType && OtherConstant->Value == Value;
	}

private:
	FLinearColor Value;
	uint8 ValueType;
};

/**
 */
class FMaterialUniformExpressionVectorParameter: public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionVectorParameter);
public:

	FMaterialUniformExpressionVectorParameter()
#if WITH_EDITOR
		: bUseOverriddenDefault(false)
#endif
	{}

	FMaterialUniformExpressionVectorParameter(const FMaterialParameterInfo& InParameterInfo,const FLinearColor& InDefaultValue)
		: ParameterInfo(InParameterInfo)
		, DefaultValue(InDefaultValue)
#if WITH_EDITOR
		, bUseOverriddenDefault(false)
#endif
	{}

	// FMaterialUniformExpression interface.
	virtual void Serialize(FArchive& Ar)
	{
		Ar << ParameterInfo << DefaultValue;
	}

	// inefficient compared to GetGameThreadNumberValue(), for editor purpose
	virtual void GetNumberValue(const FMaterialRenderContext& Context,FLinearColor& OutValue) const
	{
		OutValue.R = OutValue.G = OutValue.B = OutValue.A = 0;

		if(!Context.MaterialRenderProxy || !Context.MaterialRenderProxy->GetVectorValue(ParameterInfo, &OutValue, Context))
		{
			const bool bOveriddenParameterOnly = ParameterInfo.Association == EMaterialParameterAssociation::GlobalParameter;
			
			if (AreExperimentalMaterialLayersEnabled())
			{
				UMaterialInterface* Interface = Context.Material.GetMaterialInterface();
				if (!Interface || !Interface->GetVectorParameterDefaultValue(ParameterInfo, OutValue, bOveriddenParameterOnly))
				{
					GetDefaultValue(OutValue);
				}
			}
			else
			{
				GetDefaultValue(OutValue);
			}
		}
	}

	void GetDefaultValue(FLinearColor& OutValue) const
	{
#if WITH_EDITOR
		OutValue = bUseOverriddenDefault ? OverriddenDefaultValue : DefaultValue;
#else
		OutValue = DefaultValue;
#endif
	}

	// faster than GetNumberValue(), good for run-time use
	void GetGameThreadNumberValue(const UMaterialInterface* SourceMaterialToCopyFrom, FLinearColor& OutValue) const;

	virtual bool IsConstant() const
	{
		return false;
	}

	const FMaterialParameterInfo& GetParameterInfo() const
	{
		return ParameterInfo;
	}

	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const
	{
		if (GetType() != OtherExpression->GetType())
		{
			return false;
		}
		FMaterialUniformExpressionVectorParameter* OtherParameter = (FMaterialUniformExpressionVectorParameter*)OtherExpression;
		return ParameterInfo == OtherParameter->ParameterInfo && DefaultValue == OtherParameter->DefaultValue;
	}

#if WITH_EDITOR
	void SetTransientOverrideDefaultValue(const FLinearColor& InOverrideDefaultValue, bool bInUseOverriddenDefault)
	{
		bUseOverriddenDefault = bInUseOverriddenDefault;
		OverriddenDefaultValue = InOverrideDefaultValue;
	}
#endif

private:
	FMaterialParameterInfo ParameterInfo;
	FLinearColor DefaultValue;
#if WITH_EDITOR
	bool bUseOverriddenDefault;
	FLinearColor OverriddenDefaultValue;
#endif
};

/**
 */
class FMaterialUniformExpressionScalarParameter: public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionScalarParameter);
public:

	FMaterialUniformExpressionScalarParameter()
#if WITH_EDITOR
		: bUseOverriddenDefault(false)
#endif
	{}

	FMaterialUniformExpressionScalarParameter(const FMaterialParameterInfo& InParameterInfo,float InDefaultValue)
		: ParameterInfo(InParameterInfo)
		, DefaultValue(InDefaultValue)
#if WITH_EDITOR
		, bUseOverriddenDefault(false)
#endif
	{}

	// FMaterialUniformExpression interface.
	virtual void Serialize(FArchive& Ar)
	{
		Ar << ParameterInfo << DefaultValue;
	}

	// inefficient compared to GetGameThreadNumberValue(), for editor purpose
	virtual void GetNumberValue(const FMaterialRenderContext& Context,FLinearColor& OutValue) const
	{	
		OutValue.A = 0;

		if (!Context.MaterialRenderProxy || !Context.MaterialRenderProxy->GetScalarValue(ParameterInfo, &OutValue.A, Context))
		{
			const bool bOveriddenParameterOnly = ParameterInfo.Association == EMaterialParameterAssociation::GlobalParameter;
			
			if (AreExperimentalMaterialLayersEnabled())
			{
				UMaterialInterface* Interface = Context.Material.GetMaterialInterface();
				if (!Interface || !Interface->GetScalarParameterDefaultValue(ParameterInfo, OutValue.A, bOveriddenParameterOnly))
				{
					GetDefaultValue(OutValue.A);
				}
			}
			else
			{
				GetDefaultValue(OutValue.A);
			}
		}

		OutValue.R = OutValue.G = OutValue.B = OutValue.A;
	}

	void GetDefaultValue(float& OutValue) const
	{
#if WITH_EDITOR
		OutValue = bUseOverriddenDefault ? OverriddenDefaultValue : DefaultValue;
#else
		OutValue = DefaultValue;
#endif
	}
	
	// faster than GetNumberValue(), good for run-time use
	void GetGameThreadNumberValue(const UMaterialInterface* SourceMaterialToCopyFrom, float& OutValue) const;

	void GetGameThreadUsedAsAtlas(const UMaterialInterface* SourceMaterialToCopyFrom, bool& OutValue, TSoftObjectPtr<class UCurveLinearColor>& Curve, TSoftObjectPtr<class UCurveLinearColorAtlas>& Atlas) const;
	
	virtual bool IsConstant() const
	{
		return false;
	}

	const FMaterialParameterInfo& GetParameterInfo() const
	{
		return ParameterInfo;
	}

	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const
	{
		if (GetType() != OtherExpression->GetType())
		{
			return false;
		}
		FMaterialUniformExpressionScalarParameter* OtherParameter = (FMaterialUniformExpressionScalarParameter*)OtherExpression;
		return ParameterInfo == OtherParameter->ParameterInfo && DefaultValue == OtherParameter->DefaultValue;
	}

#if WITH_EDITOR
	void SetTransientOverrideDefaultValue(float InOverrideDefaultValue, bool bInUseOverriddenDefault)
	{
		bUseOverriddenDefault = bInUseOverriddenDefault;
		OverriddenDefaultValue = InOverrideDefaultValue;
	}
#endif

private:
	FMaterialParameterInfo ParameterInfo;
	float DefaultValue;
#if WITH_EDITOR
	bool bUseOverriddenDefault;
	float OverriddenDefaultValue;
#endif
};

/** @return The texture that was associated with the given index when the given material had its uniform expressions/HLSL code generated. */
template<typename TextureType>
static TextureType* GetIndexedTexture(const FMaterial& Material, int32 TextureIndex)
{
	TextureType* IndexedTexture = NULL;

	const TArray<UObject*>& ReferencedTextures = Material.GetReferencedTextures();
	if (ReferencedTextures.IsValidIndex(TextureIndex))
	{
		IndexedTexture = Cast<TextureType>(ReferencedTextures[TextureIndex]);
	}
	else
	{
		static bool bWarnedOnce = false;
		if (!bWarnedOnce)
		{
			UE_LOG(LogMaterial, Warning, TEXT("Requesting an invalid TextureIndex! (%u / %u)"), TextureIndex, ReferencedTextures.Num());
			bWarnedOnce = true;
		}
	}

	if (IndexedTexture == nullptr)
	{
		static bool bWarnedOnce = false;
		if (!bWarnedOnce)
		{
			UE_LOG(LogMaterial, Warning, TEXT("GetIndexedTexture returning NULL (%u)"), TextureIndex);
			bWarnedOnce = true;
		}
	}

	return IndexedTexture;
}

/**
 * A texture parameter expression.
 */
class FMaterialUniformExpressionTextureParameter: public FMaterialUniformExpressionTexture
{
	typedef FMaterialUniformExpressionTexture Super;
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionTextureParameter);
public:

	FMaterialUniformExpressionTextureParameter() {}

	FMaterialUniformExpressionTextureParameter(const FMaterialParameterInfo& InParameterInfo, int32 InTextureIndex, EMaterialSamplerType InSamplerType, ESamplerSourceMode InSourceMode, bool InVirtualTexture) :
		Super(InTextureIndex, InSamplerType, InSourceMode, InVirtualTexture),
		ParameterInfo(InParameterInfo)
	{}

	// FMaterialUniformExpression interface.
	virtual class FMaterialUniformExpressionTextureParameter* GetTextureParameterUniformExpression() override { return this; }

	virtual void Serialize(FArchive& Ar)
	{
		Ar << ParameterInfo;
		Super::Serialize(Ar);
	}
	virtual void GetTextureValue(const FMaterialRenderContext& Context, const FMaterial& Material, const UTexture*& OutValue) const override
	{
		check(IsInParallelRenderingThread());
		if (TransientOverrideValue_RenderThread != NULL)
		{
			OutValue = TransientOverrideValue_RenderThread;
		}
		else
		{
			if (!Context.MaterialRenderProxy || !Context.MaterialRenderProxy->GetTextureValue(ParameterInfo, &OutValue, Context))
			{
				UTexture* Value = nullptr;

				if (AreExperimentalMaterialLayersEnabled())
				{
					UMaterialInterface* Interface = Context.Material.GetMaterialInterface();
					if (!Interface || !Interface->GetTextureParameterDefaultValue(ParameterInfo, Value))
					{
						Value = GetIndexedTexture<UTexture>(Material, TextureIndex);
					}
				}
				else
				{
					Value = GetIndexedTexture<UTexture>(Material, TextureIndex);
				}

				OutValue = Value;
			}
		}
	}
	virtual void GetGameThreadTextureValue(const UMaterialInterface* MaterialInterface,const FMaterial& Material,UTexture*& OutValue,bool bAllowOverride=true) const
	{
		check(IsInGameThread());
		if( bAllowOverride && TransientOverrideValue_GameThread != NULL )
		{
			OutValue = TransientOverrideValue_GameThread;
		}
		else
		{
			OutValue = NULL;
		const bool bOverrideValuesOnly = !AreExperimentalMaterialLayersEnabled();
			if(!MaterialInterface->GetTextureParameterValue(ParameterInfo,OutValue,bOverrideValuesOnly))
		{
				OutValue = GetIndexedTexture<UTexture>(Material, TextureIndex);
			}
		}
	}

	virtual bool IsConstant() const
	{
		return false;
	}

	FName GetParameterName() const
	{
		return ParameterInfo.Name;
	}

	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const
	{
		if (GetType() != OtherExpression->GetType())
		{
			return false;
		}
		FMaterialUniformExpressionTextureParameter* OtherParameter = (FMaterialUniformExpressionTextureParameter*)OtherExpression;
		return ParameterInfo == OtherParameter->ParameterInfo && Super::IsIdentical(OtherParameter);
	}

private:
	FMaterialParameterInfo ParameterInfo;
};

/**
 * A flipbook texture parameter expression.
 */
class FMaterialUniformExpressionFlipBookTextureParameter : public FMaterialUniformExpressionTexture
{
	typedef FMaterialUniformExpressionTexture Super;
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionFlipBookTextureParameter);
public:

	FMaterialUniformExpressionFlipBookTextureParameter() {}

	virtual void GetNumberValue(const FMaterialRenderContext& Context,FLinearColor& OutValue) const
	{
		OutValue.R = OutValue.G = OutValue.B = OutValue.A = 0;
	}

	virtual bool IsConstant() const
	{
		return false;
	}
};


/**
 * An external texture parameter expression.
 */
class FMaterialUniformExpressionExternalTextureParameter: public FMaterialUniformExpressionExternalTexture
{
	typedef FMaterialUniformExpressionExternalTexture Super;
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionExternalTextureParameter);
public:

	FMaterialUniformExpressionExternalTextureParameter();
	FMaterialUniformExpressionExternalTextureParameter(FName InParameterName, int32 InTextureIndex);

	virtual void Serialize(FArchive& Ar) override;
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const override;
	virtual bool GetExternalTexture(const FMaterialRenderContext& Context, FTextureRHIRef& OutTextureRHI, FSamplerStateRHIRef& OutSamplerStateRHI) const override;
	virtual FMaterialUniformExpressionExternalTextureParameter* GetExternalTextureParameterUniformExpression() override { return this; }

	FName GetParameterName() const
	{
		return ParameterName;
	}

private:
	FName ParameterName;
};

/**
 */
class FMaterialUniformExpressionSine: public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionSine);
public:

	FMaterialUniformExpressionSine() {}
	FMaterialUniformExpressionSine(FMaterialUniformExpression* InX,bool bInIsCosine):
		X(InX),
		bIsCosine(bInIsCosine)
	{}

	// FMaterialUniformExpression interface.
	virtual void Serialize(FArchive& Ar)
	{
		Ar << X << bIsCosine;
	}
	virtual void GetNumberValue(const FMaterialRenderContext& Context,FLinearColor& OutValue) const
	{
		FLinearColor ValueX = FLinearColor::Black;
		X->GetNumberValue(Context,ValueX);
		OutValue.R = bIsCosine ? FMath::Cos(ValueX.R) : FMath::Sin(ValueX.R);
		OutValue.G = bIsCosine ? FMath::Cos(ValueX.G) : FMath::Sin(ValueX.G);
		OutValue.B = bIsCosine ? FMath::Cos(ValueX.B) : FMath::Sin(ValueX.B);
		OutValue.A = bIsCosine ? FMath::Cos(ValueX.A) : FMath::Sin(ValueX.A);
	}
	virtual bool IsConstant() const
	{
		return X->IsConstant();
	}
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const
	{
		if (GetType() != OtherExpression->GetType())
		{
			return false;
		}
		FMaterialUniformExpressionSine* OtherSine = (FMaterialUniformExpressionSine*)OtherExpression;
		return X->IsIdentical(OtherSine->X) && bIsCosine == OtherSine->bIsCosine;
	}

private:
	TRefCountPtr<FMaterialUniformExpression> X;
	bool bIsCosine;
};

/**
 */
enum ETrigMathOperation
{
	TMO_Sin,
	TMO_Cos,
	TMO_Tan,
	TMO_Asin,
	TMO_Acos,
	TMO_Atan,
	TMO_Atan2
};

/**
 */
class FMaterialUniformExpressionTrigMath: public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionTrigMath);
public:

	FMaterialUniformExpressionTrigMath() {}
	FMaterialUniformExpressionTrigMath(FMaterialUniformExpression* InX, ETrigMathOperation InOp):
		X(InX),
		Y(InX),
		Op(InOp)
	{}

	FMaterialUniformExpressionTrigMath(FMaterialUniformExpression* InX, FMaterialUniformExpression* InY, ETrigMathOperation InOp):
		X(InX),
		Y(InY),
		Op(InOp)
	{}

	// FMaterialUniformExpression interface.
	virtual void Serialize(FArchive& Ar)
	{
		Ar << X << Y << Op;
	}
	virtual void GetNumberValue(const FMaterialRenderContext& Context,FLinearColor& OutValue) const
	{
		FLinearColor ValueX = FLinearColor::Black;
		FLinearColor ValueY = FLinearColor::Black;
		X->GetNumberValue(Context,ValueX);
		Y->GetNumberValue(Context,ValueY);

		switch (Op)
		{
		case TMO_Sin: OutValue.R = FMath::Sin(ValueX.R); OutValue.G = FMath::Sin(ValueX.G); OutValue.B = FMath::Sin(ValueX.B); OutValue.A = FMath::Sin(ValueX.A); break;
		case TMO_Cos: OutValue.R = FMath::Cos(ValueX.R); OutValue.G = FMath::Cos(ValueX.G); OutValue.B = FMath::Cos(ValueX.B); OutValue.A = FMath::Cos(ValueX.A); break;
		case TMO_Tan: OutValue.R = FMath::Tan(ValueX.R); OutValue.G = FMath::Tan(ValueX.G); OutValue.B = FMath::Tan(ValueX.B); OutValue.A = FMath::Tan(ValueX.A); break;

		case TMO_Asin: OutValue.R = FMath::Asin(ValueX.R); OutValue.G = FMath::Asin(ValueX.G); OutValue.B = FMath::Asin(ValueX.B); OutValue.A = FMath::Asin(ValueX.A); break;
		case TMO_Acos: OutValue.R = FMath::Acos(ValueX.R); OutValue.G = FMath::Acos(ValueX.G); OutValue.B = FMath::Acos(ValueX.B); OutValue.A = FMath::Acos(ValueX.A); break;
		case TMO_Atan: OutValue.R = FMath::Atan(ValueX.R); OutValue.G = FMath::Atan(ValueX.G); OutValue.B = FMath::Atan(ValueX.B); OutValue.A = FMath::Atan(ValueX.A); break;

		case TMO_Atan2:
			// Note: Param names are reversed here for a trade-off of order consistency vs sharing code
			OutValue.R = FMath::Atan2(ValueX.R, ValueY.R); OutValue.G = FMath::Atan2(ValueX.G, ValueY.G);
			OutValue.B = FMath::Atan2(ValueX.B, ValueY.B); OutValue.A = FMath::Atan2(ValueX.A, ValueY.A);
			break;

		default:
			checkf(0, TEXT("Invalid trigonometry math operation in uniform expression."));
		}
		
	}
	virtual bool IsConstant() const
	{
		return X->IsConstant() && Y->IsConstant();
	}
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const
	{
		if (GetType() != OtherExpression->GetType())
		{
			return false;
		}
		FMaterialUniformExpressionTrigMath* OtherTrig = (FMaterialUniformExpressionTrigMath*)OtherExpression;
		return X->IsIdentical(OtherTrig->X) && Y->IsIdentical(OtherTrig->Y) && Op == OtherTrig->Op;
	}

private:
	TRefCountPtr<FMaterialUniformExpression> X;
	TRefCountPtr<FMaterialUniformExpression> Y;
	uint8 Op;
};

/**
 */
class FMaterialUniformExpressionSquareRoot: public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionSquareRoot);
public:

	FMaterialUniformExpressionSquareRoot() {}
	FMaterialUniformExpressionSquareRoot(FMaterialUniformExpression* InX):
		X(InX)
	{}

	// FMaterialUniformExpression interface.
	virtual void Serialize(FArchive& Ar)
	{
		Ar << X;
	}
	virtual void GetNumberValue(const FMaterialRenderContext& Context,FLinearColor& OutValue) const
	{
		FLinearColor ValueX = FLinearColor::Black;
		X->GetNumberValue(Context,ValueX);
		OutValue.R = FMath::Sqrt(ValueX.R);
		OutValue.G = FMath::Sqrt(ValueX.G);
		OutValue.B = FMath::Sqrt(ValueX.B);
		OutValue.A = FMath::Sqrt(ValueX.A);
	}
	virtual bool IsConstant() const
	{
		return X->IsConstant();
	}
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const
	{
		if (GetType() != OtherExpression->GetType())
		{
			return false;
		}
		FMaterialUniformExpressionSquareRoot* OtherSqrt = (FMaterialUniformExpressionSquareRoot*)OtherExpression;
		return X->IsIdentical(OtherSqrt->X);
	}

private:
	TRefCountPtr<FMaterialUniformExpression> X;
};

/**
 */
class FMaterialUniformExpressionLength: public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionLength);
public:

	FMaterialUniformExpressionLength() : ValueType(MCT_Float) {}
	FMaterialUniformExpressionLength(FMaterialUniformExpression* InX, uint32 InValueType = MCT_Float):
		X(InX),
		ValueType(InValueType)
	{}

	// FMaterialUniformExpression interface.
	virtual void Serialize(FArchive& Ar)
	{
		Ar.UsingCustomVersion(FRenderingObjectVersion::GUID);
		Ar << X;
				
		if (Ar.CustomVer(FRenderingObjectVersion::GUID) >= FRenderingObjectVersion::TypeHandlingForMaterialSqrtNodes)
		{
			Ar << ValueType;
		}	
	}
	virtual void GetNumberValue(const FMaterialRenderContext& Context,FLinearColor& OutValue) const
	{
		FLinearColor ValueX = FLinearColor::Black;
		X->GetNumberValue(Context,ValueX);

		check(ValueType & MCT_Float);
		float LengthSq = ValueX.R * ValueX.R;
		LengthSq += (ValueType >= MCT_Float2) ? ValueX.G * ValueX.G : 0;
		LengthSq += (ValueType >= MCT_Float3) ? ValueX.B * ValueX.B : 0;
		LengthSq += (ValueType >= MCT_Float4) ? ValueX.A * ValueX.A : 0;

		OutValue.R = OutValue.G = OutValue.B = OutValue.A = FMath::Sqrt(LengthSq);
	}
	virtual bool IsConstant() const
	{
		return X->IsConstant();
	}
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const
	{
		if (GetType() != OtherExpression->GetType())
		{
			return false;
		}
		FMaterialUniformExpressionLength* OtherSqrt = (FMaterialUniformExpressionLength*)OtherExpression;
		return X->IsIdentical(OtherSqrt->X) && ValueType == OtherSqrt->ValueType;
	}

private:
	TRefCountPtr<FMaterialUniformExpression> X;
	uint32 ValueType;
};

/**
 */
class FMaterialUniformExpressionLogarithm2: public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionLogarithm2);
public:

	FMaterialUniformExpressionLogarithm2() {}
	FMaterialUniformExpressionLogarithm2(FMaterialUniformExpression* InX):
		X(InX)
	{}

	// FMaterialUniformExpression interface.
	void Serialize(FArchive& Ar) override
	{
		Ar << X;
	}
	void GetNumberValue(const FMaterialRenderContext& Context,FLinearColor& OutValue) const override
	{
		FLinearColor ValueX = FLinearColor::Black;
		X->GetNumberValue(Context,ValueX);
		OutValue.R = FMath::Log2(ValueX.R);
		OutValue.G = FMath::Log2(ValueX.G);
		OutValue.B = FMath::Log2(ValueX.B);
		OutValue.A = FMath::Log2(ValueX.A);
	}
	bool IsConstant() const override
	{
		return X->IsConstant();
	}
	bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const override
	{
		if (GetType() != OtherExpression->GetType())
		{
			return false;
		}

		auto OtherLog = static_cast<const FMaterialUniformExpressionLogarithm2 *>(OtherExpression);
		return X->IsIdentical(OtherLog->X);
	}

private:
	TRefCountPtr<FMaterialUniformExpression> X;
};

/**
 */
class FMaterialUniformExpressionLogarithm10: public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionLogarithm10);
public:

	FMaterialUniformExpressionLogarithm10() {}
	FMaterialUniformExpressionLogarithm10(FMaterialUniformExpression* InX):
		X(InX)
	{}

	// FMaterialUniformExpression interface.
	void Serialize(FArchive& Ar) override
	{
		Ar << X;
	}
	void GetNumberValue(const FMaterialRenderContext& Context,FLinearColor& OutValue) const override
	{
		FLinearColor ValueX = FLinearColor::Black;
		X->GetNumberValue(Context,ValueX);

		static const float LogToLog10 = 1.0f / FMath::Loge(10.f);
		OutValue.R = FMath::Loge(ValueX.R) * LogToLog10;
		OutValue.G = FMath::Loge(ValueX.G) * LogToLog10;
		OutValue.B = FMath::Loge(ValueX.B) * LogToLog10;
		OutValue.A = FMath::Loge(ValueX.A) * LogToLog10;
	}
	bool IsConstant() const override
	{
		return X->IsConstant();
	}
	bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const override
	{
		if (GetType() != OtherExpression->GetType())
		{
			return false;
		}

		auto OtherLog = static_cast<const FMaterialUniformExpressionLogarithm10*>(OtherExpression);
		return X->IsIdentical(OtherLog->X);
	}

private:
	TRefCountPtr<FMaterialUniformExpression> X;
};

/**
 */
enum EFoldedMathOperation
{
	FMO_Add,
	FMO_Sub,
	FMO_Mul,
	FMO_Div,
	FMO_Dot,
	FMO_Cross
};

/** Converts an arbitrary number into a safe divisor. i.e. FMath::Abs(Number) >= DELTA */
static float GetSafeDivisor(float Number)
{
	if(FMath::Abs(Number) < DELTA)
	{
		if(Number < 0.0f)
		{
			return -DELTA;
		}
		else
		{
			return +DELTA;
		}
	}
	else
	{
		return Number;
	}
}

class FMaterialUniformExpressionFoldedMath: public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionFoldedMath);
public:

	FMaterialUniformExpressionFoldedMath() : ValueType(MCT_Float) {}
	FMaterialUniformExpressionFoldedMath(FMaterialUniformExpression* InA,FMaterialUniformExpression* InB,uint8 InOp, uint32 InValueType = MCT_Float):
		A(InA),
		B(InB),
		ValueType(InValueType),
		Op(InOp)	
	{}

	// FMaterialUniformExpression interface.
	virtual void Serialize(FArchive& Ar)
	{
		Ar.UsingCustomVersion(FRenderingObjectVersion::GUID);
		Ar << A << B << Op;
		
		if (Ar.CustomVer(FRenderingObjectVersion::GUID) >= FRenderingObjectVersion::TypeHandlingForMaterialSqrtNodes)
		{
			Ar << ValueType;
		}	
	}
	virtual void GetNumberValue(const FMaterialRenderContext& Context,FLinearColor& OutValue) const
	{
		FLinearColor ValueA = FLinearColor::Black;
		FLinearColor ValueB = FLinearColor::Black;
		A->GetNumberValue(Context, ValueA);
		B->GetNumberValue(Context, ValueB);

		switch(Op)
		{
			case FMO_Add: OutValue = ValueA + ValueB; break;
			case FMO_Sub: OutValue = ValueA - ValueB; break;
			case FMO_Mul: OutValue = ValueA * ValueB; break;
			case FMO_Div: 
				OutValue.R = ValueA.R / GetSafeDivisor(ValueB.R);
				OutValue.G = ValueA.G / GetSafeDivisor(ValueB.G);
				OutValue.B = ValueA.B / GetSafeDivisor(ValueB.B);
				OutValue.A = ValueA.A / GetSafeDivisor(ValueB.A);
				break;
			case FMO_Dot: 
				{
					check(ValueType & MCT_Float);
					float DotProduct = ValueA.R * ValueB.R;
					DotProduct += (ValueType >= MCT_Float2) ? ValueA.G * ValueB.G : 0;
					DotProduct += (ValueType >= MCT_Float3) ? ValueA.B * ValueB.B : 0;
					DotProduct += (ValueType >= MCT_Float4) ? ValueA.A * ValueB.A : 0;
					OutValue.R = OutValue.G = OutValue.B = OutValue.A = DotProduct;
				}
				break;
			case FMO_Cross: 
				{
					// Must be Float3, replicate CoerceParameter behavior
					switch (ValueType)
					{
					case MCT_Float:
						ValueA.B = ValueA.G = ValueA.R;
						ValueB.B = ValueB.G = ValueB.R;
						break;
					case MCT_Float1:
						ValueA.B = ValueA.G = 0.f;
						ValueB.B = ValueB.G = 0.f;
						break;
					case MCT_Float2:
						ValueA.B = 0.f;
						ValueB.B = 0.f;
						break;
					};
					FVector Cross = FVector::CrossProduct(FVector(ValueA), FVector(ValueB));
					OutValue.R = Cross.X; OutValue.G = Cross.Y; OutValue.B = Cross.Z;
					OutValue.A = 0.f;
				}
				break;
			default: UE_LOG(LogMaterial, Fatal,TEXT("Unknown folded math operation: %08x"),(int32)Op);
		};
	}
	virtual bool IsConstant() const
	{
		return A->IsConstant() && B->IsConstant();
	}
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const
	{
		if (GetType() != OtherExpression->GetType())
		{
			return false;
		}
		FMaterialUniformExpressionFoldedMath* OtherMath = (FMaterialUniformExpressionFoldedMath*)OtherExpression;
		return A->IsIdentical(OtherMath->A) && B->IsIdentical(OtherMath->B) && Op == OtherMath->Op && ValueType == OtherMath->ValueType;
	}

private:
	TRefCountPtr<FMaterialUniformExpression> A;
	TRefCountPtr<FMaterialUniformExpression> B;
	uint32 ValueType;
	uint8 Op;
};

/**
 * A hint that only the fractional part of this expession's value matters.
 */
class FMaterialUniformExpressionPeriodic: public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionPeriodic);
public:

	FMaterialUniformExpressionPeriodic() {}
	FMaterialUniformExpressionPeriodic(FMaterialUniformExpression* InX):
		X(InX)
	{}

	// FMaterialUniformExpression interface.
	virtual void Serialize(FArchive& Ar)
	{
		Ar << X;
	}
	virtual void GetNumberValue(const FMaterialRenderContext& Context,FLinearColor& OutValue) const
	{
		FLinearColor TempValue = FLinearColor::Black;
		X->GetNumberValue(Context,TempValue);

		OutValue.R = FMath::Fractional(TempValue.R);
		OutValue.G = FMath::Fractional(TempValue.G);
		OutValue.B = FMath::Fractional(TempValue.B);
		OutValue.A = FMath::Fractional(TempValue.A);
	}
	virtual bool IsConstant() const
	{
		return X->IsConstant();
	}
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const
	{
		if (GetType() != OtherExpression->GetType())
		{
			return false;
		}
		FMaterialUniformExpressionPeriodic* OtherPeriodic = (FMaterialUniformExpressionPeriodic*)OtherExpression;
		return X->IsIdentical(OtherPeriodic->X);
	}

private:
	TRefCountPtr<FMaterialUniformExpression> X;
};

/**
 */
class FMaterialUniformExpressionAppendVector: public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionAppendVector);
public:

	FMaterialUniformExpressionAppendVector() {}
	FMaterialUniformExpressionAppendVector(FMaterialUniformExpression* InA,FMaterialUniformExpression* InB,uint32 InNumComponentsA):
		A(InA),
		B(InB),
		NumComponentsA(InNumComponentsA)
	{}

	// FMaterialUniformExpression interface.
	virtual void Serialize(FArchive& Ar)
	{
		Ar << A << B << NumComponentsA;
	}
	virtual void GetNumberValue(const FMaterialRenderContext& Context,FLinearColor& OutValue) const
	{
		FLinearColor ValueA = FLinearColor::Black;
		FLinearColor ValueB = FLinearColor::Black;
		A->GetNumberValue(Context, ValueA);
		B->GetNumberValue(Context, ValueB);

		OutValue.R = NumComponentsA >= 1 ? ValueA.R : (&ValueB.R)[0 - NumComponentsA];
		OutValue.G = NumComponentsA >= 2 ? ValueA.G : (&ValueB.R)[1 - NumComponentsA];
		OutValue.B = NumComponentsA >= 3 ? ValueA.B : (&ValueB.R)[2 - NumComponentsA];
		OutValue.A = NumComponentsA >= 4 ? ValueA.A : (&ValueB.R)[3 - NumComponentsA];
	}
	virtual bool IsConstant() const
	{
		return A->IsConstant() && B->IsConstant();
	}
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const
	{
		if (GetType() != OtherExpression->GetType())
		{
			return false;
		}
		FMaterialUniformExpressionAppendVector* OtherAppend = (FMaterialUniformExpressionAppendVector*)OtherExpression;
		return A->IsIdentical(OtherAppend->A) && B->IsIdentical(OtherAppend->B) && NumComponentsA == OtherAppend->NumComponentsA;
	}

private:
	TRefCountPtr<FMaterialUniformExpression> A;
	TRefCountPtr<FMaterialUniformExpression> B;
	uint32 NumComponentsA;
};

/**
 */
class FMaterialUniformExpressionMin: public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionMin);
public:

	FMaterialUniformExpressionMin() {}
	FMaterialUniformExpressionMin(FMaterialUniformExpression* InA,FMaterialUniformExpression* InB):
		A(InA),
		B(InB)
	{}

	// FMaterialUniformExpression interface.
	virtual void Serialize(FArchive& Ar)
	{
		Ar << A << B;
	}
	virtual void GetNumberValue(const FMaterialRenderContext& Context,FLinearColor& OutValue) const
	{
		FLinearColor ValueA = FLinearColor::Black;
		FLinearColor ValueB = FLinearColor::Black;
		A->GetNumberValue(Context, ValueA);
		B->GetNumberValue(Context, ValueB);

		OutValue.R = FMath::Min(ValueA.R, ValueB.R);
		OutValue.G = FMath::Min(ValueA.G, ValueB.G);
		OutValue.B = FMath::Min(ValueA.B, ValueB.B);
		OutValue.A = FMath::Min(ValueA.A, ValueB.A);
	}
	virtual bool IsConstant() const
	{
		return A->IsConstant() && B->IsConstant();
	}
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const
	{
		if (GetType() != OtherExpression->GetType())
		{
			return false;
		}
		FMaterialUniformExpressionMin* OtherMin = (FMaterialUniformExpressionMin*)OtherExpression;
		return A->IsIdentical(OtherMin->A) && B->IsIdentical(OtherMin->B);
	}

private:
	TRefCountPtr<FMaterialUniformExpression> A;
	TRefCountPtr<FMaterialUniformExpression> B;
};

/**
 */
class FMaterialUniformExpressionMax: public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionMax);
public:

	FMaterialUniformExpressionMax() {}
	FMaterialUniformExpressionMax(FMaterialUniformExpression* InA,FMaterialUniformExpression* InB):
		A(InA),
		B(InB)
	{}

	// FMaterialUniformExpression interface.
	virtual void Serialize(FArchive& Ar)
	{
		Ar << A << B;
	}
	virtual void GetNumberValue(const FMaterialRenderContext& Context,FLinearColor& OutValue) const
	{
		FLinearColor ValueA = FLinearColor::Black;
		FLinearColor ValueB = FLinearColor::Black;
		A->GetNumberValue(Context, ValueA);
		B->GetNumberValue(Context, ValueB);

		OutValue.R = FMath::Max(ValueA.R, ValueB.R);
		OutValue.G = FMath::Max(ValueA.G, ValueB.G);
		OutValue.B = FMath::Max(ValueA.B, ValueB.B);
		OutValue.A = FMath::Max(ValueA.A, ValueB.A);
	}
	virtual bool IsConstant() const
	{
		return A->IsConstant() && B->IsConstant();
	}
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const
	{
		if (GetType() != OtherExpression->GetType())
		{
			return false;
		}
		FMaterialUniformExpressionMax* OtherMax = (FMaterialUniformExpressionMax*)OtherExpression;
		return A->IsIdentical(OtherMax->A) && B->IsIdentical(OtherMax->B);
	}

private:
	TRefCountPtr<FMaterialUniformExpression> A;
	TRefCountPtr<FMaterialUniformExpression> B;
};

/**
 */
class FMaterialUniformExpressionClamp: public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionClamp);
public:

	FMaterialUniformExpressionClamp() {}
	FMaterialUniformExpressionClamp(FMaterialUniformExpression* InInput,FMaterialUniformExpression* InMin,FMaterialUniformExpression* InMax):
		Input(InInput),
		Min(InMin),
		Max(InMax)
	{}

	// FMaterialUniformExpression interface.
	virtual void Serialize(FArchive& Ar)
	{
		Ar << Input << Min << Max;
	}
	virtual void GetNumberValue(const FMaterialRenderContext& Context,FLinearColor& OutValue) const
	{
		FLinearColor ValueMin = FLinearColor::Black;
		FLinearColor ValueMax = FLinearColor::Black;
		FLinearColor ValueInput = FLinearColor::Black;
		Min->GetNumberValue(Context, ValueMin);
		Max->GetNumberValue(Context, ValueMax);
		Input->GetNumberValue(Context, ValueInput);

		OutValue.R = FMath::Clamp(ValueInput.R, ValueMin.R, ValueMax.R);
		OutValue.G = FMath::Clamp(ValueInput.G, ValueMin.G, ValueMax.G);
		OutValue.B = FMath::Clamp(ValueInput.B, ValueMin.B, ValueMax.B);
		OutValue.A = FMath::Clamp(ValueInput.A, ValueMin.A, ValueMax.A);
	}
	virtual bool IsConstant() const
	{
		return Input->IsConstant() && Min->IsConstant() && Max->IsConstant();
	}
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const
	{
		if (GetType() != OtherExpression->GetType())
		{
			return false;
		}
		FMaterialUniformExpressionClamp* OtherClamp = (FMaterialUniformExpressionClamp*)OtherExpression;
		return Input->IsIdentical(OtherClamp->Input) && Min->IsIdentical(OtherClamp->Min) && Max->IsIdentical(OtherClamp->Max);
	}

private:
	TRefCountPtr<FMaterialUniformExpression> Input;
	TRefCountPtr<FMaterialUniformExpression> Min;
	TRefCountPtr<FMaterialUniformExpression> Max;
};

/**
 */
class FMaterialUniformExpressionSaturate: public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionSaturate);
public:

	FMaterialUniformExpressionSaturate() {}
	FMaterialUniformExpressionSaturate(FMaterialUniformExpression* InInput):
		Input(InInput)
	{}

	// FMaterialUniformExpression interface.
	virtual void Serialize(FArchive& Ar)
	{
		Ar << Input;
	}
	virtual void GetNumberValue(const FMaterialRenderContext& Context,FLinearColor& OutValue) const
	{
		FLinearColor ValueInput = FLinearColor::Black;
		Input->GetNumberValue(Context, ValueInput);

		OutValue.R = FMath::Clamp<float>(ValueInput.R, 0, 1);
		OutValue.G = FMath::Clamp<float>(ValueInput.G, 0, 1);
		OutValue.B = FMath::Clamp<float>(ValueInput.B, 0, 1);
		OutValue.A = FMath::Clamp<float>(ValueInput.A, 0, 1);
	}
	virtual bool IsConstant() const
	{
		return Input->IsConstant();
	}
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const
	{
		if (GetType() != OtherExpression->GetType())
		{
			return false;
		}
		FMaterialUniformExpressionSaturate* OtherClamp = (FMaterialUniformExpressionSaturate*)OtherExpression;
		return Input->IsIdentical(OtherClamp->Input);
	}

private:
	TRefCountPtr<FMaterialUniformExpression> Input;
};

class FMaterialUniformExpressionComponentSwizzle : public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionComponentSwizzle);
public:

	FMaterialUniformExpressionComponentSwizzle() {}
	FMaterialUniformExpressionComponentSwizzle(FMaterialUniformExpression* InX, int8 InR, int8 InG, int8 InB, int8 InA) :
		X(InX),
		IndexR(InR),
		IndexG(InG),
		IndexB(InB),
		IndexA(InA)
	{
		NumElements = 0;
		if (InA >= 0)
		{
			check(InA <= 3);
			++NumElements;
			check(InB >= 0);
		}

		if (InB >= 0)
		{
			check(InB <= 3);
			++NumElements;
			check(InG >= 0);
		}

		if (InG >= 0)
		{
			check(InG <= 3);
			++NumElements;
		}

		// At least one proper index
		check(InR >= 0 && InR <= 3);
		++NumElements;
	}

	// FMaterialUniformExpression interface.
	virtual void Serialize(FArchive& Ar)
	{
		Ar << X;
		Ar << IndexR;
		Ar << IndexG;
		Ar << IndexB;
		Ar << IndexA;
		Ar << NumElements;
	}
	virtual void GetNumberValue(const FMaterialRenderContext& Context, FLinearColor& OutValue) const
	{
		FLinearColor Temp = OutValue;
		X->GetNumberValue(Context, Temp);
		// Clear
		OutValue *= 0;
		switch (NumElements)
		{
		case 1:
			// Replicate scalar
			OutValue.R = OutValue.G = OutValue.B = OutValue.A = Temp.Component(IndexR);
			break;
		case 4:
			OutValue.A = Temp.Component(IndexA);
			// Fallthrough...
		case 3:
			OutValue.B = Temp.Component(IndexB);
			// Fallthrough...
		case 2:
			OutValue.G = Temp.Component(IndexG);
			OutValue.R = Temp.Component(IndexR);
			break;
		default: UE_LOG(LogMaterial, Fatal, TEXT("Invalid number of swizzle elements: %d"), NumElements);
		}
	}
	virtual bool IsConstant() const
	{
		return X->IsConstant();
	}
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const
	{
		if (GetType() != OtherExpression->GetType())
		{
			return false;
		}
		auto* OtherSwizzle = (FMaterialUniformExpressionComponentSwizzle*)OtherExpression;
		return X->IsIdentical(OtherSwizzle->X) &&
			NumElements == OtherSwizzle->NumElements &&
			IndexR == OtherSwizzle->IndexR &&
			IndexG == OtherSwizzle->IndexG &&
			IndexB == OtherSwizzle->IndexB &&
			IndexA == OtherSwizzle->IndexA;
	}

private:
	TRefCountPtr<FMaterialUniformExpression> X;
	int8 IndexR;
	int8 IndexG;
	int8 IndexB;
	int8 IndexA;
	int8 NumElements;
};

/**
 */
class FMaterialUniformExpressionFloor: public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionFloor);
public:

	FMaterialUniformExpressionFloor() {}
	FMaterialUniformExpressionFloor(FMaterialUniformExpression* InX):
		X(InX)
	{}

	// FMaterialUniformExpression interface.
	virtual void Serialize(FArchive& Ar)
	{
		Ar << X;
	}
	virtual void GetNumberValue(const FMaterialRenderContext& Context,FLinearColor& OutValue) const
	{
		X->GetNumberValue(Context, OutValue);

		OutValue.R = FMath::FloorToInt(OutValue.R);
		OutValue.G = FMath::FloorToInt(OutValue.G);
		OutValue.B = FMath::FloorToInt(OutValue.B);
		OutValue.A = FMath::FloorToInt(OutValue.A);
	}
	virtual bool IsConstant() const
	{
		return X->IsConstant();
	}
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const
	{
		if (GetType() != OtherExpression->GetType())
		{
			return false;
		}
		FMaterialUniformExpressionFloor* OtherFloor = (FMaterialUniformExpressionFloor*)OtherExpression;
		return X->IsIdentical(OtherFloor->X);
	}

private:
	TRefCountPtr<FMaterialUniformExpression> X;
};

/**
 */
class FMaterialUniformExpressionCeil: public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionCeil);
public:

	FMaterialUniformExpressionCeil() {}
	FMaterialUniformExpressionCeil(FMaterialUniformExpression* InX):
		X(InX)
	{}

	// FMaterialUniformExpression interface.
	virtual void Serialize(FArchive& Ar)
	{
		Ar << X;
	}
	virtual void GetNumberValue(const FMaterialRenderContext& Context,FLinearColor& OutValue) const
	{
		X->GetNumberValue(Context, OutValue);

		OutValue.R = FMath::CeilToInt(OutValue.R);
		OutValue.G = FMath::CeilToInt(OutValue.G);
		OutValue.B = FMath::CeilToInt(OutValue.B);
		OutValue.A = FMath::CeilToInt(OutValue.A);
	}
	virtual bool IsConstant() const
	{
		return X->IsConstant();
	}
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const
	{
		if (GetType() != OtherExpression->GetType())
		{
			return false;
		}
		FMaterialUniformExpressionCeil* OtherCeil = (FMaterialUniformExpressionCeil*)OtherExpression;
		return X->IsIdentical(OtherCeil->X);
	}

private:
	TRefCountPtr<FMaterialUniformExpression> X;
};

/**
 */
class FMaterialUniformExpressionRound: public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionRound);
public:

	FMaterialUniformExpressionRound() {}
	FMaterialUniformExpressionRound(FMaterialUniformExpression* InX):
		X(InX)
	{}

	// FMaterialUniformExpression interface.
	virtual void Serialize(FArchive& Ar)
	{
		Ar << X;
	}
	virtual void GetNumberValue(const FMaterialRenderContext& Context,FLinearColor& OutValue) const
	{
		X->GetNumberValue(Context, OutValue);

		OutValue.R = FMath::RoundToFloat(OutValue.R);
		OutValue.G = FMath::RoundToFloat(OutValue.G);
		OutValue.B = FMath::RoundToFloat(OutValue.B);
		OutValue.A = FMath::RoundToFloat(OutValue.A);
	}
	virtual bool IsConstant() const
	{
		return X->IsConstant();
	}
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const
	{
		if (GetType() != OtherExpression->GetType())
		{
			return false;
		}
		FMaterialUniformExpressionRound* OtherRound = (FMaterialUniformExpressionRound*)OtherExpression;
		return X->IsIdentical(OtherRound->X);
	}

private:
	TRefCountPtr<FMaterialUniformExpression> X;
};

/**
 */
class FMaterialUniformExpressionTruncate: public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionTruncate);
public:

	FMaterialUniformExpressionTruncate() {}
	FMaterialUniformExpressionTruncate(FMaterialUniformExpression* InX):
		X(InX)
	{}

	// FMaterialUniformExpression interface.
	virtual void Serialize(FArchive& Ar)
	{
		Ar << X;
	}
	virtual void GetNumberValue(const FMaterialRenderContext& Context,FLinearColor& OutValue) const
	{
		X->GetNumberValue(Context, OutValue);

		OutValue.R = FMath::TruncToFloat(OutValue.R);
		OutValue.G = FMath::TruncToFloat(OutValue.G);
		OutValue.B = FMath::TruncToFloat(OutValue.B);
		OutValue.A = FMath::TruncToFloat(OutValue.A);
	}
	virtual bool IsConstant() const
	{
		return X->IsConstant();
	}
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const
	{
		if (GetType() != OtherExpression->GetType())
		{
			return false;
		}
		FMaterialUniformExpressionTruncate* OtherTrunc = (FMaterialUniformExpressionTruncate*)OtherExpression;
		return X->IsIdentical(OtherTrunc->X);
	}

private:
	TRefCountPtr<FMaterialUniformExpression> X;
};

/**
 */
class FMaterialUniformExpressionSign: public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionSign);
public:

	FMaterialUniformExpressionSign() {}
	FMaterialUniformExpressionSign(FMaterialUniformExpression* InX):
		X(InX)
	{}

	// FMaterialUniformExpression interface.
	virtual void Serialize(FArchive& Ar)
	{
		Ar << X;
	}
	virtual void GetNumberValue(const FMaterialRenderContext& Context,FLinearColor& OutValue) const
	{
		X->GetNumberValue(Context, OutValue);

		OutValue.R = FMath::Sign(OutValue.R);
		OutValue.G = FMath::Sign(OutValue.G);
		OutValue.B = FMath::Sign(OutValue.B);
		OutValue.A = FMath::Sign(OutValue.A);
	}
	virtual bool IsConstant() const
	{
		return X->IsConstant();
	}
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const
	{
		if (GetType() != OtherExpression->GetType())
		{
			return false;
		}
		FMaterialUniformExpressionSign* OtherSign = (FMaterialUniformExpressionSign*)OtherExpression;
		return X->IsIdentical(OtherSign->X);
	}

private:
	TRefCountPtr<FMaterialUniformExpression> X;
};

/**
 */
class FMaterialUniformExpressionFrac: public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionFrac);
public:

	FMaterialUniformExpressionFrac() {}
	FMaterialUniformExpressionFrac(FMaterialUniformExpression* InX):
		X(InX)
	{}

	// FMaterialUniformExpression interface.
	virtual void Serialize(FArchive& Ar)
	{
		Ar << X;
	}
	virtual void GetNumberValue(const FMaterialRenderContext& Context,FLinearColor& OutValue) const
	{
		X->GetNumberValue(Context, OutValue);

		OutValue.R = OutValue.R - FMath::FloorToInt(OutValue.R);
		OutValue.G = OutValue.G - FMath::FloorToInt(OutValue.G);
		OutValue.B = OutValue.B - FMath::FloorToInt(OutValue.B);
		OutValue.A = OutValue.A - FMath::FloorToInt(OutValue.A);
	}
	virtual bool IsConstant() const
	{
		return X->IsConstant();
	}
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const
	{
		if (GetType() != OtherExpression->GetType())
		{
			return false;
		}
		FMaterialUniformExpressionFrac* OtherFrac = (FMaterialUniformExpressionFrac*)OtherExpression;
		return X->IsIdentical(OtherFrac->X);
	}

private:
	TRefCountPtr<FMaterialUniformExpression> X;
};

/**
 */
class FMaterialUniformExpressionFmod : public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionFmod);
public:

	FMaterialUniformExpressionFmod() {}
	FMaterialUniformExpressionFmod(FMaterialUniformExpression* InA,FMaterialUniformExpression* InB):
		A(InA),
		B(InB)
	{}

	// FMaterialUniformExpression interface.
	virtual void Serialize(FArchive& Ar)
	{
		Ar << A << B;
	}
	virtual void GetNumberValue(const FMaterialRenderContext& Context,FLinearColor& OutValue) const
	{
		FLinearColor ValueA = FLinearColor::Black;
		FLinearColor ValueB = FLinearColor::Black;
		A->GetNumberValue(Context, ValueA);
		B->GetNumberValue(Context, ValueB);

		OutValue.R = FMath::Fmod(ValueA.R, ValueB.R);
		OutValue.G = FMath::Fmod(ValueA.G, ValueB.G);
		OutValue.B = FMath::Fmod(ValueA.B, ValueB.B);
		OutValue.A = FMath::Fmod(ValueA.A, ValueB.A);
	}
	virtual bool IsConstant() const
	{
		return A->IsConstant() && B->IsConstant();
	}
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const
	{
		if (GetType() != OtherExpression->GetType())
		{
			return false;
		}
		FMaterialUniformExpressionFmod* OtherMax = (FMaterialUniformExpressionFmod*)OtherExpression;
		return A->IsIdentical(OtherMax->A) && B->IsIdentical(OtherMax->B);
	}

private:
	TRefCountPtr<FMaterialUniformExpression> A;
	TRefCountPtr<FMaterialUniformExpression> B;
};

/**
 * Absolute value evaluator for a given input expression
 */
class FMaterialUniformExpressionAbs: public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionAbs);
public:

	FMaterialUniformExpressionAbs() {}
	FMaterialUniformExpressionAbs( FMaterialUniformExpression* InX ):
		X(InX)
	{}

	// FMaterialUniformExpression interface.
	virtual void Serialize(FArchive& Ar)
	{
		Ar << X;
	}
	virtual void GetNumberValue(const FMaterialRenderContext& Context,FLinearColor& OutValue) const
	{
		X->GetNumberValue(Context, OutValue);
		OutValue.R = FMath::Abs<float>(OutValue.R);
		OutValue.G = FMath::Abs<float>(OutValue.G);
		OutValue.B = FMath::Abs<float>(OutValue.B);
		OutValue.A = FMath::Abs<float>(OutValue.A);
	}
	virtual bool IsConstant() const
	{
		return X->IsConstant();
	}
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const
	{
		if (GetType() != OtherExpression->GetType())
		{
			return false;
		}
		FMaterialUniformExpressionAbs* OtherAbs = (FMaterialUniformExpressionAbs*)OtherExpression;
		return X->IsIdentical(OtherAbs->X);
	}

private:
	TRefCountPtr<FMaterialUniformExpression> X;
};

/**
 */
class FMaterialUniformExpressionTextureProperty: public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionTextureProperty);
public:
	
	FMaterialUniformExpressionTextureProperty() {}
	FMaterialUniformExpressionTextureProperty(FMaterialUniformExpressionTexture* InTextureExpression, EMaterialExposedTextureProperty InTextureProperty)
		: TextureExpression(InTextureExpression)
		, TextureProperty(InTextureProperty)
	{}

	// FMaterialUniformExpression interface.
	virtual void Serialize(FArchive& Ar) override
	{
		Ar << TextureExpression << TextureProperty;
	}
	virtual void GetNumberValue(const FMaterialRenderContext& Context,FLinearColor& OutValue) const override
	{
		const UTexture* Texture = nullptr;
		TextureExpression->GetTextureValue(Context, Context.Material, Texture);

		if (!Texture || !Texture->Resource)
		{
			return;
		}
	
		if (TextureProperty == TMTM_TextureSize)
		{
			OutValue.R = Texture->Resource->GetSizeX();
			OutValue.G = Texture->Resource->GetSizeY();
		}
		else if (TextureProperty == TMTM_TexelSize)
		{
			OutValue.R = 1.0f / float(Texture->Resource->GetSizeX());
			OutValue.G = 1.0f / float(Texture->Resource->GetSizeY());
		}
		else
		{
			check(0);
		}
	}
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const override
	{
		if (GetType() != OtherExpression->GetType())
		{
			return false;
		}

		auto OtherTexturePropertyExpression = (const FMaterialUniformExpressionTextureProperty*)OtherExpression;
		
		if (TextureProperty != OtherTexturePropertyExpression->TextureProperty)
		{
			return false;
		}

		return TextureExpression->IsIdentical(OtherTexturePropertyExpression->TextureExpression);
	}
	
private:
	TRefCountPtr<FMaterialUniformExpressionTexture> TextureExpression;
	int8 TextureProperty;
};


/**
 * A uniform expression to lookup the UV coordinate rotation and scale for an external texture
 */
class FMaterialUniformExpressionExternalTextureCoordinateScaleRotation : public FMaterialUniformExpressionExternalTextureBase
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionExternalTextureCoordinateScaleRotation);
public:

	FMaterialUniformExpressionExternalTextureCoordinateScaleRotation(){}
	FMaterialUniformExpressionExternalTextureCoordinateScaleRotation(const FGuid& InGuid) : FMaterialUniformExpressionExternalTextureBase(InGuid) {}
	FMaterialUniformExpressionExternalTextureCoordinateScaleRotation(int32 InSourceTextureIndex, TOptional<FName> InParameterName) : FMaterialUniformExpressionExternalTextureBase(InSourceTextureIndex), ParameterName(InParameterName) {}

	virtual void Serialize(FArchive& Ar) override;
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const override;
	virtual void GetNumberValue(const FMaterialRenderContext& Context, FLinearColor& OutValue) const override;

protected:
	typedef FMaterialUniformExpressionExternalTextureBase Super;

	/** Optional texture parameter name */
	TOptional<FName> ParameterName;
};

/**
 * A uniform expression to lookup the UV coordinate offset for an external texture
 */
class FMaterialUniformExpressionExternalTextureCoordinateOffset : public FMaterialUniformExpressionExternalTextureBase
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionExternalTextureCoordinateOffset);
public:

	FMaterialUniformExpressionExternalTextureCoordinateOffset(){}
	FMaterialUniformExpressionExternalTextureCoordinateOffset(const FGuid& InGuid) : FMaterialUniformExpressionExternalTextureBase(InGuid) {}
	FMaterialUniformExpressionExternalTextureCoordinateOffset(int32 InSourceTextureIndex, TOptional<FName> InParameterName) : FMaterialUniformExpressionExternalTextureBase(InSourceTextureIndex), ParameterName(InParameterName) {}

	virtual void Serialize(FArchive& Ar) override;
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const override;
	virtual void GetNumberValue(const FMaterialRenderContext& Context, FLinearColor& OutValue) const override;

protected:
	typedef FMaterialUniformExpressionExternalTextureBase Super;

	/** Optional texture parameter name */
	TOptional<FName> ParameterName;
};

/**
 * A uniform expression to retrieve one of the parameters associated with a URuntimeVirtualTexture
 */
class FMaterialUniformExpressionRuntimeVirtualTextureParameter : public FMaterialUniformExpression
{
	DECLARE_MATERIALUNIFORMEXPRESSION_TYPE(FMaterialUniformExpressionRuntimeVirtualTextureParameter);

public:
	FMaterialUniformExpressionRuntimeVirtualTextureParameter();
	/** Construct with the index of the texture reference and the parameter index that we want to retrieve. */
	FMaterialUniformExpressionRuntimeVirtualTextureParameter(int32 InTextureIndex, int32 InParamIndex);

	//~ Begin FMaterialUniformExpression Interface.
	virtual bool IsConstant() const override { return false; }
	virtual void Serialize(FArchive& Ar) override;
	virtual bool IsIdentical(const FMaterialUniformExpression* OtherExpression) const override;
	virtual void GetNumberValue(const struct FMaterialRenderContext& Context, FLinearColor& OutValue) const override;
	//~ End FMaterialUniformExpression Interface.

protected:
	/** Index of a URuntimeVirtualTexture in the material texture references. */
	int32 TextureIndex;
	/** Index of the parameter to fetch from the URuntimeVirtualTexture. */
	int32 ParamIndex;
};
