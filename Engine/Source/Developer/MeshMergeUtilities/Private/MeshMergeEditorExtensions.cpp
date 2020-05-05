// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshMergeEditorExtensions.h"
#include "StaticMeshEditorModule.h"
#include "ISkeletalMeshEditorModule.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Commands/UICommandList.h"
#include "Modules/ModuleManager.h"
#include "IPersonaToolkit.h"
#include "Internationalization/Internationalization.h"
#include "Templates/SharedPointer.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "MeshMergeUtilities.h"
#include "Modules/ModuleManager.h"
#include "MeshMergeModule.h"
#include "ToolMenus.h"
#include "SkeletalMeshToolMenuContext.h"

#define LOCTEXT_NAMESPACE "MeshMergeEditorExtensions"

FDelegateHandle FMeshMergeEditorExtensions::StaticMeshEditorExtenderHandle;

void FMeshMergeEditorExtensions::OnModulesChanged(FName InModuleName, EModuleChangeReason InChangeReason)
{
	// If one of the modules we are interested in is loaded apply editor extensions
	if (InChangeReason == EModuleChangeReason::ModuleLoaded)
	{		
		if (InModuleName == "StaticMeshEditor")
		{
			AddStaticMeshEditorToolbarExtender();
		}
	}
}

void FMeshMergeEditorExtensions::RemoveExtenders()
{
	RemoveStaticMeshEditorToolbarExtender();
}

TSharedRef<FExtender> FMeshMergeEditorExtensions::GetStaticMeshEditorToolbarExtender(const TSharedRef<FUICommandList> CommandList, const TArray<UObject*> Objects)
{
	TSharedRef<FExtender> Extender = MakeShareable(new FExtender);

	checkf(Objects.Num() && Objects[0]->IsA<UStaticMesh>(), TEXT("Invalid object for static mesh editor toolbar extender"));

	// Add button on static mesh editor toolbar
	Extender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		CommandList,
		FToolBarExtensionDelegate::CreateStatic(&FMeshMergeEditorExtensions::HandleAddStaticMeshActionExtenderToToolbar, Cast<UStaticMesh>(Objects[0]))
	);

	return Extender;
}

void FMeshMergeEditorExtensions::AddStaticMeshEditorToolbarExtender()
{
	IStaticMeshEditorModule& StaticMeshEditorModule = FModuleManager::Get().LoadModuleChecked<IStaticMeshEditorModule>("StaticMeshEditor");

	auto& ToolbarExtenders = StaticMeshEditorModule.GetToolBarExtensibilityManager()->GetExtenderDelegates();
	ToolbarExtenders.Add(FAssetEditorExtender::CreateStatic(&FMeshMergeEditorExtensions::GetStaticMeshEditorToolbarExtender));
	StaticMeshEditorExtenderHandle = ToolbarExtenders.Last().GetHandle();
}

void FMeshMergeEditorExtensions::RemoveStaticMeshEditorToolbarExtender()
{
	IStaticMeshEditorModule* StaticMeshEditorModule = FModuleManager::Get().GetModulePtr<IStaticMeshEditorModule>("StaticMeshEditor");

	if (StaticMeshEditorModule)
	{
		StaticMeshEditorModule->GetToolBarExtensibilityManager()->GetExtenderDelegates().RemoveAll([=](const auto& In) { return In.GetHandle() == StaticMeshEditorExtenderHandle; });
	}
}

void FMeshMergeEditorExtensions::HandleAddStaticMeshActionExtenderToToolbar(FToolBarBuilder& ParentToolbarBuilder, UStaticMesh* StaticMesh)
{
	ParentToolbarBuilder.AddToolBarButton(
		FUIAction(FExecuteAction::CreateLambda([StaticMesh]()
		{
			const IMeshMergeModule& Module = FModuleManager::Get().LoadModuleChecked<IMeshMergeModule>("MeshMergeUtilities");
			Module.GetUtilities().BakeMaterialsForMesh(StaticMesh);
		})),
		NAME_None,
		LOCTEXT("BakeMaterials", "Bake out Materials"),
		LOCTEXT("BakeMaterialsTooltip", "Bake out Materials for given LOD(s)."),
		FSlateIcon("EditorStyle", "Persona.BakeMaterials")
	);
}

void FMeshMergeEditorExtensions::RegisterMenus()
{
	{
		UToolMenu* Toolbar = UToolMenus::Get()->ExtendMenu("AssetEditor.SkeletalMeshEditor.ToolBar");
		FToolMenuSection& Section = Toolbar->FindOrAddSection("SkeletalMesh");
		Section.AddEntry(FToolMenuEntry::InitToolBarButton(
			"BakeMaterials",
			FToolMenuExecuteAction::CreateLambda([](const FToolMenuContext& InMenuContext)
			{
				USkeletalMeshToolMenuContext* Context = InMenuContext.FindContext<USkeletalMeshToolMenuContext>();
				if (Context && Context->SkeletalMeshEditor.IsValid())
				{
					if (UDebugSkelMeshComponent* SkeletalMeshComponent = Context->SkeletalMeshEditor.Pin()->GetPersonaToolkit()->GetPreviewMeshComponent())
					{
						IMeshMergeModule& Module = FModuleManager::Get().LoadModuleChecked<IMeshMergeModule>("MeshMergeUtilities");
						Module.GetUtilities().BakeMaterialsForComponent(SkeletalMeshComponent);
					}
				}
			}),
			LOCTEXT("BakeMaterials", "Bake out Materials"),
			LOCTEXT("BakeMaterialsTooltip", "Bake out Materials for given LOD(s)."),
			FSlateIcon("EditorStyle", "Persona.BakeMaterials")
		));
	}
}

#undef LOCTEXT_NAMESPACE // "MeshMergeEditorExtensions"