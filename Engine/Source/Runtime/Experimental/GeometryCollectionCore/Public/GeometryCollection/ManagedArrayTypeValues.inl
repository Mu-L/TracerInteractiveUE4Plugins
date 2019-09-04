// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// usage
//
// General purpose ManagedArrayCollection::ArrayType definition

#ifndef MANAGED_ARRAY_TYPE
#error MANAGED_ARRAY_TYPE macro is undefined.
#endif

// NOTE: new types must be added at the bottom to keep serialization from breaking

MANAGED_ARRAY_TYPE(FVector, Vector)
MANAGED_ARRAY_TYPE(FIntVector, IntVector)
MANAGED_ARRAY_TYPE(FVector2D, Vector2D)
MANAGED_ARRAY_TYPE(FLinearColor, LinearColor)
MANAGED_ARRAY_TYPE(int32, Int32)
MANAGED_ARRAY_TYPE(bool, Bool)
MANAGED_ARRAY_TYPE(FTransform, Transform)
MANAGED_ARRAY_TYPE(FString, String)
MANAGED_ARRAY_TYPE(float, Float)
MANAGED_ARRAY_TYPE(FQuat, Quat)
MANAGED_ARRAY_TYPE(FGeometryCollectionBoneNode, BoneNode)
MANAGED_ARRAY_TYPE(FGeometryCollectionSection, MeshSection)
MANAGED_ARRAY_TYPE(FBox, Box)
MANAGED_ARRAY_TYPE(TSet<int32>, IntArray)
MANAGED_ARRAY_TYPE(FGuid, Guid)
MANAGED_ARRAY_TYPE(uint8, UInt8)
MANAGED_ARRAY_TYPE(TArray<FVector>*, VectorArrayPointer)
MANAGED_ARRAY_TYPE(TUniquePtr<TArray<FVector>>, VectorArrayUniquePointer)
MANAGED_ARRAY_TYPE(Chaos::FImplicitObject3*, FImplicitObject3Pointer)
MANAGED_ARRAY_TYPE(TUniquePtr<Chaos::FImplicitObject3>, FImplicitObject3UniquePointer)
MANAGED_ARRAY_TYPE(Chaos::TSerializablePtr<Chaos::FImplicitObject3>, FImplicitObject3SerializablePtr)
MANAGED_ARRAY_TYPE(Chaos::FBVHParticlesFloat3, FBVHParticlesFloat3Pointer)
MANAGED_ARRAY_TYPE(TUniquePtr<Chaos::FBVHParticlesFloat3>, FBVHParticlesFloat3UniquePointer)

// NOTE: new types must be added at the bottom to keep serialization from breaking


#undef MANAGED_ARRAY_TYPE
