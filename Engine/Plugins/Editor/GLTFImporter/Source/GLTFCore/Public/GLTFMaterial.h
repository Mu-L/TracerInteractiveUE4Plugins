// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/SecureHash.h"

namespace GLTF
{
	struct GLTFCORE_API FTextureMap
	{
		int32 TextureIndex;
		uint8 TexCoord;

		FTextureMap()
		    : TextureIndex(INDEX_NONE)
		    , TexCoord(0)
		{
		}

		FMD5Hash GetHash() const
		{
			FMD5 MD5;
			MD5.Update(reinterpret_cast<const uint8*>(&TextureIndex), sizeof(TextureIndex));
			MD5.Update(&TexCoord, sizeof(TexCoord));
			FMD5Hash Hash;
			Hash.Set(MD5);
			return Hash;
		}
	};

	struct GLTFCORE_API FMaterial
	{
		enum class EAlphaMode
		{
			Opaque,
			Mask,
			Blend
		};

		enum class EShadingModel
		{
			MetallicRoughness,
			SpecularGlossiness,
		};

		enum class EPackingFlags
		{
			// no packing, i.e. default: Unused (R) Roughness (G), Metallic (B) map
			None = 0x0,
			// packing two channel (RG) normal map
			NormalRG = 0x1,
			// packing Occlusion (R), Roughness (G), Metallic (B) map
			OcclusionRoughnessMetallic = 0x2,
			// packing Roughness (R), Metallic (G), Occlusion (B) map
			RoughnessMetallicOcclusion = 0x4,
			// packing Normal (RG), Roughness (B), Metallic (A) map
			NormalRoughnessMetallic = NormalRG | 0x8,
		};

		struct FMetallicRoughness
		{
			FTextureMap Map;
			float       MetallicFactor;
			float       RoughnessFactor;

			FMetallicRoughness()
			    : MetallicFactor(1.f)
			    , RoughnessFactor(1.f)
			{
			}
		};
		struct FSpecularGlossiness
		{
			FTextureMap Map;
			FVector     SpecularFactor;
			float       GlossinessFactor;

			FSpecularGlossiness()
			    : SpecularFactor(1.f)
			    , GlossinessFactor(1.f)
			{
			}
		};

		struct FPacking
		{
			int         Flags;  // see EPackingFlags
			FTextureMap Map;
			FTextureMap NormalMap;

			FPacking()
			    : Flags((int)EPackingFlags::None)
			{
			}
		};

		FString Name;

		// PBR properties
		FTextureMap         BaseColor;		 // Used for DiffuseColor on Specular-Glossiness mode
		FVector4            BaseColorFactor; // Used for DiffuseFactor on Specular-Glossiness mode
		EShadingModel       ShadingModel;
		FMetallicRoughness  MetallicRoughness;
		FSpecularGlossiness SpecularGlossiness;

		// base properties
		FTextureMap Normal;
		FTextureMap Occlusion;
		FTextureMap Emissive;
		float       NormalScale;
		float       OcclusionStrength;
		FVector     EmissiveFactor;

		// material properties
		bool       bIsDoubleSided;
		EAlphaMode AlphaMode;
		float      AlphaCutoff;  // only used when AlphaMode == Mask

		// extension properties
		FPacking Packing;
		bool     bIsUnlitShadingModel;

		FMaterial(const FString& Name)
		    : Name(Name)
		    , BaseColorFactor {1.0f, 1.0f, 1.0f, 1.0f}
		    , ShadingModel(EShadingModel::MetallicRoughness)
		    , NormalScale(1.f)
		    , OcclusionStrength(1.f)
		    , EmissiveFactor(FVector::ZeroVector)
		    , bIsDoubleSided(false)
		    , AlphaMode(EAlphaMode::Opaque)
		    , AlphaCutoff(0.5f)
		    , bIsUnlitShadingModel(false)
		{
		}

		bool IsOpaque() const
		{
			return AlphaMode == EAlphaMode::Opaque;
		}

		FMD5Hash GetHash() const;
	};
}  // namespace GLTF

