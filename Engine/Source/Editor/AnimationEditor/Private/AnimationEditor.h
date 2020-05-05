// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "Widgets/SWidget.h"
#include "UObject/GCObject.h"
#include "Toolkits/IToolkitHost.h"
#include "IAnimationEditor.h"
#include "TickableEditorObject.h"
#include "EditorUndoClient.h"
#include "Containers/ArrayView.h"

struct FAssetData;
class FMenuBuilder;
class IAnimationSequenceBrowser;
class IDetailsView;
class IPersonaToolkit;
class IPersonaViewport;
class ISkeletonTree;
class UAnimationAsset;
class USkeletalMeshComponent;
class UAnimSequence;
class UAnimSequenceBase;
class ISkeletonTreeItem;
class IAnimSequenceCurveEditor;
struct FRichCurve;

namespace AnimationEditorModes
{
	// Mode identifiers
	extern const FName AnimationEditorMode;
}

namespace AnimationEditorTabs
{
	// Tab identifiers
	extern const FName DetailsTab;
	extern const FName SkeletonTreeTab;
	extern const FName ViewportTab;
	extern const FName AdvancedPreviewTab;
	extern const FName DocumentTab;
	extern const FName CurveEditorTab;
	extern const FName AssetBrowserTab;
	extern const FName AssetDetailsTab;
	extern const FName CurveNamesTab;
	extern const FName SlotNamesTab;
	extern const FName AnimMontageSectionsTab;
}

class FAnimationEditor : public IAnimationEditor, public FGCObject, public FEditorUndoClient, public FTickableEditorObject
{
public:
	FAnimationEditor();

	virtual ~FAnimationEditor();

	/** Edits the specified Skeleton object */
	void InitAnimationEditor(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost, class UAnimationAsset* InAnimationAsset);

	/** IAnimationEditor interface */
	virtual void SetAnimationAsset(UAnimationAsset* AnimAsset) override;
	virtual IAnimationSequenceBrowser* GetAssetBrowser() const override;
	virtual void EditCurves(UAnimSequenceBase* InAnimSequence, const TArray<FCurveEditInfo>& InCurveInfo, const TSharedPtr<ITimeSliderController>& InExternalTimeSliderController) override;
	virtual void StopEditingCurves(const TArray<FCurveEditInfo>& InCurveInfo) override;

	/** IHasPersonaToolkit interface */
	virtual TSharedRef<class IPersonaToolkit> GetPersonaToolkit() const override { return PersonaToolkit.ToSharedRef(); }

	/** IToolkit interface */
	virtual void RegisterTabSpawners(const TSharedRef<class FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<class FTabManager>& TabManager) override;
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;

	/** FTickableEditorObject Interface */
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Always; }
	
	/** FEditorUndoClient interface */
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	/** @return the documentation location for this editor */
	virtual FString GetDocumentationLink() const override
	{
		return FString(TEXT("Engine/Animation/AnimationEditor"));
	}

	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	/** Get the skeleton tree widget */
	TSharedRef<class ISkeletonTree> GetSkeletonTree() const { return SkeletonTree.ToSharedRef(); }

	void HandleDetailsCreated(const TSharedRef<class IDetailsView>& InDetailsView);

	UObject* HandleGetAsset();

	void HandleOpenNewAsset(UObject* InNewAsset);

	void HandleAnimationSequenceBrowserCreated(const TSharedRef<class IAnimationSequenceBrowser>& InSequenceBrowser);

	void HandleSelectionChanged(const TArrayView<TSharedPtr<ISkeletonTreeItem>>& InSelectedItems, ESelectInfo::Type InSelectInfo);

	void HandleObjectSelected(UObject* InObject);

	void HandleObjectsSelected(const TArray<UObject*>& InObjects);

private:

	/** Options for asset export */
	enum class EExportSourceOption : uint8
	{
		CurrentAnimation_AnimData,
		CurrentAnimation_PreviewMesh,
		Max
	};

	void HandleSectionsChanged();

	bool HasValidAnimationSequence() const;

	bool CanSetKey() const;

	void OnSetKey();

	bool CanApplyRawAnimChanges() const;

	void OnApplyRawAnimChanges();

	void OnReimportAnimation();

	void OnApplyCompression();

	void OnExportToFBX(const EExportSourceOption Option);
	//Return true mean the asset was exported, false it was cancel or it fail
	bool ExportToFBX(const TArray<UObject*> NewAssets, bool bRecordAnimation);

	void OnAddLoopingInterpolation();
	void OnRemoveBoneTrack();

	TSharedRef< SWidget > GenerateExportAssetMenu() const;

	void FillCopyToSoundWaveMenu(FMenuBuilder& MenuBuilder) const;

	void FillExportAssetMenu(FMenuBuilder& MenuBuilder) const;

	void CopyCurveToSoundWave(const FAssetData& SoundWaveAssetData) const;

	void ConditionalRefreshEditor(UObject* InObject);

	void HandlePostReimport(UObject* InObject, bool bSuccess);

	void HandlePostImport(class UFactory* InFactory, UObject* InObject);

private:
	void ExtendMenu();

	void ExtendToolbar();

	void BindCommands();

	TSharedPtr<SDockTab> OpenNewAnimationDocumentTab(UAnimationAsset* InAnimAsset);

	bool RecordMeshToAnimation(USkeletalMeshComponent* PreviewComponent, UAnimSequence* NewAsset) const;
public:
	/** Multicast delegate fired on global undo/redo */
	FSimpleMulticastDelegate OnPostUndo;

	/** Multicast delegate fired on global undo/redo */
	FSimpleMulticastDelegate OnLODChanged;

	/** Multicast delegate fired on sections changing */
	FSimpleMulticastDelegate OnSectionsChanged;

private:
	/** The animation asset we are editing */
	UAnimationAsset* AnimationAsset;

	/** Toolbar extender */
	TSharedPtr<FExtender> ToolbarExtender;

	/** Menu extender */
	TSharedPtr<FExtender> MenuExtender;

	/** Persona toolkit */
	TSharedPtr<class IPersonaToolkit> PersonaToolkit;

	/** Skeleton tree */
	TSharedPtr<class ISkeletonTree> SkeletonTree;

	/** Viewport */
	TSharedPtr<class IPersonaViewport> Viewport;

	/** Details panel */
	TSharedPtr<class IDetailsView> DetailsView;

	/** The animation document currently being edited */
	TWeakPtr<SDockTab> SharedAnimDocumentTab;

	/** The animation document's curves that are currently being edited */
	TWeakPtr<SDockTab> AnimCurveDocumentTab;

	/** Sequence Browser **/
	TWeakPtr<class IAnimationSequenceBrowser> SequenceBrowser;

	/** The anim sequence curve editor */
	TWeakPtr<IAnimSequenceCurveEditor> CurveEditor;
};
