// Copyright Epic Games, Inc. All Rights Reserved.


#include "TessellationRendering.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"

/** Returns true if the Material and Vertex Factory combination require adjacency information.
  * Game thread version that looks at the material settings. Will not change answer during a shader compile */
bool MaterialSettingsRequireAdjacencyInformation_GameThread(UMaterialInterface* Material, const FVertexFactoryType* VertexFactoryType, const FStaticFeatureLevel InFeatureLevel)
{
	check(IsInGameThread());

	//if we pass null here we have to guarantee that the VF supports tessellation (e.g by using type LocalVF)
	bool VertexFactorySupportsTessellation = !VertexFactoryType || (VertexFactoryType && VertexFactoryType->SupportsTessellationShaders());

	if (RHISupportsTessellation(GShaderPlatformForFeatureLevel[InFeatureLevel]) && VertexFactorySupportsTessellation && Material)
	{
		UMaterial* BaseMaterial = Material->GetMaterial();
		check(BaseMaterial);
		EMaterialTessellationMode TessellationMode = (EMaterialTessellationMode)BaseMaterial->D3D11TessellationMode;
		bool bEnableCrackFreeDisplacement = BaseMaterial->bEnableCrackFreeDisplacement;
		return TessellationMode == MTM_PNTriangles || (TessellationMode == MTM_FlatTessellation && bEnableCrackFreeDisplacement);
	}
	return false;
}

/** Returns true if the Material and Vertex Factory combination require adjacency information.
  * Rendering thread version that looks at the current shader that will be used. **Will change answer during a shader compile** */
bool MaterialRenderingRequiresAdjacencyInformation_RenderingThread(UMaterialInterface* Material, const FVertexFactoryType* VertexFactoryType, const FStaticFeatureLevel InFeatureLevel)
{
	check(IsInRenderingThread() || IsInParallelRenderingThread());

	//if we pass null here we have to guarantee that the VF supports tessellation (e.g by using type LocalVF)
	bool VertexFactorySupportsTessellation = !VertexFactoryType || (VertexFactoryType && VertexFactoryType->SupportsTessellationShaders());

	if (RHISupportsTessellation(GShaderPlatformForFeatureLevel[InFeatureLevel]) && VertexFactorySupportsTessellation && Material)
	{
		FMaterialRenderProxy* MaterialRenderProxy = Material->GetRenderProxy();
		if (ensureMsgf(MaterialRenderProxy, TEXT("Could not determine if RequiresAdjacencyInformation. Invalid MaterialRenderProxy on Material '%s'"), *GetNameSafe(Material)))
		{
			const FMaterial* MaterialResource = MaterialRenderProxy->GetMaterial(InFeatureLevel);
			if (ensureMsgf(MaterialResource, TEXT("Could not determine if RequiresAdjacencyInformation. Invalid MaterialResource on Material '%s'"), *GetNameSafe(Material)))
			{
				EMaterialTessellationMode TessellationMode = MaterialResource->GetTessellationMode();
				bool bEnableCrackFreeDisplacement = MaterialResource->IsCrackFreeDisplacementEnabled();
				return TessellationMode == MTM_PNTriangles || (TessellationMode == MTM_FlatTessellation && bEnableCrackFreeDisplacement);
			}
		}
	}
	return false;
}

/** Returns true if the Material and Vertex Factory combination require adjacency information.
  * Returns different information depending on whether it is called on the rendering thread or game thread -
  * On the game thread, it looks at the material settings. Will not change answer during a shader compile
  * On the rendering thread, it looks at the current shader that will be used. **Will change answer during a shader compile**
  *
  * WARNING: In single-threaded mode as the game thread will return the rendering thread information
  * Please use the explicit game/render thread functions above instead */
bool RequiresAdjacencyInformation(UMaterialInterface* Material, const FVertexFactoryType* VertexFactoryType, const FStaticFeatureLevel InFeatureLevel)
{
	if (!RHISupportsTessellation(GShaderPlatformForFeatureLevel[InFeatureLevel]))
	{
		return false;
	}

	if (IsInRenderingThread() || IsInParallelRenderingThread())
	{
		return MaterialRenderingRequiresAdjacencyInformation_RenderingThread(Material, VertexFactoryType, InFeatureLevel);
	}
	else if (IsInGameThread())
	{
		return MaterialSettingsRequireAdjacencyInformation_GameThread(Material, VertexFactoryType, InFeatureLevel);
	}
	else
	{
		//if we pass null here we have to guarantee that the VF supports tessellation (e.g by using type LocalVF)
		bool VertexFactorySupportsTessellation = !VertexFactoryType || (VertexFactoryType && VertexFactoryType->SupportsTessellationShaders());

		// Concurrent?
		if (VertexFactorySupportsTessellation && Material)
		{
			const UMaterial* BaseMaterial = Material->GetMaterial_Concurrent();
			check(BaseMaterial);
			EMaterialTessellationMode TessellationMode = (EMaterialTessellationMode)BaseMaterial->D3D11TessellationMode;
			bool bEnableCrackFreeDisplacement = BaseMaterial->bEnableCrackFreeDisplacement;
			return TessellationMode == MTM_PNTriangles || (TessellationMode == MTM_FlatTessellation && bEnableCrackFreeDisplacement);
		}
	}
	return false;
}
