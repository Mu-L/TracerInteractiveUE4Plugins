// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "MaterialProxySettingsCustomizations.h"
#include "Misc/Attribute.h"
#include "UObject/UnrealType.h"
#include "Engine/MaterialMerging.h"
#include "PropertyHandle.h"
#include "IDetailChildrenBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailPropertyRow.h"
#include "RHI.h"
#include "Modules/ModuleManager.h"
#include "IMeshReductionManagerModule.h"
#include "IMeshReductionInterfaces.h" // IMeshMerging
#include "PropertyRestriction.h"

#define LOCTEXT_NAMESPACE "MaterialProxySettingsCustomizations"


TSharedRef<IPropertyTypeCustomization> FMaterialProxySettingsCustomizations::MakeInstance()
{
	return MakeShareable(new FMaterialProxySettingsCustomizations);
}

void FMaterialProxySettingsCustomizations::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	HeaderRow.
		NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			StructPropertyHandle->CreatePropertyValueWidget()
		];
}

bool FMaterialProxySettingsCustomizations::UseNativeProxyLODTool() const
{
	IMeshMerging* MergeModule = FModuleManager::Get().LoadModuleChecked<IMeshReductionManagerModule>("MeshReductionInterface").GetMeshMergingInterface();
	return MergeModule && MergeModule->GetName().Equals("ProxyLODMeshMerging");
}

void FMaterialProxySettingsCustomizations::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	// Retrieve structure's child properties
	uint32 NumChildren;
	StructPropertyHandle->GetNumChildren( NumChildren );	
	TMap<FName, TSharedPtr< IPropertyHandle > > PropertyHandles;	
	for( uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex )
	{
		TSharedRef<IPropertyHandle> ChildHandle = StructPropertyHandle->GetChildHandle( ChildIndex ).ToSharedRef();
		const FName PropertyName = ChildHandle->GetProperty()->GetFName();

		PropertyHandles.Add(PropertyName, ChildHandle);
	}

	// Determine if we are using our native module  If so, we will supress some of the options used by the current thirdparty tool (simplygon).
	// NB: this only needs to be called once (static) since the tool can only change on editor restart
	static bool bUseNativeTool = UseNativeProxyLODTool();
	

	// Retrieve special case properties
	EnumHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FMaterialProxySettings, TextureSizingType));
	TextureSizeHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FMaterialProxySettings, TextureSize));
	PropertyTextureSizeHandles.Add(PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FMaterialProxySettings, DiffuseTextureSize)));
	PropertyTextureSizeHandles.Add(PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FMaterialProxySettings, NormalTextureSize)));
	PropertyTextureSizeHandles.Add(PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FMaterialProxySettings, MetallicTextureSize)));
	PropertyTextureSizeHandles.Add(PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FMaterialProxySettings, RoughnessTextureSize)));
	PropertyTextureSizeHandles.Add(PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FMaterialProxySettings, SpecularTextureSize)));
	PropertyTextureSizeHandles.Add(PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FMaterialProxySettings, EmissiveTextureSize)));
	PropertyTextureSizeHandles.Add(PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FMaterialProxySettings, OpacityTextureSize)));
	PropertyTextureSizeHandles.Add(PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FMaterialProxySettings, OpacityMaskTextureSize)));
	PropertyTextureSizeHandles.Add(PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FMaterialProxySettings, AmbientOcclusionTextureSize)));

	if (PropertyHandles.Contains(GET_MEMBER_NAME_CHECKED(FMaterialProxySettings, MaterialMergeType)))
	{
		MergeTypeHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FMaterialProxySettings, MaterialMergeType));
	}
	
	GutterSpaceHandle = PropertyHandles.FindChecked(GET_MEMBER_NAME_CHECKED(FMaterialProxySettings, GutterSpace));

	auto Parent = StructPropertyHandle->GetParentHandle();

	for( auto Iter(PropertyHandles.CreateIterator()); Iter; ++Iter  )
	{
		// Handle special property cases (done inside the loop to maintain order according to the struct
		if (PropertyTextureSizeHandles.Contains(Iter.Value()))
		{
			IDetailPropertyRow& SizeRow = ChildBuilder.AddProperty(Iter.Value().ToSharedRef());
			SizeRow.Visibility(TAttribute<EVisibility>(this, &FMaterialProxySettingsCustomizations::AreManualOverrideTextureSizesEnabled));
			AddTextureSizeClamping(Iter.Value());
		}
		else if (Iter.Value() == TextureSizeHandle)
		{
			IDetailPropertyRow& SettingsRow = ChildBuilder.AddProperty(Iter.Value().ToSharedRef());
			SettingsRow.Visibility(TAttribute<EVisibility>(this, &FMaterialProxySettingsCustomizations::IsTextureSizeEnabled));
			AddTextureSizeClamping(Iter.Value());
		}
		else if (Iter.Value() == GutterSpaceHandle)
		{
			IDetailPropertyRow& SettingsRow = ChildBuilder.AddProperty(Iter.Value().ToSharedRef());
			SettingsRow.Visibility(TAttribute<EVisibility>(this, &FMaterialProxySettingsCustomizations::IsSimplygonMaterialMergingVisible));
		}
		else if (Iter.Value() == EnumHandle)
		{
			// Remove the simplygon specific option.
			if (bUseNativeTool)
			{
				TSharedPtr<FPropertyRestriction> EnumRestriction = MakeShareable(new FPropertyRestriction(LOCTEXT("NoSupport", "Unable to support this option in Merge Actor")));
				const UEnum* const TextureSizingTypeEnum = FindObject<UEnum>(ANY_PACKAGE, TEXT("ETextureSizingType"));
				EnumRestriction->AddHiddenValue(TextureSizingTypeEnum->GetNameStringByValue((uint8)ETextureSizingType::TextureSizingType_UseSimplygonAutomaticSizing));
				EnumHandle->AddRestriction(EnumRestriction.ToSharedRef());
			}

			IDetailPropertyRow& SettingsRow = ChildBuilder.AddProperty(Iter.Value().ToSharedRef());
		}
		// Do not show the merge type property
		else if (Iter.Value() != MergeTypeHandle)
		{
			IDetailPropertyRow& SettingsRow = ChildBuilder.AddProperty(Iter.Value().ToSharedRef());
		}
	}	

}

void FMaterialProxySettingsCustomizations::AddTextureSizeClamping(TSharedPtr<IPropertyHandle> TextureSizeProperty)
{
	TSharedPtr<IPropertyHandle> PropertyX = TextureSizeProperty->GetChildHandle(GET_MEMBER_NAME_CHECKED(FIntPoint, X));
	TSharedPtr<IPropertyHandle> PropertyY = TextureSizeProperty->GetChildHandle(GET_MEMBER_NAME_CHECKED(FIntPoint, Y));
	// NB: the current gobal value 16384 GetMax2DTextureDimension() will cause int32 overflow for 32bit color formats 
	//     with 16 bytes per pixel.
	//     See implimentations of ImageUtils.cpp :: GetRawData()  and ImageCore.cpp :: CopyTo()
	//     11585 = Floor( Sqrt ( Max_int32 / 16)  )
	const int32 TmpMaxSize = FMath::FloorToInt(FMath::Sqrt(MAX_int32 / 16));
	const int32 MaxProxyTextureResolution = FMath::Min(TmpMaxSize, (int32)GetMax2DTextureDimension());
	
	const FString MaxTextureResolutionString = FString::FromInt(MaxProxyTextureResolution);
	TextureSizeProperty->GetProperty()->SetMetaData(TEXT("ClampMax"), *MaxTextureResolutionString);
	TextureSizeProperty->GetProperty()->SetMetaData(TEXT("UIMax"), *MaxTextureResolutionString);
	PropertyX->GetProperty()->SetMetaData(TEXT("ClampMax"), *MaxTextureResolutionString);
	PropertyX->GetProperty()->SetMetaData(TEXT("UIMax"), *MaxTextureResolutionString);
	PropertyY->GetProperty()->SetMetaData(TEXT("ClampMax"), *MaxTextureResolutionString);
	PropertyY->GetProperty()->SetMetaData(TEXT("UIMax"), *MaxTextureResolutionString);

	const FString MinTextureResolutionString("1");
	PropertyX->GetProperty()->SetMetaData(TEXT("ClampMin"), *MinTextureResolutionString);
	PropertyX->GetProperty()->SetMetaData(TEXT("UIMin"), *MinTextureResolutionString);
	PropertyY->GetProperty()->SetMetaData(TEXT("ClampMin"), *MinTextureResolutionString);
	PropertyY->GetProperty()->SetMetaData(TEXT("UIMin"), *MinTextureResolutionString);
}

EVisibility FMaterialProxySettingsCustomizations::AreManualOverrideTextureSizesEnabled() const
{
	uint8 TypeValue;
	EnumHandle->GetValue(TypeValue);

	if (TypeValue == TextureSizingType_UseManualOverrideTextureSize)
	{
		return EVisibility::Visible;
	}

	return EVisibility::Hidden;
}

EVisibility FMaterialProxySettingsCustomizations::IsTextureSizeEnabled() const
{
	uint8 TypeValue;
	EnumHandle->GetValue(TypeValue);

	if (TypeValue == TextureSizingType_UseSimplygonAutomaticSizing || TypeValue == TextureSizingType_UseManualOverrideTextureSize)
	{		
		return EVisibility::Hidden;
	}

	return EVisibility::Visible;	
}

EVisibility FMaterialProxySettingsCustomizations::IsSimplygonMaterialMergingVisible() const
{
	uint8 MergeType = EMaterialMergeType::MaterialMergeType_Default;
	if (MergeTypeHandle.IsValid())
	{
		MergeTypeHandle->GetValue(MergeType);
	}

	return ( MergeType == EMaterialMergeType::MaterialMergeType_Simplygon ) ? EVisibility::Visible : EVisibility::Hidden;
}


#undef LOCTEXT_NAMESPACE
