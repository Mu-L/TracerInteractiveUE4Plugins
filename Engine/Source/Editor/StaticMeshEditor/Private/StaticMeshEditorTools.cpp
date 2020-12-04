// Copyright Epic Games, Inc. All Rights Reserved.

#include "StaticMeshEditorTools.h"
#include "Framework/Commands/UIAction.h"
#include "Textures/SlateIcon.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "EngineDefines.h"
#include "EditorStyleSet.h"
#include "PropertyHandle.h"
#include "IDetailGroup.h"
#include "IDetailChildrenBuilder.h"
#include "Misc/MessageDialog.h"
#include "Misc/FeedbackContext.h"
#include "Modules/ModuleManager.h"
#include "SlateOptMacros.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Materials/Material.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "RawMesh.h"
#include "MeshUtilities.h"
#include "StaticMeshResources.h"
#include "StaticMeshEditor.h"
#include "PropertyCustomizationHelpers.h"
#include "MaterialList.h"
#include "PhysicsEngine/BodySetup.h"
#include "FbxMeshUtils.h"
#include "Widgets/Input/SVectorInputBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "SPerPlatformPropertiesWidget.h"
#include "PlatformInfo.h"

#include "ContentStreaming.h"
#include "EditorDirectories.h"
#include "EditorFramework/AssetImportData.h"
#include "Engine/SkeletalMesh.h"
#include "EngineAnalytics.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "HAL/PlatformApplicationMisc.h"
#include "IMeshReductionManagerModule.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "JsonObjectConverter.h"
#include "Runtime/Analytics/Analytics/Public/Interfaces/IAnalyticsProvider.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/Input/SFilePathPicker.h"
#include "Widgets/Input/STextComboBox.h"

const uint32 MaxHullCount = 64;
const uint32 MinHullCount = 2;
const uint32 DefaultHullCount = 4;
const uint32 HullCountDelta = 1;

const uint32 MaxHullPrecision = 1000000;
const uint32 MinHullPrecision = 10000;
const uint32 DefaultHullPrecision = 100000;
const uint32 HullPrecisionDelta = 10000;


const int32 MaxVertsPerHullCount = 32;
const int32 MinVertsPerHullCount = 6;
const int32 DefaultVertsPerHull = 16;

#define LOCTEXT_NAMESPACE "StaticMeshEditor"
DEFINE_LOG_CATEGORY_STATIC(LogStaticMeshEditorTools,Log,All);

/*
* Custom data key
*/
enum SM_CustomDataKey
{
	CustomDataKey_LODVisibilityState = 0, //This is the key to know if a LOD is shown in custom mode. Do CustomDataKey_LODVisibilityState + LodIndex for a specific LOD
	CustomDataKey_LODEditMode = 100 //This is the key to know the state of the custom lod edit mode.
};


FStaticMeshDetails::FStaticMeshDetails( class FStaticMeshEditor& InStaticMeshEditor )
	: StaticMeshEditor( InStaticMeshEditor )
{}

FStaticMeshDetails::~FStaticMeshDetails()
{
}

void FStaticMeshDetails::CustomizeDetails( class IDetailLayoutBuilder& DetailBuilder )
{
	IDetailCategoryBuilder& LODSettingsCategory = DetailBuilder.EditCategory( "LodSettings", LOCTEXT("LodSettingsCategory", "LOD Settings") );
	IDetailCategoryBuilder& StaticMeshCategory = DetailBuilder.EditCategory( "StaticMesh", LOCTEXT("StaticMeshGeneralSettings", "General Settings") );
	IDetailCategoryBuilder& CollisionCategory = DetailBuilder.EditCategory( "Collision", LOCTEXT("CollisionCategory", "Collision") );
	IDetailCategoryBuilder& ImportSettingsCategory = DetailBuilder.EditCategory("ImportSettings");

	TSharedRef<IPropertyHandle> LightMapCoordinateIndexProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UStaticMesh, LightMapCoordinateIndex));
	TSharedRef<IPropertyHandle> LightMapResolutionProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UStaticMesh, LightMapResolution));
	LightMapCoordinateIndexProperty->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FStaticMeshDetails::OnLightmapSettingsChanged));
	LightMapResolutionProperty->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FStaticMeshDetails::OnLightmapSettingsChanged));

	TSharedRef<IPropertyHandle> StaticMaterials = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UStaticMesh, StaticMaterials));
	StaticMaterials->MarkHiddenByCustomization();

	TSharedRef<IPropertyHandle> ImportSettings = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UStaticMesh, AssetImportData));
	if (!StaticMeshEditor.GetStaticMesh() || 
		!StaticMeshEditor.GetStaticMesh()->AssetImportData ||
		!StaticMeshEditor.GetStaticMesh()->AssetImportData->IsA<UFbxStaticMeshImportData>())
	{
		ImportSettings->MarkResetToDefaultCustomized();

		IDetailPropertyRow& Row = ImportSettingsCategory.AddProperty(ImportSettings);
		Row.CustomWidget(true)
			.NameContent()
			[
				ImportSettings->CreatePropertyNameWidget()
			];
	}
	else
	{
		// If the AssetImportData is an instance of UFbxStaticMeshImportData we create a custom UI.
		// Since DetailCustomization UI is not supported on instanced properties and because IDetailLayoutBuilder does not work well inside instanced objects scopes,
		// we need to manually recreate the whole FbxStaticMeshImportData UI in order to customize it.
		ImportSettings->MarkHiddenByCustomization();
		VertexColorImportOptionHandle = ImportSettings->GetChildHandle(GET_MEMBER_NAME_CHECKED(UFbxStaticMeshImportData, VertexColorImportOption));
		VertexColorImportOverrideHandle = ImportSettings->GetChildHandle(GET_MEMBER_NAME_CHECKED(UFbxStaticMeshImportData, VertexOverrideColor));
		TMap<FName, IDetailGroup*> ExistingGroup;
		PropertyCustomizationHelpers::MakeInstancedPropertyCustomUI(ExistingGroup, ImportSettingsCategory, ImportSettings, FOnInstancedPropertyIteration::CreateSP(this, &FStaticMeshDetails::OnInstancedFbxStaticMeshImportDataPropertyIteration));
	}

	DetailBuilder.EditCategory( "Navigation", FText::GetEmpty(), ECategoryPriority::Uncommon );

	LevelOfDetailSettings = MakeShareable( new FLevelOfDetailSettingsLayout( StaticMeshEditor ) );

	LevelOfDetailSettings->AddToDetailsPanel( DetailBuilder );

	
	TSharedRef<IPropertyHandle> BodyProp = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UStaticMesh,BodySetup));
	BodyProp->MarkHiddenByCustomization();

	static TArray<FName> HiddenBodyInstanceProps;

	if( HiddenBodyInstanceProps.Num() == 0 )
	{
		//HiddenBodyInstanceProps.Add("DefaultInstance");
		HiddenBodyInstanceProps.Add("BoneName");
		HiddenBodyInstanceProps.Add("PhysicsType");
		HiddenBodyInstanceProps.Add("bConsiderForBounds");
		HiddenBodyInstanceProps.Add("CollisionReponse");
	}

	uint32 NumChildren = 0;
	BodyProp->GetNumChildren( NumChildren );

	TSharedPtr<IPropertyHandle> BodyPropObject;

	if( NumChildren == 1 )
	{
		// This is an edit inline new property so the first child is the object instance for the edit inline new.  The instance contains the child we want to display
		BodyPropObject = BodyProp->GetChildHandle( 0 );

		NumChildren = 0;
		BodyPropObject->GetNumChildren( NumChildren );

		for( uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex )
		{
			TSharedPtr<IPropertyHandle> ChildProp = BodyPropObject->GetChildHandle(ChildIndex);
			if( ChildProp.IsValid() && ChildProp->GetProperty() && !HiddenBodyInstanceProps.Contains(ChildProp->GetProperty()->GetFName()) )
			{
				CollisionCategory.AddProperty( ChildProp );
			}
		}
	}
}

void FStaticMeshDetails::OnInstancedFbxStaticMeshImportDataPropertyIteration(IDetailCategoryBuilder& BaseCategory, IDetailGroup* PropertyGroup, TSharedRef<IPropertyHandle>& Property) const
{
	IDetailPropertyRow* Row = nullptr;

	if (PropertyGroup)
	{
		Row = &PropertyGroup->AddPropertyRow(Property);
	}
	else
	{
		Row = &BaseCategory.AddProperty(Property);
	}

	if (Row)
	{
		//Vertex Override Color property should be disabled if we are not in override mode.
		if (Property->IsValidHandle() && Property->GetProperty() == VertexColorImportOverrideHandle->GetProperty())
		{
			Row->IsEnabled(TAttribute<bool>(this, &FStaticMeshDetails::GetVertexOverrideColorEnabledState));
		}
	}
}

void FStaticMeshDetails::OnLightmapSettingsChanged()
{
	UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();
	check(StaticMesh);
	StaticMesh->EnforceLightmapRestrictions(false);
}

bool FStaticMeshDetails::GetVertexOverrideColorEnabledState() const
{
	uint8 VertexColorImportOption;
	check(VertexColorImportOptionHandle.IsValid());
	ensure(VertexColorImportOptionHandle->GetValue(VertexColorImportOption) == FPropertyAccess::Success);

	return (VertexColorImportOption == EVertexColorImportOption::Override);
}

void SConvexDecomposition::Construct(const FArguments& InArgs)
{
	StaticMeshEditorPtr = InArgs._StaticMeshEditorPtr;
	CurrentHullPrecision = DefaultHullPrecision;
	CurrentHullCount = DefaultHullCount;
	CurrentMaxVertsPerHullCount = DefaultVertsPerHull;

	this->ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 8.0f, 0.0f, 8.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("HullCount_ConvexDecomp", "Hull Count"))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(3.0f)
			[	
				SAssignNew(HullCount, SSpinBox<uint32>)
				.ToolTipText(LOCTEXT("HullCount_ConvexDecomp_Tip", "Maximum number of convex pieces that will be created."))
				.MinValue(MinHullCount)
				.MaxValue(MaxHullCount)
				.Delta(HullCountDelta)
				.Value(this, &SConvexDecomposition::GetHullCount)
				.OnValueCommitted(this, &SConvexDecomposition::OnHullCountCommitted)
				.OnValueChanged(this, &SConvexDecomposition::OnHullCountChanged)
			]
		]


		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 8.0f, 0.0f, 8.0f)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text( LOCTEXT("MaxHullVerts_ConvexDecomp", "Max Hull Verts") )
			]

			+SHorizontalBox::Slot()
			.FillWidth(3.0f)
			[
				SAssignNew(MaxVertsPerHull, SSpinBox<int32>)
				.ToolTipText(LOCTEXT("MaxHullVerts_ConvexDecomp_Tip", "Maximum number of vertices allowed for any generated convex hull."))
				.MinValue(MinVertsPerHullCount)
				.MaxValue(MaxVertsPerHullCount)
				.Value( this, &SConvexDecomposition::GetVertsPerHullCount )
				.OnValueCommitted( this, &SConvexDecomposition::OnVertsPerHullCountCommitted )
				.OnValueChanged( this, &SConvexDecomposition::OnVertsPerHullCountChanged )
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.0f, 8.0f, 0.0f, 8.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("HullPrecision_ConvexDecomp", "Hull Precision"))
			]

			+ SHorizontalBox::Slot()
			.FillWidth(3.0f)
			[
				SAssignNew(HullPrecision, SSpinBox<uint32>)
				.ToolTipText(LOCTEXT("HullPrecision_ConvexDecomp_Tip", "Number of voxels to use when generating collision."))
				.MinValue(MinHullPrecision)
				.MaxValue(MaxHullPrecision)
				.Delta(HullPrecisionDelta)
				.Value(this, &SConvexDecomposition::GetHullPrecision)
				.OnValueCommitted(this, &SConvexDecomposition::OnHullPrecisionCommitted)
				.OnValueChanged(this, &SConvexDecomposition::OnHullPrecisionChanged)
			]
		]

		+SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SButton)
				.Text( LOCTEXT("Apply_ConvexDecomp", "Apply") )
				.OnClicked(this, &SConvexDecomposition::OnApplyDecomp)
			]

			+SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SButton)
				.Text( LOCTEXT("Defaults_ConvexDecomp", "Defaults") )
				.OnClicked(this, &SConvexDecomposition::OnDefaults)
			]
		]
	];
}

bool FStaticMeshDetails::IsApplyNeeded() const
{
	return LevelOfDetailSettings.IsValid() && LevelOfDetailSettings->IsApplyNeeded();
}

void FStaticMeshDetails::ApplyChanges()
{
	if( LevelOfDetailSettings.IsValid() )
	{
		LevelOfDetailSettings->ApplyChanges();
	}
}

SConvexDecomposition::~SConvexDecomposition()
{

}

FReply SConvexDecomposition::OnApplyDecomp()
{
	StaticMeshEditorPtr.Pin()->DoDecomp(CurrentHullCount, CurrentMaxVertsPerHullCount, CurrentHullPrecision);

	return FReply::Handled();
}

FReply SConvexDecomposition::OnDefaults()
{
	CurrentHullCount = DefaultHullCount;
	CurrentHullPrecision = DefaultHullPrecision;
	CurrentMaxVertsPerHullCount = DefaultVertsPerHull;


	return FReply::Handled();
}

void SConvexDecomposition::OnHullCountCommitted(uint32 InNewValue, ETextCommit::Type CommitInfo)
{
	OnHullCountChanged(InNewValue);
}

void SConvexDecomposition::OnHullCountChanged(uint32 InNewValue)
{
	CurrentHullCount = InNewValue;
}

uint32 SConvexDecomposition::GetHullCount() const
{
	return CurrentHullCount;
}

void SConvexDecomposition::OnHullPrecisionCommitted(uint32 InNewValue, ETextCommit::Type CommitInfo)
{
	OnHullPrecisionChanged(InNewValue);
}

void SConvexDecomposition::OnHullPrecisionChanged(uint32 InNewValue)
{
	CurrentHullPrecision = InNewValue;
}

uint32 SConvexDecomposition::GetHullPrecision() const
{
	return CurrentHullPrecision;
}

void SConvexDecomposition::OnVertsPerHullCountCommitted(int32 InNewValue,  ETextCommit::Type CommitInfo)
{
	OnVertsPerHullCountChanged(InNewValue);
}

void SConvexDecomposition::OnVertsPerHullCountChanged(int32 InNewValue)
{
	CurrentMaxVertsPerHullCount = InNewValue;
}

int32 SConvexDecomposition::GetVertsPerHullCount() const
{
	return CurrentMaxVertsPerHullCount;
}

static UEnum& GetFeatureImportanceEnum()
{
	static FName FeatureImportanceName(TEXT("EMeshFeatureImportance::Off"));
	static UEnum* FeatureImportanceEnum = NULL;
	if (FeatureImportanceEnum == NULL)
	{
		UEnum::LookupEnumName(FeatureImportanceName, &FeatureImportanceEnum);
		check(FeatureImportanceEnum);
	}
	return *FeatureImportanceEnum;
}

static UEnum& GetTerminationCriterionEunum()
{
	static FName Name(TEXT("EStaticMeshReductionTerimationCriterion::Triangles"));
	static UEnum* EnumPtr = NULL;
	if (EnumPtr == NULL)
	{
		UEnum::LookupEnumName(Name, &EnumPtr);
		check(EnumPtr);
	}
	return *EnumPtr;
}

static void FillEnumOptions(TArray<TSharedPtr<FString> >& OutStrings, UEnum& InEnum)
{
	for (int32 EnumIndex = 0; EnumIndex < InEnum.NumEnums() - 1; ++EnumIndex)
	{
		OutStrings.Add(MakeShareable(new FString(InEnum.GetNameStringByIndex(EnumIndex))));
	}
}

FMeshBuildSettingsLayout::FMeshBuildSettingsLayout( TSharedRef<FLevelOfDetailSettingsLayout> InParentLODSettings, int32 InLODIndex)
	: ParentLODSettings( InParentLODSettings )
	, LODIndex(InLODIndex)
{

}

FMeshBuildSettingsLayout::~FMeshBuildSettingsLayout()
{
}

void FMeshBuildSettingsLayout::GenerateHeaderRowContent( FDetailWidgetRow& NodeRow )
{
	NodeRow.NameContent()
	[
		SNew( STextBlock )
		.Text( LOCTEXT("MeshBuildSettings", "Build Settings") )
		.Font( IDetailLayoutBuilder::GetDetailFont() )
	];
}

FString FMeshBuildSettingsLayout::GetCurrentDistanceFieldReplacementMeshPath() const
{
	return BuildSettings.DistanceFieldReplacementMesh ? BuildSettings.DistanceFieldReplacementMesh->GetPathName() : FString("");
}

void FMeshBuildSettingsLayout::OnDistanceFieldReplacementMeshSelected(const FAssetData& AssetData)
{
	BuildSettings.DistanceFieldReplacementMesh = Cast<UStaticMesh>(AssetData.GetAsset());
}

void FMeshBuildSettingsLayout::GenerateChildContent( IDetailChildrenBuilder& ChildrenBuilder )
{
	{
		ChildrenBuilder.AddCustomRow( LOCTEXT("RecomputeNormals", "Recompute Normals") )
		.NameContent()
		[
			SNew(STextBlock)
			.Font( IDetailLayoutBuilder::GetDetailFont() )
			.Text(LOCTEXT("RecomputeNormals", "Recompute Normals"))
		
		]
		.ValueContent()
		[
			SNew(SCheckBox)
			.IsChecked(this, &FMeshBuildSettingsLayout::ShouldRecomputeNormals)
			.OnCheckStateChanged(this, &FMeshBuildSettingsLayout::OnRecomputeNormalsChanged)
		];
	}

	{
		ChildrenBuilder.AddCustomRow( LOCTEXT("RecomputeTangents", "Recompute Tangents") )
		.NameContent()
		[
			SNew(STextBlock)
			.Font( IDetailLayoutBuilder::GetDetailFont() )
			.Text(LOCTEXT("RecomputeTangents", "Recompute Tangents"))
		]
		.ValueContent()
		[
			SNew(SCheckBox)
			.IsChecked(this, &FMeshBuildSettingsLayout::ShouldRecomputeTangents)
			.OnCheckStateChanged(this, &FMeshBuildSettingsLayout::OnRecomputeTangentsChanged)
		];
	}

	{
		ChildrenBuilder.AddCustomRow( LOCTEXT("UseMikkTSpace", "Use MikkTSpace Tangent Space") )
		.NameContent()
		[
			SNew(STextBlock)
			.Font( IDetailLayoutBuilder::GetDetailFont() )
			.Text(LOCTEXT("UseMikkTSpace", "Use MikkTSpace Tangent Space"))
		]
		.ValueContent()
		[
			SNew(SCheckBox)
			.IsChecked(this, &FMeshBuildSettingsLayout::ShouldUseMikkTSpace)
			.OnCheckStateChanged(this, &FMeshBuildSettingsLayout::OnUseMikkTSpaceChanged)
		];
	}

	{
		ChildrenBuilder.AddCustomRow( LOCTEXT("ComputeWeightedNormals", "Compute Weighted Normals") )
		.NameContent()
		[
			SNew(STextBlock)
			.Font( IDetailLayoutBuilder::GetDetailFont() )
			.Text(LOCTEXT("ComputeWeightedNormals", "Compute Weighted Normals"))
		]
		.ValueContent()
		[
			SNew(SCheckBox)
			.IsChecked(this, &FMeshBuildSettingsLayout::ShouldComputeWeightedNormals)
			.OnCheckStateChanged(this, &FMeshBuildSettingsLayout::OnComputeWeightedNormalsChanged)
		];
	}

	{
		ChildrenBuilder.AddCustomRow( LOCTEXT("RemoveDegenerates", "Remove Degenerates") )
		.NameContent()
		[
			SNew(STextBlock)
			.Font( IDetailLayoutBuilder::GetDetailFont() )
			.Text(LOCTEXT("RemoveDegenerates", "Remove Degenerates"))
		]
		.ValueContent()
		[
			SNew(SCheckBox)
			.IsChecked(this, &FMeshBuildSettingsLayout::ShouldRemoveDegenerates)
			.OnCheckStateChanged(this, &FMeshBuildSettingsLayout::OnRemoveDegeneratesChanged)
		];
	}

	{
		ChildrenBuilder.AddCustomRow( LOCTEXT("BuildAdjacencyBuffer", "Build Adjacency Buffer") )
		.NameContent()
		[
			SNew(STextBlock)
			.Font( IDetailLayoutBuilder::GetDetailFont() )
			.Text(LOCTEXT("BuildAdjacencyBuffer", "Build Adjacency Buffer"))
		]
		.ValueContent()
		[
			SNew(SCheckBox)
			.IsChecked(this, &FMeshBuildSettingsLayout::ShouldBuildAdjacencyBuffer)
			.OnCheckStateChanged(this, &FMeshBuildSettingsLayout::OnBuildAdjacencyBufferChanged)
		];
	}

	{
		ChildrenBuilder.AddCustomRow( LOCTEXT("BuildReversedIndexBuffer", "Build Reversed Index Buffer") )
		.NameContent()
		[
			SNew(STextBlock)
			.Font( IDetailLayoutBuilder::GetDetailFont() )
			.Text(LOCTEXT("BuildReversedIndexBuffer", "Build Reversed Index Buffer"))
		]
		.ValueContent()
		[
			SNew(SCheckBox)
			.IsChecked(this, &FMeshBuildSettingsLayout::ShouldBuildReversedIndexBuffer)
			.OnCheckStateChanged(this, &FMeshBuildSettingsLayout::OnBuildReversedIndexBufferChanged)
		];
	}

	{
		ChildrenBuilder.AddCustomRow(LOCTEXT("UseHighPrecisionTangentBasis", "Use High Precision Tangent Basis"))
		.NameContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(LOCTEXT("UseHighPrecisionTangentBasis", "Use High Precision Tangent Basis"))
		]
		.ValueContent()
		[
			SNew(SCheckBox)
			.IsChecked(this, &FMeshBuildSettingsLayout::ShouldUseHighPrecisionTangentBasis)
			.OnCheckStateChanged(this, &FMeshBuildSettingsLayout::OnUseHighPrecisionTangentBasisChanged)
		];
	}

	{
		ChildrenBuilder.AddCustomRow( LOCTEXT("UseFullPrecisionUVs", "Use Full Precision UVs") )
		.NameContent()
		[
			SNew(STextBlock)
			.Font( IDetailLayoutBuilder::GetDetailFont() )
			.Text(LOCTEXT("UseFullPrecisionUVs", "Use Full Precision UVs"))
		]
		.ValueContent()
		[
			SNew(SCheckBox)
			.IsChecked(this, &FMeshBuildSettingsLayout::ShouldUseFullPrecisionUVs)
			.OnCheckStateChanged(this, &FMeshBuildSettingsLayout::OnUseFullPrecisionUVsChanged)
		];
	}

	{
		ChildrenBuilder.AddCustomRow( LOCTEXT("GenerateLightmapUVs", "Generate Lightmap UVs") )
		.NameContent()
		[
			SNew(STextBlock)
			.Font( IDetailLayoutBuilder::GetDetailFont() )
			.Text(LOCTEXT("GenerateLightmapUVs", "Generate Lightmap UVs"))
		]
		.ValueContent()
		[
			SNew(SCheckBox)
			.IsChecked(this, &FMeshBuildSettingsLayout::ShouldGenerateLightmapUVs)
			.OnCheckStateChanged(this, &FMeshBuildSettingsLayout::OnGenerateLightmapUVsChanged)
		];
	}

	{
		ChildrenBuilder.AddCustomRow( LOCTEXT("MinLightmapResolution", "Min Lightmap Resolution") )
		.NameContent()
		[
			SNew(STextBlock)
			.Font( IDetailLayoutBuilder::GetDetailFont() )
			.Text(LOCTEXT("MinLightmapResolution", "Min Lightmap Resolution"))
		]
		.ValueContent()
		[
			SNew(SSpinBox<int32>)
			.Font( IDetailLayoutBuilder::GetDetailFont() )
			.MinValue(1)
			.MaxValue(2048)
			.Value(this, &FMeshBuildSettingsLayout::GetMinLightmapResolution)
			.OnValueChanged(this, &FMeshBuildSettingsLayout::OnMinLightmapResolutionChanged)
		];
	}

	{
		ChildrenBuilder.AddCustomRow( LOCTEXT("SourceLightmapIndex", "Source Lightmap Index") )
		.NameContent()
		[
			SNew(STextBlock)
			.Font( IDetailLayoutBuilder::GetDetailFont() )
			.Text(LOCTEXT("SourceLightmapIndex", "Source Lightmap Index"))
		]
		.ValueContent()
		[
			SNew(SSpinBox<int32>)
			.Font( IDetailLayoutBuilder::GetDetailFont() )
			.MinValue(0)
			.MaxValue(7)
			.Value(this, &FMeshBuildSettingsLayout::GetSrcLightmapIndex)
			.OnValueChanged(this, &FMeshBuildSettingsLayout::OnSrcLightmapIndexChanged)
		];
	}

	{
		ChildrenBuilder.AddCustomRow( LOCTEXT("DestinationLightmapIndex", "Destination Lightmap Index") )
		.NameContent()
		[
			SNew(STextBlock)
			.Font( IDetailLayoutBuilder::GetDetailFont() )
			.Text(LOCTEXT("DestinationLightmapIndex", "Destination Lightmap Index"))
		]
		.ValueContent()
		[
			SNew(SSpinBox<int32>)
			.Font( IDetailLayoutBuilder::GetDetailFont() )
			.MinValue(0)
			.MaxValue(7)
			.Value(this, &FMeshBuildSettingsLayout::GetDstLightmapIndex)
			.OnValueChanged(this, &FMeshBuildSettingsLayout::OnDstLightmapIndexChanged)
		];
	}

	{
		ChildrenBuilder.AddCustomRow(LOCTEXT("BuildScale", "Build Scale"))
		.NameContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(LOCTEXT("BuildScale", "Build Scale"))
			.ToolTipText( LOCTEXT("BuildScale_ToolTip", "The local scale applied when building the mesh") )
		]
		.ValueContent()
		.MinDesiredWidth(125.0f * 3.0f)
		.MaxDesiredWidth(125.0f * 3.0f)
		[
			SNew(SVectorInputBox)
			.X(this, &FMeshBuildSettingsLayout::GetBuildScaleX)
			.Y(this, &FMeshBuildSettingsLayout::GetBuildScaleY)
			.Z(this, &FMeshBuildSettingsLayout::GetBuildScaleZ)
			.bColorAxisLabels(false)
			.AllowResponsiveLayout(true)
			.AllowSpin(false)
			.OnXCommitted(this, &FMeshBuildSettingsLayout::OnBuildScaleXChanged)
			.OnYCommitted(this, &FMeshBuildSettingsLayout::OnBuildScaleYChanged)
			.OnZCommitted(this, &FMeshBuildSettingsLayout::OnBuildScaleZChanged)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		];
	}

	{
		ChildrenBuilder.AddCustomRow( LOCTEXT("DistanceFieldResolutionScale", "Distance Field Resolution Scale") )
		.NameContent()
		[
			SNew(STextBlock)
			.Font( IDetailLayoutBuilder::GetDetailFont() )
			.Text(LOCTEXT("DistanceFieldResolutionScale", "Distance Field Resolution Scale"))
		]
		.ValueContent()
		[
			SNew(SSpinBox<float>)
			.Font( IDetailLayoutBuilder::GetDetailFont() )
			.MinValue(0.0f)
			.MaxValue(100.0f)
			.Value(this, &FMeshBuildSettingsLayout::GetDistanceFieldResolutionScale)
			.OnValueChanged(this, &FMeshBuildSettingsLayout::OnDistanceFieldResolutionScaleChanged)
			.OnValueCommitted(this, &FMeshBuildSettingsLayout::OnDistanceFieldResolutionScaleCommitted)
		];
	}
		
	{
		ChildrenBuilder.AddCustomRow( LOCTEXT("GenerateDistanceFieldAsIfTwoSided", "Two-Sided Distance Field Generation") )
		.NameContent()
		[
			SNew(STextBlock)
			.Font( IDetailLayoutBuilder::GetDetailFont() )
			.Text(LOCTEXT("GenerateDistanceFieldAsIfTwoSided", "Two-Sided Distance Field Generation"))
		]
		.ValueContent()
		[
			SNew(SCheckBox)
			.IsChecked(this, &FMeshBuildSettingsLayout::ShouldGenerateDistanceFieldAsIfTwoSided)
			.OnCheckStateChanged(this, &FMeshBuildSettingsLayout::OnGenerateDistanceFieldAsIfTwoSidedChanged)
		];
	}

	{
		TSharedRef<SWidget> PropWidget = SNew(SObjectPropertyEntryBox)
			.AllowedClass(UStaticMesh::StaticClass())
			.AllowClear(true)
			.ObjectPath(this, &FMeshBuildSettingsLayout::GetCurrentDistanceFieldReplacementMeshPath)
			.OnObjectChanged(this, &FMeshBuildSettingsLayout::OnDistanceFieldReplacementMeshSelected);

		ChildrenBuilder.AddCustomRow( LOCTEXT("DistanceFieldReplacementMesh", "Distance Field Replacement Mesh") )
		.NameContent()
		[
			SNew(STextBlock)
			.Font( IDetailLayoutBuilder::GetDetailFont() )
			.Text(LOCTEXT("DistanceFieldReplacementMesh", "Distance Field Replacement Mesh"))
		]
		.ValueContent()
		[
			PropWidget
		];
	}

	{
		ChildrenBuilder.AddCustomRow( LOCTEXT("ApplyChanges", "Apply Changes") )
		.ValueContent()
		.HAlign(HAlign_Left)
		[
			SNew(SButton)
			.OnClicked(this, &FMeshBuildSettingsLayout::OnApplyChanges)
			.IsEnabled(ParentLODSettings.Pin().ToSharedRef(), &FLevelOfDetailSettingsLayout::IsApplyNeeded)
			[
				SNew( STextBlock )
				.Text(LOCTEXT("ApplyChanges", "Apply Changes"))
				.Font( IDetailLayoutBuilder::GetDetailFont() )
			]
		];
	}
}

void FMeshBuildSettingsLayout::UpdateSettings(const FMeshBuildSettings& InSettings)
{
	BuildSettings = InSettings;
}

FReply FMeshBuildSettingsLayout::OnApplyChanges()
{
	if( ParentLODSettings.IsValid() )
	{
		ParentLODSettings.Pin()->ApplyChanges();
	}
	return FReply::Handled();
}

ECheckBoxState FMeshBuildSettingsLayout::ShouldRecomputeNormals() const
{
	return BuildSettings.bRecomputeNormals ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState FMeshBuildSettingsLayout::ShouldRecomputeTangents() const
{
	return BuildSettings.bRecomputeTangents ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState FMeshBuildSettingsLayout::ShouldUseMikkTSpace() const
{
	return BuildSettings.bUseMikkTSpace ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState FMeshBuildSettingsLayout::ShouldComputeWeightedNormals() const
{
	return BuildSettings.bComputeWeightedNormals ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState FMeshBuildSettingsLayout::ShouldRemoveDegenerates() const
{
	return BuildSettings.bRemoveDegenerates ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState FMeshBuildSettingsLayout::ShouldBuildAdjacencyBuffer() const
{
	return BuildSettings.bBuildAdjacencyBuffer ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState FMeshBuildSettingsLayout::ShouldBuildReversedIndexBuffer() const
{
	return BuildSettings.bBuildReversedIndexBuffer ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState FMeshBuildSettingsLayout::ShouldUseHighPrecisionTangentBasis() const
{
	return BuildSettings.bUseHighPrecisionTangentBasis ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState FMeshBuildSettingsLayout::ShouldUseFullPrecisionUVs() const
{
	return BuildSettings.bUseFullPrecisionUVs ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState FMeshBuildSettingsLayout::ShouldGenerateLightmapUVs() const
{
	return BuildSettings.bGenerateLightmapUVs ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState FMeshBuildSettingsLayout::ShouldGenerateDistanceFieldAsIfTwoSided() const
{
	return BuildSettings.bGenerateDistanceFieldAsIfTwoSided ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

int32 FMeshBuildSettingsLayout::GetMinLightmapResolution() const
{
	return BuildSettings.MinLightmapResolution;
}

int32 FMeshBuildSettingsLayout::GetSrcLightmapIndex() const
{
	return BuildSettings.SrcLightmapIndex;
}

int32 FMeshBuildSettingsLayout::GetDstLightmapIndex() const
{
	return BuildSettings.DstLightmapIndex;
}

TOptional<float> FMeshBuildSettingsLayout::GetBuildScaleX() const
{
	return BuildSettings.BuildScale3D.X;
}

TOptional<float> FMeshBuildSettingsLayout::GetBuildScaleY() const
{
	return BuildSettings.BuildScale3D.Y;
}

TOptional<float> FMeshBuildSettingsLayout::GetBuildScaleZ() const
{
	return BuildSettings.BuildScale3D.Z;
}

float FMeshBuildSettingsLayout::GetDistanceFieldResolutionScale() const
{
	return BuildSettings.DistanceFieldResolutionScale;
}

void FMeshBuildSettingsLayout::OnRecomputeNormalsChanged(ECheckBoxState NewState)
{
	const bool bRecomputeNormals = (NewState == ECheckBoxState::Checked) ? true : false;
	if (BuildSettings.bRecomputeNormals != bRecomputeNormals)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.StaticMesh.BuildSettings"), TEXT("bRecomputeNormals"), bRecomputeNormals ? TEXT("True") : TEXT("False"));
		}
		BuildSettings.bRecomputeNormals = bRecomputeNormals;
	}
}

void FMeshBuildSettingsLayout::OnRecomputeTangentsChanged(ECheckBoxState NewState)
{
	const bool bRecomputeTangents = (NewState == ECheckBoxState::Checked) ? true : false;
	if (BuildSettings.bRecomputeTangents != bRecomputeTangents)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.StaticMesh.BuildSettings"), TEXT("bRecomputeTangents"), bRecomputeTangents ? TEXT("True") : TEXT("False"));
		}
		BuildSettings.bRecomputeTangents = bRecomputeTangents;
	}
}

void FMeshBuildSettingsLayout::OnUseMikkTSpaceChanged(ECheckBoxState NewState)
{
	const bool bUseMikkTSpace = (NewState == ECheckBoxState::Checked) ? true : false;
	if (BuildSettings.bUseMikkTSpace != bUseMikkTSpace)
	{
		BuildSettings.bUseMikkTSpace = bUseMikkTSpace;
	}
}

void FMeshBuildSettingsLayout::OnComputeWeightedNormalsChanged(ECheckBoxState NewState)
{
	const bool bComputeWeightedNormals = (NewState == ECheckBoxState::Checked) ? true : false;
	if (BuildSettings.bComputeWeightedNormals != bComputeWeightedNormals)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.StaticMesh.BuildSettings"), TEXT("bComputeWeightedNormals"), bComputeWeightedNormals ? TEXT("True") : TEXT("False"));
		}
		BuildSettings.bComputeWeightedNormals = bComputeWeightedNormals;
	}
}

void FMeshBuildSettingsLayout::OnRemoveDegeneratesChanged(ECheckBoxState NewState)
{
	const bool bRemoveDegenerates = (NewState == ECheckBoxState::Checked) ? true : false;
	if (BuildSettings.bRemoveDegenerates != bRemoveDegenerates)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.StaticMesh.BuildSettings"), TEXT("bRemoveDegenerates"), bRemoveDegenerates ? TEXT("True") : TEXT("False"));
		}
		BuildSettings.bRemoveDegenerates = bRemoveDegenerates;
	}
}

void FMeshBuildSettingsLayout::OnBuildAdjacencyBufferChanged(ECheckBoxState NewState)
{
	const bool bBuildAdjacencyBuffer = (NewState == ECheckBoxState::Checked) ? true : false;
	if (BuildSettings.bBuildAdjacencyBuffer != bBuildAdjacencyBuffer)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.StaticMesh.BuildSettings"), TEXT("bBuildAdjacencyBuffer"), bBuildAdjacencyBuffer ? TEXT("True") : TEXT("False"));
		}
		BuildSettings.bBuildAdjacencyBuffer = bBuildAdjacencyBuffer;
		if (!BuildSettings.bBuildAdjacencyBuffer && ParentLODSettings.IsValid())
		{
			if (ParentLODSettings.Pin()->PreviewLODRequiresAdjacencyInformation(LODIndex))
			{
				//Prompt the user
				FText ConfirmRequiredAdjacencyText = LOCTEXT("ConfirmRequiredAdjacencyBufferRemove", "This LOD is using at least one tessellation material that required the adjacency buffer to be computed.\nAre you sure to want to remove the adjacency buffer?");
				EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::YesNo, ConfirmRequiredAdjacencyText);
				if (Result == EAppReturnType::No)
				{
					//Put back the adjacency buffer option to true
					BuildSettings.bBuildAdjacencyBuffer = true;
				}
			}
		}
	}
}

void FMeshBuildSettingsLayout::OnBuildReversedIndexBufferChanged(ECheckBoxState NewState)
{
	const bool bBuildReversedIndexBuffer = (NewState == ECheckBoxState::Checked) ? true : false;
	if (BuildSettings.bBuildReversedIndexBuffer != bBuildReversedIndexBuffer)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.StaticMesh.BuildSettings"), TEXT("bBuildReversedIndexBuffer"), bBuildReversedIndexBuffer ? TEXT("True") : TEXT("False"));
		}
		BuildSettings.bBuildReversedIndexBuffer = bBuildReversedIndexBuffer;
	}
}

void FMeshBuildSettingsLayout::OnUseHighPrecisionTangentBasisChanged(ECheckBoxState NewState)
{
	const bool bUseHighPrecisionTangents = (NewState == ECheckBoxState::Checked) ? true : false;
	if (BuildSettings.bUseHighPrecisionTangentBasis != bUseHighPrecisionTangents)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.StaticMesh.BuildSettings"), TEXT("bUseHighPrecisionTangentBasis"), bUseHighPrecisionTangents ? TEXT("True") : TEXT("False"));
		}
		BuildSettings.bUseHighPrecisionTangentBasis = bUseHighPrecisionTangents;
	}
}

void FMeshBuildSettingsLayout::OnUseFullPrecisionUVsChanged(ECheckBoxState NewState)
{
	const bool bUseFullPrecisionUVs = (NewState == ECheckBoxState::Checked) ? true : false;
	if (BuildSettings.bUseFullPrecisionUVs != bUseFullPrecisionUVs)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.StaticMesh.BuildSettings"), TEXT("bUseFullPrecisionUVs"), bUseFullPrecisionUVs ? TEXT("True") : TEXT("False"));
		}
		BuildSettings.bUseFullPrecisionUVs = bUseFullPrecisionUVs;
	}
}

void FMeshBuildSettingsLayout::OnGenerateLightmapUVsChanged(ECheckBoxState NewState)
{
	const bool bGenerateLightmapUVs = (NewState == ECheckBoxState::Checked) ? true : false;
	if (BuildSettings.bGenerateLightmapUVs != bGenerateLightmapUVs)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.StaticMesh.BuildSettings"), TEXT("bGenerateLightmapUVs"), bGenerateLightmapUVs ? TEXT("True") : TEXT("False"));
		}
		BuildSettings.bGenerateLightmapUVs = bGenerateLightmapUVs;
	}
}

void FMeshBuildSettingsLayout::OnGenerateDistanceFieldAsIfTwoSidedChanged(ECheckBoxState NewState)
{
	const bool bGenerateDistanceFieldAsIfTwoSided = (NewState == ECheckBoxState::Checked) ? true : false;
	if (BuildSettings.bGenerateDistanceFieldAsIfTwoSided != bGenerateDistanceFieldAsIfTwoSided)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.StaticMesh.BuildSettings"), TEXT("bGenerateDistanceFieldAsIfTwoSided"), bGenerateDistanceFieldAsIfTwoSided ? TEXT("True") : TEXT("False"));
		}
		BuildSettings.bGenerateDistanceFieldAsIfTwoSided = bGenerateDistanceFieldAsIfTwoSided;
	}
}

void FMeshBuildSettingsLayout::OnMinLightmapResolutionChanged( int32 NewValue )
{
	if (BuildSettings.MinLightmapResolution != NewValue)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.StaticMesh.BuildSettings"), TEXT("MinLightmapResolution"), FString::Printf(TEXT("%i"), NewValue));
		}
		BuildSettings.MinLightmapResolution = NewValue;
	}
}

void FMeshBuildSettingsLayout::OnSrcLightmapIndexChanged( int32 NewValue )
{
	if (BuildSettings.SrcLightmapIndex != NewValue)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.StaticMesh.BuildSettings"), TEXT("SrcLightmapIndex"), FString::Printf(TEXT("%i"), NewValue));
		}
		BuildSettings.SrcLightmapIndex = NewValue;
	}
}

void FMeshBuildSettingsLayout::OnDstLightmapIndexChanged( int32 NewValue )
{
	if (BuildSettings.DstLightmapIndex != NewValue)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.StaticMesh.BuildSettings"), TEXT("DstLightmapIndex"), FString::Printf(TEXT("%i"), NewValue));
		}
		BuildSettings.DstLightmapIndex = NewValue;
	}
}

void FMeshBuildSettingsLayout::OnBuildScaleXChanged( float NewScaleX, ETextCommit::Type TextCommitType )
{
	if (!FMath::IsNearlyEqual(NewScaleX, 0.0f) && BuildSettings.BuildScale3D.X != NewScaleX)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.StaticMesh.BuildSettings"), TEXT("BuildScale3D.X"), FString::Printf(TEXT("%.3f"), NewScaleX));
		}
		BuildSettings.BuildScale3D.X = NewScaleX;
	}
}

void FMeshBuildSettingsLayout::OnBuildScaleYChanged( float NewScaleY, ETextCommit::Type TextCommitType )
{
	if (!FMath::IsNearlyEqual(NewScaleY, 0.0f) && BuildSettings.BuildScale3D.Y != NewScaleY)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.StaticMesh.BuildSettings"), TEXT("BuildScale3D.Y"), FString::Printf(TEXT("%.3f"), NewScaleY));
		}
		BuildSettings.BuildScale3D.Y = NewScaleY;
	}
}

void FMeshBuildSettingsLayout::OnBuildScaleZChanged( float NewScaleZ, ETextCommit::Type TextCommitType )
{
	if (!FMath::IsNearlyEqual(NewScaleZ, 0.0f) && BuildSettings.BuildScale3D.Z != NewScaleZ)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.StaticMesh.BuildSettings"), TEXT("BuildScale3D.Z"), FString::Printf(TEXT("%.3f"), NewScaleZ));
		}
		BuildSettings.BuildScale3D.Z = NewScaleZ;
	}
}

void FMeshBuildSettingsLayout::OnDistanceFieldResolutionScaleChanged(float NewValue)
{
	BuildSettings.DistanceFieldResolutionScale = NewValue;
}

void FMeshBuildSettingsLayout::OnDistanceFieldResolutionScaleCommitted(float NewValue, ETextCommit::Type TextCommitType)
{
	if (FEngineAnalytics::IsAvailable())
	{
		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.StaticMesh.BuildSettings"), TEXT("DistanceFieldResolutionScale"), FString::Printf(TEXT("%.3f"), NewValue));
	}
	OnDistanceFieldResolutionScaleChanged(NewValue);
}

FMeshReductionSettingsLayout::FMeshReductionSettingsLayout( TSharedRef<FLevelOfDetailSettingsLayout> InParentLODSettings, int32 InCurrentLODIndex, bool InCanReduceMyself)
	: ParentLODSettings( InParentLODSettings )
	, CurrentLODIndex(InCurrentLODIndex)
	, bCanReduceMyself(InCanReduceMyself)
{

	FillEnumOptions(ImportanceOptions, GetFeatureImportanceEnum());

	FillEnumOptions(TerminationOptions, GetTerminationCriterionEunum());

	bUseQuadricSimplifier = UseNativeToolLayout();
}

FMeshReductionSettingsLayout::~FMeshReductionSettingsLayout()
{
}

void FMeshReductionSettingsLayout::GenerateHeaderRowContent( FDetailWidgetRow& NodeRow  )
{
	NodeRow.NameContent()
	[
		SNew( STextBlock )
		.Text( LOCTEXT("MeshReductionSettings", "Reduction Settings") )
		.Font( IDetailLayoutBuilder::GetDetailFont() )
	];
}

bool FMeshReductionSettingsLayout::UseNativeToolLayout() const 
{
	// Are we using our tool, or simplygon?  The tool is only changed during editor restarts
	IMeshReduction* ReductionModule = FModuleManager::Get().LoadModuleChecked<IMeshReductionManagerModule>("MeshReductionInterface").GetStaticMeshReductionInterface();

	FString VersionString = ReductionModule->GetVersionString();
	TArray<FString> SplitVersionString;
	VersionString.ParseIntoArray(SplitVersionString, TEXT("_"), true);

	bool bUseQuadricSimplier = SplitVersionString[0].Equals("QuadricMeshReduction");
	return bUseQuadricSimplier;
}

EVisibility FMeshReductionSettingsLayout::GetTriangleCriterionVisibility() const
{
	EVisibility VisibilityValue;
	if (!bUseQuadricSimplifier || ReductionSettings.TerminationCriterion != EStaticMeshReductionTerimationCriterion::Vertices)
	{
		VisibilityValue =  EVisibility::Visible;
	}
	else
	{
		VisibilityValue = EVisibility::Hidden;
	}
	return VisibilityValue;

}


EVisibility FMeshReductionSettingsLayout::GetVertexCriterionVisibility() const
{
	EVisibility VisibilityValue;
	if (!bUseQuadricSimplifier || ReductionSettings.TerminationCriterion != EStaticMeshReductionTerimationCriterion::Triangles)
	{
		VisibilityValue = EVisibility::Visible;
	}
	else
	{
		VisibilityValue = EVisibility::Hidden;
	}
	return VisibilityValue;

}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void FMeshReductionSettingsLayout::GenerateChildContent( IDetailChildrenBuilder& ChildrenBuilder )
{

	if (bUseQuadricSimplifier)
	{

		{
			ChildrenBuilder.AddCustomRow(LOCTEXT("Termination_MeshSimplification", "Termination"))
				.NameContent()
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("Termination_MeshSimplification", "Termination"))
				]
			.ValueContent()
			[
				SAssignNew(TerminationCriterionCombo, STextComboBox)
				.Font( IDetailLayoutBuilder::GetDetailFont() )
				.OptionsSource(&TerminationOptions)
				.InitiallySelectedItem(TerminationOptions[static_cast<int32>(ReductionSettings.TerminationCriterion)])
				.OnSelectionChanged(this, &FMeshReductionSettingsLayout::OnTerminationCriterionChanged)
			];

		}
	}

	{
		ChildrenBuilder.AddCustomRow( LOCTEXT("PercentTriangles", "Percent Triangles") )
		.NameContent()
		[
			SNew(STextBlock)
			.Font( IDetailLayoutBuilder::GetDetailFont() )
			.Text(LOCTEXT("PercentTriangles", "Percent Triangles"))
		]
		.ValueContent()
		[
			SNew(SSpinBox<float>)
			.Font( IDetailLayoutBuilder::GetDetailFont() )
			.MinValue(0.0f)
			.MaxValue(100.0f)
			.Value(this, &FMeshReductionSettingsLayout::GetPercentTriangles)
			.OnValueChanged(this, &FMeshReductionSettingsLayout::OnPercentTrianglesChanged)
			.OnValueCommitted(this, &FMeshReductionSettingsLayout::OnPercentTrianglesCommitted)
		]
		.Visibility(TAttribute<EVisibility>(this, &FMeshReductionSettingsLayout::GetTriangleCriterionVisibility));

	}

	if (bUseQuadricSimplifier)
	{
		ChildrenBuilder.AddCustomRow(LOCTEXT("PercentVertices", "Percent Vertices"))
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(LOCTEXT("PercentVertices", "Percent Vertices"))
			]
		.ValueContent()
			[
				SNew(SSpinBox<float>)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			.MinValue(0.0f)
			.MaxValue(100.0f)
			.Value(this, &FMeshReductionSettingsLayout::GetPercentVertices)
			.OnValueChanged(this, &FMeshReductionSettingsLayout::OnPercentVerticesChanged)
			.OnValueCommitted(this, &FMeshReductionSettingsLayout::OnPercentVerticesCommitted)
			]
		.Visibility(TAttribute<EVisibility>(this, &FMeshReductionSettingsLayout::GetVertexCriterionVisibility));

	}

	// Controls that only simplygon uses.
	if (!bUseQuadricSimplifier)
	{
		{
			ChildrenBuilder.AddCustomRow(LOCTEXT("MaxDeviation", "Max Deviation"))
				.NameContent()
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("MaxDeviation", "Max Deviation"))
				]
			.ValueContent()
				[
					SNew(SSpinBox<float>)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				.MinValue(0.0f)
				.MaxValue(1000.0f)
				.Value(this, &FMeshReductionSettingsLayout::GetMaxDeviation)
				.OnValueChanged(this, &FMeshReductionSettingsLayout::OnMaxDeviationChanged)
				.OnValueCommitted(this, &FMeshReductionSettingsLayout::OnMaxDeviationCommitted)
				];

		}

		{
			ChildrenBuilder.AddCustomRow(LOCTEXT("PixelError", "Pixel Error"))
				.NameContent()
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("PixelError", "Pixel Error"))
				]
			.ValueContent()
				[
					SNew(SSpinBox<float>)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				.MinValue(0.0f)
				.MaxValue(40.0f)
				.Value(this, &FMeshReductionSettingsLayout::GetPixelError)
				.OnValueChanged(this, &FMeshReductionSettingsLayout::OnPixelErrorChanged)
				.OnValueCommitted(this, &FMeshReductionSettingsLayout::OnPixelErrorCommitted)
				];

		}

		{
			ChildrenBuilder.AddCustomRow(LOCTEXT("Silhouette_MeshSimplification", "Silhouette"))
				.NameContent()
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("Silhouette_MeshSimplification", "Silhouette"))
				]
			.ValueContent()
				[
					SAssignNew(SilhouetteCombo, STextComboBox)
					//.Font( IDetailLayoutBuilder::GetDetailFont() )
				.ContentPadding(0)
				.OptionsSource(&ImportanceOptions)
				.InitiallySelectedItem(ImportanceOptions[ReductionSettings.SilhouetteImportance])
				.OnSelectionChanged(this, &FMeshReductionSettingsLayout::OnSilhouetteImportanceChanged)
				];

		}

		{
			ChildrenBuilder.AddCustomRow(LOCTEXT("Texture_MeshSimplification", "Texture"))
				.NameContent()
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("Texture_MeshSimplification", "Texture"))
				]
			.ValueContent()
				[
					SAssignNew(TextureCombo, STextComboBox)
					//.Font( IDetailLayoutBuilder::GetDetailFont() )
				.ContentPadding(0)
				.OptionsSource(&ImportanceOptions)
				.InitiallySelectedItem(ImportanceOptions[ReductionSettings.TextureImportance])
				.OnSelectionChanged(this, &FMeshReductionSettingsLayout::OnTextureImportanceChanged)
				];

		}

		{
			ChildrenBuilder.AddCustomRow(LOCTEXT("Shading_MeshSimplification", "Shading"))
				.NameContent()
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("Shading_MeshSimplification", "Shading"))
				]
			.ValueContent()
				[
					SAssignNew(ShadingCombo, STextComboBox)
					//.Font( IDetailLayoutBuilder::GetDetailFont() )
				.ContentPadding(0)
				.OptionsSource(&ImportanceOptions)
				.InitiallySelectedItem(ImportanceOptions[ReductionSettings.ShadingImportance])
				.OnSelectionChanged(this, &FMeshReductionSettingsLayout::OnShadingImportanceChanged)
				];

		}
	}
	{
		ChildrenBuilder.AddCustomRow( LOCTEXT("WeldingThreshold", "Welding Threshold") )
		.NameContent()
		[
			SNew(STextBlock)
			.Font( IDetailLayoutBuilder::GetDetailFont() )
			.Text(LOCTEXT("WeldingThreshold", "Welding Threshold"))
		]
		.ValueContent()
		[
			SNew(SSpinBox<float>)
			.Font( IDetailLayoutBuilder::GetDetailFont() )
			.MinValue(0.0f)
			.MaxValue(10.0f)
			.Value(this, &FMeshReductionSettingsLayout::GetWeldingThreshold)
			.OnValueChanged(this, &FMeshReductionSettingsLayout::OnWeldingThresholdChanged)
			.OnValueCommitted(this, &FMeshReductionSettingsLayout::OnWeldingThresholdCommitted)
		];

	}

	// controls that only simplygon uses
	if (!bUseQuadricSimplifier)
	{
		{
			ChildrenBuilder.AddCustomRow(LOCTEXT("RecomputeNormals", "Recompute Normals"))
				.NameContent()
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("RecomputeNormals", "Recompute Normals"))

				]
			.ValueContent()
				[
					SNew(SCheckBox)
					.IsChecked(this, &FMeshReductionSettingsLayout::ShouldRecalculateNormals)
				.OnCheckStateChanged(this, &FMeshReductionSettingsLayout::OnRecalculateNormalsChanged)
				];
		}

		{
			ChildrenBuilder.AddCustomRow(LOCTEXT("HardEdgeAngle", "Hard Edge Angle"))
				.NameContent()
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("HardEdgeAngle", "Hard Edge Angle"))
				]
			.ValueContent()
				[
					SNew(SSpinBox<float>)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				.MinValue(0.0f)
				.MaxValue(180.0f)
				.Value(this, &FMeshReductionSettingsLayout::GetHardAngleThreshold)
				.OnValueChanged(this, &FMeshReductionSettingsLayout::OnHardAngleThresholdChanged)
				.OnValueCommitted(this, &FMeshReductionSettingsLayout::OnHardAngleThresholdCommitted)
				];

		}
	}

	//Base LOD
	{
		int32 MaxBaseReduceIndex = bCanReduceMyself ? CurrentLODIndex : CurrentLODIndex - 1;
		ChildrenBuilder.AddCustomRow(LOCTEXT("ReductionBaseLOD", "Base LOD"))
			.NameContent()
			.HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ReductionBaseLOD", "Base LOD"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			.ValueContent()
			.HAlign(HAlign_Left)
			[
				SNew(SNumericEntryBox<int32>)
				.AllowSpin(true)
				.MinSliderValue(0)
				.MaxSliderValue(MaxBaseReduceIndex)
				.MinValue(0)
				.MaxValue(MaxBaseReduceIndex)
				.Value(this, &FMeshReductionSettingsLayout::GetBaseLODIndex)
				.OnValueChanged(this, &FMeshReductionSettingsLayout::SetBaseLODIndex)
			];
	}

	{
		ChildrenBuilder.AddCustomRow( LOCTEXT("ApplyChanges", "Apply Changes") )
			.ValueContent()
			.HAlign(HAlign_Left)
			[
				SNew(SButton)
				.OnClicked(this, &FMeshReductionSettingsLayout::OnApplyChanges)
				.IsEnabled(ParentLODSettings.Pin().ToSharedRef(), &FLevelOfDetailSettingsLayout::IsApplyNeeded)
				[
					SNew( STextBlock )
					.Text(LOCTEXT("ApplyChanges", "Apply Changes"))
					.Font( IDetailLayoutBuilder::GetDetailFont() )
				]
			];
	}

	if (!bUseQuadricSimplifier)
	{
		SilhouetteCombo->SetSelectedItem(ImportanceOptions[ReductionSettings.SilhouetteImportance]);
		TextureCombo->SetSelectedItem(ImportanceOptions[ReductionSettings.TextureImportance]);
		ShadingCombo->SetSelectedItem(ImportanceOptions[ReductionSettings.ShadingImportance]);
	}
	else
	{
		TerminationCriterionCombo->SetSelectedItem(TerminationOptions[static_cast<int32>(ReductionSettings.TerminationCriterion)]);
	}
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

const FMeshReductionSettings& FMeshReductionSettingsLayout::GetSettings() const
{
	return ReductionSettings;
}

void FMeshReductionSettingsLayout::UpdateSettings(const FMeshReductionSettings& InSettings)
{
	ReductionSettings = InSettings;
}

FReply FMeshReductionSettingsLayout::OnApplyChanges()
{
	if( ParentLODSettings.IsValid() )
	{
		ParentLODSettings.Pin()->ApplyChanges();
	}
	return FReply::Handled();
}

float FMeshReductionSettingsLayout::GetPercentTriangles() const
{
	return ReductionSettings.PercentTriangles * 100.0f; // Display fraction as percentage.
}

float FMeshReductionSettingsLayout::GetPercentVertices() const
{
	return ReductionSettings.PercentVertices * 100.0f; // Display fraction as percentage.
}

float FMeshReductionSettingsLayout::GetMaxDeviation() const
{
	return ReductionSettings.MaxDeviation;
}

float FMeshReductionSettingsLayout::GetPixelError() const
{
	return ReductionSettings.PixelError;
}

float FMeshReductionSettingsLayout::GetWeldingThreshold() const
{
	return ReductionSettings.WeldingThreshold;
}

ECheckBoxState FMeshReductionSettingsLayout::ShouldRecalculateNormals() const
{
	return ReductionSettings.bRecalculateNormals ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

float FMeshReductionSettingsLayout::GetHardAngleThreshold() const
{
	return ReductionSettings.HardAngleThreshold;
}

void FMeshReductionSettingsLayout::OnPercentTrianglesChanged(float NewValue)
{
	// Percentage -> fraction.
	ReductionSettings.PercentTriangles = NewValue * 0.01f;
}

void FMeshReductionSettingsLayout::OnPercentVerticesChanged(float NewValue)
{
	// Percentage -> fraction.
	ReductionSettings.PercentVertices = NewValue * 0.01f;
}

void FMeshReductionSettingsLayout::OnPercentTrianglesCommitted(float NewValue, ETextCommit::Type TextCommitType)
{
	if (FEngineAnalytics::IsAvailable())
	{
		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.StaticMesh.ReductionSettings"), TEXT("PercentTriangles"), FString::Printf(TEXT("%.1f"), NewValue));
	}
	OnPercentTrianglesChanged(NewValue);
}


void FMeshReductionSettingsLayout::OnPercentVerticesCommitted(float NewValue, ETextCommit::Type TextCommitType)
{
	if (FEngineAnalytics::IsAvailable())
	{
		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.StaticMesh.ReductionSettings"), TEXT("PercentVertices"), FString::Printf(TEXT("%.1f"), NewValue));
	}
	OnPercentVerticesChanged(NewValue);
}

void FMeshReductionSettingsLayout::OnMaxDeviationChanged(float NewValue)
{
	ReductionSettings.MaxDeviation = NewValue;
}

void FMeshReductionSettingsLayout::OnMaxDeviationCommitted(float NewValue, ETextCommit::Type TextCommitType)
{
	if (FEngineAnalytics::IsAvailable())
	{
		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.StaticMesh.ReductionSettings"), TEXT("MaxDeviation"), FString::Printf(TEXT("%.1f"), NewValue));
	}
	OnMaxDeviationChanged(NewValue);
}

void FMeshReductionSettingsLayout::OnPixelErrorChanged(float NewValue)
{
	ReductionSettings.PixelError = NewValue;
}

void FMeshReductionSettingsLayout::OnPixelErrorCommitted(float NewValue, ETextCommit::Type TextCommitType)
{
	if (FEngineAnalytics::IsAvailable())
	{
		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.StaticMesh.ReductionSettings"), TEXT("PixelError"), FString::Printf(TEXT("%.1f"), NewValue));
	}
	OnPixelErrorChanged(NewValue);
}

void FMeshReductionSettingsLayout::OnWeldingThresholdChanged(float NewValue)
{
	ReductionSettings.WeldingThreshold = NewValue;
}

void FMeshReductionSettingsLayout::OnWeldingThresholdCommitted(float NewValue, ETextCommit::Type TextCommitType)
{
	if (FEngineAnalytics::IsAvailable())
	{
		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.StaticMesh.ReductionSettings"), TEXT("WeldingThreshold"), FString::Printf(TEXT("%.2f"), NewValue));
	}
	OnWeldingThresholdChanged(NewValue);
}

void FMeshReductionSettingsLayout::OnRecalculateNormalsChanged(ECheckBoxState NewValue)
{
	const bool bRecalculateNormals = NewValue == ECheckBoxState::Checked;
	if (ReductionSettings.bRecalculateNormals != bRecalculateNormals)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.StaticMesh.ReductionSettings"), TEXT("bRecalculateNormals"), bRecalculateNormals ? TEXT("True") : TEXT("False"));
		}
		ReductionSettings.bRecalculateNormals = bRecalculateNormals;
	}
}

void FMeshReductionSettingsLayout::OnHardAngleThresholdChanged(float NewValue)
{
	ReductionSettings.HardAngleThreshold = NewValue;
}

void FMeshReductionSettingsLayout::OnHardAngleThresholdCommitted(float NewValue, ETextCommit::Type TextCommitType)
{
	if (FEngineAnalytics::IsAvailable())
	{
		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.StaticMesh.ReductionSettings"), TEXT("HardAngleThreshold"), FString::Printf(TEXT("%.3f"), NewValue));
	}
	OnHardAngleThresholdChanged(NewValue);
}

void FMeshReductionSettingsLayout::OnSilhouetteImportanceChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo)
{
	const EMeshFeatureImportance::Type SilhouetteImportance = (EMeshFeatureImportance::Type)ImportanceOptions.Find(NewValue);
	if (ReductionSettings.SilhouetteImportance != SilhouetteImportance)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.StaticMesh.ReductionSettings"), TEXT("SilhouetteImportance"), *NewValue.Get());
		}
		ReductionSettings.SilhouetteImportance = SilhouetteImportance;
	}
}

void FMeshReductionSettingsLayout::OnTextureImportanceChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo)
{
	const EMeshFeatureImportance::Type TextureImportance = (EMeshFeatureImportance::Type)ImportanceOptions.Find(NewValue);
	if (ReductionSettings.TextureImportance != TextureImportance)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.StaticMesh.ReductionSettings"), TEXT("TextureImportance"), *NewValue.Get());
		}
		ReductionSettings.TextureImportance = TextureImportance;
	}
}

void FMeshReductionSettingsLayout::OnShadingImportanceChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo)
{
	const EMeshFeatureImportance::Type ShadingImportance = (EMeshFeatureImportance::Type)ImportanceOptions.Find(NewValue);
	if (ReductionSettings.ShadingImportance != ShadingImportance)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.StaticMesh.ReductionSettings"), TEXT("ShadingImportance"), *NewValue.Get());
		}
		ReductionSettings.ShadingImportance = ShadingImportance;
	}
}

void FMeshReductionSettingsLayout::OnTerminationCriterionChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo)
{
	const EStaticMeshReductionTerimationCriterion TerminationCriterion = (EStaticMeshReductionTerimationCriterion)TerminationOptions.Find(NewValue);
	if (ReductionSettings.TerminationCriterion != TerminationCriterion)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.StaticMesh.ReductionSettings"), TEXT("TerminationCriterion"), *NewValue.Get());
		}
		ReductionSettings.TerminationCriterion = TerminationCriterion;
	}
}

TOptional<int32> FMeshReductionSettingsLayout::GetBaseLODIndex() const
{
	return ReductionSettings.BaseLODModel;
}

void FMeshReductionSettingsLayout::SetBaseLODIndex(int32 NewLODBaseIndex)
{
	if (NewLODBaseIndex <= CurrentLODIndex)
	{
		ReductionSettings.BaseLODModel = NewLODBaseIndex;
	}
}

FMeshSectionSettingsLayout::~FMeshSectionSettingsLayout()
{
}

UStaticMesh& FMeshSectionSettingsLayout::GetStaticMesh() const
{
	UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();
	check(StaticMesh);
	return *StaticMesh;
}

void FMeshSectionSettingsLayout::AddToCategory( IDetailCategoryBuilder& CategoryBuilder )
{
	FSectionListDelegates SectionListDelegates;

	SectionListDelegates.OnGetSections.BindSP(this, &FMeshSectionSettingsLayout::OnGetSectionsForView, LODIndex);
	SectionListDelegates.OnSectionChanged.BindSP(this, &FMeshSectionSettingsLayout::OnSectionChanged);
	SectionListDelegates.OnGenerateCustomNameWidgets.BindSP(this, &FMeshSectionSettingsLayout::OnGenerateCustomNameWidgetsForSection);
	SectionListDelegates.OnGenerateCustomSectionWidgets.BindSP(this, &FMeshSectionSettingsLayout::OnGenerateCustomSectionWidgetsForSection);

	SectionListDelegates.OnCopySectionList.BindSP(this, &FMeshSectionSettingsLayout::OnCopySectionList, LODIndex);
	SectionListDelegates.OnCanCopySectionList.BindSP(this, &FMeshSectionSettingsLayout::OnCanCopySectionList, LODIndex);
	SectionListDelegates.OnPasteSectionList.BindSP(this, &FMeshSectionSettingsLayout::OnPasteSectionList, LODIndex);
	SectionListDelegates.OnCopySectionItem.BindSP(this, &FMeshSectionSettingsLayout::OnCopySectionItem);
	SectionListDelegates.OnCanCopySectionItem.BindSP(this, &FMeshSectionSettingsLayout::OnCanCopySectionItem);
	SectionListDelegates.OnPasteSectionItem.BindSP(this, &FMeshSectionSettingsLayout::OnPasteSectionItem);
	//We need a valid name if we want the section expand state to be saved
	FName StaticMeshSectionListName = FName(*(FString(TEXT("StaticMeshSectionListNameLOD_")) + FString::FromInt(LODIndex)));
	CategoryBuilder.AddCustomBuilder(MakeShareable(new FSectionList(CategoryBuilder.GetParentLayout(), SectionListDelegates, true, 64, LODIndex, StaticMeshSectionListName)));

	StaticMeshEditor.RegisterOnSelectedLODChanged(FOnSelectedLODChanged::CreateSP(this, &FMeshSectionSettingsLayout::UpdateLODCategoryVisibility), false);
}

void FMeshSectionSettingsLayout::OnCopySectionList(int32 CurrentLODIndex)
{
	TSharedRef<FJsonObject> RootJsonObject = MakeShareable(new FJsonObject());

	UStaticMesh& StaticMesh = GetStaticMesh();
	FStaticMeshRenderData* RenderData = StaticMesh.RenderData.Get();

	if (RenderData != nullptr && RenderData->LODResources.IsValidIndex(CurrentLODIndex))
	{
		FStaticMeshLODResources& LOD = RenderData->LODResources[CurrentLODIndex];

		for (int32 SectionIndex = 0; SectionIndex < LOD.Sections.Num(); ++SectionIndex)
		{
			const FStaticMeshSection& Section = LOD.Sections[SectionIndex];

			TSharedPtr<FJsonObject> JSonSection = MakeShareable(new FJsonObject);

			JSonSection->SetNumberField(TEXT("MaterialIndex"), Section.MaterialIndex);
			JSonSection->SetBoolField(TEXT("EnableCollision"), Section.bEnableCollision);
			JSonSection->SetBoolField(TEXT("CastShadow"), Section.bCastShadow);

			RootJsonObject->SetObjectField(FString::Printf(TEXT("Section_%d"), SectionIndex), JSonSection);
		}
	}

	typedef TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>> FStringWriter;
	typedef TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>> FStringWriterFactory;

	FString CopyStr;
	TSharedRef<FStringWriter> Writer = FStringWriterFactory::Create(&CopyStr);
	FJsonSerializer::Serialize(RootJsonObject, Writer);

	if (!CopyStr.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardCopy(*CopyStr);
	}
}

bool FMeshSectionSettingsLayout::OnCanCopySectionList(int32 CurrentLODIndex) const
{
	UStaticMesh& StaticMesh = GetStaticMesh();
	FStaticMeshRenderData* RenderData = StaticMesh.RenderData.Get();

	if (RenderData != nullptr && RenderData->LODResources.IsValidIndex(CurrentLODIndex))
	{
		FStaticMeshLODResources& LOD = RenderData->LODResources[CurrentLODIndex];

		return LOD.Sections.Num() > 0;
	}

	return false;
}

void FMeshSectionSettingsLayout::OnPasteSectionList(int32 CurrentLODIndex)
{
	FString PastedText;
	FPlatformApplicationMisc::ClipboardPaste(PastedText);

	TSharedPtr<FJsonObject> RootJsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PastedText);
	bool bResult = FJsonSerializer::Deserialize(Reader, RootJsonObject);

	if (RootJsonObject.IsValid())
	{
		UStaticMesh& StaticMesh = GetStaticMesh();
		FStaticMeshRenderData* RenderData = StaticMesh.RenderData.Get();

		if (RenderData != nullptr && RenderData->LODResources.IsValidIndex(CurrentLODIndex))
		{
			// @todo: When SectionInfoMap moves location, this will need to be fixed up.
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			FProperty* Property = UStaticMesh::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_STRING_CHECKED(UStaticMesh, SectionInfoMap));
			PRAGMA_ENABLE_DEPRECATION_WARNINGS

			GetStaticMesh().PreEditChange(Property);

			FScopedTransaction Transaction(LOCTEXT("StaticMeshToolChangedPasteSectionList", "Staticmesh editor: Pasted section list"));
			GetStaticMesh().Modify();

			FStaticMeshLODResources& LOD = RenderData->LODResources[CurrentLODIndex];

			for (int32 SectionIndex = 0; SectionIndex < LOD.Sections.Num(); ++SectionIndex)
			{
				FStaticMeshSection& Section = LOD.Sections[SectionIndex];

				const TSharedPtr<FJsonObject>* JSonSection = nullptr;

				if (RootJsonObject->TryGetObjectField(FString::Printf(TEXT("Section_%d"), SectionIndex), JSonSection))
				{
					(*JSonSection)->TryGetNumberField(TEXT("MaterialIndex"), Section.MaterialIndex);
					(*JSonSection)->TryGetBoolField(TEXT("EnableCollision"), Section.bEnableCollision);
					(*JSonSection)->TryGetBoolField(TEXT("CastShadow"), Section.bCastShadow);

					// Update the section info
					FMeshSectionInfo Info = StaticMesh.GetSectionInfoMap().Get(LODIndex, SectionIndex);
					Info.MaterialIndex = Section.MaterialIndex;
					Info.bCastShadow = Section.bCastShadow;
					Info.bEnableCollision = Section.bEnableCollision;

					StaticMesh.GetSectionInfoMap().Set(LODIndex, SectionIndex, Info);
				}
			}

			CallPostEditChange(Property);
		}
	}
}

void FMeshSectionSettingsLayout::OnCopySectionItem(int32 CurrentLODIndex, int32 SectionIndex)
{
	TSharedRef<FJsonObject> RootJsonObject = MakeShareable(new FJsonObject());

	UStaticMesh& StaticMesh = GetStaticMesh();
	FStaticMeshRenderData* RenderData = StaticMesh.RenderData.Get();

	if (RenderData != nullptr && RenderData->LODResources.IsValidIndex(CurrentLODIndex))
	{
		FStaticMeshLODResources& LOD = RenderData->LODResources[CurrentLODIndex];

		if (LOD.Sections.IsValidIndex(SectionIndex))
		{
			const FStaticMeshSection& Section = LOD.Sections[SectionIndex];

			RootJsonObject->SetNumberField(TEXT("MaterialIndex"), Section.MaterialIndex);
			RootJsonObject->SetBoolField(TEXT("EnableCollision"), Section.bEnableCollision);
			RootJsonObject->SetBoolField(TEXT("CastShadow"), Section.bCastShadow);
		}
	}

	typedef TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>> FStringWriter;
	typedef TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>> FStringWriterFactory;

	FString CopyStr;
	TSharedRef<FStringWriter> Writer = FStringWriterFactory::Create(&CopyStr);
	FJsonSerializer::Serialize(RootJsonObject, Writer);

	if (!CopyStr.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardCopy(*CopyStr);
	}
}

bool FMeshSectionSettingsLayout::OnCanCopySectionItem(int32 CurrentLODIndex, int32 SectionIndex) const
{
	UStaticMesh& StaticMesh = GetStaticMesh();
	FStaticMeshRenderData* RenderData = StaticMesh.RenderData.Get();

	if (RenderData != nullptr && RenderData->LODResources.IsValidIndex(CurrentLODIndex))
	{
		FStaticMeshLODResources& LOD = RenderData->LODResources[CurrentLODIndex];

		return LOD.Sections.IsValidIndex(SectionIndex);
	}

	return false;
}

void FMeshSectionSettingsLayout::OnPasteSectionItem(int32 CurrentLODIndex, int32 SectionIndex)
{
	FString PastedText;
	FPlatformApplicationMisc::ClipboardPaste(PastedText);

	TSharedPtr<FJsonObject> RootJsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PastedText);
	bool bResult = FJsonSerializer::Deserialize(Reader, RootJsonObject);

	if (RootJsonObject.IsValid())
	{
		UStaticMesh& StaticMesh = GetStaticMesh();
		FStaticMeshRenderData* RenderData = StaticMesh.RenderData.Get();

		if (RenderData != nullptr && RenderData->LODResources.IsValidIndex(CurrentLODIndex))
		{
			// @todo: When SectionInfoMap moves location, this will need to be fixed up
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			FProperty* Property = UStaticMesh::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_STRING_CHECKED(UStaticMesh, SectionInfoMap));
			PRAGMA_ENABLE_DEPRECATION_WARNINGS

			GetStaticMesh().PreEditChange(Property);

			FScopedTransaction Transaction(LOCTEXT("StaticMeshToolChangedPasteSectionItem", "Staticmesh editor: Pasted section item"));
			GetStaticMesh().Modify();

			FStaticMeshLODResources& LOD = RenderData->LODResources[CurrentLODIndex];

			if (LOD.Sections.IsValidIndex(SectionIndex))
			{
				FStaticMeshSection& Section = LOD.Sections[SectionIndex];

				RootJsonObject->TryGetNumberField(TEXT("MaterialIndex"), Section.MaterialIndex);
				RootJsonObject->TryGetBoolField(TEXT("EnableCollision"), Section.bEnableCollision);
				RootJsonObject->TryGetBoolField(TEXT("CastShadow"), Section.bCastShadow);

				// Update the section info
				FMeshSectionInfo Info = StaticMesh.GetSectionInfoMap().Get(LODIndex, SectionIndex);
				Info.MaterialIndex = Section.MaterialIndex;
				Info.bCastShadow = Section.bCastShadow;
				Info.bEnableCollision = Section.bEnableCollision;

				StaticMesh.GetSectionInfoMap().Set(LODIndex, SectionIndex, Info);
			}

			CallPostEditChange(Property);
		}
	}
}

void FMeshSectionSettingsLayout::OnGetSectionsForView(ISectionListBuilder& OutSections, int32 ForLODIndex)
{
	check(LODIndex == ForLODIndex);
	UStaticMesh& StaticMesh = GetStaticMesh();
	FStaticMeshRenderData* RenderData = StaticMesh.RenderData.Get();
	if (RenderData && RenderData->LODResources.IsValidIndex(LODIndex))
	{
		FStaticMeshLODResources& LOD = RenderData->LODResources[LODIndex];
		int32 NumSections = LOD.Sections.Num();
		
		for (int32 SectionIndex = 0; SectionIndex < NumSections; ++SectionIndex)
		{
			FMeshSectionInfo Info = StaticMesh.GetSectionInfoMap().Get(LODIndex, SectionIndex);
			int32 MaterialIndex = Info.MaterialIndex;
			if (StaticMesh.StaticMaterials.IsValidIndex(MaterialIndex))
			{
				FName CurrentSectionMaterialSlotName = StaticMesh.StaticMaterials[MaterialIndex].MaterialSlotName;
				FName CurrentSectionOriginalImportedMaterialName = StaticMesh.StaticMaterials[MaterialIndex].ImportedMaterialSlotName;
				TMap<int32, FName> AvailableSectionName;
				int32 CurrentIterMaterialIndex = 0;
				for (const FStaticMaterial &SkeletalMaterial : StaticMesh.StaticMaterials)
				{
					if (MaterialIndex != CurrentIterMaterialIndex)
						AvailableSectionName.Add(CurrentIterMaterialIndex, SkeletalMaterial.MaterialSlotName);
					CurrentIterMaterialIndex++;
				}
				UMaterialInterface* SectionMaterial = StaticMesh.StaticMaterials[MaterialIndex].MaterialInterface;
				if (SectionMaterial == NULL)
				{
					SectionMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
				}
				//TODO: Need to know if a section material slot assignment was change from the default value (implemented in skeletalmesh editor)
				OutSections.AddSection(LODIndex, SectionIndex, CurrentSectionMaterialSlotName, MaterialIndex, CurrentSectionOriginalImportedMaterialName, AvailableSectionName, StaticMesh.StaticMaterials[MaterialIndex].MaterialInterface, false, false, MaterialIndex);
			}
		}
	}
}

void FMeshSectionSettingsLayout::OnSectionChanged(int32 ForLODIndex, int32 SectionIndex, int32 NewMaterialSlotIndex, FName NewMaterialSlotName)
{
	check(LODIndex == ForLODIndex);
	UStaticMesh& StaticMesh = GetStaticMesh();

	check(StaticMesh.StaticMaterials.IsValidIndex(NewMaterialSlotIndex));

	int32 NewStaticMaterialIndex = INDEX_NONE;
	for (int StaticMaterialIndex = 0; StaticMaterialIndex < StaticMesh.StaticMaterials.Num(); ++StaticMaterialIndex)
	{
		if (NewMaterialSlotIndex == StaticMaterialIndex && StaticMesh.StaticMaterials[StaticMaterialIndex].MaterialSlotName == NewMaterialSlotName)
		{
			NewStaticMaterialIndex = StaticMaterialIndex;
			break;
		}
	}
	check(NewStaticMaterialIndex != INDEX_NONE);
	check(StaticMesh.RenderData);
	FStaticMeshRenderData* RenderData = StaticMesh.RenderData.Get();
	if (RenderData && RenderData->LODResources.IsValidIndex(LODIndex))
	{
		bool bRefreshAll = false;
		FStaticMeshLODResources& LOD = RenderData->LODResources[LODIndex];
		if (LOD.Sections.IsValidIndex(SectionIndex))
		{
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			FProperty* Property = UStaticMesh::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_STRING_CHECKED(UStaticMesh, SectionInfoMap));
			PRAGMA_ENABLE_DEPRECATION_WARNINGS

			GetStaticMesh().PreEditChange(Property);

			FScopedTransaction Transaction(LOCTEXT("StaticMeshOnSectionChangedTransaction", "Staticmesh editor: Section material slot changed"));
			GetStaticMesh().Modify();
			FMeshSectionInfo Info = StaticMesh.GetSectionInfoMap().Get(LODIndex, SectionIndex);
			int32 CancelOldValue = Info.MaterialIndex;
			Info.MaterialIndex = NewStaticMaterialIndex;
			StaticMesh.GetSectionInfoMap().Set(LODIndex, SectionIndex, Info);
			bool bUserCancel = false;
			bRefreshAll = StaticMesh.FixLODRequiresAdjacencyInformation(ForLODIndex, false, true, &bUserCancel);
			if (bUserCancel)
			{
				//Revert the section info map change
				Info.MaterialIndex = CancelOldValue;
				StaticMesh.GetSectionInfoMap().Set(LODIndex, SectionIndex, Info);
			}
			CallPostEditChange();
		}
		if (bRefreshAll)
		{
			StaticMeshEditor.RefreshTool();
		}
	}
}

TSharedRef<SWidget> FMeshSectionSettingsLayout::OnGenerateCustomNameWidgetsForSection(int32 ForLODIndex, int32 SectionIndex)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SCheckBox)
			.IsChecked(this, &FMeshSectionSettingsLayout::IsSectionHighlighted, SectionIndex)
			.OnCheckStateChanged(this, &FMeshSectionSettingsLayout::OnSectionHighlightedChanged, SectionIndex)
			.ToolTipText(LOCTEXT("Highlight_ToolTip", "Highlights this section in the viewport"))
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ColorAndOpacity( FLinearColor( 0.4f, 0.4f, 0.4f, 1.0f) )
				.Text(LOCTEXT("Highlight", "Highlight"))

			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 2, 0, 0)
		[
			SNew(SCheckBox)
			.IsChecked(this, &FMeshSectionSettingsLayout::IsSectionIsolatedEnabled, SectionIndex)
			.OnCheckStateChanged(this, &FMeshSectionSettingsLayout::OnSectionIsolatedChanged, SectionIndex)
			.ToolTipText(LOCTEXT("Isolate_ToolTip", "Isolates this section in the viewport"))
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ColorAndOpacity(FLinearColor(0.4f, 0.4f, 0.4f, 1.0f))
				.Text(LOCTEXT("Isolate", "Isolate"))

			]
		];
}

TSharedRef<SWidget> FMeshSectionSettingsLayout::OnGenerateCustomSectionWidgetsForSection(int32 ForLODIndex, int32 SectionIndex)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2, 0, 2, 0)
		[
			SNew(SCheckBox)
				.IsChecked(this, &FMeshSectionSettingsLayout::DoesSectionCastShadow, SectionIndex)
				.OnCheckStateChanged(this, &FMeshSectionSettingsLayout::OnSectionCastShadowChanged, SectionIndex)
			[
				SNew(STextBlock)
					.Font(FEditorStyle::GetFontStyle("StaticMeshEditor.NormalFont"))
					.Text(LOCTEXT("CastShadow", "Cast Shadow"))
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2,0,2,0)
		[
			SNew(SCheckBox)
				.IsEnabled(this, &FMeshSectionSettingsLayout::SectionCollisionEnabled)
				.ToolTipText(this, &FMeshSectionSettingsLayout::GetCollisionEnabledToolTip)
				.IsChecked(this, &FMeshSectionSettingsLayout::DoesSectionCollide, SectionIndex)
				.OnCheckStateChanged(this, &FMeshSectionSettingsLayout::OnSectionCollisionChanged, SectionIndex)
			[
				SNew(STextBlock)
					.Font(FEditorStyle::GetFontStyle("StaticMeshEditor.NormalFont"))
					.Text(LOCTEXT("EnableCollision", "Enable Collision"))
			]
		]
		+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2, 0, 2, 0)
			[
				SNew(SCheckBox)
				.IsChecked(this, &FMeshSectionSettingsLayout::IsSectionVisibleInRayTracing, SectionIndex)
			.OnCheckStateChanged(this, &FMeshSectionSettingsLayout::OnSectionVisibleInRayTracingChanged, SectionIndex)
			[
				SNew(STextBlock)
				.Font(FEditorStyle::GetFontStyle("StaticMeshEditor.NormalFont"))
			.Text(LOCTEXT("VisibleInRayTracing", "Visible In Ray Tracing"))
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2, 0, 2, 0)
		[
			SNew(SCheckBox)
			.IsChecked(this, &FMeshSectionSettingsLayout::IsSectionOpaque, SectionIndex)
			.OnCheckStateChanged(this, &FMeshSectionSettingsLayout::OnSectionForceOpaqueFlagChanged, SectionIndex)
			[
				SNew(STextBlock)
					.Font(FEditorStyle::GetFontStyle("StaticMeshEditor.NormalFont"))
					.Text(LOCTEXT("ForceOpaque", "Force Opaque"))
			]
		];
}

ECheckBoxState FMeshSectionSettingsLayout::IsSectionVisibleInRayTracing(int32 SectionIndex) const
{
	UStaticMesh& StaticMesh = GetStaticMesh();
	FMeshSectionInfo Info = StaticMesh.GetSectionInfoMap().Get(LODIndex, SectionIndex);
	return Info.bVisibleInRayTracing ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FMeshSectionSettingsLayout::OnSectionVisibleInRayTracingChanged(ECheckBoxState NewState, int32 SectionIndex)
{
	UStaticMesh& StaticMesh = GetStaticMesh();

	FText TransactionTest = LOCTEXT("StaticMeshEditorSetVisibleInRayTracingSectionFlag", "Staticmesh editor: Set VisibleInRayTracing For section, the section will be visible in ray tracing effects");
	if (NewState == ECheckBoxState::Unchecked)
	{
		TransactionTest = LOCTEXT("StaticMeshEditorClearVisibleInRayTracingSectionFlag", "Staticmesh editor: Clear VisibleInRayTracing For section");
	}
	FScopedTransaction Transaction(TransactionTest);

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
		FProperty* Property = UStaticMesh::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_STRING_CHECKED(UStaticMesh, SectionInfoMap));
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

		StaticMesh.PreEditChange(Property);
	StaticMesh.Modify();

	FMeshSectionInfo Info = StaticMesh.GetSectionInfoMap().Get(LODIndex, SectionIndex);
	Info.bVisibleInRayTracing = (NewState == ECheckBoxState::Checked) ? true : false;
	StaticMesh.GetSectionInfoMap().Set(LODIndex, SectionIndex, Info);
	CallPostEditChange();
}

ECheckBoxState FMeshSectionSettingsLayout::IsSectionOpaque(int32 SectionIndex) const
{
	UStaticMesh& StaticMesh = GetStaticMesh();
	FMeshSectionInfo Info = StaticMesh.GetSectionInfoMap().Get(LODIndex, SectionIndex);
	return Info.bForceOpaque ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FMeshSectionSettingsLayout::OnSectionForceOpaqueFlagChanged( ECheckBoxState NewState, int32 SectionIndex )
{
	UStaticMesh& StaticMesh = GetStaticMesh();

	FText TransactionTest = LOCTEXT("StaticMeshEditorSetForceOpaqueSectionFlag", "Staticmesh editor: Set Force Opaque For section, the section will be considered opaque in ray tracing effects");
	if (NewState == ECheckBoxState::Unchecked)
	{
		TransactionTest = LOCTEXT("StaticMeshEditorClearForceOpaqueSectionFlag", "Staticmesh editor: Clear Force Opaque For section");
	}
	FScopedTransaction Transaction(TransactionTest);

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
		FProperty* Property = UStaticMesh::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_STRING_CHECKED(UStaticMesh, SectionInfoMap));
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

		StaticMesh.PreEditChange(Property);
	StaticMesh.Modify();

	FMeshSectionInfo Info = StaticMesh.GetSectionInfoMap().Get(LODIndex, SectionIndex);
	Info.bForceOpaque = (NewState == ECheckBoxState::Checked) ? true : false;
	StaticMesh.GetSectionInfoMap().Set(LODIndex, SectionIndex, Info);
	CallPostEditChange();
}

ECheckBoxState FMeshSectionSettingsLayout::DoesSectionCastShadow(int32 SectionIndex) const
{
	UStaticMesh& StaticMesh = GetStaticMesh();
	FMeshSectionInfo Info = StaticMesh.GetSectionInfoMap().Get(LODIndex, SectionIndex);
	return Info.bCastShadow ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FMeshSectionSettingsLayout::OnSectionCastShadowChanged(ECheckBoxState NewState, int32 SectionIndex)
{
	UStaticMesh& StaticMesh = GetStaticMesh();

	FText TransactionTest = LOCTEXT("StaticMeshEditorSetShadowCastingSectionFlag", "Staticmesh editor: Set Shadow Casting For section");
	if (NewState == ECheckBoxState::Unchecked)
	{
		TransactionTest = LOCTEXT("StaticMeshEditorClearShadowCastingSectionFlag", "Staticmesh editor: Clear Shadow Casting For section");
	}
	FScopedTransaction Transaction(TransactionTest);

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FProperty* Property = UStaticMesh::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_STRING_CHECKED(UStaticMesh, SectionInfoMap));
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	StaticMesh.PreEditChange(Property);
	StaticMesh.Modify();
	
	FMeshSectionInfo Info = StaticMesh.GetSectionInfoMap().Get(LODIndex, SectionIndex);
	Info.bCastShadow = (NewState == ECheckBoxState::Checked) ? true : false;
	StaticMesh.GetSectionInfoMap().Set(LODIndex, SectionIndex, Info);
	CallPostEditChange();
}

bool FMeshSectionSettingsLayout::SectionCollisionEnabled() const
{
	UStaticMesh& StaticMesh = GetStaticMesh();
	// Only enable 'Enable Collision' check box if this LOD is used for collision
	return (StaticMesh.LODForCollision == LODIndex);
}

FText FMeshSectionSettingsLayout::GetCollisionEnabledToolTip() const
{
	UStaticMesh& StaticMesh = GetStaticMesh();
	
	// If using a different LOD for collision, disable the check box
	if (StaticMesh.LODForCollision != LODIndex)
	{
		return LOCTEXT("EnableCollisionToolTipDisabled", "This LOD is not used for collision, see the LODForCollision setting.");
	}
	// This LOD is used for collision, give info on what flag does
	else
	{
		return LOCTEXT("EnableCollisionToolTipEnabled", "Controls whether this section ever has per-poly collision. Disabling this where possible will lower memory usage for this mesh.");
	}
}

ECheckBoxState FMeshSectionSettingsLayout::DoesSectionCollide(int32 SectionIndex) const
{
	UStaticMesh& StaticMesh = GetStaticMesh();
	FMeshSectionInfo Info = StaticMesh.GetSectionInfoMap().Get(LODIndex, SectionIndex);
	return Info.bEnableCollision ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FMeshSectionSettingsLayout::OnSectionCollisionChanged(ECheckBoxState NewState, int32 SectionIndex)
{
	UStaticMesh& StaticMesh = GetStaticMesh();

	FText TransactionTest = LOCTEXT("StaticMeshEditorSetCollisionSectionFlag", "Staticmesh editor: Set Collision For section");
	if (NewState == ECheckBoxState::Unchecked)
	{
		TransactionTest = LOCTEXT("StaticMeshEditorClearCollisionSectionFlag", "Staticmesh editor: Clear Collision For section");
	}
	FScopedTransaction Transaction(TransactionTest);

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FProperty* Property = UStaticMesh::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_STRING_CHECKED(UStaticMesh, SectionInfoMap));
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	StaticMesh.PreEditChange(Property);
	StaticMesh.Modify();

	FMeshSectionInfo Info = StaticMesh.GetSectionInfoMap().Get(LODIndex, SectionIndex);
	Info.bEnableCollision = (NewState == ECheckBoxState::Checked) ? true : false;
	StaticMesh.GetSectionInfoMap().Set(LODIndex, SectionIndex, Info);
	CallPostEditChange();
}

ECheckBoxState FMeshSectionSettingsLayout::IsSectionHighlighted(int32 SectionIndex) const
{
	ECheckBoxState State = ECheckBoxState::Unchecked;
	UStaticMeshComponent* Component = StaticMeshEditor.GetStaticMeshComponent();
	if (Component)
	{
		State = Component->SelectedEditorSection == SectionIndex ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return State;
}

void FMeshSectionSettingsLayout::OnSectionHighlightedChanged(ECheckBoxState NewState, int32 SectionIndex)
{
	UStaticMeshComponent* Component = StaticMeshEditor.GetStaticMeshComponent();
	if (Component)
	{
		if (NewState == ECheckBoxState::Checked)
		{
			Component->SelectedEditorSection = SectionIndex;
			if (Component->SectionIndexPreview != SectionIndex)
			{
				// Unhide all mesh sections
				Component->SetSectionPreview(INDEX_NONE);
			}
			Component->SetMaterialPreview(INDEX_NONE);
			Component->SelectedEditorMaterial = INDEX_NONE;
		}
		else if (NewState == ECheckBoxState::Unchecked)
		{
			Component->SelectedEditorSection = INDEX_NONE;
		}
		Component->MarkRenderStateDirty();
		StaticMeshEditor.RefreshViewport();
	}
}

ECheckBoxState FMeshSectionSettingsLayout::IsSectionIsolatedEnabled(int32 SectionIndex) const
{
	ECheckBoxState State = ECheckBoxState::Unchecked;
	const UStaticMeshComponent* Component = StaticMeshEditor.GetStaticMeshComponent();
	if (Component)
	{
		State = Component->SectionIndexPreview == SectionIndex ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return State;
}

void FMeshSectionSettingsLayout::OnSectionIsolatedChanged(ECheckBoxState NewState, int32 SectionIndex)
{
	UStaticMeshComponent* Component = StaticMeshEditor.GetStaticMeshComponent();
	if (Component)
	{
		if (NewState == ECheckBoxState::Checked)
		{
			Component->SetSectionPreview(SectionIndex);
			if (Component->SelectedEditorSection != SectionIndex)
			{
				Component->SelectedEditorSection = INDEX_NONE;
			}
			Component->SetMaterialPreview(INDEX_NONE);
			Component->SelectedEditorMaterial = INDEX_NONE;
		}
		else if (NewState == ECheckBoxState::Unchecked)
		{
			Component->SetSectionPreview(INDEX_NONE);
		}
		Component->MarkRenderStateDirty();
		StaticMeshEditor.RefreshViewport();
	}
}

void FMeshSectionSettingsLayout::CallPostEditChange(FProperty* PropertyChanged/*=nullptr*/)
{
	UStaticMesh& StaticMesh = GetStaticMesh();
	if( PropertyChanged )
	{
		FPropertyChangedEvent PropertyUpdateStruct(PropertyChanged);
		StaticMesh.PostEditChangeProperty(PropertyUpdateStruct);
	}
	else
	{
		StaticMesh.Modify();
		StaticMesh.PostEditChange();
	}
	if(StaticMesh.BodySetup)
	{
		StaticMesh.BodySetup->CreatePhysicsMeshes();
	}
	StaticMeshEditor.RefreshViewport();
}

void FMeshSectionSettingsLayout::SetCurrentLOD(int32 NewLodIndex)
{
	if (StaticMeshEditor.GetStaticMeshComponent() == nullptr || LodCategoriesPtr == nullptr)
	{
		return;
	}
	int32 CurrentDisplayLOD = StaticMeshEditor.GetStaticMeshComponent()->ForcedLodModel;
	int32 RealCurrentDisplayLOD = CurrentDisplayLOD == 0 ? 0 : CurrentDisplayLOD - 1;
	int32 RealNewLOD = NewLodIndex == 0 ? 0 : NewLodIndex - 1;
	
	if (CurrentDisplayLOD == NewLodIndex || !LodCategoriesPtr->IsValidIndex(RealCurrentDisplayLOD) || !LodCategoriesPtr->IsValidIndex(RealNewLOD))
	{
		return;
	}

	StaticMeshEditor.GetStaticMeshComponent()->SetForcedLodModel(NewLodIndex);

	//Reset the preview section since we do not edit the same LOD
	StaticMeshEditor.GetStaticMeshComponent()->SetSectionPreview(INDEX_NONE);
	StaticMeshEditor.GetStaticMeshComponent()->SelectedEditorSection = INDEX_NONE;
}

void FMeshSectionSettingsLayout::UpdateLODCategoryVisibility()
{
	if (StaticMeshEditor.GetCustomData(CustomDataKey_LODEditMode) > 0)
	{
		//Do not change the Category visibility if we are in custom mode
		return;
	}
	bool bAutoLod = false;
	if (StaticMeshEditor.GetStaticMeshComponent() != nullptr)
	{
		bAutoLod = StaticMeshEditor.GetStaticMeshComponent()->ForcedLodModel == 0;
	}
	int32 CurrentDisplayLOD = bAutoLod ? 0 : StaticMeshEditor.GetStaticMeshComponent()->ForcedLodModel - 1;

	if (LodCategoriesPtr != nullptr && LodCategoriesPtr->IsValidIndex(CurrentDisplayLOD) && StaticMeshEditor.GetStaticMesh())
	{
		int32 StaticMeshLodNumber = StaticMeshEditor.GetStaticMesh()->GetNumLODs();
		for (int32 LodCategoryIndex = 0; LodCategoryIndex < StaticMeshLodNumber; ++LodCategoryIndex)
		{
			if (!LodCategoriesPtr->IsValidIndex(LodCategoryIndex))
			{
				break;
			}
			(*LodCategoriesPtr)[LodCategoryIndex]->SetCategoryVisibility(CurrentDisplayLOD == LodCategoryIndex);
		}
		//Reset the preview section since we do not edit the same LOD
		StaticMeshEditor.GetStaticMeshComponent()->SetSectionPreview(INDEX_NONE);
		StaticMeshEditor.GetStaticMeshComponent()->SelectedEditorSection = INDEX_NONE;
	}
}

//////////////////////////////////////////////////////////////////////////
// FMeshMaterialLayout
//////////////////////////////////////////////////////////////////////////

FMeshMaterialsLayout::~FMeshMaterialsLayout()
{
}

UStaticMesh& FMeshMaterialsLayout::GetStaticMesh() const
{
	UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();
	check(StaticMesh);
	return *StaticMesh;
}

void FMeshMaterialsLayout::AddToCategory(IDetailCategoryBuilder& CategoryBuilder, const TArray<FAssetData>& AssetDataArray)
{
	CategoryBuilder.AddCustomRow(LOCTEXT("AddLODLevelCategories_MaterialArrayOperationAdd", "Add Material Slot"))
		.CopyAction(FUIAction(FExecuteAction::CreateSP(this, &FMeshMaterialsLayout::OnCopyMaterialList), FCanExecuteAction::CreateSP(this, &FMeshMaterialsLayout::OnCanCopyMaterialList)))
		.PasteAction(FUIAction(FExecuteAction::CreateSP(this, &FMeshMaterialsLayout::OnPasteMaterialList)))
		.NameContent()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(LOCTEXT("AddLODLevelCategories_MaterialArrayOperations", "Material Slots"))
		]
		.ValueContent()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(this, &FMeshMaterialsLayout::GetMaterialArrayText)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(2.0f, 1.0f)
				[
					SNew(SButton)
					.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
					.Text(LOCTEXT("AddLODLevelCategories_MaterialArrayOpAdd", "Add Material Slot"))
					.ToolTipText(LOCTEXT("AddLODLevelCategories_MaterialArrayOpAdd_Tooltip", "Add Material Slot at the end of the Material slot array. Those Material slots can be used to override a LODs section, (not the base LOD)"))
					.ContentPadding(4.0f)
					.ForegroundColor(FSlateColor::UseForeground())
					.OnClicked(this, &FMeshMaterialsLayout::AddMaterialSlot)
					.IsEnabled(true)
					.IsFocusable(false)
					[
						SNew(SImage)
						.Image(FEditorStyle::GetBrush("PropertyWindow.Button_AddToArray"))
						.ColorAndOpacity(FSlateColor::UseForeground())
					]
				]
			]
		];

	FMaterialListDelegates MaterialListDelegates;
	MaterialListDelegates.OnGetMaterials.BindSP(this, &FMeshMaterialsLayout::GetMaterials);
	MaterialListDelegates.OnMaterialChanged.BindSP(this, &FMeshMaterialsLayout::OnMaterialChanged);
	MaterialListDelegates.OnGenerateCustomMaterialWidgets.BindSP(this, &FMeshMaterialsLayout::OnGenerateWidgetsForMaterial);
	MaterialListDelegates.OnGenerateCustomNameWidgets.BindSP(this, &FMeshMaterialsLayout::OnGenerateNameWidgetsForMaterial);
	MaterialListDelegates.OnMaterialListDirty.BindSP(this, &FMeshMaterialsLayout::OnMaterialListDirty);
	MaterialListDelegates.OnResetMaterialToDefaultClicked.BindSP(this, &FMeshMaterialsLayout::OnResetMaterialToDefaultClicked);

	MaterialListDelegates.OnCopyMaterialItem.BindSP(this, &FMeshMaterialsLayout::OnCopyMaterialItem);
	MaterialListDelegates.OnCanCopyMaterialItem.BindSP(this, &FMeshMaterialsLayout::OnCanCopyMaterialItem);
	MaterialListDelegates.OnPasteMaterialItem.BindSP(this, &FMeshMaterialsLayout::OnPasteMaterialItem);

	CategoryBuilder.AddCustomBuilder(MakeShareable(new FMaterialList(CategoryBuilder.GetParentLayout(), MaterialListDelegates, AssetDataArray, false, true, true)));
}

void FMeshMaterialsLayout::OnCopyMaterialList()
{
	FProperty* Property = UStaticMesh::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_STRING_CHECKED(UStaticMesh, StaticMaterials));
	check(Property != nullptr);

	auto JsonValue = FJsonObjectConverter::UPropertyToJsonValue(Property, &GetStaticMesh().StaticMaterials, 0, 0);

	typedef TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>> FStringWriter;
	typedef TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>> FStringWriterFactory;

	FString CopyStr;
	TSharedRef<FStringWriter> Writer = FStringWriterFactory::Create(&CopyStr);
	FJsonSerializer::Serialize(JsonValue.ToSharedRef(), TEXT(""), Writer);

	if (!CopyStr.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardCopy(*CopyStr);
	}
}

bool FMeshMaterialsLayout::OnCanCopyMaterialList() const
{
	return GetStaticMesh().StaticMaterials.Num() > 0;
}

void FMeshMaterialsLayout::OnPasteMaterialList()
{
	FString PastedText;
	FPlatformApplicationMisc::ClipboardPaste(PastedText);

	TSharedPtr<FJsonValue> RootJsonValue;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PastedText);
	FJsonSerializer::Deserialize(Reader, RootJsonValue);

	if (RootJsonValue.IsValid())
	{
		FProperty* Property = UStaticMesh::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_STRING_CHECKED(UStaticMesh, StaticMaterials));
		check(Property != nullptr);

		GetStaticMesh().PreEditChange(Property);
		FScopedTransaction Transaction(LOCTEXT("StaticMeshToolChangedPasteMaterialList", "Staticmesh editor: Pasted material list"));
		GetStaticMesh().Modify();

		TArray<FStaticMaterial> TempMaterials;
		FJsonObjectConverter::JsonValueToUProperty(RootJsonValue, Property, &TempMaterials, 0, 0);
		//Do not change the number of material in the array
		for (int32 MaterialIndex = 0; MaterialIndex < TempMaterials.Num(); ++MaterialIndex)
		{
			if (GetStaticMesh().StaticMaterials.IsValidIndex(MaterialIndex))
			{
				GetStaticMesh().StaticMaterials[MaterialIndex].MaterialInterface = TempMaterials[MaterialIndex].MaterialInterface;
			}
		}

		CallPostEditChange(Property);
	}
}

void FMeshMaterialsLayout::OnCopyMaterialItem(int32 CurrentSlot)
{
	TSharedRef<FJsonObject> RootJsonObject = MakeShareable(new FJsonObject());

	if (GetStaticMesh().StaticMaterials.IsValidIndex(CurrentSlot))
	{
		const FStaticMaterial &Material = GetStaticMesh().StaticMaterials[CurrentSlot];

		FJsonObjectConverter::UStructToJsonObject(FStaticMaterial::StaticStruct(), &Material, RootJsonObject, 0, 0);
	}

	typedef TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>> FStringWriter;
	typedef TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>> FStringWriterFactory;

	FString CopyStr;
	TSharedRef<FStringWriter> Writer = FStringWriterFactory::Create(&CopyStr);
	FJsonSerializer::Serialize(RootJsonObject, Writer);

	if (!CopyStr.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardCopy(*CopyStr);
	}
}

bool FMeshMaterialsLayout::OnCanCopyMaterialItem(int32 CurrentSlot) const
{
	return GetStaticMesh().StaticMaterials.IsValidIndex(CurrentSlot);
}

void FMeshMaterialsLayout::OnPasteMaterialItem(int32 CurrentSlot)
{
	FString PastedText;
	FPlatformApplicationMisc::ClipboardPaste(PastedText);

	TSharedPtr<FJsonObject> RootJsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PastedText);
	FJsonSerializer::Deserialize(Reader, RootJsonObject);

	if (RootJsonObject.IsValid())
	{
		FProperty* Property = UStaticMesh::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_STRING_CHECKED(UStaticMesh, StaticMaterials));
		check(Property != nullptr);

		GetStaticMesh().PreEditChange(Property);

		FScopedTransaction Transaction(LOCTEXT("StaticMeshToolChangedPasteMaterialItem", "Staticmesh editor: Pasted material item"));
		GetStaticMesh().Modify();

		if (GetStaticMesh().StaticMaterials.IsValidIndex(CurrentSlot))
		{
			FStaticMaterial TmpStaticMaterial;
			FJsonObjectConverter::JsonObjectToUStruct(RootJsonObject.ToSharedRef(), FStaticMaterial::StaticStruct(), &TmpStaticMaterial, 0, 0);
			GetStaticMesh().StaticMaterials[CurrentSlot].MaterialInterface = TmpStaticMaterial.MaterialInterface;
		}

		CallPostEditChange(Property);
	}
}

FReply FMeshMaterialsLayout::AddMaterialSlot()
{
	UStaticMesh& StaticMesh = GetStaticMesh();

	FScopedTransaction Transaction(LOCTEXT("FMeshMaterialsLayout_AddMaterialSlot", "Staticmesh editor: Add material slot"));
	StaticMesh.Modify();
	StaticMesh.StaticMaterials.Add(FStaticMaterial());

	StaticMesh.PostEditChange();

	return FReply::Handled();
}

FText FMeshMaterialsLayout::GetMaterialArrayText() const
{
	UStaticMesh& StaticMesh = GetStaticMesh();

	FString MaterialArrayText = TEXT(" Material Slots");
	int32 SlotNumber = 0;
	SlotNumber = StaticMesh.StaticMaterials.Num();
	MaterialArrayText = FString::FromInt(SlotNumber) + MaterialArrayText;
	return FText::FromString(MaterialArrayText);
}

void FMeshMaterialsLayout::GetMaterials(IMaterialListBuilder& ListBuilder)
{
	UStaticMesh& StaticMesh = GetStaticMesh();
	for (int32 MaterialIndex = 0; MaterialIndex < StaticMesh.StaticMaterials.Num(); ++MaterialIndex)
	{
		UMaterialInterface* Material = StaticMesh.GetMaterial(MaterialIndex);
		if (Material == NULL)
		{
			Material = UMaterial::GetDefaultMaterial(MD_Surface);
		}
		ListBuilder.AddMaterial(MaterialIndex, Material, /*bCanBeReplaced=*/ true);
	}
}

void FMeshMaterialsLayout::OnMaterialChanged(UMaterialInterface* NewMaterial, UMaterialInterface* PrevMaterial, int32 MaterialIndex, bool bReplaceAll)
{
	UStaticMesh& StaticMesh = GetStaticMesh();
	StaticMesh.SetMaterial(MaterialIndex, NewMaterial);
	StaticMeshEditor.RefreshTool();
}

TSharedRef<SWidget> FMeshMaterialsLayout::OnGenerateWidgetsForMaterial(UMaterialInterface* Material, int32 SlotIndex)
{
	UStaticMesh& StaticMesh = GetStaticMesh();
	bool bMaterialIsUsed = false;
	if(MaterialUsedMap.Contains(SlotIndex))
	{
		bMaterialIsUsed = MaterialUsedMap.Find(SlotIndex)->Num() > 0;
	}

	return 
		SNew(SMaterialSlotWidget, SlotIndex, bMaterialIsUsed)
		.MaterialName(this, &FMeshMaterialsLayout::GetMaterialNameText, SlotIndex)
		.OnMaterialNameCommitted(this, &FMeshMaterialsLayout::OnMaterialNameCommitted, SlotIndex)
		.CanDeleteMaterialSlot(this, &FMeshMaterialsLayout::CanDeleteMaterialSlot, SlotIndex)
		.OnDeleteMaterialSlot(this, &FMeshMaterialsLayout::OnDeleteMaterialSlot, SlotIndex)
		.ToolTipText(this, &FMeshMaterialsLayout::GetOriginalImportMaterialNameText, SlotIndex);

#if 0 // HACK!!! Temporary disabled
		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0,2,0,0)
		[
			SNew(SCheckBox)
				.Visibility(this, &FMeshMaterialsLayout::GetOverrideUVDensityVisibililty)
				.IsChecked(this, &FMeshMaterialsLayout::IsUVDensityOverridden, SlotIndex)
				.OnCheckStateChanged(this, &FMeshMaterialsLayout::OnOverrideUVDensityChanged, SlotIndex)
			[
				SNew(STextBlock)
					.Font(FEditorStyle::GetFontStyle("StaticMeshEditor.NormalFont"))
					.Text(LOCTEXT("OverrideUVDensity", "Override UV Density"))
			]
		]
		+ GetUVDensitySlot(SlotIndex, 0)
		+ GetUVDensitySlot(SlotIndex, 1)
		+ GetUVDensitySlot(SlotIndex, 2)
		+ GetUVDensitySlot(SlotIndex, 3);
#endif
}

TSharedRef<SWidget> FMeshMaterialsLayout::OnGenerateNameWidgetsForMaterial(UMaterialInterface* Material, int32 SlotIndex)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SCheckBox)
			.IsChecked(this, &FMeshMaterialsLayout::IsMaterialHighlighted, SlotIndex)
			.OnCheckStateChanged(this, &FMeshMaterialsLayout::OnMaterialHighlightedChanged, SlotIndex)
			.ToolTipText(LOCTEXT("Highlight_CustomMaterialName_ToolTip", "Highlights this material in the viewport"))
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ColorAndOpacity(FLinearColor(0.4f, 0.4f, 0.4f, 1.0f))
				.Text(LOCTEXT("Highlight", "Highlight"))
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 2, 0, 0)
		[
			SNew(SCheckBox)
			.IsChecked(this, &FMeshMaterialsLayout::IsMaterialIsolatedEnabled, SlotIndex)
			.OnCheckStateChanged(this, &FMeshMaterialsLayout::OnMaterialIsolatedChanged, SlotIndex)
			.ToolTipText(LOCTEXT("Isolate_CustomMaterialName_ToolTip", "Isolates this material in the viewport"))
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ColorAndOpacity(FLinearColor(0.4f, 0.4f, 0.4f, 1.0f))
				.Text(LOCTEXT("Isolate", "Isolate"))
			]
		];
}

ECheckBoxState FMeshMaterialsLayout::IsMaterialHighlighted(int32 SlotIndex) const
{
	ECheckBoxState State = ECheckBoxState::Unchecked;
	UStaticMeshComponent* Component = StaticMeshEditor.GetStaticMeshComponent();
	if (Component)
	{
		State = Component->SelectedEditorMaterial == SlotIndex ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return State;
}

void FMeshMaterialsLayout::OnMaterialHighlightedChanged(ECheckBoxState NewState, int32 SlotIndex)
{
	UStaticMeshComponent* Component = StaticMeshEditor.GetStaticMeshComponent();
	if (Component)
	{
		if (NewState == ECheckBoxState::Checked)
		{
			Component->SelectedEditorMaterial = SlotIndex;
			if (Component->MaterialIndexPreview != SlotIndex)
			{
				Component->SetMaterialPreview(INDEX_NONE);
			}
			Component->SetSectionPreview(INDEX_NONE);
			Component->SelectedEditorSection = INDEX_NONE;
		}
		else if (NewState == ECheckBoxState::Unchecked)
		{
			Component->SelectedEditorMaterial = INDEX_NONE;
		}
		Component->MarkRenderStateDirty();
		Component->PushSelectionToProxy();
		StaticMeshEditor.RefreshViewport();
	}
}

ECheckBoxState FMeshMaterialsLayout::IsMaterialIsolatedEnabled(int32 SlotIndex) const
{
	ECheckBoxState State = ECheckBoxState::Unchecked;
	const UStaticMeshComponent* Component = StaticMeshEditor.GetStaticMeshComponent();
	if (Component)
	{
		State = Component->MaterialIndexPreview == SlotIndex ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return State;
}

void FMeshMaterialsLayout::OnMaterialIsolatedChanged(ECheckBoxState NewState, int32 SlotIndex)
{
	UStaticMeshComponent* Component = StaticMeshEditor.GetStaticMeshComponent();
	if (Component)
	{
		if (NewState == ECheckBoxState::Checked)
		{
			Component->SetMaterialPreview(SlotIndex);
			if (Component->SelectedEditorMaterial != SlotIndex)
			{
				Component->SelectedEditorMaterial = INDEX_NONE;
			}
			Component->SetSectionPreview(INDEX_NONE);
			Component->SelectedEditorSection = INDEX_NONE;
		}
		else if (NewState == ECheckBoxState::Unchecked)
		{
			Component->SetMaterialPreview(INDEX_NONE);
		}
		Component->MarkRenderStateDirty();
		StaticMeshEditor.RefreshViewport();
	}
}

void FMeshMaterialsLayout::OnResetMaterialToDefaultClicked(UMaterialInterface* Material, int32 MaterialIndex)
{
	UStaticMesh& StaticMesh = GetStaticMesh();
	check(StaticMesh.StaticMaterials.IsValidIndex(MaterialIndex));
	StaticMesh.StaticMaterials[MaterialIndex].MaterialInterface = UMaterial::GetDefaultMaterial(MD_Surface);
	CallPostEditChange();
}

FText FMeshMaterialsLayout::GetOriginalImportMaterialNameText(int32 MaterialIndex) const
{
	UStaticMesh& StaticMesh = GetStaticMesh();
	if (StaticMesh.StaticMaterials.IsValidIndex(MaterialIndex))
	{
		FString OriginalImportMaterialName;
		StaticMesh.StaticMaterials[MaterialIndex].ImportedMaterialSlotName.ToString(OriginalImportMaterialName);
		OriginalImportMaterialName = TEXT("Original Imported Material Name: ") + OriginalImportMaterialName;
		return FText::FromString(OriginalImportMaterialName);
	}
	return FText::FromName(NAME_None);
}

FText FMeshMaterialsLayout::GetMaterialNameText(int32 MaterialIndex) const
{
	UStaticMesh& StaticMesh = GetStaticMesh();
	if (StaticMesh.StaticMaterials.IsValidIndex(MaterialIndex))
	{
		return FText::FromName(StaticMesh.StaticMaterials[MaterialIndex].MaterialSlotName);
	}
	return FText::FromName(NAME_None);
}

void FMeshMaterialsLayout::OnMaterialNameCommitted(const FText& InValue, ETextCommit::Type CommitType, int32 MaterialIndex)
{
	UStaticMesh& StaticMesh = GetStaticMesh();
	FName InValueName = FName(*(InValue.ToString()));
	if (StaticMesh.StaticMaterials.IsValidIndex(MaterialIndex) && StaticMesh.StaticMaterials[MaterialIndex].MaterialSlotName != InValueName)
	{
		FScopedTransaction ScopeTransaction(LOCTEXT("StaticMeshEditorMaterialSlotNameChanged", "Staticmesh editor: Material slot name change"));

		FProperty* ChangedProperty = NULL;
		ChangedProperty = FindFProperty<FProperty>(UStaticMesh::StaticClass(), "StaticMaterials");
		check(ChangedProperty);
		StaticMesh.PreEditChange(ChangedProperty);

		StaticMesh.StaticMaterials[MaterialIndex].MaterialSlotName = InValueName;
		
		FPropertyChangedEvent PropertyUpdateStruct(ChangedProperty);
		StaticMesh.PostEditChangeProperty(PropertyUpdateStruct);
	}
}

bool FMeshMaterialsLayout::CanDeleteMaterialSlot(int32 MaterialIndex) const
{
	UStaticMesh& StaticMesh = GetStaticMesh();
	return StaticMesh.StaticMaterials.IsValidIndex(MaterialIndex);
}

void FMeshMaterialsLayout::OnDeleteMaterialSlot(int32 MaterialIndex)
{
	UStaticMesh& StaticMesh = GetStaticMesh();
	if (CanDeleteMaterialSlot(MaterialIndex))
	{
		if (!bDeleteWarningConsumed)
		{
			EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::OkCancel, LOCTEXT("FMeshMaterialsLayout_DeleteMaterialSlot", "WARNING - Deleting a material slot can break the game play blueprint or the game play code. All indexes after the delete slot will change"));
			if (Answer == EAppReturnType::Cancel)
			{
				return;
			}
			bDeleteWarningConsumed = true;
		}

		FScopedTransaction Transaction(LOCTEXT("StaticMeshEditorDeletedMaterialSlot", "Staticmesh editor: Deleted material slot"));

		StaticMesh.Modify();
		StaticMesh.StaticMaterials.RemoveAt(MaterialIndex);

		//Fix the section info, the FMeshDescription use FName to retrieve the indexes when we build so no need to fix it
		for (int32 LodIndex = 0; LodIndex < StaticMesh.GetNumLODs(); ++LodIndex)
		{
			for (int32 SectionIndex = 0; SectionIndex < StaticMesh.GetNumSections(LodIndex); ++SectionIndex)
			{
				if (StaticMesh.GetSectionInfoMap().IsValidSection(LodIndex, SectionIndex))
				{
					FMeshSectionInfo SectionInfo = StaticMesh.GetSectionInfoMap().Get(LodIndex, SectionIndex);
					if (SectionInfo.MaterialIndex > MaterialIndex)
					{
						SectionInfo.MaterialIndex -= 1;
						StaticMesh.GetSectionInfoMap().Set(LodIndex, SectionIndex, SectionInfo);
					}
				}
			}
		}

		StaticMesh.PostEditChange();
	}

}

TSharedRef<SWidget> FMeshMaterialsLayout::OnGetMaterialSlotUsedByMenuContent(int32 MaterialIndex)
{
	UStaticMesh& StaticMesh = GetStaticMesh();
	FMenuBuilder MenuBuilder(true, NULL);
	TArray<FSectionLocalizer> *SectionLocalizers;
	if (MaterialUsedMap.Contains(MaterialIndex))
	{
		SectionLocalizers = MaterialUsedMap.Find(MaterialIndex);
		FUIAction Action;
		FText EmptyTooltip;
		// Add a menu item for each texture.  Clicking on the texture will display it in the content browser
		for (const FSectionLocalizer& SectionUsingMaterial : (*SectionLocalizers))
		{
			FString ArrayItemName = TEXT("Lod ") + FString::FromInt(SectionUsingMaterial.LODIndex) + TEXT("  Index ") + FString::FromInt(SectionUsingMaterial.SectionIndex);
			MenuBuilder.AddMenuEntry(FText::FromString(ArrayItemName), EmptyTooltip, FSlateIcon(), Action);
		}
	}
	return MenuBuilder.MakeWidget();
}

FText FMeshMaterialsLayout::GetFirstMaterialSlotUsedBySection(int32 MaterialIndex) const
{
	UStaticMesh& StaticMesh = GetStaticMesh();
	if (MaterialUsedMap.Contains(MaterialIndex))
	{
		const TArray<FSectionLocalizer> *SectionLocalizers = MaterialUsedMap.Find(MaterialIndex);
		if (SectionLocalizers->Num() > 0)
		{
			FString ArrayItemName = FString::FromInt(SectionLocalizers->Num()) + TEXT(" Sections");
			return FText::FromString(ArrayItemName);
		}
	}
	return FText();
}

bool FMeshMaterialsLayout::OnMaterialListDirty()
{
	UStaticMesh& StaticMesh = GetStaticMesh();
	bool ForceMaterialListRefresh = false;
	TMap<int32, TArray<FSectionLocalizer>> TempMaterialUsedMap;
	for (int32 MaterialIndex = 0; MaterialIndex < StaticMesh.StaticMaterials.Num(); ++MaterialIndex)
	{
		TArray<FSectionLocalizer> SectionLocalizers;
		for (int32 LODIndex = 0; LODIndex < StaticMesh.GetNumLODs(); ++LODIndex)
		{
			for (int32 SectionIndex = 0; SectionIndex < StaticMesh.GetNumSections(LODIndex); ++SectionIndex)
			{
				FMeshSectionInfo Info = StaticMesh.GetSectionInfoMap().Get(LODIndex, SectionIndex);

				if (Info.MaterialIndex == MaterialIndex)
				{
					SectionLocalizers.Add(FSectionLocalizer(LODIndex, SectionIndex));
				}
			}
		}
		TempMaterialUsedMap.Add(MaterialIndex, SectionLocalizers);
	}
	if (TempMaterialUsedMap.Num() != MaterialUsedMap.Num())
	{
		ForceMaterialListRefresh = true;
	}
	else if (!ForceMaterialListRefresh)
	{
		for (auto KvpOld : MaterialUsedMap)
		{
			if (!TempMaterialUsedMap.Contains(KvpOld.Key))
			{
				ForceMaterialListRefresh = true;
				break;
			}
			const TArray<FSectionLocalizer> &TempSectionLocalizers = (*(TempMaterialUsedMap.Find(KvpOld.Key)));
			const TArray<FSectionLocalizer> &OldSectionLocalizers = KvpOld.Value;
			if (TempSectionLocalizers.Num() != OldSectionLocalizers.Num())
			{
				ForceMaterialListRefresh = true;
				break;
			}
			for (int32 SectionLocalizerIndex = 0; SectionLocalizerIndex < OldSectionLocalizers.Num(); ++SectionLocalizerIndex)
			{
				if (OldSectionLocalizers[SectionLocalizerIndex] != TempSectionLocalizers[SectionLocalizerIndex])
				{
					ForceMaterialListRefresh = true;
					break;
				}
			}
			if (ForceMaterialListRefresh)
			{
				break;
			}
		}
	}
	MaterialUsedMap = TempMaterialUsedMap;

	return ForceMaterialListRefresh;
}

ECheckBoxState FMeshMaterialsLayout::IsShadowCastingEnabled(int32 SlotIndex) const
{
	bool FirstEvalDone = false;
	bool ShadowCastingValue = false;
	UStaticMesh& StaticMesh = GetStaticMesh();
	for (int32 LODIndex = 0; LODIndex < StaticMesh.GetNumLODs(); ++LODIndex)
	{
		for (int32 SectionIndex = 0; SectionIndex < StaticMesh.GetNumSections(LODIndex); ++SectionIndex)
		{
			FMeshSectionInfo Info = StaticMesh.GetSectionInfoMap().Get(LODIndex, SectionIndex);
			if (Info.MaterialIndex == SlotIndex)
			{
				if (!FirstEvalDone)
				{
					ShadowCastingValue = Info.bCastShadow;
					FirstEvalDone = true;
				}
				else if (ShadowCastingValue != Info.bCastShadow)
				{
					return ECheckBoxState::Undetermined;
				}
			}
		}
	}
	if (FirstEvalDone)
	{
		return ShadowCastingValue ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return ECheckBoxState::Undetermined;
}

void FMeshMaterialsLayout::OnShadowCastingChanged(ECheckBoxState NewState, int32 SlotIndex)
{
	UStaticMesh& StaticMesh = GetStaticMesh();
	
	if (NewState == ECheckBoxState::Undetermined)
		return;
	
	bool CastShadow = (NewState == ECheckBoxState::Checked) ? true : false;
	bool SomethingChange = false;
	for (int32 LODIndex = 0; LODIndex < StaticMesh.GetNumLODs(); ++LODIndex)
	{
		for (int32 SectionIndex = 0; SectionIndex < StaticMesh.GetNumSections(LODIndex); ++SectionIndex)
		{
			FMeshSectionInfo Info = StaticMesh.GetSectionInfoMap().Get(LODIndex, SectionIndex);
			if (Info.MaterialIndex == SlotIndex)
			{
				Info.bCastShadow = CastShadow;
				StaticMesh.GetSectionInfoMap().Set(LODIndex, SectionIndex, Info);
				SomethingChange = true;
			}
		}
	}

	if (SomethingChange)
	{
		CallPostEditChange();
	}
}

EVisibility FMeshMaterialsLayout::GetOverrideUVDensityVisibililty() const
{
	if (StaticMeshEditor.GetViewMode() == VMI_MeshUVDensityAccuracy)
	{
		return EVisibility::SelfHitTestInvisible;
	}
	else
	{
		return EVisibility::Collapsed;
	}
}

ECheckBoxState FMeshMaterialsLayout::IsUVDensityOverridden(int32 SlotIndex) const
{
	const UStaticMesh& StaticMesh = GetStaticMesh();
	if (!StaticMesh.StaticMaterials.IsValidIndex(SlotIndex))
	{
		return ECheckBoxState::Undetermined;
	}
	else if (StaticMesh.StaticMaterials[SlotIndex].UVChannelData.bOverrideDensities)
	{
		return ECheckBoxState::Checked;
	}
	else
	{
		return ECheckBoxState::Unchecked;
	}
}


void FMeshMaterialsLayout::OnOverrideUVDensityChanged(ECheckBoxState NewState, int32 SlotIndex)
{
	UStaticMesh& StaticMesh = GetStaticMesh();
	if (NewState != ECheckBoxState::Undetermined && StaticMesh.StaticMaterials.IsValidIndex(SlotIndex))
	{
		StaticMesh.StaticMaterials[SlotIndex].UVChannelData.bOverrideDensities = (NewState == ECheckBoxState::Checked);
		StaticMesh.UpdateUVChannelData(true);
	}
}

EVisibility FMeshMaterialsLayout::GetUVDensityVisibility(int32 SlotIndex, int32 UVChannelIndex) const
{
	UStaticMesh& StaticMesh = GetStaticMesh();
	if (StaticMeshEditor.GetViewMode() == VMI_MeshUVDensityAccuracy && IsUVDensityOverridden(SlotIndex) == ECheckBoxState::Checked && UVChannelIndex < StaticMeshEditor.GetNumUVChannels())
	{
		return EVisibility::SelfHitTestInvisible;
	}
	else
	{
		return EVisibility::Collapsed;
	}
}

TOptional<float> FMeshMaterialsLayout::GetUVDensityValue(int32 SlotIndex, int32 UVChannelIndex) const
{
	const UStaticMesh& StaticMesh = GetStaticMesh();
	if (StaticMesh.StaticMaterials.IsValidIndex(SlotIndex))
	{
		float Value = StaticMesh.StaticMaterials[SlotIndex].UVChannelData.LocalUVDensities[UVChannelIndex];
		return FMath::RoundToFloat(Value * 4.f) * .25f;
	}
	return TOptional<float>();
}

void FMeshMaterialsLayout::SetUVDensityValue(float InDensity, ETextCommit::Type CommitType, int32 SlotIndex, int32 UVChannelIndex)
{
	UStaticMesh& StaticMesh = GetStaticMesh();
	if (StaticMesh.StaticMaterials.IsValidIndex(SlotIndex))
	{
		StaticMesh.StaticMaterials[SlotIndex].UVChannelData.LocalUVDensities[UVChannelIndex] = FMath::Max<float>(0, InDensity);
		StaticMesh.UpdateUVChannelData(true);
	}
}

void FMeshMaterialsLayout::CallPostEditChange(FProperty* PropertyChanged/*=nullptr*/)
{
	UStaticMesh& StaticMesh = GetStaticMesh();
	if (PropertyChanged)
	{
		FPropertyChangedEvent PropertyUpdateStruct(PropertyChanged);
		StaticMesh.PostEditChangeProperty(PropertyUpdateStruct);
	}
	else
	{
		StaticMesh.Modify();
		StaticMesh.PostEditChange();
	}
	if (StaticMesh.BodySetup)
	{
		StaticMesh.BodySetup->CreatePhysicsMeshes();
	}
	StaticMeshEditor.RefreshViewport();
}


/////////////////////////////////
// FLevelOfDetailSettingsLayout
/////////////////////////////////

FLevelOfDetailSettingsLayout::FLevelOfDetailSettingsLayout( FStaticMeshEditor& InStaticMeshEditor )
	: StaticMeshEditor( InStaticMeshEditor )
{
	LODGroupNames.Reset();
	UStaticMesh::GetLODGroups(LODGroupNames);
	for (int32 GroupIndex = 0; GroupIndex < LODGroupNames.Num(); ++GroupIndex)
	{
		LODGroupOptions.Add(MakeShareable(new FString(LODGroupNames[GroupIndex].GetPlainNameString())));
	}

	for (int32 i = 0; i < MAX_STATIC_MESH_LODS; ++i)
	{
		bBuildSettingsExpanded[i] = false;
		bReductionSettingsExpanded[i] = false;
		bSectionSettingsExpanded[i] = (i == 0);

		LODScreenSizes[i] = 0.0f;
	}

	LODCount = StaticMeshEditor.GetStaticMesh()->GetNumLODs();

	UpdateLODNames();
}

/** Returns true if automatic mesh reduction is available. */
static bool IsAutoMeshReductionAvailable()
{
	bool bAutoMeshReductionAvailable = FModuleManager::Get().LoadModuleChecked<IMeshReductionManagerModule>("MeshReductionInterface").GetStaticMeshReductionInterface() != NULL;
	return bAutoMeshReductionAvailable;
}

void FLevelOfDetailSettingsLayout::AddToDetailsPanel( IDetailLayoutBuilder& DetailBuilder )
{
	UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();

	IDetailCategoryBuilder& LODSettingsCategory =
		DetailBuilder.EditCategory( "LodSettings", LOCTEXT("LodSettingsCategory", "LOD Settings") );

	int32 LODGroupIndex = LODGroupNames.Find(StaticMesh->LODGroup);
	check(LODGroupIndex == INDEX_NONE || LODGroupIndex < LODGroupOptions.Num());


	IDetailPropertyRow& LODGroupRow = LODSettingsCategory.AddProperty(GET_MEMBER_NAME_CHECKED(UStaticMesh, LODGroup));

	LODGroupRow.CustomWidget()
	.NameContent()
	[
		SNew(STextBlock)
		.Font( IDetailLayoutBuilder::GetDetailFont() )
		.Text(LOCTEXT("LODGroup", "LOD Group"))
	]
	.ValueContent()
	[
		SAssignNew(LODGroupComboBox, STextComboBox)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.OptionsSource(&LODGroupOptions)
		.InitiallySelectedItem(LODGroupOptions[(LODGroupIndex == INDEX_NONE) ? 0 : LODGroupIndex])
		.OnSelectionChanged(this, &FLevelOfDetailSettingsLayout::OnLODGroupChanged)
	];
	
	LODSettingsCategory.AddCustomRow( LOCTEXT("LODImport", "LOD Import") )
		.NameContent()
		[
			SNew(STextBlock)
			.Font( IDetailLayoutBuilder::GetDetailFont() )
			.Text(LOCTEXT("LODImport", "LOD Import"))
		]
	.ValueContent()
		[
			SNew(STextComboBox)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.OptionsSource(&LODNames)
			.InitiallySelectedItem(LODNames[0])
			.OnSelectionChanged(this, &FLevelOfDetailSettingsLayout::OnImportLOD)
		];

	int32 PlatformNumber = PlatformInfo::GetAllPlatformGroupNames().Num();

	LODSettingsCategory.AddCustomRow( LOCTEXT("MinLOD", "Minimum LOD") )
	.NameContent()
	[
		SNew(STextBlock)
		.Font( IDetailLayoutBuilder::GetDetailFont() )
		.Text(LOCTEXT("MinLOD", "Minimum LOD"))
	]
	.ValueContent()
	.MinDesiredWidth((float)(StaticMesh->MinLOD.PerPlatform.Num() + 1)*125.0f)
	.MaxDesiredWidth((float)(PlatformNumber + 1)*125.0f)
	[
		SNew(SPerPlatformPropertiesWidget)
		.IsEnabled(FLevelOfDetailSettingsLayout::GetLODCount() > 1)
		.OnGenerateWidget(this, &FLevelOfDetailSettingsLayout::GetMinLODWidget)
		.OnAddPlatform(this, &FLevelOfDetailSettingsLayout::AddMinLODPlatformOverride)
		.OnRemovePlatform(this, &FLevelOfDetailSettingsLayout::RemoveMinLODPlatformOverride)
		.PlatformOverrideNames(this, &FLevelOfDetailSettingsLayout::GetMinLODPlatformOverrideNames)
	];

	LODSettingsCategory.AddCustomRow(LOCTEXT("NumStreamedLODs", "Num Streamed LODs"))
	.NameContent()
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("NumStreamdLODs", "Num Streamed LODs"))
	]
	.ValueContent()
	.MinDesiredWidth((float)(StaticMesh->NumStreamedLODs.PerPlatform.Num() + 1)*125.0f)
	.MaxDesiredWidth((float)(PlatformNumber + 1)*125.0f)
	[
		SNew(SPerPlatformPropertiesWidget)
		.IsEnabled(FLevelOfDetailSettingsLayout::GetLODCount() > 1)
		.OnGenerateWidget(this, &FLevelOfDetailSettingsLayout::GetNumStreamedLODsWidget)
		.OnAddPlatform(this, &FLevelOfDetailSettingsLayout::AddNumStreamedLODsPlatformOverride)
		.OnRemovePlatform(this, &FLevelOfDetailSettingsLayout::RemoveNumStreamedLODsPlatformOverride)
		.PlatformOverrideNames(this, &FLevelOfDetailSettingsLayout::GetNumStreamedLODsPlatformOverrideNames)
	];

	// Add Number of LODs slider.
	const int32 MinAllowedLOD = 1;
	LODSettingsCategory.AddCustomRow( LOCTEXT("NumberOfLODs", "Number of LODs") )
	.NameContent()
	[
		SNew(STextBlock)
		.Font( IDetailLayoutBuilder::GetDetailFont() )
		.Text(LOCTEXT("NumberOfLODs", "Number of LODs"))
	]
	.ValueContent()
	[
		SNew(SSpinBox<int32>)
		.Font( IDetailLayoutBuilder::GetDetailFont() )
		.Value(this, &FLevelOfDetailSettingsLayout::GetLODCount)
		.OnValueChanged(this, &FLevelOfDetailSettingsLayout::OnLODCountChanged)
		.OnValueCommitted(this, &FLevelOfDetailSettingsLayout::OnLODCountCommitted)
		.MinValue(MinAllowedLOD)
		.MaxValue(MAX_STATIC_MESH_LODS)
		.ToolTipText(this, &FLevelOfDetailSettingsLayout::GetLODCountTooltip)
		.IsEnabled(IsAutoMeshReductionAvailable())
	];

	// Auto LOD distance check box.
	LODSettingsCategory.AddCustomRow( LOCTEXT("AutoComputeLOD", "Auto Compute LOD Distances") )
	.NameContent()
	[
		SNew(STextBlock)
		.Font( IDetailLayoutBuilder::GetDetailFont() )
		.Text(LOCTEXT("AutoComputeLOD", "Auto Compute LOD Distances"))
	]
	.ValueContent()
	[
		SNew(SCheckBox)
		.IsChecked(this, &FLevelOfDetailSettingsLayout::IsAutoLODChecked)
		.OnCheckStateChanged(this, &FLevelOfDetailSettingsLayout::OnAutoLODChanged)
	];

	LODSettingsCategory.AddCustomRow( LOCTEXT("ApplyChanges", "Apply Changes") )
	.ValueContent()
	.HAlign(HAlign_Left)
	[
		SNew(SButton)
		.OnClicked(this, &FLevelOfDetailSettingsLayout::OnApply)
		.IsEnabled(this, &FLevelOfDetailSettingsLayout::IsApplyNeeded)
		[
			SNew( STextBlock )
			.Text(LOCTEXT("ApplyChanges", "Apply Changes"))
			.Font( DetailBuilder.GetDetailFont() )
		]
	];

	AddLODLevelCategories( DetailBuilder );
}

bool FLevelOfDetailSettingsLayout::CanRemoveLOD(int32 LODIndex) const
{
	UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();

	if (StaticMesh != nullptr)
	{
		const int32 NumLODs = StaticMesh->GetNumLODs();
		
		// LOD0 should never be removed
		return (NumLODs > 1 && LODIndex > 0 && LODIndex < NumLODs);
	}

	return false;
}

FReply FLevelOfDetailSettingsLayout::OnRemoveLOD(int32 LODIndex)
{
	UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();

	if (StaticMesh != nullptr)
	{
		const int32 NumLODs = StaticMesh->GetNumLODs();

		if (NumLODs > 1 && LODIndex > 0 && LODIndex < NumLODs)
		{
			FText RemoveLODText = FText::Format( LOCTEXT("ConfirmRemoveLOD", "Are you sure you want to remove LOD {0} from {1}?"), LODIndex, FText::FromString(StaticMesh->GetName()) );

			if (FMessageDialog::Open(EAppMsgType::YesNo, RemoveLODText) == EAppReturnType::Yes)
			{
				FText TransactionDescription = FText::Format( LOCTEXT("OnRemoveLOD", "Staticmesh editor: Remove LOD {0}"), LODIndex);
				FScopedTransaction Transaction( TEXT(""), TransactionDescription, StaticMesh );

				StaticMesh->Modify();
				StaticMesh->RemoveSourceModel(LODIndex);
				--LODCount;
				StaticMesh->PostEditChange();

				StaticMeshEditor.RefreshTool();
			}
		}
	}

	return FReply::Handled();
}

void FLevelOfDetailSettingsLayout::AddLODLevelCategories( IDetailLayoutBuilder& DetailBuilder )
{
	UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();
	
	if( StaticMesh )
	{
		const int32 StaticMeshLODCount = StaticMesh->GetNumLODs();
		FStaticMeshRenderData* RenderData = StaticMesh->RenderData.Get();

		//Add the Materials array
		{
			FString CategoryName = FString(TEXT("StaticMeshMaterials"));

			IDetailCategoryBuilder& MaterialsCategory = DetailBuilder.EditCategory(*CategoryName, LOCTEXT("StaticMeshMaterialsLabel", "Material Slots"), ECategoryPriority::Important);
			MaterialsLayoutWidget = MakeShareable(new FMeshMaterialsLayout(StaticMeshEditor));
			TArray<FAssetData> AssetDataArray;
			AssetDataArray.Add(FAssetData(StaticMesh, false));
			MaterialsLayoutWidget->AddToCategory(MaterialsCategory, AssetDataArray);
		}

		int32 CurrentLodIndex = 0;
		
		if (StaticMeshEditor.GetStaticMeshComponent() != nullptr)
		{
			CurrentLodIndex = StaticMeshEditor.GetStaticMeshComponent()->ForcedLodModel;
		}
		LodCategories.Empty(StaticMeshLODCount);

		FString LODControllerCategoryName = FString(TEXT("LODCustomMode"));
		FText LODControllerString = LOCTEXT("LODCustomModeCategoryName", "LOD Picker");

		IDetailCategoryBuilder& LODCustomModeCategory = DetailBuilder.EditCategory( *LODControllerCategoryName, LODControllerString, ECategoryPriority::Important );
		LodCustomCategory = &LODCustomModeCategory;

		LODCustomModeCategory.AddCustomRow((LOCTEXT("LODCustomModeSelect", "Select LOD")))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("LODCustomModeSelectTitle", "LOD"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.IsEnabled(this, &FLevelOfDetailSettingsLayout::IsLodComboBoxEnabledForLodPicker)
		]
		.ValueContent()
		[
			OnGenerateLodComboBoxForLodPicker()
		];

		LODCustomModeCategory.AddCustomRow((LOCTEXT("LODCustomModeFirstRowName", "LODCustomMode")))
		.NameContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(this, &FLevelOfDetailSettingsLayout::GetLODCustomModeNameContent, (int32)INDEX_NONE)
			.ToolTipText(LOCTEXT("LODCustomModeFirstRowTooltip", "Custom Mode shows multiple LOD's properties at the same time for easier editing."))
		]
		.ValueContent()
		[
			SNew(SCheckBox)
			.IsChecked(this, &FLevelOfDetailSettingsLayout::IsLODCustomModeCheck, (int32)INDEX_NONE)
			.OnCheckStateChanged(this, &FLevelOfDetailSettingsLayout::SetLODCustomModeCheck, (int32)INDEX_NONE)
			.ToolTipText(LOCTEXT("LODCustomModeFirstRowTooltip", "Custom Mode shows multiple LOD's properties at the same time for easier editing."))
		];
		// Create information panel for each LOD level.
		for(int32 LODIndex = 0; LODIndex < StaticMeshLODCount; ++LODIndex)
		{
			//Show the viewport LOD at start
			bool IsViewportLOD = (CurrentLodIndex == 0 ? 0 : CurrentLodIndex - 1) == LODIndex;
			DetailDisplayLODs[LODIndex] = true; //enable all LOD in custom mode
			LODCustomModeCategory.AddCustomRow((LOCTEXT("LODCustomModeRowName", "LODCheckBoxRowName")), true)
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(this, &FLevelOfDetailSettingsLayout::GetLODCustomModeNameContent, LODIndex)
				.IsEnabled(this, &FLevelOfDetailSettingsLayout::IsLODCustomModeEnable, LODIndex)
			]
			.ValueContent()
			[
				SNew(SCheckBox)
				.IsChecked(this, &FLevelOfDetailSettingsLayout::IsLODCustomModeCheck, LODIndex)
				.OnCheckStateChanged(this, &FLevelOfDetailSettingsLayout::SetLODCustomModeCheck, LODIndex)
				.IsEnabled(this, &FLevelOfDetailSettingsLayout::IsLODCustomModeEnable, LODIndex)
			];

			if (IsAutoMeshReductionAvailable())
			{
				ReductionSettingsWidgets[LODIndex] = MakeShareable( new FMeshReductionSettingsLayout(AsShared(), LODIndex, StaticMesh->IsMeshDescriptionValid(LODIndex)));
			}

			if (LODIndex < StaticMesh->GetNumSourceModels())
			{
				const FStaticMeshSourceModel& SrcModel = StaticMesh->GetSourceModel(LODIndex);
				if (ReductionSettingsWidgets[LODIndex].IsValid())
				{
					ReductionSettingsWidgets[LODIndex]->UpdateSettings(SrcModel.ReductionSettings);
				}

				if (StaticMesh->IsMeshDescriptionValid(LODIndex))
				{
					BuildSettingsWidgets[LODIndex] = MakeShareable( new FMeshBuildSettingsLayout( AsShared(), LODIndex ) );
					BuildSettingsWidgets[LODIndex]->UpdateSettings(SrcModel.BuildSettings);
				}

				LODScreenSizes[LODIndex] = SrcModel.ScreenSize;
			}
			else if (LODIndex > 0)
			{
				if (ReductionSettingsWidgets[LODIndex].IsValid() && ReductionSettingsWidgets[LODIndex-1].IsValid())
				{
					FMeshReductionSettings ReductionSettings = ReductionSettingsWidgets[LODIndex-1]->GetSettings();
					// By default create LODs with half the triangles of the previous LOD.
					ReductionSettings.PercentTriangles *= 0.5f;
					ReductionSettingsWidgets[LODIndex]->UpdateSettings(ReductionSettings);
				}

				if(LODScreenSizes[LODIndex].Default >= LODScreenSizes[LODIndex-1].Default)
				{
					const float DefaultScreenSizeDifference = 0.01f;
					LODScreenSizes[LODIndex].Default = LODScreenSizes[LODIndex-1].Default - DefaultScreenSizeDifference;
				}
			}

			FString CategoryName = FString(TEXT("LOD"));
			CategoryName.AppendInt( LODIndex );

			FText LODLevelString = FText::FromString(FString(TEXT("LOD ")) + FString::FromInt(LODIndex) );
			bool bHasBeenSimplified = !StaticMesh->IsMeshDescriptionValid(LODIndex) || StaticMesh->IsReductionActive(LODIndex);
			FText GeneratedString = FText::FromString(bHasBeenSimplified ? TEXT("[generated]") : TEXT(""));

			IDetailCategoryBuilder& LODCategory = DetailBuilder.EditCategory( *CategoryName, LODLevelString, ECategoryPriority::Important );
			LodCategories.Add(&LODCategory);

			LODCategory.HeaderContent
			(
				SNew( SHorizontalBox )
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.Padding(FMargin(4.0f, 0.0f))
					[
						SNew(STextBlock)
						.Text(GeneratedString)
						.Font(IDetailLayoutBuilder::GetDetailFontItalic())
					]
				]
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew( SBox )
					.HAlign( HAlign_Right )
					[
						SNew( SHorizontalBox )
						+ SHorizontalBox::Slot()
						.Padding(FMargin(5.0f, 0.0f))
						.AutoWidth()
						[
							SNew(STextBlock)
							.Font(FEditorStyle::GetFontStyle("StaticMeshEditor.NormalFont"))
							.Text(this, &FLevelOfDetailSettingsLayout::GetLODScreenSizeTitle, LODIndex)
							.Visibility( LODIndex > 0 ? EVisibility::Visible : EVisibility::Collapsed )
						]
						+ SHorizontalBox::Slot()
						.Padding( FMargin( 5.0f, 0.0f ) )
						.AutoWidth()
						[
							SNew(STextBlock)
							.Font(FEditorStyle::GetFontStyle("StaticMeshEditor.NormalFont"))
							.Text( FText::Format( LOCTEXT("Triangles_MeshSimplification", "Triangles: {0}"), FText::AsNumber( StaticMeshEditor.GetNumTriangles(LODIndex) ) ) )
						]
						+ SHorizontalBox::Slot()
						.Padding( FMargin( 5.0f, 0.0f ) )
						.AutoWidth()
						[
							SNew(STextBlock)
							.Font(FEditorStyle::GetFontStyle("StaticMeshEditor.NormalFont"))
							.Text( FText::Format( LOCTEXT("Vertices_MeshSimplification", "Vertices: {0}"), FText::AsNumber( StaticMeshEditor.GetNumVertices(LODIndex) ) ) )
						]
					]
				]
			);
					
			SectionSettingsWidgets[ LODIndex ] = MakeShareable( new FMeshSectionSettingsLayout( StaticMeshEditor, LODIndex, LodCategories) );
			SectionSettingsWidgets[ LODIndex ]->AddToCategory( LODCategory );

			int32 PlatformNumber = PlatformInfo::GetAllPlatformGroupNames().Num();

			LODCategory.AddCustomRow(( LOCTEXT("ScreenSizeRow", "ScreenSize")))
			.NameContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("ScreenSizeName", "Screen Size"))
			]
			.ValueContent()
			.MinDesiredWidth(GetScreenSizeWidgetWidth(LODIndex))
			.MaxDesiredWidth((float)(PlatformNumber + 1)*125.0f)
			[
				SNew(SPerPlatformPropertiesWidget)
				.IsEnabled(this, &FLevelOfDetailSettingsLayout::CanChangeLODScreenSize)
				.OnGenerateWidget(this, &FLevelOfDetailSettingsLayout::GetLODScreenSizeWidget, LODIndex)
				.OnAddPlatform(this, &FLevelOfDetailSettingsLayout::AddLODScreenSizePlatformOverride, LODIndex)
				.OnRemovePlatform(this, &FLevelOfDetailSettingsLayout::RemoveLODScreenSizePlatformOverride, LODIndex)
				.PlatformOverrideNames(this, &FLevelOfDetailSettingsLayout::GetLODScreenSizePlatformOverrideNames, LODIndex)
			];

			if(LODIndex > 0 && StaticMesh->IsMeshDescriptionValid(LODIndex))
			{
				FString FileTypeFilter = TEXT("All files (*.*)|*.*");
				LODCategory.AddCustomRow(( LOCTEXT("SourceImporFilenameRow", "SourceImportFilename")))
				.NameContent()
				[
					SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text(LOCTEXT("SourceImportFilenameName", "Source Import Filename"))
				]
				.ValueContent()
					.MinDesiredWidth(125.0f)
					.MaxDesiredWidth(0.0f)
				[
					SNew(SFilePathPicker)
						.BrowseButtonImage(FEditorStyle::GetBrush("PropertyWindow.Button_Ellipsis"))
						.BrowseButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
						.BrowseButtonToolTip(LOCTEXT("FileButtonToolTipText", "Choose a source import file"))
						.BrowseDirectory(FEditorDirectories::Get().GetLastDirectory(ELastDirectory::GENERIC_OPEN))
						.BrowseTitle(LOCTEXT("PropertyEditorTitle", "Source import file picker..."))
						.FilePath(this, &FLevelOfDetailSettingsLayout::GetSourceImportFilename, LODIndex)
						.FileTypeFilter(FileTypeFilter)
						.OnPathPicked(this, &FLevelOfDetailSettingsLayout::SetSourceImportFilename, LODIndex)
				];
			}

			if (BuildSettingsWidgets[LODIndex].IsValid())
			{
				LODCategory.AddCustomBuilder( BuildSettingsWidgets[LODIndex].ToSharedRef() );
			}

			if( ReductionSettingsWidgets[LODIndex].IsValid() )
			{
				LODCategory.AddCustomBuilder( ReductionSettingsWidgets[LODIndex].ToSharedRef() );
			}

			if (LODIndex != 0)
			{
				LODCategory.AddCustomRow( LOCTEXT("RemoveLOD", "Remove LOD") )
				.ValueContent()
				.HAlign(HAlign_Left)
				[
					SNew(SButton)
					.OnClicked(this, &FLevelOfDetailSettingsLayout::OnRemoveLOD, LODIndex)
					.IsEnabled(this, &FLevelOfDetailSettingsLayout::CanRemoveLOD, LODIndex)
					.ToolTipText( LOCTEXT("RemoveLOD_ToolTip", "Removes this LOD from the Static Mesh") )
					[
						SNew(STextBlock)
						.Text( LOCTEXT("RemoveLOD", "Remove LOD") )
						.Font( DetailBuilder.GetDetailFont() )
					]
				];
			}
			LODCategory.SetCategoryVisibility(IsViewportLOD);
		}

		//Show the LOD custom category 
		if (StaticMeshLODCount > 1)
		{
			LODCustomModeCategory.SetCategoryVisibility(true);
			LODCustomModeCategory.SetShowAdvanced(false);
		}



		//Restore the state of the custom check LOD
		for (int32 DetailLODIndex = 0; DetailLODIndex < StaticMeshLODCount; ++DetailLODIndex)
		{
			int32 LodCheckValue = StaticMeshEditor.GetCustomData(CustomDataKey_LODVisibilityState + DetailLODIndex);
			if (LodCheckValue != INDEX_NONE)
			{
				DetailDisplayLODs[DetailLODIndex] = LodCheckValue > 0;
			}
		}

		//Restore the state of the custom LOD mode if its true (greater then 0)
		bool bCustomLodEditMode = StaticMeshEditor.GetCustomData(CustomDataKey_LODEditMode) > 0;
		if (bCustomLodEditMode)
		{
			for (int32 DetailLODIndex = 0; DetailLODIndex < StaticMeshLODCount; ++DetailLODIndex)
			{
				if (!LodCategories.IsValidIndex(DetailLODIndex))
				{
					break;
				}
				LodCategories[DetailLODIndex]->SetCategoryVisibility(DetailDisplayLODs[DetailLODIndex]);
			}
		}

		if (LodCustomCategory != nullptr)
		{
			LodCustomCategory->SetShowAdvanced(bCustomLodEditMode);
		}
	}
}


FLevelOfDetailSettingsLayout::~FLevelOfDetailSettingsLayout()
{
}

FString FLevelOfDetailSettingsLayout::GetSourceImportFilename(int32 LODIndex) const
{
	UStaticMesh* Mesh = StaticMeshEditor.GetStaticMesh();
	if (!Mesh->IsSourceModelValid(LODIndex) || Mesh->GetSourceModel(LODIndex).SourceImportFilename.IsEmpty())
	{
		return FString(TEXT(""));
	}
	return UAssetImportData::ResolveImportFilename(Mesh->GetSourceModel(LODIndex).SourceImportFilename, nullptr);
}

void FLevelOfDetailSettingsLayout::SetSourceImportFilename(const FString& SourceFileName, int32 LODIndex) const
{
	UStaticMesh* Mesh = StaticMeshEditor.GetStaticMesh();
	if (!Mesh->IsSourceModelValid(LODIndex))
	{
		return;
	}
	if (SourceFileName.IsEmpty())
	{
		Mesh->GetSourceModel(LODIndex).SourceImportFilename = SourceFileName;
	}
	else
	{
		Mesh->GetSourceModel(LODIndex).SourceImportFilename = UAssetImportData::SanitizeImportFilename(SourceFileName, nullptr);
	}
	Mesh->Modify();
}

int32 FLevelOfDetailSettingsLayout::GetLODCount() const
{
	return LODCount;
}

float FLevelOfDetailSettingsLayout::GetLODScreenSize(FName PlatformGroupName, int32 LODIndex) const
{
	check(LODIndex < MAX_STATIC_MESH_LODS);
	UStaticMesh* Mesh = StaticMeshEditor.GetStaticMesh();
	const FPerPlatformFloat& LODScreenSize = LODScreenSizes[FMath::Clamp(LODIndex, 0, MAX_STATIC_MESH_LODS - 1)];
	float ScreenSize = LODScreenSize.Default;
	if (PlatformGroupName != NAME_None)
	{
		const float* PlatformScreenSize = LODScreenSize.PerPlatform.Find(PlatformGroupName);
		if (PlatformScreenSize != nullptr)
		{
			ScreenSize = *PlatformScreenSize;
		}
	}

	if(Mesh->bAutoComputeLODScreenSize)
	{
		ScreenSize = Mesh->RenderData->ScreenSize[LODIndex].Default;
	}
	else if(Mesh->IsSourceModelValid(LODIndex))
	{
		ScreenSize = Mesh->GetSourceModel(LODIndex).ScreenSize.Default;
		const float* PlatformScreenSize = Mesh->GetSourceModel(LODIndex).ScreenSize.PerPlatform.Find(PlatformGroupName);
		if (PlatformScreenSize != nullptr)
		{
			ScreenSize = *PlatformScreenSize;
		}
	}
	return ScreenSize;
}

FText FLevelOfDetailSettingsLayout::GetLODScreenSizeTitle( int32 LODIndex ) const
{
	return FText::Format( LOCTEXT("ScreenSize_MeshSimplification", "Screen Size: {0}"), FText::AsNumber(GetLODScreenSize(NAME_None, LODIndex)));
}

bool FLevelOfDetailSettingsLayout::CanChangeLODScreenSize() const
{
	return !IsAutoLODEnabled();
}

TSharedRef<SWidget> FLevelOfDetailSettingsLayout::GetLODScreenSizeWidget(FName PlatformGroupName, int32 LODIndex) const
{
	return SNew(SSpinBox<float>)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.MinDesiredWidth(60.0f)
		.MinValue(0.0f)
		.MaxValue(WORLD_MAX)
		.SliderExponent(2.0f)
		.Value(this, &FLevelOfDetailSettingsLayout::GetLODScreenSize, PlatformGroupName, LODIndex)
		.OnValueChanged(const_cast<FLevelOfDetailSettingsLayout*>(this), &FLevelOfDetailSettingsLayout::OnLODScreenSizeChanged, PlatformGroupName, LODIndex)
		.OnValueCommitted(const_cast<FLevelOfDetailSettingsLayout*>(this), &FLevelOfDetailSettingsLayout::OnLODScreenSizeCommitted, PlatformGroupName, LODIndex)
		.IsEnabled(this, &FLevelOfDetailSettingsLayout::CanChangeLODScreenSize);
}

TArray<FName> FLevelOfDetailSettingsLayout::GetLODScreenSizePlatformOverrideNames(int32 LODIndex) const
{
	TArray<FName> KeyArray;
	LODScreenSizes[LODIndex].PerPlatform.GenerateKeyArray(KeyArray);
	KeyArray.Sort(FNameLexicalLess());
	return KeyArray;
}

float FLevelOfDetailSettingsLayout::GetScreenSizeWidgetWidth(int32 LODIndex) const
{
	return (float)(LODScreenSizes[LODIndex].PerPlatform.Num() + 1) * 125.f;
}

bool FLevelOfDetailSettingsLayout::AddLODScreenSizePlatformOverride(FName PlatformGroupName, int32 LODIndex)
{
	FScopedTransaction Transaction(LOCTEXT("AddLODScreenSizePlatformOverride", "Add LOD Screen Size Platform Override"));
	UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();
	if (LODScreenSizes[LODIndex].PerPlatform.Find(PlatformGroupName) == nullptr)
	{
		if(!StaticMesh->bAutoComputeLODScreenSize && StaticMesh->IsSourceModelValid(LODIndex))
		{
			StaticMesh->Modify();
			float Value = StaticMesh->GetSourceModel(LODIndex).ScreenSize.Default;
			StaticMesh->GetSourceModel(LODIndex).ScreenSize.PerPlatform.Add(PlatformGroupName, Value);
			OnLODScreenSizeChanged(Value, PlatformGroupName, LODIndex);
			return true;
		}
	}
	return false;
}

bool FLevelOfDetailSettingsLayout::RemoveLODScreenSizePlatformOverride(FName PlatformGroupName, int32 LODIndex)
{
	FScopedTransaction Transaction(LOCTEXT("RemoveLODScreenSizePlatformOverride", "Remove LOD Screen Size Platform Override"));
	UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();
	if (!StaticMesh->bAutoComputeLODScreenSize && StaticMesh->IsSourceModelValid(LODIndex))
	{
		StaticMesh->Modify();
		if (StaticMesh->GetSourceModel(LODIndex).ScreenSize.PerPlatform.Remove(PlatformGroupName) != 0)
		{
			OnLODScreenSizeChanged(StaticMesh->GetSourceModel(LODIndex).ScreenSize.Default, PlatformGroupName, LODIndex);
			return true;
		}
	}
	return false;
}

void FLevelOfDetailSettingsLayout::OnLODScreenSizeChanged( float NewValue, FName PlatformGroupName, int32 LODIndex )
{
	check(LODIndex < MAX_STATIC_MESH_LODS);
	UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();
	if (!StaticMesh->bAutoComputeLODScreenSize)
	{
		// First propagate any changes from the source models to our local scratch.
		for (int32 i = 0; i < StaticMesh->GetNumSourceModels(); ++i)
		{
			LODScreenSizes[i] = StaticMesh->GetSourceModel(i).ScreenSize;
		}

		// Update Display factors for further LODs
		const float MinimumDifferenceInScreenSize = KINDA_SMALL_NUMBER;
		
		if (PlatformGroupName == NAME_None)
		{
			LODScreenSizes[LODIndex].Default = NewValue;

			// Make sure we aren't trying to overlap or have more than one LOD for a value
			for (int32 i = 1; i < MAX_STATIC_MESH_LODS; ++i)
			{
				float MaxValue = FMath::Max(LODScreenSizes[i-1].Default - MinimumDifferenceInScreenSize, 0.0f);
				LODScreenSizes[i].Default = FMath::Min(LODScreenSizes[i].Default, MaxValue);
			}
		}
		else
		{
			// Per-platform overrides don't have any restrictions
			float* PlatformScreenSize = LODScreenSizes[LODIndex].PerPlatform.Find(PlatformGroupName);
			if (PlatformScreenSize != nullptr)
			{
				*PlatformScreenSize = NewValue;
			}
		}

		// Push changes immediately.
		for (int32 i = 0; i < MAX_STATIC_MESH_LODS; ++i)
		{
			if (StaticMesh->IsSourceModelValid(i))
			{
				StaticMesh->GetSourceModel(i).ScreenSize = LODScreenSizes[i];
			}
			if (StaticMesh->RenderData
				&& StaticMesh->RenderData->LODResources.IsValidIndex(i))
			{
				StaticMesh->RenderData->ScreenSize[i] = LODScreenSizes[i];
			}
		}

		// Reregister static mesh components using this mesh.
		{
			FStaticMeshComponentRecreateRenderStateContext ReregisterContext(StaticMesh,false);
			StaticMesh->Modify();
		}

		StaticMeshEditor.RefreshViewport();
	}
}

void FLevelOfDetailSettingsLayout::OnLODScreenSizeCommitted( float NewValue, ETextCommit::Type CommitType, FName PlatformGroupName, int32 LODIndex )
{
	OnLODScreenSizeChanged(NewValue, PlatformGroupName, LODIndex);
}

void FLevelOfDetailSettingsLayout::UpdateLODNames()
{
	LODNames.Empty();
	LODNames.Add( MakeShareable( new FString( LOCTEXT("BaseLOD", "LOD 0").ToString() ) ) );
	for(int32 LODLevelID = 1; LODLevelID < LODCount; ++LODLevelID)
	{
		LODNames.Add( MakeShareable( new FString( FText::Format( NSLOCTEXT("LODSettingsLayout", "LODLevel_Reimport", "Reimport LOD Level {0}"), FText::AsNumber( LODLevelID ) ).ToString() ) ) );
	}
	LODNames.Add( MakeShareable( new FString( FText::Format( NSLOCTEXT("LODSettingsLayout", "LODLevel_Import", "Import LOD Level {0}"), FText::AsNumber( LODCount ) ).ToString() ) ) );
}

void FLevelOfDetailSettingsLayout::OnBuildSettingsExpanded(bool bIsExpanded, int32 LODIndex)
{
	check(LODIndex >= 0 && LODIndex < MAX_STATIC_MESH_LODS);
	bBuildSettingsExpanded[LODIndex] = bIsExpanded;
}

void FLevelOfDetailSettingsLayout::OnReductionSettingsExpanded(bool bIsExpanded, int32 LODIndex)
{
	check(LODIndex >= 0 && LODIndex < MAX_STATIC_MESH_LODS);
	bReductionSettingsExpanded[LODIndex] = bIsExpanded;
}

void FLevelOfDetailSettingsLayout::OnSectionSettingsExpanded(bool bIsExpanded, int32 LODIndex)
{
	check(LODIndex >= 0 && LODIndex < MAX_STATIC_MESH_LODS);
	bSectionSettingsExpanded[LODIndex] = bIsExpanded;
}

void FLevelOfDetailSettingsLayout::OnLODGroupChanged(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo)
{
	UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();
	check(StaticMesh);
	int32 GroupIndex = LODGroupOptions.Find(NewValue);
	FName NewGroup = LODGroupNames[GroupIndex];
	if (StaticMesh->LODGroup != NewGroup)
	{
		if (NewGroup != NAME_None)
		{
			EAppReturnType::Type DialogResult = FMessageDialog::Open(
				EAppMsgType::YesNo,
				FText::Format(LOCTEXT("ApplyDefaultLODSettings", "Changing LOD group will overwrite the current settings with the defaults from LOD group '{0}'. Do you wish to continue?"), FText::FromString(**NewValue))
			);
			if (DialogResult == EAppReturnType::Yes)
			{
				StaticMesh->SetLODGroup(NewGroup);
				// update the internal count
				LODCount = StaticMesh->GetNumSourceModels();
				StaticMeshEditor.RefreshTool();
			}
			else
			{
				// Overriding the selection; ensure that the widget correctly reflects the property value
				int32 Index = LODGroupNames.Find(StaticMesh->LODGroup);
				check(Index != INDEX_NONE);
				LODGroupComboBox->SetSelectedItem(LODGroupOptions[Index]);
			}
		}
		else
		{
			//Setting to none just change the LODGroup to None, the LOD count will not change
			StaticMesh->SetLODGroup(NewGroup);
			StaticMeshEditor.RefreshTool();
		}
	}
}

bool FLevelOfDetailSettingsLayout::IsAutoLODEnabled() const
{
	UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();
	check(StaticMesh);
	return StaticMesh->bAutoComputeLODScreenSize;
}

ECheckBoxState FLevelOfDetailSettingsLayout::IsAutoLODChecked() const
{
	return IsAutoLODEnabled() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FLevelOfDetailSettingsLayout::OnAutoLODChanged(ECheckBoxState NewState)
{
	UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();
	check(StaticMesh);
	StaticMesh->Modify();
	StaticMesh->bAutoComputeLODScreenSize = (NewState == ECheckBoxState::Checked) ? true : false;
	if (!StaticMesh->bAutoComputeLODScreenSize)
	{
		if (StaticMesh->GetNumSourceModels() > 0)
		{
			StaticMesh->GetSourceModel(0).ScreenSize.Default = 1.0f;
		}
		for (int32 LODIndex = 1; LODIndex < StaticMesh->GetNumSourceModels(); ++LODIndex)
		{
			StaticMesh->GetSourceModel(LODIndex).ScreenSize.Default = StaticMesh->RenderData->ScreenSize[LODIndex].Default;
		}
	}
	StaticMesh->PostEditChange();
	StaticMeshEditor.RefreshTool();
}

void FLevelOfDetailSettingsLayout::OnImportLOD(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo)
{
	int32 LODIndex = 0;
	if( LODNames.Find(NewValue, LODIndex) && LODIndex > 0 )
	{
		UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();
		check(StaticMesh);

		if (StaticMesh->LODGroup != NAME_None && StaticMesh->IsSourceModelValid(LODIndex))
		{
			// Cache derived data for the running platform.
			ITargetPlatformManagerModule& TargetPlatformManager = GetTargetPlatformManagerRef();
			ITargetPlatform* RunningPlatform = TargetPlatformManager.GetRunningTargetPlatform();
			check(RunningPlatform);
			const FStaticMeshLODSettings& LODSettings = RunningPlatform->GetStaticMeshLODSettings();
			const FStaticMeshLODGroup& LODGroup = LODSettings.GetLODGroup(StaticMesh->LODGroup);
			if (LODIndex < LODGroup.GetDefaultNumLODs())
			{
				//Ask the user to change the LODGroup to None, if the user cancel do not re-import the LOD
				//We can have a LODGroup with custom LOD only if custom LOD are after the generated LODGroup LODs
				EAppReturnType::Type ReturnResult = FMessageDialog::Open(EAppMsgType::OkCancel, EAppReturnType::Ok, FText::Format(LOCTEXT("LODImport_LODGroupVersusCustomLODConflict", "This static mesh uses the LOD group \"{0}\" which generates the LOD {1}. To import a custom LOD at index {1}, the LODGroup must be cleared to \"None\"."), FText::FromName(StaticMesh->LODGroup), FText::AsNumber(LODIndex)));
				if (ReturnResult == EAppReturnType::Cancel)
				{
					StaticMeshEditor.RefreshTool();
					return;
				}
				//Clear the LODGroup
				StaticMesh->SetLODGroup(NAME_None, false);
				//Make sure the importdata point on LOD Group None
				UFbxStaticMeshImportData* ImportData = Cast<UFbxStaticMeshImportData>(StaticMesh->AssetImportData);
				if (ImportData != nullptr)
				{
					ImportData->StaticMeshLODGroup = NAME_None;
				}
			}
		}

		//Are we a new imported LOD, we want to set some value for new imported LOD.
		//This boolean prevent changing the value when the LOD is reimport
		bool bImportCustomLOD = (LODIndex >= StaticMesh->GetNumSourceModels());

		bool bResult = FbxMeshUtils::ImportMeshLODDialog(StaticMesh, LODIndex);

		if (bImportCustomLOD && bResult && StaticMesh->IsSourceModelValid(LODIndex))
		{
			//Custom LOD should reduce base on them self when they get imported.
			StaticMesh->GetSourceModel(LODIndex).ReductionSettings.BaseLODModel = LODIndex;
		}
		
		StaticMesh->PostEditChange();
		StaticMeshEditor.RefreshTool();
	}

}

bool FLevelOfDetailSettingsLayout::IsApplyNeeded() const
{
	UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();
	check(StaticMesh);

	if (StaticMesh->GetNumSourceModels() != LODCount)
	{
		return true;
	}

	for (int32 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
	{
		FStaticMeshSourceModel& SrcModel = StaticMesh->GetSourceModel(LODIndex);
		if (BuildSettingsWidgets[LODIndex].IsValid()
			&& SrcModel.BuildSettings != BuildSettingsWidgets[LODIndex]->GetSettings())
		{
			return true;
		}
		if (ReductionSettingsWidgets[LODIndex].IsValid()
			&& SrcModel.ReductionSettings != ReductionSettingsWidgets[LODIndex]->GetSettings())
		{
			return true;
		}
	}

	return false;
}

void FLevelOfDetailSettingsLayout::ApplyChanges()
{
	UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();
	check(StaticMesh);

	// Calling Begin and EndSlowTask are rather dangerous because they tick
	// Slate. Call them here and flush rendering commands to be sure!.

	FFormatNamedArguments Args;
	Args.Add( TEXT("StaticMeshName"), FText::FromString( StaticMesh->GetName() ) );
	GWarn->BeginSlowTask( FText::Format( LOCTEXT("ApplyLODChanges", "Applying changes to {StaticMeshName}..."), Args ), true );
	FlushRenderingCommands();

	StaticMesh->Modify();
	StaticMesh->SetNumSourceModels(LODCount);

	for (int32 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
	{
		FStaticMeshSourceModel& SrcModel = StaticMesh->GetSourceModel(LODIndex);
		if (BuildSettingsWidgets[LODIndex].IsValid())
		{
			SrcModel.BuildSettings = BuildSettingsWidgets[LODIndex]->GetSettings();
		}
		if (ReductionSettingsWidgets[LODIndex].IsValid())
		{
			SrcModel.ReductionSettings = ReductionSettingsWidgets[LODIndex]->GetSettings();
		}

		if (LODIndex == 0)
		{
			SrcModel.ScreenSize.Default = 1.0f;
		}
		else
		{
			SrcModel.ScreenSize = LODScreenSizes[LODIndex];
			FStaticMeshSourceModel& PrevModel = StaticMesh->GetSourceModel(LODIndex-1);
			if(SrcModel.ScreenSize.Default >= PrevModel.ScreenSize.Default)
			{
				const float DefaultScreenSizeDifference = 0.01f;
				LODScreenSizes[LODIndex].Default = LODScreenSizes[LODIndex-1].Default - DefaultScreenSizeDifference;

				// Make sure there are no incorrectly overlapping values
				SrcModel.ScreenSize.Default = 1.0f - 0.01f * LODIndex;
			}
		}
	}
	StaticMesh->PostEditChange();

	GWarn->EndSlowTask();

	StaticMeshEditor.RefreshTool();
}

bool FLevelOfDetailSettingsLayout::PreviewLODRequiresAdjacencyInformation(int32 LODIndex)
{
	UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();
	check(StaticMesh);
	return StaticMesh->FixLODRequiresAdjacencyInformation(LODIndex, true, false, nullptr);
}

FReply FLevelOfDetailSettingsLayout::OnApply()
{
	ApplyChanges();
	return FReply::Handled();
}

void FLevelOfDetailSettingsLayout::OnLODCountChanged(int32 NewValue)
{
	LODCount = FMath::Clamp<int32>(NewValue, 1, MAX_STATIC_MESH_LODS);

	UpdateLODNames();
}

void FLevelOfDetailSettingsLayout::OnLODCountCommitted(int32 InValue, ETextCommit::Type CommitInfo)
{
	OnLODCountChanged(InValue);
}

FText FLevelOfDetailSettingsLayout::GetLODCountTooltip() const
{
	if(IsAutoMeshReductionAvailable())
	{
		return LOCTEXT("LODCountTooltip", "The number of LODs for this static mesh. If auto mesh reduction is available, setting this number will determine the number of LOD levels to auto generate.");
	}

	return LOCTEXT("LODCountTooltip_Disabled", "Auto mesh reduction is unavailable! Please provide a mesh reduction interface such as Simplygon to use this feature or manually import LOD levels.");
}

int32 FLevelOfDetailSettingsLayout::GetMinLOD(FName Platform) const
{
	UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();
	check(StaticMesh);

	int32* ValuePtr = (Platform == NAME_None) ? nullptr : StaticMesh->MinLOD.PerPlatform.Find(Platform);
	return (ValuePtr != nullptr) ? *ValuePtr : StaticMesh->MinLOD.Default;
}

void FLevelOfDetailSettingsLayout::OnMinLODChanged(int32 NewValue, FName Platform)
{
	UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();
	check(StaticMesh);

	{
		FStaticMeshComponentRecreateRenderStateContext ReregisterContext(StaticMesh, false);
		NewValue = FMath::Clamp<int32>(NewValue, 0, MAX_STATIC_MESH_LODS - 1);
		if (Platform == NAME_None)
		{
			StaticMesh->MinLOD.Default = NewValue;
		}
		else
		{
			int32* ValuePtr = StaticMesh->MinLOD.PerPlatform.Find(Platform);
			if (ValuePtr != nullptr)
			{
				*ValuePtr = NewValue;
			}
		}
		StaticMesh->Modify();
	}
	StaticMeshEditor.RefreshViewport();
}

void FLevelOfDetailSettingsLayout::OnMinLODCommitted(int32 InValue, ETextCommit::Type CommitInfo, FName Platform)
{
	OnMinLODChanged(InValue, Platform);
}

FText FLevelOfDetailSettingsLayout::GetMinLODTooltip() const
{
	return LOCTEXT("MinLODTooltip", "The minimum LOD to use for rendering.  This can be overridden in components.");
}

TSharedRef<SWidget> FLevelOfDetailSettingsLayout::GetMinLODWidget(FName PlatformGroupName) const
{
	return SNew(SSpinBox<int32>)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Value(this, &FLevelOfDetailSettingsLayout::GetMinLOD, PlatformGroupName)
		.OnValueChanged(const_cast<FLevelOfDetailSettingsLayout*>(this), &FLevelOfDetailSettingsLayout::OnMinLODChanged, PlatformGroupName)
		.OnValueCommitted(const_cast<FLevelOfDetailSettingsLayout*>(this), &FLevelOfDetailSettingsLayout::OnMinLODCommitted, PlatformGroupName)
		.MinValue(0)
		.MaxValue(MAX_STATIC_MESH_LODS)
		.ToolTipText(this, &FLevelOfDetailSettingsLayout::GetMinLODTooltip)
		.IsEnabled(FLevelOfDetailSettingsLayout::GetLODCount() > 1);
}

bool FLevelOfDetailSettingsLayout::AddMinLODPlatformOverride(FName PlatformGroupName)
{
	FScopedTransaction Transaction(LOCTEXT("AddMinLODPlatformOverride", "Add Min LOD Platform Override"));
	UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();
	check(StaticMesh);
	StaticMesh->Modify();
	if (StaticMesh->MinLOD.PerPlatform.Find(PlatformGroupName) == nullptr)
	{
		float Value = StaticMesh->MinLOD.Default;
		StaticMesh->MinLOD.PerPlatform.Add(PlatformGroupName, Value);
		OnMinLODChanged(Value, PlatformGroupName);
		return true;
	}
	return false;
}

bool FLevelOfDetailSettingsLayout::RemoveMinLODPlatformOverride(FName PlatformGroupName)
{
	FScopedTransaction Transaction(LOCTEXT("RemoveMinLODPlatformOverride", "Remove Min LOD Platform Override"));
	UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();
	check(StaticMesh);
	StaticMesh->Modify();
	if (StaticMesh->MinLOD.PerPlatform.Remove(PlatformGroupName) != 0)
	{
		OnMinLODChanged(StaticMesh->MinLOD.Default, PlatformGroupName);
		return true;
	}
	return false;
}

TArray<FName> FLevelOfDetailSettingsLayout::GetMinLODPlatformOverrideNames() const
{
	UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();
	check(StaticMesh);
	TArray<FName> KeyArray;
	StaticMesh->MinLOD.PerPlatform.GenerateKeyArray(KeyArray);
	KeyArray.Sort(FNameLexicalLess());
	return KeyArray;
}

/** @return - whether value was different */
static bool UpdateStaticMeshNumStreamedLODsHelper(UStaticMesh* StaticMesh, int32 NewValue, FName Platform)
{
	bool bWasDifferent = false;
	StaticMesh->Modify();
	{
		FStaticMeshComponentRecreateRenderStateContext ReregisterContext(StaticMesh, false);
		NewValue = FMath::Clamp<int32>(NewValue, -1, MAX_STATIC_MESH_LODS);
		if (Platform == NAME_None)
		{
			bWasDifferent = StaticMesh->NumStreamedLODs.Default != NewValue;
			StaticMesh->NumStreamedLODs.Default = NewValue;
		}
		else
		{
			int32* ValuePtr = StaticMesh->NumStreamedLODs.PerPlatform.Find(Platform);
			if (ValuePtr != nullptr)
			{
				bWasDifferent = *ValuePtr != NewValue;
				*ValuePtr = NewValue;
			}
		}
	}
	return bWasDifferent;
}

void FLevelOfDetailSettingsLayout::OnNumStreamedLODsChanged(int32 NewValue, FName Platform)
{
	UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();
	check(StaticMesh);
	UpdateStaticMeshNumStreamedLODsHelper(StaticMesh, NewValue, Platform);
	StaticMeshEditor.RefreshViewport();
}

void FLevelOfDetailSettingsLayout::OnNumStreamedLODsCommitted(int32 InValue, ETextCommit::Type CommitInfo, FName Platform)
{
	UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();
	check(StaticMesh);
	if (UpdateStaticMeshNumStreamedLODsHelper(StaticMesh, InValue, Platform))
	{
		if (IStreamingManager::Get().IsRenderAssetStreamingEnabled(EStreamableRenderAssetType::StaticMesh))
		{
			// Make sure FStaticMeshRenderData::CurrentFirstLODIdx is not accessed on other threads
			IStreamingManager::Get().GetRenderAssetStreamingManager().BlockTillAllRequestsFinished();
		}
		// Recache derived data and relink streaming
		ApplyChanges();
	}
	StaticMeshEditor.RefreshViewport();
}

int32 FLevelOfDetailSettingsLayout::GetNumStreamedLODs(FName Platform) const
{
	UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();
	check(StaticMesh);

	int32* ValuePtr = (Platform == NAME_None) ? nullptr : StaticMesh->NumStreamedLODs.PerPlatform.Find(Platform);
	return (ValuePtr != nullptr) ? *ValuePtr : StaticMesh->NumStreamedLODs.Default;
}

TSharedRef<SWidget> FLevelOfDetailSettingsLayout::GetNumStreamedLODsWidget(FName PlatformGroupName) const
{
	return SNew(SSpinBox<int32>)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Value(this, &FLevelOfDetailSettingsLayout::GetNumStreamedLODs, PlatformGroupName)
		.OnValueChanged(const_cast<FLevelOfDetailSettingsLayout*>(this), &FLevelOfDetailSettingsLayout::OnNumStreamedLODsChanged, PlatformGroupName)
		.OnValueCommitted(const_cast<FLevelOfDetailSettingsLayout*>(this), &FLevelOfDetailSettingsLayout::OnNumStreamedLODsCommitted, PlatformGroupName)
		.MinValue(-1)
		.MaxValue(MAX_STATIC_MESH_LODS)
		.ToolTipText(this, &FLevelOfDetailSettingsLayout::GetNumStreamedLODsTooltip)
		.IsEnabled(FLevelOfDetailSettingsLayout::GetLODCount() > 1);
}

bool FLevelOfDetailSettingsLayout::AddNumStreamedLODsPlatformOverride(FName PlatformGroupName)
{
	FScopedTransaction Transaction(LOCTEXT("AddNumStreamedLODsPlatformOverride", "Add NumStreamdLODs Platform Override"));
	UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();
	check(StaticMesh);
	StaticMesh->Modify();
	if (StaticMesh->NumStreamedLODs.PerPlatform.Find(PlatformGroupName) == nullptr)
	{
		float Value = StaticMesh->NumStreamedLODs.Default;
		StaticMesh->NumStreamedLODs.PerPlatform.Add(PlatformGroupName, Value);
		OnNumStreamedLODsChanged(Value, PlatformGroupName);
		return true;
	}
	return false;
}

bool FLevelOfDetailSettingsLayout::RemoveNumStreamedLODsPlatformOverride(FName PlatformGroupName)
{
	FScopedTransaction Transaction(LOCTEXT("RemoveNumStreamedLODsPlatformOverride", "Remove NumStreamedLODs Platform Override"));
	UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();
	check(StaticMesh);
	StaticMesh->Modify();
	if (StaticMesh->NumStreamedLODs.PerPlatform.Remove(PlatformGroupName) != 0)
	{
		OnNumStreamedLODsChanged(StaticMesh->NumStreamedLODs.Default, PlatformGroupName);
		return true;
	}
	return false;
}

TArray<FName> FLevelOfDetailSettingsLayout::GetNumStreamedLODsPlatformOverrideNames() const
{
	UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();
	check(StaticMesh);
	TArray<FName> KeyArray;
	StaticMesh->NumStreamedLODs.PerPlatform.GenerateKeyArray(KeyArray);
	KeyArray.Sort(FNameLexicalLess());
	return KeyArray;
}

FText FLevelOfDetailSettingsLayout::GetNumStreamedLODsTooltip() const
{
	return LOCTEXT("NumStreamedLODsTooltip", "If non-negative, the number of LODs that can be streamed. Only has effect if mesh LOD streaming is enabled on the target platform.");
}

FText FLevelOfDetailSettingsLayout::GetLODCustomModeNameContent(int32 LODIndex) const
{
	int32 CurrentLodIndex = 0;
	if (StaticMeshEditor.GetStaticMeshComponent() != nullptr)
	{
		CurrentLodIndex = StaticMeshEditor.GetStaticMeshComponent()->ForcedLodModel;
	}
	int32 RealCurrentLODIndex = (CurrentLodIndex == 0 ? 0 : CurrentLodIndex - 1);
	if (LODIndex == INDEX_NONE)
	{
		return LOCTEXT("GetLODCustomModeNameContent", "Custom");
	}
	return FText::Format(LOCTEXT("GetLODModeNameContent", "LOD{0}"), LODIndex);
}

ECheckBoxState FLevelOfDetailSettingsLayout::IsLODCustomModeCheck(int32 LODIndex) const
{
	int32 CurrentLodIndex = 0;
	if (StaticMeshEditor.GetStaticMeshComponent() != nullptr)
	{
		CurrentLodIndex = StaticMeshEditor.GetStaticMeshComponent()->ForcedLodModel;
	}
	if (LODIndex == INDEX_NONE)
	{
		return StaticMeshEditor.GetCustomData(CustomDataKey_LODEditMode) > 0 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	}
	return DetailDisplayLODs[LODIndex] ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FLevelOfDetailSettingsLayout::SetLODCustomModeCheck(ECheckBoxState NewState, int32 LODIndex)
{
	int32 CurrentLodIndex = 0;
	if (StaticMeshEditor.GetStaticMeshComponent() != nullptr)
	{
		CurrentLodIndex = StaticMeshEditor.GetStaticMeshComponent()->ForcedLodModel;
	}
	if (LODIndex == INDEX_NONE)
	{
		if (NewState == ECheckBoxState::Unchecked)
		{
			StaticMeshEditor.SetCustomData(CustomDataKey_LODEditMode, 0);
			SectionSettingsWidgets[0]->SetCurrentLOD(CurrentLodIndex);
			for (int32 DetailLODIndex = 0; DetailLODIndex < MAX_STATIC_MESH_LODS; ++DetailLODIndex)
			{
				if (!LodCategories.IsValidIndex(DetailLODIndex))
				{
					break;
				}
				LodCategories[DetailLODIndex]->SetCategoryVisibility(DetailLODIndex == (CurrentLodIndex == 0 ? 0 : CurrentLodIndex-1));
			}
		}
		else
		{
			StaticMeshEditor.SetCustomData(CustomDataKey_LODEditMode, 1);
			SectionSettingsWidgets[0]->SetCurrentLOD(0);
		}
	}
	else if(StaticMeshEditor.GetCustomData(CustomDataKey_LODEditMode) > 0)
	{
		DetailDisplayLODs[LODIndex] = NewState == ECheckBoxState::Checked;
		StaticMeshEditor.SetCustomData(CustomDataKey_LODVisibilityState + LODIndex, DetailDisplayLODs[LODIndex] ? 1 : 0);
	}

	if (StaticMeshEditor.GetCustomData(CustomDataKey_LODEditMode) > 0)
	{
		for (int32 DetailLODIndex = 0; DetailLODIndex < MAX_STATIC_MESH_LODS; ++DetailLODIndex)
		{
			if (!LodCategories.IsValidIndex(DetailLODIndex))
			{
				break;
			}
			LodCategories[DetailLODIndex]->SetCategoryVisibility(DetailDisplayLODs[DetailLODIndex]);
		}
	}

	if (LodCustomCategory != nullptr)
	{
		LodCustomCategory->SetShowAdvanced(StaticMeshEditor.GetCustomData(CustomDataKey_LODEditMode) > 0);
	}
}

bool FLevelOfDetailSettingsLayout::IsLODCustomModeEnable(int32 LODIndex) const
{
	if (LODIndex == INDEX_NONE)
	{
		// Custom checkbox is always enable
		return true;
	}
	return StaticMeshEditor.GetCustomData(CustomDataKey_LODEditMode) > 0;
}

TSharedRef<SWidget> FLevelOfDetailSettingsLayout::OnGenerateLodComboBoxForLodPicker()
{
	return SNew(SComboButton)
		//.Visibility(this, &FLevelOfDetailSettingsLayout::LodComboBoxVisibilityForLodPicker)
		.IsEnabled(this, &FLevelOfDetailSettingsLayout::IsLodComboBoxEnabledForLodPicker)
		.OnGetMenuContent(this, &FLevelOfDetailSettingsLayout::OnGenerateLodMenuForLodPicker)
		.VAlign(VAlign_Center)
		.ContentPadding(2)
		.ButtonContent()
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(this, &FLevelOfDetailSettingsLayout::GetCurrentLodName)
			.ToolTipText(this, &FLevelOfDetailSettingsLayout::GetCurrentLodTooltip)
		];
}

EVisibility FLevelOfDetailSettingsLayout::LodComboBoxVisibilityForLodPicker() const
{
	//No combo box when in Custom mode
	if (StaticMeshEditor.GetCustomData(CustomDataKey_LODEditMode) > 0)
	{
		return EVisibility::Hidden;
	}
	return EVisibility::All;
}

bool FLevelOfDetailSettingsLayout::IsLodComboBoxEnabledForLodPicker() const
{
	//No combo box when in Custom mode
	return StaticMeshEditor.GetCustomData(CustomDataKey_LODEditMode) <= 0;
}

TSharedRef<SWidget> FLevelOfDetailSettingsLayout::OnGenerateLodMenuForLodPicker()
{
	UStaticMesh* StaticMesh = StaticMeshEditor.GetStaticMesh();

	if (StaticMesh == nullptr)
	{
		return SNullWidget::NullWidget;
	}

	bool bAutoLod = false;
	if (StaticMeshEditor.GetStaticMeshComponent() != nullptr)
	{
		bAutoLod = StaticMeshEditor.GetStaticMeshComponent()->ForcedLodModel == 0;
	}
	const int32 StaticMeshLODCount = StaticMesh->GetNumLODs();
	if (StaticMeshLODCount < 2)
	{
		return SNullWidget::NullWidget;
	}
	FMenuBuilder MenuBuilder(true, NULL);

	FText AutoLodText = FText::FromString((TEXT("LOD Auto")));
	FUIAction AutoLodAction(FExecuteAction::CreateSP(this, &FLevelOfDetailSettingsLayout::OnSelectedLODChanged, 0));
	MenuBuilder.AddMenuEntry(AutoLodText, LOCTEXT("OnGenerateLodMenuForLodPicker_Auto_ToolTip", "With Auto LOD selected, LOD0's properties are visible for editing."), FSlateIcon(), AutoLodAction);
	// Add a menu item for each texture.  Clicking on the texture will display it in the content browser
	for (int32 AllLodIndex = 0; AllLodIndex < StaticMeshLODCount; ++AllLodIndex)
	{
		FText LODLevelString = FText::FromString((TEXT("LOD ") + FString::FromInt(AllLodIndex)));
		FUIAction Action(FExecuteAction::CreateSP(this, &FLevelOfDetailSettingsLayout::OnSelectedLODChanged, AllLodIndex + 1));
		MenuBuilder.AddMenuEntry(LODLevelString, FText::GetEmpty(), FSlateIcon(), Action);
	}

	return MenuBuilder.MakeWidget();
}

void FLevelOfDetailSettingsLayout::OnSelectedLODChanged(int32 NewLodIndex)
{
	if (StaticMeshEditor.GetStaticMeshComponent() == nullptr)
	{
		return;
	}
	int32 CurrentDisplayLOD = StaticMeshEditor.GetStaticMeshComponent()->ForcedLodModel;
	int32 RealNewLOD = NewLodIndex == 0 ? 0 : NewLodIndex - 1;

	if (CurrentDisplayLOD == NewLodIndex || !LodCategories.IsValidIndex(RealNewLOD))
	{
		return;
	}

	StaticMeshEditor.GetStaticMeshComponent()->SetForcedLodModel(NewLodIndex);

	//Reset the preview section since we do not edit the same LOD
	StaticMeshEditor.GetStaticMeshComponent()->SetSectionPreview(INDEX_NONE);
	StaticMeshEditor.GetStaticMeshComponent()->SelectedEditorSection = INDEX_NONE;

	//Broadcast that the LOD model has changed
	StaticMeshEditor.BroadcastOnSelectedLODChanged();
}

FText FLevelOfDetailSettingsLayout::GetCurrentLodName() const
{
	bool bAutoLod = false;
	if (StaticMeshEditor.GetStaticMeshComponent() != nullptr)
	{
		bAutoLod = StaticMeshEditor.GetStaticMeshComponent()->ForcedLodModel == 0;
	}
	int32 CurrentDisplayLOD = bAutoLod ? 0 : StaticMeshEditor.GetStaticMeshComponent()->ForcedLodModel - 1;
	return FText::FromString(bAutoLod ? FString(TEXT("LOD Auto")) : (FString(TEXT("LOD ")) + FString::FromInt(CurrentDisplayLOD)));
}

FText FLevelOfDetailSettingsLayout::GetCurrentLodTooltip() const
{
	if (StaticMeshEditor.GetStaticMeshComponent() != nullptr && StaticMeshEditor.GetStaticMeshComponent()->ForcedLodModel == 0)
	{
		return LOCTEXT("StaticMeshEditorLODPickerCurrentLODTooltip", "With Auto LOD selected, LOD0's properties are visible for editing");
	}
	return FText::GetEmpty();
}


#undef LOCTEXT_NAMESPACE
