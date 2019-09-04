// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Units/Control/RigUnit_Control_StaticMesh.h"
#include "ControlRigStaticMeshControl.h"

FRigUnit_Control_StaticMesh::FRigUnit_Control_StaticMesh()
#if WITH_EDITORONLY_DATA
	: StaticMesh(nullptr)
	, MeshTransform(FTransform::Identity)
#endif
{
#if WITH_EDITORONLY_DATA
	ControlClass = AControlRigStaticMeshControl::StaticClass();
#endif 
}
