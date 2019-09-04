// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "StereoStaticMeshComponent.h"
#include "StaticMeshResources.h"
#include "Engine/StaticMesh.h"


class FStereoStaticMeshSceneProxy final
	: public FStaticMeshSceneProxy
{
    ESPStereoCameraLayer EyeToRender;

public:
	SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

    FStereoStaticMeshSceneProxy(UStereoStaticMeshComponent* Component) :
        FStaticMeshSceneProxy(Component, false)
    {
        EyeToRender = Component->EyeToRender;
    }

    virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
    {
        FPrimitiveViewRelevance viewRelevance = FStaticMeshSceneProxy::GetViewRelevance(View);
        bool bVisible = true;

        switch (View->StereoPass)
        {
        case eSSP_RIGHT_EYE:
            if ((EyeToRender != ESPStereoCameraLayer::RightEye) && (EyeToRender != ESPStereoCameraLayer::BothEyes))
            {
                bVisible = false;
            }
            break;

        case eSSP_LEFT_EYE:
            if ((EyeToRender != ESPStereoCameraLayer::LeftEye) && (EyeToRender != ESPStereoCameraLayer::BothEyes))
            {
                bVisible = false;
            }
            break;

        case eSSP_FULL:
        default:
            //Draw both planes when in mono mode
            break;
        }

        viewRelevance.bDrawRelevance &= bVisible;

        return viewRelevance;

    }
};


FPrimitiveSceneProxy* UStereoStaticMeshComponent::CreateSceneProxy()
{
	const auto FeatureLevel = GetScene()->GetFeatureLevel();

    if ((GetStaticMesh() == nullptr) ||
		(GetStaticMesh()->RenderData == nullptr) ||
		(GetStaticMesh()->RenderData->LODResources.Num() == 0) ||
		(GetStaticMesh()->RenderData->LODResources[GetStaticMesh()->MinLOD.GetValueForFeatureLevel(FeatureLevel)].VertexBuffers.PositionVertexBuffer.GetNumVertices() == 0))
    {
        return nullptr;
    }

    FPrimitiveSceneProxy* Proxy = ::new FStereoStaticMeshSceneProxy(this);

	return Proxy;
}
