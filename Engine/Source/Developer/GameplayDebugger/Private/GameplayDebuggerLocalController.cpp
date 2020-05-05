// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameplayDebuggerLocalController.h"
#include "InputCoreTypes.h"
#include "Framework/Commands/InputChord.h"
#include "Components/InputComponent.h"
#include "TimerManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpectatorPawn.h"
#include "GameplayDebuggerTypes.h"
#include "GameplayDebuggerCategoryReplicator.h"
#include "GameplayDebuggerPlayerManager.h"
#include "GameplayDebuggerAddonBase.h"
#include "GameplayDebuggerCategory.h"
#include "GameplayDebuggerAddonManager.h"
#include "GameplayDebuggerExtension.h"
#include "GameplayDebuggerConfig.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Selection.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/DebugCameraController.h"
#include "UnrealEngine.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerInput.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

#if WITH_EDITOR
#include "Editor/GameplayDebuggerEdMode.h"
#include "EditorModeManager.h"
#endif // WITH_EDITOR

UGameplayDebuggerLocalController::UGameplayDebuggerLocalController(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	bSimulateMode = false;
	bNeedsCleanup = false;
	bIsSelectingActor = false;
	bIsLocallyEnabled = false;
	bPrevLocallyEnabled = false;
	ActiveRowIdx = 0;
}

void UGameplayDebuggerLocalController::Initialize(AGameplayDebuggerCategoryReplicator& Replicator, AGameplayDebuggerPlayerManager& Manager)
{
	CachedReplicator = &Replicator;
	CachedPlayerManager = &Manager;
	bSimulateMode = FGameplayDebuggerAddonBase::IsSimulateInEditor();

	UDebugDrawService::Register(bSimulateMode ? TEXT("DebugAI") : TEXT("Game"), FDebugDrawDelegate::CreateUObject(this, &UGameplayDebuggerLocalController::OnDebugDraw));

#if WITH_EDITOR
	if (GIsEditor)
	{
		USelection::SelectionChangedEvent.AddUObject(this, &UGameplayDebuggerLocalController::OnSelectionChanged);
		USelection::SelectObjectEvent.AddUObject(this, &UGameplayDebuggerLocalController::OnSelectedObject);
	}
#endif

	FGameplayDebuggerAddonManager& AddonManager = FGameplayDebuggerAddonManager::GetCurrent();
	AddonManager.OnCategoriesChanged.AddUObject(this, &UGameplayDebuggerLocalController::OnCategoriesChanged);
	OnCategoriesChanged();

	const UGameplayDebuggerConfig* SettingsCDO = UGameplayDebuggerConfig::StaticClass()->GetDefaultObject<UGameplayDebuggerConfig>();
	const FKey NumpadKeys[] = { EKeys::NumPadZero, EKeys::NumPadOne, EKeys::NumPadTwo, EKeys::NumPadThree, EKeys::NumPadFour,
		EKeys::NumPadFive, EKeys::NumPadSix, EKeys::NumPadSeven, EKeys::NumPadEight, EKeys::NumPadNine };
	const FKey CategorySlots[] = { SettingsCDO->CategorySlot0, SettingsCDO->CategorySlot1, SettingsCDO->CategorySlot2, SettingsCDO->CategorySlot3, SettingsCDO->CategorySlot4,
		SettingsCDO->CategorySlot5, SettingsCDO->CategorySlot6, SettingsCDO->CategorySlot7, SettingsCDO->CategorySlot8, SettingsCDO->CategorySlot9 };

	bool bIsNumpadOnly = true;
	for (int32 Idx = 0; Idx < UE_ARRAY_COUNT(CategorySlots); Idx++)
	{
		bool bHasPattern = false;
		for (int32 PatternIdx = 0; PatternIdx < UE_ARRAY_COUNT(NumpadKeys); PatternIdx++)
		{
			if (CategorySlots[Idx] == NumpadKeys[PatternIdx])
			{
				bHasPattern = true;
				break;
			}
		}

		if (!bHasPattern)
		{
			bIsNumpadOnly = false;
			break;
		}
	}

	ActivationKeyDesc = GetKeyDescriptionLong(SettingsCDO->ActivationKey);
	RowUpKeyDesc = GetKeyDescriptionShort(SettingsCDO->CategoryRowPrevKey);
	RowDownKeyDesc = GetKeyDescriptionShort(SettingsCDO->CategoryRowNextKey);
	CategoryKeysDesc = bIsNumpadOnly ? TEXT("{yellow}Numpad{white}") : TEXT("highlighted keys");

	PaddingLeft = SettingsCDO->DebugCanvasPaddingLeft;
	PaddingRight = SettingsCDO->DebugCanvasPaddingRight;
	PaddingTop = SettingsCDO->DebugCanvasPaddingTop;
	PaddingBottom = SettingsCDO->DebugCanvasPaddingBottom;

	bNeedsCleanup = true;
}

void UGameplayDebuggerLocalController::Cleanup()
{
#if WITH_EDITOR
	USelection::SelectionChangedEvent.RemoveAll(this);
	USelection::SelectObjectEvent.RemoveAll(this);

	if (bSimulateMode)
	{
		FGameplayDebuggerEdMode::SafeCloseMode();
	}
#endif // WITH_EDITOR

	bNeedsCleanup = false;
}

void UGameplayDebuggerLocalController::BeginDestroy()
{
	Super::BeginDestroy();
	if (bNeedsCleanup)
	{
		Cleanup();
	}
}

void UGameplayDebuggerLocalController::OnDebugDraw(class UCanvas* Canvas, class APlayerController* PC)
{
	if (CachedReplicator && CachedReplicator->IsEnabled())
	{
		FGameplayDebuggerCanvasContext CanvasContext(Canvas, GEngine->GetSmallFont());
		CanvasContext.CursorX = CanvasContext.DefaultX = PaddingLeft;
		CanvasContext.CursorY = CanvasContext.DefaultY = PaddingTop;

		DrawHeader(CanvasContext);

		if (DataPackMap.Num() != NumCategories)
		{
			RebuildDataPackMap();
		}

		const bool bHasDebugActor = CachedReplicator->HasDebugActor();
		for (int32 Idx = 0; Idx < NumCategories; Idx++)
		{
			TSharedRef<FGameplayDebuggerCategory> Category = CachedReplicator->GetCategory(Idx);
			if (Category->ShouldDrawCategory(bHasDebugActor))
			{
				if (Category->IsCategoryHeaderVisible())
				{
					DrawCategoryHeader(Idx, Category, CanvasContext);
				}

				Category->DrawCategory(CachedReplicator->GetReplicationOwner(), CanvasContext);
			}
		}
	}
}

extern RENDERCORE_API FTexture* GWhiteTexture;

void UGameplayDebuggerLocalController::DrawHeader(FGameplayDebuggerCanvasContext& CanvasContext)
{
	const int32 NumRows = (NumCategorySlots + (NumCategoriesPerRow-1)) / NumCategoriesPerRow;
	const float LineHeight = CanvasContext.GetLineHeight();
	const int32 NumExtensions = bSimulateMode ? 0 : CachedReplicator->GetNumExtensions();
	const int32 NumExtensionRows = (NumExtensions > 0) ? 1 : 0;
	const float CanvasSizeX = CanvasContext.Canvas->SizeX - PaddingLeft - PaddingRight;
	const float UsePaddingTop = PaddingTop + (bSimulateMode ? 30.0f : 0);
	
	const float BackgroundPadding = 5.0f;
	const float BackgroundPaddingBothSides = BackgroundPadding * 2.0f;

	if (NumRows > 1)
	{
		FCanvasTileItem TileItemUpper(FVector2D(0, 0), GWhiteTexture, FVector2D(CanvasSizeX + BackgroundPaddingBothSides, (LineHeight * (ActiveRowIdx + NumExtensionRows + 1)) + BackgroundPadding), FLinearColor(0, 0, 0, 0.2f));
		FCanvasTileItem ActiveRowTileItem(FVector2D(0, 0), GWhiteTexture, FVector2D(CanvasSizeX + BackgroundPaddingBothSides, LineHeight), FLinearColor(0, 0.5f, 0, 0.3f));
		FCanvasTileItem TileItemLower(FVector2D(0, 0), GWhiteTexture, FVector2D(CanvasSizeX + BackgroundPaddingBothSides, LineHeight * ((NumRows - ActiveRowIdx - 1)) + BackgroundPadding), FLinearColor(0, 0, 0, 0.2f));

		TileItemUpper.BlendMode = SE_BLEND_Translucent;
		ActiveRowTileItem.BlendMode = SE_BLEND_Translucent;
		TileItemLower.BlendMode = SE_BLEND_Translucent;

		CanvasContext.DrawItem(TileItemUpper, PaddingLeft - BackgroundPadding, UsePaddingTop - BackgroundPadding);
		CanvasContext.DrawItem(ActiveRowTileItem, PaddingLeft - BackgroundPadding, UsePaddingTop - BackgroundPadding + TileItemUpper.Size.Y);
		CanvasContext.DrawItem(TileItemLower, PaddingLeft - BackgroundPadding, UsePaddingTop - BackgroundPadding + TileItemUpper.Size.Y + ActiveRowTileItem.Size.Y);
	}
	else
	{
		FCanvasTileItem TileItem(FVector2D(0, 0), GWhiteTexture, FVector2D(CanvasSizeX, LineHeight * (NumRows + NumExtensionRows + 1)) + BackgroundPaddingBothSides, FLinearColor(0, 0, 0, 0.2f));
		TileItem.BlendMode = SE_BLEND_Translucent;
		CanvasContext.DrawItem(TileItem, PaddingLeft - BackgroundPadding, UsePaddingTop - BackgroundPadding);
	}

	CanvasContext.CursorY = UsePaddingTop;
	if (bSimulateMode)
	{
		CanvasContext.Printf(TEXT("Clear {yellow}DebugAI{white} show flag to close, use %s to toggle categories."), *CategoryKeysDesc);

		// reactivate editor mode when this is being drawn = show flag is set
#if WITH_EDITOR
		GLevelEditorModeTools().ActivateMode(FGameplayDebuggerEdMode::EM_GameplayDebugger);
#endif // WITH_EDITOR
	}
	else
	{
		CanvasContext.Printf(TEXT("Tap {yellow}%s{white} to close, use %s to toggle categories."), *ActivationKeyDesc, *CategoryKeysDesc);
	}

	const FString DebugActorDesc = FString::Printf(TEXT("Debug actor: {cyan}%s"), *CachedReplicator->GetDebugActorName().ToString());
	float DebugActorSizeX = 0.0f, DebugActorSizeY = 0.0f;
	CanvasContext.MeasureString(DebugActorDesc, DebugActorSizeX, DebugActorSizeY);
	CanvasContext.PrintAt(CanvasContext.Canvas->SizeX - PaddingRight - DebugActorSizeX, UsePaddingTop, DebugActorDesc);

	const FString VLogDesc = FString::Printf(TEXT("VLog: {cyan}%s"), CachedReplicator->GetVisLogSyncData().DeviceIDs.Len() > 0
			? *CachedReplicator->GetVisLogSyncData().DeviceIDs
			: TEXT("not recording to file"));
	float VLogSizeX = 0.0f, VLogSizeY = 0.0f;
	CanvasContext.MeasureString(VLogDesc, VLogSizeX, VLogSizeY);
	CanvasContext.PrintAt(CanvasContext.Canvas->SizeX - PaddingRight - VLogSizeX, UsePaddingTop + LineHeight, VLogDesc);

	const FString TimestampDesc = FString::Printf(TEXT("Time: %.2fs"), CachedReplicator->GetWorld()->GetTimeSeconds());
	float TimestampSizeX = 0.0f, TimestampSizeY = 0.0f;
	CanvasContext.MeasureString(TimestampDesc, TimestampSizeX, TimestampSizeY);
	CanvasContext.PrintAt((CanvasSizeX - TimestampSizeX) * 0.5f, UsePaddingTop, TimestampDesc);

	if (NumRows > 1)
	{
		const FString ChangeRowDesc = FString::Printf(TEXT("Prev row: {yellow}%s\n{white}Next row: {yellow}%s"), *RowUpKeyDesc, *RowDownKeyDesc);
		float RowDescSizeX = 0.0f, RowDescSizeY = 0.0f;
		CanvasContext.MeasureString(ChangeRowDesc, RowDescSizeX, RowDescSizeY);
		CanvasContext.PrintAt(CanvasContext.Canvas->SizeX - PaddingRight - RowDescSizeX, UsePaddingTop + LineHeight * (NumExtensionRows + 1), ChangeRowDesc);
	}

	if (NumExtensionRows)
	{
		FString ExtensionRowDesc;
		for (int32 ExtensionIdx = 0; ExtensionIdx < NumExtensions; ExtensionIdx++)
		{
			TSharedRef<FGameplayDebuggerExtension> Extension = CachedReplicator->GetExtension(ExtensionIdx);
			FString ExtensionDesc = Extension->GetDescription();
			ExtensionDesc.ReplaceInline(TEXT("\n"), TEXT(""));

			if (ExtensionDesc.Len())
			{
				if (ExtensionRowDesc.Len())
				{
					ExtensionRowDesc += FGameplayDebuggerCanvasStrings::SeparatorSpace;
				}

				ExtensionRowDesc += ExtensionDesc;
			}
		}

		CanvasContext.Print(ExtensionRowDesc);
	}

	for (int32 RowIdx = 0; RowIdx < NumRows; RowIdx++)
	{
		FString CategoryRowDesc;
		for (int32 Idx = 0; Idx < NumCategoriesPerRow; Idx++)
		{
			const int32 CategorySlotIdx = (RowIdx * NumCategoriesPerRow) + Idx;
			if (SlotCategoryIds.IsValidIndex(CategorySlotIdx) && 
				SlotNames.IsValidIndex(CategorySlotIdx) &&
				SlotCategoryIds[CategorySlotIdx].Num())
			{
				TSharedRef<FGameplayDebuggerCategory> Category0 = CachedReplicator->GetCategory(SlotCategoryIds[CategorySlotIdx][0]);
				const bool bIsEnabled = Category0->IsCategoryEnabled();
				const FString CategoryColorName = (RowIdx == ActiveRowIdx) && (NumRows > 1) ?
					(bIsEnabled ? *FGameplayDebuggerCanvasStrings::ColorNameEnabledActiveRow : *FGameplayDebuggerCanvasStrings::ColorNameDisabledActiveRow) :
					(bIsEnabled ? *FGameplayDebuggerCanvasStrings::ColorNameEnabled : *FGameplayDebuggerCanvasStrings::ColorNameDisabled);

				const FString CategoryDesc = (RowIdx == ActiveRowIdx) ?
					FString::Printf(TEXT("%s{%s}%d:{%s}%s"),
						Idx ? *FGameplayDebuggerCanvasStrings::SeparatorSpace : TEXT(""),
						*FGameplayDebuggerCanvasStrings::ColorNameInput,
						Idx,
						*CategoryColorName,
						*SlotNames[CategorySlotIdx]) :
					FString::Printf(TEXT("%s{%s}%s"),
						Idx ? *FGameplayDebuggerCanvasStrings::Separator : TEXT(""),
						*CategoryColorName,
						*SlotNames[CategorySlotIdx]);

				CategoryRowDesc += CategoryDesc;
			}
		}

		CanvasContext.Print(CategoryRowDesc);
	}

	CanvasContext.DefaultY = CanvasContext.CursorY + LineHeight;
}

void UGameplayDebuggerLocalController::DrawCategoryHeader(int32 CategoryId, TSharedRef<FGameplayDebuggerCategory> Category, FGameplayDebuggerCanvasContext& CanvasContext)
{
	FString DataPackDesc;
	
	if (DataPackMap.IsValidIndex(CategoryId) &&
		!Category->IsCategoryAuth() &&
		!Category->ShouldDrawReplicationStatus() &&
		Category->GetNumDataPacks() > 0)
	{
		// collect brief data pack status, detailed info is displayed only when ShouldDrawReplicationStatus is true
		const int32 CurrentSyncCounter = CachedReplicator->GetDebugActorCounter();

		DataPackDesc = TEXT("{white} ver[");
		bool bIsPrevOutdated = false;
		bool bAddSeparator = false;

		for (int32 Idx = 0; Idx < DataPackMap[CategoryId].Num(); Idx++)
		{
			TSharedRef<FGameplayDebuggerCategory> MappedCategory = CachedReplicator->GetCategory(DataPackMap[CategoryId][Idx]);
			for (int32 DataPackIdx = 0; DataPackIdx < MappedCategory->GetNumDataPacks(); DataPackIdx++)
			{
				FGameplayDebuggerDataPack::FHeader DataHeader = MappedCategory->GetDataPackHeaderCopy(DataPackIdx);
				const bool bIsOutdated = (DataHeader.SyncCounter != CurrentSyncCounter);

				if (bAddSeparator)
				{
					DataPackDesc += TEXT(';');
				}

				if (bIsOutdated != bIsPrevOutdated)
				{
					DataPackDesc += bIsOutdated ? TEXT("{red}") : TEXT("{white}");
					bIsPrevOutdated = bIsOutdated;
				}

				DataPackDesc += TTypeToString<int16>::ToString(DataHeader.DataVersion);
				bAddSeparator = true;
			}
		}

		if (bIsPrevOutdated)
		{
			DataPackDesc += TEXT("{white}");
		}

		DataPackDesc += TEXT(']');
	}

	CanvasContext.MoveToNewLine();
	CanvasContext.Printf(FColor::Green, TEXT("[CATEGORY: %s]%s"), *Category->GetCategoryName().ToString(), *DataPackDesc);
}

void UGameplayDebuggerLocalController::BindInput(UInputComponent& InputComponent)
{
	TSet<FName> NewBindings;

	const UGameplayDebuggerConfig* SettingsCDO = UGameplayDebuggerConfig::StaticClass()->GetDefaultObject<UGameplayDebuggerConfig>();
	if (!bSimulateMode)
	{
		InputComponent.BindKey(SettingsCDO->ActivationKey, IE_Pressed, this, &UGameplayDebuggerLocalController::OnActivationPressed);
		InputComponent.BindKey(SettingsCDO->ActivationKey, IE_Released, this, &UGameplayDebuggerLocalController::OnActivationReleased);
		NewBindings.Add(SettingsCDO->ActivationKey.GetFName());
	}

	if (bIsLocallyEnabled || bSimulateMode)
	{
		InputComponent.BindKey(SettingsCDO->CategorySlot0, IE_Pressed, this, &UGameplayDebuggerLocalController::OnCategory0Pressed);
		InputComponent.BindKey(SettingsCDO->CategorySlot1, IE_Pressed, this, &UGameplayDebuggerLocalController::OnCategory1Pressed);
		InputComponent.BindKey(SettingsCDO->CategorySlot2, IE_Pressed, this, &UGameplayDebuggerLocalController::OnCategory2Pressed);
		InputComponent.BindKey(SettingsCDO->CategorySlot3, IE_Pressed, this, &UGameplayDebuggerLocalController::OnCategory3Pressed);
		InputComponent.BindKey(SettingsCDO->CategorySlot4, IE_Pressed, this, &UGameplayDebuggerLocalController::OnCategory4Pressed);
		InputComponent.BindKey(SettingsCDO->CategorySlot5, IE_Pressed, this, &UGameplayDebuggerLocalController::OnCategory5Pressed);
		InputComponent.BindKey(SettingsCDO->CategorySlot6, IE_Pressed, this, &UGameplayDebuggerLocalController::OnCategory6Pressed);
		InputComponent.BindKey(SettingsCDO->CategorySlot7, IE_Pressed, this, &UGameplayDebuggerLocalController::OnCategory7Pressed);
		InputComponent.BindKey(SettingsCDO->CategorySlot8, IE_Pressed, this, &UGameplayDebuggerLocalController::OnCategory8Pressed);
		InputComponent.BindKey(SettingsCDO->CategorySlot9, IE_Pressed, this, &UGameplayDebuggerLocalController::OnCategory9Pressed);

		InputComponent.BindKey(SettingsCDO->CategoryRowPrevKey, IE_Pressed, this, &UGameplayDebuggerLocalController::OnCategoryRowUpPressed);
		InputComponent.BindKey(SettingsCDO->CategoryRowNextKey, IE_Pressed, this, &UGameplayDebuggerLocalController::OnCategoryRowDownPressed);

		NewBindings.Add(SettingsCDO->CategorySlot0.GetFName());
		NewBindings.Add(SettingsCDO->CategorySlot1.GetFName());
		NewBindings.Add(SettingsCDO->CategorySlot2.GetFName());
		NewBindings.Add(SettingsCDO->CategorySlot3.GetFName());
		NewBindings.Add(SettingsCDO->CategorySlot4.GetFName());
		NewBindings.Add(SettingsCDO->CategorySlot5.GetFName());
		NewBindings.Add(SettingsCDO->CategorySlot6.GetFName());
		NewBindings.Add(SettingsCDO->CategorySlot7.GetFName());
		NewBindings.Add(SettingsCDO->CategorySlot8.GetFName());
		NewBindings.Add(SettingsCDO->CategorySlot9.GetFName());
		NewBindings.Add(SettingsCDO->CategoryRowPrevKey.GetFName());
		NewBindings.Add(SettingsCDO->CategoryRowNextKey.GetFName());

		for (int32 Idx = 0; Idx < NumCategories; Idx++)
		{
			TSharedRef<FGameplayDebuggerCategory> Category = CachedReplicator->GetCategory(Idx);
			const int32 NumInputHandlers = Category->GetNumInputHandlers();

			for (int32 HandlerIdx = 0; HandlerIdx < NumInputHandlers; HandlerIdx++)
			{
				FGameplayDebuggerInputHandler& HandlerData = Category->GetInputHandler(HandlerIdx);
				if (HandlerData.Modifier.bPressed || HandlerData.Modifier.bReleased)
				{
					FInputChord InputChord(FKey(HandlerData.KeyName), HandlerData.Modifier.bShift, HandlerData.Modifier.bCtrl, HandlerData.Modifier.bAlt, HandlerData.Modifier.bCmd);
					FInputKeyBinding InputBinding(InputChord, HandlerData.Modifier.bPressed ? IE_Pressed : IE_Released);
					InputBinding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &UGameplayDebuggerLocalController::OnCategoryBindingEvent, Idx, HandlerIdx);

					InputComponent.KeyBindings.Add(InputBinding);
					NewBindings.Add(HandlerData.KeyName);
				}
			}
		}

		const int32 NumExtentions = bSimulateMode ? 0 : CachedReplicator->GetNumExtensions();
		for (int32 Idx = 0; Idx < NumExtentions; Idx++)
		{
			TSharedRef<FGameplayDebuggerExtension> Extension = CachedReplicator->GetExtension(Idx); //-V595
			const int32 NumInputHandlers = Extension->GetNumInputHandlers();

			for (int32 HandlerIdx = 0; HandlerIdx < NumInputHandlers; HandlerIdx++)
			{
				FGameplayDebuggerInputHandler& HandlerData = Extension->GetInputHandler(HandlerIdx);
				if (HandlerData.Modifier.bPressed || HandlerData.Modifier.bReleased)
				{
					FInputChord InputChord(FKey(HandlerData.KeyName), HandlerData.Modifier.bShift, HandlerData.Modifier.bCtrl, HandlerData.Modifier.bAlt, HandlerData.Modifier.bCmd);
					FInputKeyBinding InputBinding(InputChord, HandlerData.Modifier.bPressed ? IE_Pressed : IE_Released);
					InputBinding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &UGameplayDebuggerLocalController::OnExtensionBindingEvent, Idx, HandlerIdx);

					InputComponent.KeyBindings.Add(InputBinding);
					NewBindings.Add(HandlerData.KeyName);
				}
			}
		}
	}

	if (CachedReplicator && CachedReplicator->GetReplicationOwner() && CachedReplicator->GetReplicationOwner()->PlayerInput)
	{
		TSet<FName> RemovedMasks = UsedBindings.Difference(NewBindings);
		TSet<FName> AddedMasks = NewBindings.Difference(UsedBindings);

		UPlayerInput* Input = CachedReplicator->GetReplicationOwner()->PlayerInput;
		for (int32 Idx = 0; Idx < Input->DebugExecBindings.Num(); Idx++)
		{
			FKeyBind& DebugBinding = Input->DebugExecBindings[Idx];
			const bool bRemoveMask = RemovedMasks.Contains(DebugBinding.Key.GetFName());
			const bool bAddMask = AddedMasks.Contains(DebugBinding.Key.GetFName());

			if (bAddMask || bRemoveMask)
			{
				DebugBinding.bDisabled = bAddMask;
			}
		}

		UsedBindings = NewBindings;
	}
}

bool UGameplayDebuggerLocalController::IsKeyBound(const FName KeyName) const
{
	return UsedBindings.Contains(KeyName);
}

void UGameplayDebuggerLocalController::OnActivationPressed()
{
	bPrevLocallyEnabled = bIsLocallyEnabled;
	if (CachedReplicator)
	{
		const float HoldTimeThr = 0.2f * (FApp::UseFixedTimeStep() ? (FApp::GetFixedDeltaTime() * 60.0f) : 1.0f);

		CachedReplicator->GetWorldTimerManager().SetTimer(StartSelectingActorHandle, this, &UGameplayDebuggerLocalController::OnStartSelectingActor, HoldTimeThr);
	}
}

void UGameplayDebuggerLocalController::OnActivationReleased()
{
	ToggleActivation();
}

void UGameplayDebuggerLocalController::ToggleActivation()
{
	if (CachedReplicator)
	{
		UWorld* World = CachedReplicator->GetWorld();
		if (!bIsSelectingActor || StartSelectingActorHandle.IsValid())
		{
			bIsLocallyEnabled = !CachedReplicator->IsEnabled();
			CachedReplicator->SetEnabled(bIsLocallyEnabled);

			if (bIsLocallyEnabled)
			{
				DebugActorCandidate = nullptr;
				OnSelectActorTick();
			}
		}

		World->GetTimerManager().ClearTimer(StartSelectingActorHandle);
		World->GetTimerManager().ClearTimer(SelectActorTickHandle);

		CachedReplicator->MarkComponentsRenderStateDirty();
	}

	StartSelectingActorHandle.Invalidate();
	SelectActorTickHandle.Invalidate();
	bIsSelectingActor = false;

	if (CachedReplicator && (bPrevLocallyEnabled != bIsLocallyEnabled))
	{
		CachedPlayerManager->RefreshInputBindings(*CachedReplicator);
	}
}

void UGameplayDebuggerLocalController::OnCategory0Pressed()
{
	ToggleSlotState((ActiveRowIdx * NumCategoriesPerRow) + 0);
}

void UGameplayDebuggerLocalController::OnCategory1Pressed()
{
	ToggleSlotState((ActiveRowIdx * NumCategoriesPerRow) + 1);
}

void UGameplayDebuggerLocalController::OnCategory2Pressed()
{
	ToggleSlotState((ActiveRowIdx * NumCategoriesPerRow) + 2);
}

void UGameplayDebuggerLocalController::OnCategory3Pressed()
{
	ToggleSlotState((ActiveRowIdx * NumCategoriesPerRow) + 3);
}

void UGameplayDebuggerLocalController::OnCategory4Pressed()
{
	ToggleSlotState((ActiveRowIdx * NumCategoriesPerRow) + 4);
}

void UGameplayDebuggerLocalController::OnCategory5Pressed()
{
	ToggleSlotState((ActiveRowIdx * NumCategoriesPerRow) + 5);
}

void UGameplayDebuggerLocalController::OnCategory6Pressed()
{
	ToggleSlotState((ActiveRowIdx * NumCategoriesPerRow) + 6);
}

void UGameplayDebuggerLocalController::OnCategory7Pressed()
{
	ToggleSlotState((ActiveRowIdx * NumCategoriesPerRow) + 7);
}

void UGameplayDebuggerLocalController::OnCategory8Pressed()
{
	ToggleSlotState((ActiveRowIdx * NumCategoriesPerRow) + 8);
}

void UGameplayDebuggerLocalController::OnCategory9Pressed()
{
	ToggleSlotState((ActiveRowIdx * NumCategoriesPerRow) + 9);
}

void UGameplayDebuggerLocalController::OnCategoryRowUpPressed()
{
	const int32 NumRows = (NumCategorySlots + (NumCategoriesPerRow-1)) / NumCategoriesPerRow;
	ActiveRowIdx = (NumRows > 1) ? ((ActiveRowIdx + NumRows - 1) % NumRows) : 0;
}

void UGameplayDebuggerLocalController::OnCategoryRowDownPressed()
{
	const int32 NumRows = (NumCategorySlots + (NumCategoriesPerRow-1)) / NumCategoriesPerRow;
	ActiveRowIdx = (NumRows > 1) ? ((ActiveRowIdx + 1) % NumRows) : 0;
}

void UGameplayDebuggerLocalController::OnCategoryBindingEvent(int32 CategoryId, int32 HandlerId)
{
	if (CachedReplicator)
	{
		CachedReplicator->SendCategoryInputEvent(CategoryId, HandlerId);
	}
}

void UGameplayDebuggerLocalController::OnExtensionBindingEvent(int32 ExtensionId, int32 HandlerId)
{
	if (CachedReplicator)
	{
		CachedReplicator->SendExtensionInputEvent(ExtensionId, HandlerId);
	}
}

void UGameplayDebuggerLocalController::OnStartSelectingActor()
{
	StartSelectingActorHandle.Invalidate();
	if (CachedReplicator)
	{
		if (!CachedReplicator->IsEnabled())
		{
			bIsLocallyEnabled = true;
			CachedReplicator->SetEnabled(bIsLocallyEnabled);
		}

		bIsSelectingActor = true;
		DebugActorCandidate = nullptr;

		const bool bLooping = true;
		CachedReplicator->GetWorldTimerManager().SetTimer(SelectActorTickHandle, this, &UGameplayDebuggerLocalController::OnSelectActorTick, 0.01f, bLooping);

		OnSelectActorTick();
	}
}

void UGameplayDebuggerLocalController::OnSelectActorTick()
{
	APlayerController* OwnerPC = CachedReplicator ? CachedReplicator->GetReplicationOwner() : nullptr;
	if (OwnerPC)
	{
		FVector CameraLocation;
		FRotator CameraRotation;
		if (OwnerPC->Player)
		{
			// normal game
			OwnerPC->GetPlayerViewPoint(CameraLocation, CameraRotation);
		}
		else
		{
			// spectator mode
			for (FLocalPlayerIterator It(GEngine, OwnerPC->GetWorld()); It; ++It)
			{
				ADebugCameraController* SpectatorPC = Cast<ADebugCameraController>(It->PlayerController);
				if (SpectatorPC)
				{
					SpectatorPC->GetPlayerViewPoint(CameraLocation, CameraRotation);
					break;
				}
			}
		}

		// TODO: move to module's settings
		const float MaxScanDistance = 25000.0f;
		const float MinViewDirDot = 0.8f;

		AActor* BestCandidate = nullptr;
		float BestScore = MinViewDirDot;
		
		const FVector ViewDir = CameraRotation.Vector();
		for (APawn* TestPawn  : TActorRange<APawn>(OwnerPC->GetWorld()))
		{
			if (!TestPawn->IsHidden() && TestPawn->GetActorEnableCollision() &&
				!TestPawn->IsA(ASpectatorPawn::StaticClass()) &&
				TestPawn != OwnerPC->GetPawn())
			{
				FVector DirToPawn = (TestPawn->GetActorLocation() - CameraLocation);
				float DistToPawn = DirToPawn.Size();
				if (FMath::IsNearlyZero(DistToPawn))
				{
					DirToPawn = ViewDir;
					DistToPawn = 1.0f;
				}
				else
				{
					DirToPawn /= DistToPawn;
				}

				const float ViewDot = FVector::DotProduct(ViewDir, DirToPawn);
				if (DistToPawn < MaxScanDistance && ViewDot > BestScore)
				{
					BestScore = ViewDot;
					BestCandidate = TestPawn;
				}
			}
		}

		// cache to avoid multiple RPC with the same actor
		if (DebugActorCandidate != BestCandidate)
		{
			DebugActorCandidate = BestCandidate;
			CachedReplicator->SetDebugActor(BestCandidate, true);
		}
	}
}

void UGameplayDebuggerLocalController::ToggleSlotState(int32 SlotIdx)
{
	if (CachedReplicator && SlotCategoryIds.IsValidIndex(SlotIdx) && SlotCategoryIds[SlotIdx].Num())
	{
		const bool bIsEnabled = CachedReplicator->IsCategoryEnabled(SlotCategoryIds[SlotIdx][0]);
		for (int32 Idx = 0; Idx < SlotCategoryIds[SlotIdx].Num(); Idx++)
		{
			const int32 CategoryId = SlotCategoryIds[SlotIdx][Idx];
			CachedReplicator->SetCategoryEnabled(CategoryId, !bIsEnabled);
		}

		CachedReplicator->MarkComponentsRenderStateDirty();
	}
}

FString UGameplayDebuggerLocalController::GetKeyDescriptionShort(const FKey& KeyBind) const
{
	return FString::Printf(TEXT("[%s]"), *KeyBind.GetFName().ToString());
}

FString UGameplayDebuggerLocalController::GetKeyDescriptionLong(const FKey& KeyBind) const
{
	const FString KeyDisplay = KeyBind.GetDisplayName().ToString();
	const FString KeyName = KeyBind.GetFName().ToString();
	return (KeyDisplay == KeyName) ? FString::Printf(TEXT("[%s]"), *KeyDisplay) : FString::Printf(TEXT("%s [%s key])"), *KeyDisplay, *KeyName);
}

void UGameplayDebuggerLocalController::OnSelectionChanged(UObject* Object)
{
	USelection* Selection = Cast<USelection>(Object);
	if (Selection && CachedReplicator)
	{
		AActor* SelectedActor = nullptr;
		for (int32 Idx = 0; Idx < Selection->Num(); Idx++)
		{
			SelectedActor = Cast<AActor>(Selection->GetSelectedObject(Idx));
			if (SelectedActor)
			{
				break;
			}
		}

		if (SelectedActor)
		{
			CachedReplicator->SetDebugActor(SelectedActor, false);
			CachedReplicator->CollectCategoryData(/*bForce=*/true);
		}
	}
}

void UGameplayDebuggerLocalController::OnSelectedObject(UObject* Object)
{
	AController* SelectedController = Cast<AController>(Object);
	APawn* SelectedPawn = SelectedController ? SelectedController->GetPawn() : Cast<APawn>(Object);
	if (CachedReplicator && SelectedPawn && SelectedPawn->IsSelected())
	{
		CachedReplicator->SetDebugActor(SelectedPawn, false);
		CachedReplicator->CollectCategoryData(/*bForce=*/true);
	}
}

void UGameplayDebuggerLocalController::OnCategoriesChanged()
{
	FGameplayDebuggerAddonManager& AddonManager = FGameplayDebuggerAddonManager::GetCurrent();

	SlotNames.Reset();
	SlotNames.Append(AddonManager.GetSlotNames());

	// categories are already sorted using AddonManager.SlotMap, build Slot to Category Id map accordingly
	const TArray< TArray<int32> >& SlotMap = AddonManager.GetSlotMap();
	SlotCategoryIds.Reset();
	SlotCategoryIds.AddDefaulted(SlotMap.Num());

	int32 CategoryId = 0;
	for (int32 SlotIdx = 0; SlotIdx < SlotMap.Num(); SlotIdx++)
	{
		for (int32 InnerIdx = 0; InnerIdx < SlotMap[SlotIdx].Num(); InnerIdx++)
		{
			SlotCategoryIds[SlotIdx].Add(CategoryId);
			CategoryId++;
		}
	}

	NumCategorySlots = SlotCategoryIds.Num();
	NumCategories = AddonManager.GetNumVisibleCategories();

	DataPackMap.Reset();
}

void UGameplayDebuggerLocalController::RebuildDataPackMap()
{
	DataPackMap.SetNum(NumCategories);
	
	// category: get all categories from slot and combine data pack data if category header is not displayed
	for (int32 SlotIdx = 0; SlotIdx < NumCategorySlots; SlotIdx++)
	{
		TArray<int32> NoHeaderCategories;
		int32 FirstVisibleCategoryId = INDEX_NONE;

		for (int32 InnerIdx = 0; InnerIdx < SlotCategoryIds[SlotIdx].Num(); InnerIdx++)
		{
			const int32 CategoryId = SlotCategoryIds[SlotIdx][InnerIdx];
			
			TSharedRef<FGameplayDebuggerCategory> Category = CachedReplicator->GetCategory(CategoryId);
			if (!Category->IsCategoryHeaderVisible())
			{
				NoHeaderCategories.Add(CategoryId);
			}
			else
			{
				DataPackMap[CategoryId].Add(CategoryId);
				
				if (FirstVisibleCategoryId == INDEX_NONE)
				{
					FirstVisibleCategoryId = CategoryId;
				}
			}
		}

		if ((FirstVisibleCategoryId != INDEX_NONE) && NoHeaderCategories.Num())
		{
			DataPackMap[FirstVisibleCategoryId].Append(NoHeaderCategories);
		}
	}
}

/**  Helper structure to declare/define console commands in the source file and to access UGameplayDebuggerLocalController protected members */
struct FGameplayDebuggerConsoleCommands
{
private:
	static UGameplayDebuggerLocalController* GetController(UWorld* InWorld)
	{
		UGameplayDebuggerLocalController* Controller = nullptr;

		APlayerController* LocalPC = GEngine->GetFirstLocalPlayerController(InWorld);
		if (LocalPC)
		{
			Controller = AGameplayDebuggerPlayerManager::GetCurrent(InWorld).GetLocalController(*LocalPC);
		}

		UE_CLOG(Controller == nullptr, LogConsoleResponse, Error, TEXT("GameplayDebugger not available"));
		return Controller;
	}

	static void ToggleGameplayDebugger(UWorld* InWorld)
	{
		if (UGameplayDebuggerLocalController* Controller = GetController(InWorld))
		{
			Controller->ToggleActivation();
		}
	}

	static void SelectPreviousRow(UWorld* InWorld)
	{
		if (UGameplayDebuggerLocalController* Controller = GetController(InWorld))
		{
			Controller->OnCategoryRowUpPressed();
		}
	}

	static void SelectNextRow(UWorld* InWorld)
	{
		if (UGameplayDebuggerLocalController* Controller = GetController(InWorld))
		{
			Controller->OnCategoryRowDownPressed();
		}
	}

	static void ToggleCategory(const TArray<FString>& Args, UWorld* InWorld)
	{
		UGameplayDebuggerLocalController* Controller = GetController(InWorld);
		if (Controller == nullptr)
		{
			return;
		}

		if (Args.Num() != 1)
		{
			UE_LOG(LogConsoleResponse, Error, TEXT("Missing category index parameter. Usage: gdt.ToggleCategory <CategoryIdx>"));
			return;
		}

		if (!Args[0].IsNumeric())
		{
			UE_LOG(LogConsoleResponse, Error, TEXT("Must provide numerical value as index. Usage: gdt.ToggleCategory <CategoryIdx>"));
			return;
		}
		
		const int32 SlotIdx = TCString<TCHAR>::Atoi(*Args[0]);
		const int32 NumSlots = Controller->SlotCategoryIds.Num();
		const int32 NumSlotsPerRow = UGameplayDebuggerLocalController::NumCategoriesPerRow;
		const int32 NumRows = (NumSlots + (NumSlotsPerRow-1)) / NumSlotsPerRow;

		const bool bIsLastRowActive = (Controller->ActiveRowIdx == NumRows-1);
		const int32 NumSlotsOnActiveRow = bIsLastRowActive ? NumSlots - (NumSlotsPerRow * (NumRows-1)) : NumSlotsPerRow;
		const int32 MaxSlotIdx = FMath::Max(0, FMath::Min(NumSlots, NumSlotsOnActiveRow)-1);
		
		if (!(Controller->SlotCategoryIds.IsValidIndex(SlotIdx) && SlotIdx <= MaxSlotIdx))
		{
			UE_LOG(LogConsoleResponse, Error, TEXT("Requires a category index in the active row [0..%d]. Usage: gdt.ToggleCategory CategoryIndex"), MaxSlotIdx);
			return;
		}
		
		Controller->ToggleSlotState((Controller->ActiveRowIdx * NumSlotsPerRow)+SlotIdx);
	}

	/** For legacy command: EnableGDT */
	static FAutoConsoleCommandWithWorld EnableDebuggerCmd;

	/** Various gameplay debugger commands: gdt.<command> */
	static FAutoConsoleCommandWithWorld ToggleDebuggerCmd;
	static FAutoConsoleCommandWithWorld SelectPreviousRowCmd;
	static FAutoConsoleCommandWithWorld SelectNextRowCmd;
	static FAutoConsoleCommandWithWorldAndArgs ToggleCategoryCmd;
};

FAutoConsoleCommandWithWorld FGameplayDebuggerConsoleCommands::EnableDebuggerCmd(
	TEXT("EnableGDT"),
	TEXT("Toggles Gameplay Debugger Tool"),
	FConsoleCommandWithWorldDelegate::CreateStatic(&FGameplayDebuggerConsoleCommands::ToggleGameplayDebugger)
);

FAutoConsoleCommandWithWorld FGameplayDebuggerConsoleCommands::ToggleDebuggerCmd(
	TEXT("gdt.Toggle"),
	TEXT("Toggles Gameplay Debugger Tool"),
	FConsoleCommandWithWorldDelegate::CreateStatic(&FGameplayDebuggerConsoleCommands::ToggleGameplayDebugger)
);

FAutoConsoleCommandWithWorld FGameplayDebuggerConsoleCommands::SelectPreviousRowCmd(
	TEXT("gdt.SelectPreviousRow"),
	TEXT("Selects previous row"),
	FConsoleCommandWithWorldDelegate::CreateStatic(FGameplayDebuggerConsoleCommands::SelectPreviousRow)
);

FAutoConsoleCommandWithWorld FGameplayDebuggerConsoleCommands::SelectNextRowCmd(
	TEXT("gdt.SelectNextRow"),
	TEXT("Selects next row"),
	FConsoleCommandWithWorldDelegate::CreateStatic(FGameplayDebuggerConsoleCommands::SelectNextRow)
);

FAutoConsoleCommandWithWorldAndArgs FGameplayDebuggerConsoleCommands::ToggleCategoryCmd(
	TEXT("gdt.ToggleCategory"),
	TEXT("Toggles specific category index"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FGameplayDebuggerConsoleCommands::ToggleCategory)
);
