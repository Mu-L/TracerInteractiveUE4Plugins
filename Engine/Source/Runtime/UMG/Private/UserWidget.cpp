// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Blueprint/UserWidget.h"
#include "Rendering/DrawElements.h"
#include "Sound/SoundBase.h"
#include "Sound/SlateSound.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Components/NamedSlot.h"
#include "Slate/SObjectWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Animation/UMGSequencePlayer.h"
#include "UObject/UnrealType.h"
#include "Blueprint/WidgetNavigation.h"
#include "Animation/WidgetAnimation.h"
#include "MovieScene.h"
#include "Interfaces/ITargetPlatform.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "UObject/EditorObjectVersion.h"
#include "UMGPrivate.h"
#include "UObject/UObjectHash.h"
#include "UObject/PropertyPortFlags.h"
#include "Compilation/MovieSceneCompiler.h"
#include "TimerManager.h"
#include "UObject/Package.h"
#include "Editor/WidgetCompilerLog.h"

#define LOCTEXT_NAMESPACE "UMG"

bool UUserWidget::bTemplateInitializing = false;
uint32 UUserWidget::bInitializingFromWidgetTree = 0;

static FGeometry NullGeometry;
static FSlateRect NullRect;
static FWidgetStyle NullStyle;

FSlateWindowElementList& GetNullElementList()
{
	static FSlateWindowElementList NullElementList;
	return NullElementList;
}

FPaintContext::FPaintContext()
	: AllottedGeometry(NullGeometry)
	, MyCullingRect(NullRect)
	, OutDrawElements(GetNullElementList())
	, LayerId(0)
	, WidgetStyle(NullStyle)
	, bParentEnabled(true)
	, MaxLayer(0)
{
}

/////////////////////////////////////////////////////
// UUserWidget
UUserWidget::UUserWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bHasScriptImplementedTick(true)
	, bHasScriptImplementedPaint(true)
	, bInitialized(false)
	, bStoppingAllAnimations(false)
	, TickFrequency(EWidgetTickFrequency::Auto)
{
	ViewportAnchors = FAnchors(0, 0, 1, 1);
	Visibility = ESlateVisibility::SelfHitTestInvisible;

	bSupportsKeyboardFocus_DEPRECATED = true;
	bIsFocusable = false;
	ColorAndOpacity = FLinearColor::White;
	ForegroundColor = FSlateColor::UseForeground();

	MinimumDesiredSize = FVector2D(0, 0);

#if WITH_EDITORONLY_DATA
	DesignTimeSize = FVector2D(100, 100);
	PaletteCategory = LOCTEXT("UserCreated", "User Created");
	DesignSizeMode = EDesignPreviewSizeMode::FillScreen;
#endif

	static bool bStaticInit = false;
	if (!bStaticInit)
	{
		bStaticInit = true;
		FLatentActionManager::OnLatentActionsChanged().AddStatic(&UUserWidget::OnLatentActionsChanged);
	}
}

UWidgetBlueprintGeneratedClass* UUserWidget::GetWidgetTreeOwningClass()
{
	UWidgetBlueprintGeneratedClass* WidgetClass = Cast<UWidgetBlueprintGeneratedClass>(GetClass());
	if (WidgetClass != nullptr)
	{
		WidgetClass = WidgetClass->FindWidgetTreeOwningClass();
	}

	return WidgetClass;
}

void UUserWidget::TemplateInit()
{
	TGuardValue<bool> InitGuard(bTemplateInitializing, true);
	TemplateInitInner();

	ForEachObjectWithOuter(this, [] (UObject* Child)
	{
		// Make sure to clear the entire hierarchy of the transient flag, we don't want some errant widget tree
		// to be culled from serialization accidentally.
		if ( UWidgetTree* InnerWidgetTree = Cast<UWidgetTree>(Child) )
		{
			InnerWidgetTree->ClearFlags(RF_Transient | RF_DefaultSubObject);
		}
	}, true);
}

void UUserWidget::TemplateInitInner()
{
	UWidgetBlueprintGeneratedClass* WidgetClass = GetWidgetTreeOwningClass();

	FObjectDuplicationParameters Parameters(WidgetClass->WidgetTree, this);
	Parameters.FlagMask = RF_Transactional;
	Parameters.PortFlags = PPF_DuplicateVerbatim;

	WidgetTree = (UWidgetTree*)StaticDuplicateObjectEx(Parameters);
	bCookedWidgetTree = true;

	if ( ensure(WidgetTree) )
	{
		WidgetTree->ForEachWidget([this, WidgetClass] (UWidget* Widget) {

#if !UE_BUILD_SHIPPING
			Widget->WidgetGeneratedByClass = WidgetClass;
#endif

			// TODO UMG Make this an FName
			FString VariableName = Widget->GetName();

			// Find property with the same name as the template and assign the new widget to it.
			UObjectPropertyBase* Prop = FindField<UObjectPropertyBase>(WidgetClass, *VariableName);
			if ( Prop )
			{
				Prop->SetObjectPropertyValue_InContainer(this, Widget);
#if UE_BUILD_DEBUG
				UObject* Value = Prop->GetObjectPropertyValue_InContainer(this);
				check(Value == Widget);
#endif
			}

			// Initialize Navigation Data
			if ( Widget->Navigation )
			{
				Widget->Navigation->ResolveRules(this, WidgetTree);
			}

			if ( UUserWidget* UserWidget = Cast<UUserWidget>(Widget) )
			{
				UserWidget->TemplateInitInner();
			}
		});

		// Initialize the named slots!
		const bool bReparentToWidgetTree = true;
		InitializeNamedSlots(bReparentToWidgetTree);
	}
}

bool UUserWidget::VerifyTemplateIntegrity(TArray<FText>& OutErrors)
{
	bool bIsTemplateSafe = true;

	//TODO This method is terrible, need to serialize the object checking that way!

	TArray<UObject*> ClonableSubObjectsSet;
	ClonableSubObjectsSet.Add(this);
	GetObjectsWithOuter(this, ClonableSubObjectsSet, true, RF_NoFlags, EInternalObjectFlags::PendingKill);

	TMap<FName, UObject*> QuickLookup;

	for ( UObject* Obj : ClonableSubObjectsSet )
	{
		QuickLookup.Add(Obj->GetFName(), Obj);

		for ( TFieldIterator<UObjectPropertyBase> PropIt(Obj->GetClass()); PropIt; ++PropIt )
		{
			UObjectPropertyBase* ObjProp = *PropIt;

			// If the property is transient, ignore it, we're not serializing it, so it shouldn't
			// be a problem if it's not instanced.
			if ( ObjProp->HasAnyPropertyFlags(CPF_Transient) )
			{
				continue;
			}

			UObject* ExternalObject = ObjProp->GetObjectPropertyValue_InContainer(Obj);

			// If the UObject property references any object in the tree, ensure that it's referenceable back.
			if ( ExternalObject )
			{
				if ( ExternalObject->IsIn(this) || ExternalObject == this )
				{
					if ( ObjProp->HasAllPropertyFlags(CPF_InstancedReference) )
					{
						continue;
					}

					OutErrors.Add(FText::Format(LOCTEXT("TemplatingFailed", "This class can not be created using the fast path, because the property {0} on {1} references {2}.  You probably are missing 'Instanced' or the 'Transient' flag on this property in C++."),
						FText::FromString(ObjProp->GetName()), FText::FromString(ObjProp->GetOwnerClass()->GetName()), FText::FromString(ExternalObject->GetName())));

					bIsTemplateSafe = false;
				}
			}
		}
	}

	// See if a matching name appeared
	if ( UWidgetBlueprintGeneratedClass* TemplateClass = GetWidgetTreeOwningClass() )
	{
		// This code is only functional in the editor, because we don't always have a widget tree on the class
		// in non-editor builds that tree is going to be transient for fast template code, so there won't be
		// a tree available in cooked builds.
		if (TemplateClass->WidgetTree != nullptr)
		{
			TemplateClass->WidgetTree->ForEachWidgetAndDescendants([&OutErrors, &QuickLookup, &bIsTemplateSafe, TemplateClass] (UWidget* Widget) {

				if ( !QuickLookup.Contains(Widget->GetFName()) )
				{
					OutErrors.Add(FText::Format(LOCTEXT("MissingOriginWidgetInTemplate", "Widget '{0}' Missing From Template For {1}."),
						FText::FromString(Widget->GetPathName(TemplateClass->WidgetTree)), FText::FromString(TemplateClass->GetName())));

					bIsTemplateSafe = false;
				}

			});
		}
	}

	return VerifyTemplateIntegrity(this, OutErrors) && bIsTemplateSafe;
}

bool UUserWidget::VerifyTemplateIntegrity(UUserWidget* TemplateRoot, TArray<FText>& OutErrors)
{
	bool bIsTemplateSafe = true;

	if ( WidgetTree == nullptr )
	{
		OutErrors.Add(FText::Format(LOCTEXT("NoWidgetTree", "Null Widget Tree {0}"), FText::FromString(GetName())));
		bIsTemplateSafe = false;
	}

	if ( bCookedWidgetTree == false )
	{
		OutErrors.Add(FText::Format(LOCTEXT("NoCookedWidgetTree", "No Cooked Widget Tree! {0}"), FText::FromString(GetName())));
		bIsTemplateSafe = false;
	}

	UClass* TemplateClass = GetClass();
	if ( WidgetTree != nullptr )
	{
		WidgetTree->ForEachWidget([this, TemplateClass, &bIsTemplateSafe, &OutErrors, TemplateRoot] (UWidget* Widget) {

			FName VariableFName = Widget->GetFName();

			// Find property with the same name as the template and assign the new widget to it.
			UObjectPropertyBase* Prop = FindField<UObjectPropertyBase>(TemplateClass, VariableFName);
			if ( Prop )
			{
				UObject* Value = Prop->GetObjectPropertyValue_InContainer(this);
				if ( Value != Widget )
				{
					OutErrors.Add(FText::Format(LOCTEXT("WidgetTreeVerify", "Property in widget template did not load correctly, {0}. Value was {1} but should have been {2}"),
						FText::FromName(Prop->GetFName()),
						FText::FromString(GetPathNameSafe(Value)),
						FText::FromString(GetPathNameSafe(Widget))
						));

					bIsTemplateSafe = false;
				}
			}

			UUserWidget* UserWidget = Cast<UUserWidget>(Widget);
			if ( UserWidget )
			{
				bIsTemplateSafe &= UserWidget->VerifyTemplateIntegrity(TemplateRoot, OutErrors);
			}
		});
	}

	return bIsTemplateSafe;
}

bool UUserWidget::CanInitialize() const
{
#if (WITH_EDITOR || UE_BUILD_DEBUG)
	if ( HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) )
	{
		return false;
	}

	// If this object is outered to an archetype or CDO, don't initialize the user widget.  That leads to a complex
	// and confusing serialization that when re-initialized later causes problems when copies of the template are made.
	for ( const UObjectBaseUtility* It = this; It; It = It->GetOuter() )
	{
		if ( It->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) )
		{
			return false;
		}
	}
#endif

	return true;
}

bool UUserWidget::Initialize()
{
	// We don't want to initialize the widgets going into the widget templates, they're being setup in a
	// different way, and don't need to be initialized in their template form.
	ensure(bTemplateInitializing == false);

	// If it's not initialized initialize it, as long as it's not the CDO, we never initialize the CDO.
	if ( !bInitialized && ensure(CanInitialize()) )
	{
		bInitialized = true;

		// If this is a sub-widget of another UserWidget, default designer flags and player context to match those of the owning widget
		if (UUserWidget* OwningUserWidget = GetTypedOuter<UUserWidget>())
		{
#if WITH_EDITOR
			SetDesignerFlags(OwningUserWidget->GetDesignerFlags());
#endif
			SetPlayerContext(OwningUserWidget->GetPlayerContext());
		}

		UWidgetBlueprintGeneratedClass* BGClass = Cast<UWidgetBlueprintGeneratedClass>(GetClass());
		if (BGClass && !BGClass->HasTemplate())
		{
			BGClass = GetWidgetTreeOwningClass();
		}

		// Only do this if this widget is of a blueprint class
		if (BGClass)
		{
			BGClass->InitializeWidget(this);
		}
		else
		{
			InitializeNativeClassData();
		}

		if ( WidgetTree == nullptr )
		{
			WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"), RF_Transient);
		}

		if ( bCookedWidgetTree == false )
		{
			WidgetTree->SetFlags(RF_Transient);

			const bool bReparentToWidgetTree = false;
			InitializeNamedSlots(bReparentToWidgetTree);
		}

		if (!IsDesignTime() && PlayerContext.IsValid())
		{
			NativeOnInitialized();
		}

		return true;
	}

	return false;
}

void UUserWidget::InitializeNamedSlots(bool bReparentToWidgetTree)
{
	for ( FNamedSlotBinding& Binding : NamedSlotBindings )
	{
		if ( UWidget* BindingContent = Binding.Content )
		{
			UObjectPropertyBase* NamedSlotProperty = FindField<UObjectPropertyBase>(GetClass(), Binding.Name);
			if ( ensure(NamedSlotProperty) )
			{
				UNamedSlot* NamedSlot = Cast<UNamedSlot>(NamedSlotProperty->GetObjectPropertyValue_InContainer(this));
				if ( ensure(NamedSlot) )
				{
					NamedSlot->ClearChildren();
					NamedSlot->AddChild(BindingContent);

					//if ( bReparentToWidgetTree )
					//{
					//	FName NewName = MakeUniqueObjectName(WidgetTree, BindingContent->GetClass(), BindingContent->GetFName());
					//	BindingContent->Rename(*NewName.ToString(), WidgetTree, REN_DontCreateRedirectors | REN_DoNotDirty);
					//}
				}
			}
		}
	}
}

void UUserWidget::DuplicateAndInitializeFromWidgetTree(UWidgetTree* InWidgetTree)
{
	TScopeCounter<uint32> ScopeInitializingFromWidgetTree(bInitializingFromWidgetTree);

	if ( ensure(InWidgetTree) )
	{
		FObjectDuplicationParameters Parameters(InWidgetTree, this);

		// Set to be transient and strip public flags
		Parameters.FlagMask = Parameters.FlagMask & ~( RF_Public | RF_DefaultSubObject );
		Parameters.DuplicateMode = EDuplicateMode::Normal;

		// After cloning, only apply transient and duplicate transient to the widget tree, otherwise
		// when we migrate objects editinlinenew properties they'll inherit transient/duptransient and fail
		// to be saved.
		WidgetTree = Cast<UWidgetTree>(StaticDuplicateObjectEx(Parameters));
		WidgetTree->SetFlags(RF_Transient | RF_DuplicateTransient);
	}
}

void UUserWidget::BeginDestroy()
{
	Super::BeginDestroy();

	//TODO: Investigate why this would ever be called directly, RemoveFromParent isn't safe to call during GC,
	// as the widget structure may be in a partially destroyed state.

	// If anyone ever calls BeginDestroy explicitly on a widget we need to immediately remove it from
	// the the parent as it may be owned currently by a slate widget.  As long as it's the viewport we're
	// fine.
	RemoveFromParent();

	// If it's not owned by the viewport we need to take more extensive measures.  If the GC widget still
	// exists after this point we should just reset the widget, which will forcefully cause the SObjectWidget
	// to lose access to this UObject.
	TSharedPtr<SObjectWidget> SafeGCWidget = MyGCWidget.Pin();
	if ( SafeGCWidget.IsValid() )
	{
		SafeGCWidget->ResetWidget();
	}
}

void UUserWidget::PostEditImport()
{
	Super::PostEditImport();

	//Initialize();
}

void UUserWidget::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);
	
	if ( bInitializingFromWidgetTree )
	{
		Initialize();
	}
}

void UUserWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	UWidget* RootWidget = GetRootWidget();
	if ( RootWidget )
	{
		RootWidget->ReleaseSlateResources(bReleaseChildren);
	}
}

void UUserWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	// We get the GCWidget directly because MyWidget could be the fullscreen host widget if we've been added
	// to the viewport.
	TSharedPtr<SObjectWidget> SafeGCWidget = MyGCWidget.Pin();
	if ( SafeGCWidget.IsValid() )
	{
		TAttribute<FLinearColor> ColorBinding = PROPERTY_BINDING(FLinearColor, ColorAndOpacity);
		TAttribute<FSlateColor> ForegroundColorBinding = PROPERTY_BINDING(FSlateColor, ForegroundColor);

		SafeGCWidget->SetColorAndOpacity(ColorBinding);
		SafeGCWidget->SetForegroundColor(ForegroundColorBinding);
		SafeGCWidget->SetPadding(Padding);
	}
}

void UUserWidget::SetColorAndOpacity(FLinearColor InColorAndOpacity)
{
	ColorAndOpacity = InColorAndOpacity;

	TSharedPtr<SObjectWidget> SafeGCWidget = MyGCWidget.Pin();
	if ( SafeGCWidget.IsValid() )
	{
		SafeGCWidget->SetColorAndOpacity(ColorAndOpacity);
	}
}

void UUserWidget::SetForegroundColor(FSlateColor InForegroundColor)
{
	ForegroundColor = InForegroundColor;

	TSharedPtr<SObjectWidget> SafeGCWidget = MyGCWidget.Pin();
	if ( SafeGCWidget.IsValid() )
	{
		SafeGCWidget->SetForegroundColor(ForegroundColor);
	}
}

void UUserWidget::SetPadding(FMargin InPadding)
{
	Padding = InPadding;

	TSharedPtr<SObjectWidget> SafeGCWidget = MyGCWidget.Pin();
	if ( SafeGCWidget.IsValid() )
	{
		SafeGCWidget->SetPadding(Padding);
	}
}

UWorld* UUserWidget::GetWorld() const
{
	if ( UWorld* LastWorld = CachedWorld.Get() )
	{
		return LastWorld;
	}

	if ( HasAllFlags(RF_ClassDefaultObject) )
	{
		// If we are a CDO, we must return nullptr instead of calling Outer->GetWorld() to fool UObject::ImplementsGetWorld.
		return nullptr;
	}

	// Use the Player Context's world, if a specific player context is given, otherwise fall back to
	// following the outer chain.
	if ( PlayerContext.IsValid() )
	{
		if ( UWorld* World = PlayerContext.GetWorld() )
		{
			CachedWorld = World;
			return World;
		}
	}

	// Could be a GameInstance, could be World, could also be a WidgetTree, so we're just going to follow
	// the outer chain to find the world we're in.
	UObject* Outer = GetOuter();

	while ( Outer )
	{
		UWorld* World = Outer->GetWorld();
		if ( World )
		{
			CachedWorld = World;
			return World;
		}

		Outer = Outer->GetOuter();
	}

	return nullptr;
}

UUMGSequencePlayer* UUserWidget::GetSequencePlayer(const UWidgetAnimation* InAnimation) const
{
	UUMGSequencePlayer*const* FoundPlayer = ActiveSequencePlayers.FindByPredicate(
		[&](const UUMGSequencePlayer* Player)
	{
		return Player->GetAnimation() == InAnimation;
	});

	return FoundPlayer ? *FoundPlayer : nullptr;
}

UUMGSequencePlayer* UUserWidget::GetOrAddSequencePlayer(UWidgetAnimation* InAnimation)
{
	if (InAnimation && !bStoppingAllAnimations)
	{
		// @todo UMG sequencer - Restart animations which have had Play called on them?
		UUMGSequencePlayer* FoundPlayer = nullptr;
		for (UUMGSequencePlayer* Player : ActiveSequencePlayers)
		{
			// We need to make sure we haven't stopped the animation, otherwise it'll get canceled on the next frame.
			if (Player->GetAnimation() == InAnimation
				&& !StoppedSequencePlayers.Contains(Player))
			{
				FoundPlayer = Player;
				break;
			}
		}

		if (!FoundPlayer)
		{
			UUMGSequencePlayer* NewPlayer = NewObject<UUMGSequencePlayer>(this, NAME_None, RF_Transient);
			ActiveSequencePlayers.Add(NewPlayer);

			NewPlayer->InitSequencePlayer(*InAnimation, *this);

			return NewPlayer;
		}
		else
		{
			return FoundPlayer;
		}
	}

	return nullptr;
}

void UUserWidget::Invalidate()
{
	Invalidate(EInvalidateWidget::LayoutAndVolatility);
}

void UUserWidget::Invalidate(EInvalidateWidget InvalidateReason)
{
	TSharedPtr<SWidget> CachedWidget = GetCachedWidget();
	if (CachedWidget.IsValid())
	{
		CachedWidget->Invalidate(InvalidateReason);
	}
}

UUMGSequencePlayer* UUserWidget::PlayAnimation(UWidgetAnimation* InAnimation, float StartAtTime, int32 NumberOfLoops, EUMGSequencePlayMode::Type PlayMode, float PlaybackSpeed)
{
	SCOPED_NAMED_EVENT_TEXT("Widget::PlayAnimation", FColor::Emerald);

	UUMGSequencePlayer* Player = GetOrAddSequencePlayer(InAnimation);
	if (Player)
	{
		Player->Play(StartAtTime, NumberOfLoops, PlayMode, PlaybackSpeed);

		Invalidate(EInvalidateWidget::Volatility);

		OnAnimationStartedPlaying(*Player);

		UpdateCanTick();
	}

	return Player;
}

UUMGSequencePlayer* UUserWidget::PlayAnimationTimeRange(UWidgetAnimation* InAnimation, float StartAtTime, float EndAtTime, int32 NumberOfLoops, EUMGSequencePlayMode::Type PlayMode, float PlaybackSpeed)
{
	SCOPED_NAMED_EVENT_TEXT("Widget::PlayAnimationTimeRange", FColor::Emerald);

	UUMGSequencePlayer* Player = GetOrAddSequencePlayer(InAnimation);
	if (Player)
	{
		Player->PlayTo(StartAtTime, EndAtTime, NumberOfLoops, PlayMode, PlaybackSpeed);

		Invalidate(EInvalidateWidget::Volatility);

		OnAnimationStartedPlaying(*Player);

		UpdateCanTick();
	}

	return Player;
}

UUMGSequencePlayer* UUserWidget::PlayAnimationForward(UWidgetAnimation* InAnimation, float PlaybackSpeed)
{
	// Don't create the player, only search for it.
	UUMGSequencePlayer* Player = GetSequencePlayer(InAnimation);
	if (Player)
	{
		if (!Player->IsPlayingForward())
		{
			// Reverse the direction we're playing the animation if we're playing it in reverse currently.
			Player->Reverse();
		}

		return Player;
	}

	return PlayAnimation(InAnimation, 0.0f, 1.0f, EUMGSequencePlayMode::Forward, PlaybackSpeed);
}

UUMGSequencePlayer* UUserWidget::PlayAnimationReverse(UWidgetAnimation* InAnimation, float PlaybackSpeed)
{
	// Don't create the player, only search for it.
	UUMGSequencePlayer* Player = GetSequencePlayer(InAnimation);
	if (Player)
	{
		if (Player->IsPlayingForward())
		{
			// Reverse the direction we're playing the animation if we're playing it in forward currently.
			Player->Reverse();
		}

		return Player;
	}

	return PlayAnimation(InAnimation, 0.0f, 1.0f, EUMGSequencePlayMode::Reverse, PlaybackSpeed);
}

void UUserWidget::StopAnimation(const UWidgetAnimation* InAnimation)
{
	if (InAnimation)
	{
		// @todo UMG sequencer - Restart animations which have had Play called on them?
		if (UUMGSequencePlayer* FoundPlayer = GetSequencePlayer(InAnimation))
		{
			FoundPlayer->Stop();

			UpdateCanTick();
		}
	}
}

void UUserWidget::StopAllAnimations()
{
	bStoppingAllAnimations = true;
	for (UUMGSequencePlayer* FoundPlayer : ActiveSequencePlayers)
	{
		if (FoundPlayer->GetPlaybackStatus() == EMovieScenePlayerStatus::Playing)
		{
			FoundPlayer->Stop();
		}
	}
	bStoppingAllAnimations = false;

	UpdateCanTick();
}

float UUserWidget::PauseAnimation(const UWidgetAnimation* InAnimation)
{
	if (InAnimation)
	{
		// @todo UMG sequencer - Restart animations which have had Play called on them?
		if (UUMGSequencePlayer* FoundPlayer = GetSequencePlayer(InAnimation))
		{
			FoundPlayer->Pause();
			return (float)FoundPlayer->GetCurrentTime().AsSeconds();
		}
	}

	return 0;
}

float UUserWidget::GetAnimationCurrentTime(const UWidgetAnimation* InAnimation) const
{
	if (InAnimation)
	{
		if (UUMGSequencePlayer* FoundPlayer = GetSequencePlayer(InAnimation))
		{
			return (float)FoundPlayer->GetCurrentTime().AsSeconds();
		}
	}

	return 0;
}

bool UUserWidget::IsAnimationPlaying(const UWidgetAnimation* InAnimation) const
{
	if (InAnimation)
	{
		if (UUMGSequencePlayer* FoundPlayer = GetSequencePlayer(InAnimation))
		{
			return FoundPlayer->GetPlaybackStatus() == EMovieScenePlayerStatus::Playing;
		}
	}

	return false;
}

bool UUserWidget::IsAnyAnimationPlaying() const
{
	return ActiveSequencePlayers.Num() > 0;
}

void UUserWidget::SetNumLoopsToPlay(const UWidgetAnimation* InAnimation, int32 InNumLoopsToPlay)
{
	if (UUMGSequencePlayer* FoundPlayer = GetSequencePlayer(InAnimation))
	{
		FoundPlayer->SetNumLoopsToPlay(InNumLoopsToPlay);
	}
}

void UUserWidget::SetPlaybackSpeed(const UWidgetAnimation* InAnimation, float PlaybackSpeed)
{
	if (UUMGSequencePlayer* FoundPlayer = GetSequencePlayer(InAnimation))
	{
		FoundPlayer->SetPlaybackSpeed(PlaybackSpeed);
	}
}

void UUserWidget::ReverseAnimation(const UWidgetAnimation* InAnimation)
{
	if (UUMGSequencePlayer* FoundPlayer = GetSequencePlayer(InAnimation))
	{
		FoundPlayer->Reverse();
	}
}

void UUserWidget::OnAnimationStartedPlaying(UUMGSequencePlayer& Player)
{
	OnAnimationStarted(Player.GetAnimation());

	BroadcastAnimationStateChange(Player, EWidgetAnimationEvent::Started);
}

bool UUserWidget::IsAnimationPlayingForward(const UWidgetAnimation* InAnimation)
{
	if (InAnimation)
	{
		UUMGSequencePlayer** FoundPlayer = ActiveSequencePlayers.FindByPredicate([&](const UUMGSequencePlayer* Player) { return Player->GetAnimation() == InAnimation; });

		if (FoundPlayer)
		{
			return (*FoundPlayer)->IsPlayingForward();
		}
	}

	return true;
}

void UUserWidget::OnAnimationFinishedPlaying(UUMGSequencePlayer& Player)
{
	// This event is called directly by the sequence player when the animation finishes.

	OnAnimationFinished(Player.GetAnimation());

	BroadcastAnimationStateChange(Player, EWidgetAnimationEvent::Finished);

	if ( Player.GetPlaybackStatus() == EMovieScenePlayerStatus::Stopped )
	{
		StoppedSequencePlayers.Add(&Player);
	}

	UpdateCanTick();
}

void UUserWidget::BroadcastAnimationStateChange(const UUMGSequencePlayer& Player, EWidgetAnimationEvent AnimationEvent)
{
	const UWidgetAnimation* Animation = Player.GetAnimation();

	// Make a temporary copy of the animation callbacks so that everyone gets a callback
	// even if they're removed as a result of other calls, we don't want order to matter here.
	TArray<FAnimationEventBinding> TempAnimationCallbacks = AnimationCallbacks;

	for (const FAnimationEventBinding& Binding : TempAnimationCallbacks)
	{
		if (Binding.Animation == Animation && Binding.AnimationEvent == AnimationEvent)
		{
			if (Binding.UserTag == NAME_None || Binding.UserTag == Player.GetUserTag())
			{
				Binding.Delegate.ExecuteIfBound();
			}
		}
	}
}

void UUserWidget::PlaySound(USoundBase* SoundToPlay)
{
	if (SoundToPlay)
	{
		FSlateSound NewSound;
		NewSound.SetResourceObject(SoundToPlay);
		FSlateApplication::Get().PlaySound(NewSound);
	}
}

UWidget* UUserWidget::GetWidgetHandle(TSharedRef<SWidget> InWidget)
{
	return WidgetTree->FindWidget(InWidget);
}

TSharedRef<SWidget> UUserWidget::RebuildWidget()
{
	check(!HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject));

	// In the event this widget is replaced in memory by the blueprint compiler update
	// the widget won't be properly initialized, so we ensure it's initialized and initialize
	// it if it hasn't been.
	if ( !bInitialized )
	{
		Initialize();
	}

	// Setup the player context on sub user widgets, if we have a valid context
	if (PlayerContext.IsValid())
	{
		WidgetTree->ForEachWidget([&] (UWidget* Widget) {
			if ( UUserWidget* UserWidget = Cast<UUserWidget>(Widget) )
			{
				UserWidget->SetPlayerContext(PlayerContext);
			}
		});
	}

	// Add the first component to the root of the widget surface.
	TSharedRef<SWidget> UserRootWidget = WidgetTree->RootWidget ? WidgetTree->RootWidget->TakeWidget() : TSharedRef<SWidget>(SNew(SSpacer));

	return UserRootWidget;
}

void UUserWidget::OnWidgetRebuilt()
{
	// When a user widget is rebuilt we can safely initialize the navigation now since all the slate
	// widgets should be held onto by a smart pointer at this point.
	WidgetTree->ForEachWidget([&] (UWidget* Widget) {
		Widget->BuildNavigation();
	});

	if (!IsDesignTime())
	{
		// Notify the widget to run per-construct.
		NativePreConstruct();

		// Notify the widget that it has been constructed.
		NativeConstruct();
	}
#if WITH_EDITOR
	else if ( HasAnyDesignerFlags(EWidgetDesignFlags::ExecutePreConstruct) )
	{
		bool bCanCallPreConstruct = true;
		if (UWidgetBlueprintGeneratedClass* GeneratedBPClass = Cast<UWidgetBlueprintGeneratedClass>(GetClass()))
		{
			bCanCallPreConstruct = GeneratedBPClass->bCanCallPreConstruct;
		}

		if (bCanCallPreConstruct)
		{
			NativePreConstruct();
		}
	}
#endif
}

TSharedPtr<SWidget> UUserWidget::GetSlateWidgetFromName(const FName& Name) const
{
	UWidget* WidgetObject = WidgetTree->FindWidget(Name);
	if ( WidgetObject )
	{
		return WidgetObject->GetCachedWidget();
	}

	return TSharedPtr<SWidget>();
}

UWidget* UUserWidget::GetWidgetFromName(const FName& Name) const
{
	return WidgetTree->FindWidget(Name);
}

void UUserWidget::GetSlotNames(TArray<FName>& SlotNames) const
{
	// Only do this if this widget is of a blueprint class
	if ( UWidgetBlueprintGeneratedClass* BGClass = Cast<UWidgetBlueprintGeneratedClass>(GetClass()) )
	{
		SlotNames.Append(BGClass->NamedSlots);
	}
	else // For non-blueprint widget blueprints we have to go through the widget tree to locate the named slots dynamically.
	{
		TArray<FName> NamedSlots;
		WidgetTree->ForEachWidget([&] (UWidget* Widget) {
			if ( Widget && Widget->IsA<UNamedSlot>() )
			{
				NamedSlots.Add(Widget->GetFName());
			}
		});
	}
}

UWidget* UUserWidget::GetContentForSlot(FName SlotName) const
{
	for ( const FNamedSlotBinding& Binding : NamedSlotBindings )
	{
		if ( Binding.Name == SlotName )
		{
			return Binding.Content;
		}
	}

	return nullptr;
}

void UUserWidget::SetContentForSlot(FName SlotName, UWidget* Content)
{
	bool bFoundExistingSlot = false;

	// Find the binding in the existing set and replace the content for that binding.
	for ( int32 BindingIndex = 0; BindingIndex < NamedSlotBindings.Num(); BindingIndex++ )
	{
		FNamedSlotBinding& Binding = NamedSlotBindings[BindingIndex];

		if ( Binding.Name == SlotName )
		{
			bFoundExistingSlot = true;

			if ( Content )
			{
				Binding.Content = Content;
			}
			else
			{
				NamedSlotBindings.RemoveAt(BindingIndex);
			}

			break;
		}
	}

	if ( !bFoundExistingSlot && Content )
	{
		// Add the new binding to the list of bindings.
		FNamedSlotBinding NewBinding;
		NewBinding.Name = SlotName;
		NewBinding.Content = Content;

		NamedSlotBindings.Add(NewBinding);
	}

	// Dynamically insert the new widget into the hierarchy if it exists.
	if ( WidgetTree )
	{
		UNamedSlot* NamedSlot = Cast<UNamedSlot>(WidgetTree->FindWidget(SlotName));
		if ( NamedSlot )
		{
			NamedSlot->ClearChildren();

			if ( Content )
			{
				NamedSlot->AddChild(Content);
			}
		}
	}
}

UWidget* UUserWidget::GetRootWidget() const
{
	if ( WidgetTree )
	{
		return WidgetTree->RootWidget;
	}

	return nullptr;
}

void UUserWidget::AddToViewport(int32 ZOrder)
{
	AddToScreen(nullptr, ZOrder);
}

bool UUserWidget::AddToPlayerScreen(int32 ZOrder)
{
	if ( ULocalPlayer* LocalPlayer = GetOwningLocalPlayer() )
	{
		AddToScreen(LocalPlayer, ZOrder);
		return true;
	}

	FMessageLog("PIE").Error(LOCTEXT("AddToPlayerScreen_NoPlayer", "AddToPlayerScreen Failed.  No Owning Player!"));
	return false;
}

void UUserWidget::AddToScreen(ULocalPlayer* Player, int32 ZOrder)
{
	if ( !FullScreenWidget.IsValid() )
	{
		if ( UPanelWidget* ParentPanel = GetParent() )
		{
			FMessageLog("PIE").Error(FText::Format(LOCTEXT("WidgetAlreadyHasParent", "The widget '{0}' already has a parent widget.  It can't also be added to the viewport!"),
				FText::FromString(GetClass()->GetName())));
			return;
		}

		// First create and initialize the variable so that users calling this function twice don't
		// attempt to add the widget to the viewport again.
		TSharedRef<SConstraintCanvas> FullScreenCanvas = SNew(SConstraintCanvas);
		FullScreenWidget = FullScreenCanvas;

		TSharedRef<SWidget> UserSlateWidget = TakeWidget();

		FullScreenCanvas->AddSlot()
			.Offset(BIND_UOBJECT_ATTRIBUTE(FMargin, GetFullScreenOffset))
			.Anchors(BIND_UOBJECT_ATTRIBUTE(FAnchors, GetAnchorsInViewport))
			.Alignment(BIND_UOBJECT_ATTRIBUTE(FVector2D, GetAlignmentInViewport))
			[
				UserSlateWidget
			];

		// If this is a game world add the widget to the current worlds viewport.
		UWorld* World = GetWorld();
		if ( World && World->IsGameWorld() )
		{
			if ( UGameViewportClient* ViewportClient = World->GetGameViewport() )
			{
				if ( Player )
				{
					ViewportClient->AddViewportWidgetForPlayer(Player, FullScreenCanvas, ZOrder);
				}
				else
				{
					// We add 10 to the zorder when adding to the viewport to avoid 
					// displaying below any built-in controls, like the virtual joysticks on mobile builds.
					ViewportClient->AddViewportWidgetContent(FullScreenCanvas, ZOrder + 10);
				}

				// Just in case we already hooked this delegate, remove the handler.
				FWorldDelegates::LevelRemovedFromWorld.RemoveAll(this);

				// Widgets added to the viewport are automatically removed if the persistent level is unloaded.
				FWorldDelegates::LevelRemovedFromWorld.AddUObject(this, &UUserWidget::OnLevelRemovedFromWorld);
			}
		}
	}
	else
	{
		FMessageLog("PIE").Warning(FText::Format(LOCTEXT("WidgetAlreadyOnScreen", "The widget '{0}' was already added to the screen."),
			FText::FromString(GetClass()->GetName())));
	}
}

void UUserWidget::OnLevelRemovedFromWorld(ULevel* InLevel, UWorld* InWorld)
{
	// If the InLevel is null, it's a signal that the entire world is about to disappear, so
	// go ahead and remove this widget from the viewport, it could be holding onto too many
	// dangerous actor references that won't carry over into the next world.
	if ( InLevel == nullptr && InWorld == GetWorld() )
	{
		RemoveFromParent();
	}
}

void UUserWidget::RemoveFromViewport()
{
	RemoveFromParent();
}

void UUserWidget::RemoveFromParent()
{
	if (!HasAnyFlags(RF_BeginDestroyed))
	{
		if (FullScreenWidget.IsValid())
		{
			TSharedPtr<SWidget> WidgetHost = FullScreenWidget.Pin();

			// If this is a game world add the widget to the current worlds viewport.
			UWorld* World = GetWorld();
			if (World && World->IsGameWorld())
			{
				if (UGameViewportClient* ViewportClient = World->GetGameViewport())
				{
					TSharedRef<SWidget> WidgetHostRef = WidgetHost.ToSharedRef();

					ViewportClient->RemoveViewportWidgetContent(WidgetHostRef);

					if (ULocalPlayer* LocalPlayer = GetOwningLocalPlayer())
					{
						ViewportClient->RemoveViewportWidgetForPlayer(LocalPlayer, WidgetHostRef);
					}

					FWorldDelegates::LevelRemovedFromWorld.RemoveAll(this);
				}
			}
		}
		else
		{
			Super::RemoveFromParent();
		}
	}
}

bool UUserWidget::GetIsVisible() const
{
	return FullScreenWidget.IsValid();
}

bool UUserWidget::IsInViewport() const
{
	return FullScreenWidget.IsValid();
}

void UUserWidget::SetPlayerContext(const FLocalPlayerContext& InPlayerContext)
{
	PlayerContext = InPlayerContext;

	if (WidgetTree)
	{
		WidgetTree->ForEachWidget(
			[&InPlayerContext] (UWidget* Widget) 
			{
				if (UUserWidget* UserWidget = Cast<UUserWidget>(Widget))
				{
					UserWidget->SetPlayerContext(InPlayerContext);
				}
			});
	}
	
}

const FLocalPlayerContext& UUserWidget::GetPlayerContext() const
{
	return PlayerContext;
}

ULocalPlayer* UUserWidget::GetOwningLocalPlayer() const
{
	if (PlayerContext.IsValid())
	{
		return PlayerContext.GetLocalPlayer();
	}
	return nullptr;
}

void UUserWidget::SetOwningLocalPlayer(ULocalPlayer* LocalPlayer)
{
	if ( LocalPlayer )
	{
		PlayerContext = FLocalPlayerContext(LocalPlayer, GetWorld());
	}
}

APlayerController* UUserWidget::GetOwningPlayer() const
{
	return PlayerContext.IsValid() ? PlayerContext.GetPlayerController() : nullptr;
}

void UUserWidget::SetOwningPlayer(APlayerController* LocalPlayerController)
{
	if (LocalPlayerController && LocalPlayerController->IsLocalController())
	{
		PlayerContext = FLocalPlayerContext(LocalPlayerController);
	}
}

class APawn* UUserWidget::GetOwningPlayerPawn() const
{
	if ( APlayerController* PC = GetOwningPlayer() )
	{
		return PC->GetPawn();
	}

	return nullptr;
}

void UUserWidget::SetPositionInViewport(FVector2D Position, bool bRemoveDPIScale )
{
	if ( bRemoveDPIScale )
	{
		float Scale = UWidgetLayoutLibrary::GetViewportScale(this);

		ViewportOffsets.Left = Position.X / Scale;
		ViewportOffsets.Top = Position.Y / Scale;
	}
	else
	{
		ViewportOffsets.Left = Position.X;
		ViewportOffsets.Top = Position.Y;
	}

	ViewportAnchors = FAnchors(0, 0);
}

void UUserWidget::SetDesiredSizeInViewport(FVector2D DesiredSize)
{
	ViewportOffsets.Right = DesiredSize.X;
	ViewportOffsets.Bottom = DesiredSize.Y;

	ViewportAnchors = FAnchors(0, 0);
}

void UUserWidget::SetAnchorsInViewport(FAnchors Anchors)
{
	ViewportAnchors = Anchors;
}

void UUserWidget::SetAlignmentInViewport(FVector2D Alignment)
{
	ViewportAlignment = Alignment;
}

FMargin UUserWidget::GetFullScreenOffset() const
{
	// If the size is zero, and we're not stretched, then use the desired size.
	FVector2D FinalSize = FVector2D(ViewportOffsets.Right, ViewportOffsets.Bottom);
	if ( FinalSize.IsZero() && !ViewportAnchors.IsStretchedVertical() && !ViewportAnchors.IsStretchedHorizontal() )
	{
		TSharedPtr<SWidget> CachedWidget = GetCachedWidget();
		if ( CachedWidget.IsValid() )
		{
			FinalSize = CachedWidget->GetDesiredSize();
		}
	}

	return FMargin(ViewportOffsets.Left, ViewportOffsets.Top, FinalSize.X, FinalSize.Y);
}

FAnchors UUserWidget::GetAnchorsInViewport() const
{
	return ViewportAnchors;
}

FVector2D UUserWidget::GetAlignmentInViewport() const
{
	return ViewportAlignment;
}

void UUserWidget::RemoveObsoleteBindings(const TArray<FName>& NamedSlots)
{
	for (int32 BindingIndex = 0; BindingIndex < NamedSlotBindings.Num(); BindingIndex++)
	{
		const FNamedSlotBinding& Binding = NamedSlotBindings[BindingIndex];

		if (!NamedSlots.Contains(Binding.Name))
		{
			NamedSlotBindings.RemoveAt(BindingIndex);
			BindingIndex--;
		}
	}
}

#if WITH_EDITOR

const FText UUserWidget::GetPaletteCategory()
{
	return PaletteCategory;
}

void UUserWidget::SetDesignerFlags(EWidgetDesignFlags::Type NewFlags)
{
	UWidget::SetDesignerFlags(NewFlags);

	if (WidgetTree)
	{
		if (WidgetTree->RootWidget)
		{
			WidgetTree->RootWidget->SetDesignerFlags(NewFlags);
		}
	}
}

void UUserWidget::OnDesignerChanged(const FDesignerChangedEventArgs& EventArgs)
{
	Super::OnDesignerChanged(EventArgs);

	if ( ensure(WidgetTree) )
	{
		WidgetTree->ForEachWidget([&EventArgs] (UWidget* Widget) {
			Widget->OnDesignerChanged(EventArgs);
		});
	}
}

void UUserWidget::ValidateBlueprint(const UWidgetTree& BlueprintWidgetTree, IWidgetCompilerLog& CompileLog) const
{
	ValidateCompiledDefaults(CompileLog);
	ValidateCompiledWidgetTree(BlueprintWidgetTree, CompileLog);
	BlueprintWidgetTree.ForEachWidget(
		[&CompileLog] (UWidget* Widget)
		{
			Widget->ValidateCompiledDefaults(CompileLog);
		});
}

void UUserWidget::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if ( PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive )
	{
		TSharedPtr<SWidget> SafeWidget = GetCachedWidget();
		if ( SafeWidget.IsValid() )
		{
			// Re-Run execute PreConstruct when we get a post edit property change, to do something
			// akin to running Sync Properties, so users don't have to recompile to see updates.
			NativePreConstruct();
		}
	}
}

#endif

void UUserWidget::OnAnimationStarted_Implementation(const UWidgetAnimation* Animation)
{

}

void UUserWidget::OnAnimationFinished_Implementation(const UWidgetAnimation* Animation)
{

}

void UUserWidget::BindToAnimationStarted(UWidgetAnimation* InAnimation, FWidgetAnimationDynamicEvent InDelegate)
{
	FAnimationEventBinding Binding;
	Binding.Animation = InAnimation;
	Binding.Delegate = InDelegate;
	Binding.AnimationEvent = EWidgetAnimationEvent::Started;

	AnimationCallbacks.Add(Binding);
}

void UUserWidget::UnbindFromAnimationStarted(UWidgetAnimation* InAnimation, FWidgetAnimationDynamicEvent InDelegate)
{
	AnimationCallbacks.RemoveAll([InAnimation, &InDelegate](FAnimationEventBinding& InBinding) {
		return InBinding.Animation == InAnimation && InBinding.Delegate == InDelegate && InBinding.AnimationEvent == EWidgetAnimationEvent::Started;
	});
}

void UUserWidget::UnbindAllFromAnimationStarted(UWidgetAnimation* InAnimation)
{
	AnimationCallbacks.RemoveAll([InAnimation](FAnimationEventBinding& InBinding) {
		return InBinding.Animation == InAnimation && InBinding.AnimationEvent == EWidgetAnimationEvent::Started;
	});
}

void UUserWidget::UnbindAllFromAnimationFinished(UWidgetAnimation* InAnimation)
{
	AnimationCallbacks.RemoveAll([InAnimation](FAnimationEventBinding& InBinding) {
		return InBinding.Animation == InAnimation && InBinding.AnimationEvent == EWidgetAnimationEvent::Finished;
	});
}

void UUserWidget::BindToAnimationFinished(UWidgetAnimation* InAnimation, FWidgetAnimationDynamicEvent InDelegate)
{
	FAnimationEventBinding Binding;
	Binding.Animation = InAnimation;
	Binding.Delegate = InDelegate;
	Binding.AnimationEvent = EWidgetAnimationEvent::Finished;

	AnimationCallbacks.Add(Binding);
}

void UUserWidget::UnbindFromAnimationFinished(UWidgetAnimation* InAnimation, FWidgetAnimationDynamicEvent InDelegate)
{
	AnimationCallbacks.RemoveAll([InAnimation, &InDelegate](FAnimationEventBinding& InBinding) {
		return InBinding.Animation == InAnimation && InBinding.Delegate == InDelegate && InBinding.AnimationEvent == EWidgetAnimationEvent::Finished;
	});
}

void UUserWidget::BindToAnimationEvent(UWidgetAnimation* InAnimation, FWidgetAnimationDynamicEvent InDelegate, EWidgetAnimationEvent AnimationEvent, FName UserTag)
{
	FAnimationEventBinding Binding;
	Binding.Animation = InAnimation;
	Binding.Delegate = InDelegate;
	Binding.AnimationEvent = AnimationEvent;
	Binding.UserTag = UserTag;

	AnimationCallbacks.Add(Binding);
}

// Native handling for SObjectWidget

void UUserWidget::NativeOnInitialized()
{
	OnInitialized();
}

void UUserWidget::NativePreConstruct()
{
	PreConstruct(IsDesignTime());
}

void UUserWidget::NativeConstruct()
{
	Construct();
	UpdateCanTick();
}

void UUserWidget::NativeDestruct()
{
	StopListeningForAllInputActions();
	Destruct();
}

void UUserWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{ 
	// If this ensure is hit it is likely UpdateCanTick as not called somewhere
	if(ensureMsgf(TickFrequency != EWidgetTickFrequency::Never, TEXT("SObjectWidget and UUserWidget have mismatching tick states or UUserWidget::NativeTick was called manually (Never do this)")))
	{
		GInitRunaway();

		TickActionsAndAnimation(MyGeometry, InDeltaTime);

		if (bHasScriptImplementedTick)
		{
			Tick(MyGeometry, InDeltaTime);
		}
	}
}

void UUserWidget::TickActionsAndAnimation(const FGeometry& MyGeometry, float InDeltaTime)
{
#if WITH_EDITOR
	if ( IsDesignTime() )
	{
		return;
	}
#endif

	// Update active movie scenes, none will be removed here, but new
	// ones can be added during the tick, if a player ends and triggers
	// starting another animation
	for ( int32 Index = 0; Index < ActiveSequencePlayers.Num(); Index++ )
	{
		UUMGSequencePlayer* Player = ActiveSequencePlayers[Index];
		Player->Tick( InDeltaTime );
	}

	const bool bWasPlayingAnimation = IsPlayingAnimation();

	// The process of ticking the players above can stop them so we remove them after all players have ticked
	for ( UUMGSequencePlayer* StoppedPlayer : StoppedSequencePlayers )
	{
		ActiveSequencePlayers.RemoveSwap(StoppedPlayer);
	}

	StoppedSequencePlayers.Empty();

	// If we're no longer playing animations invalidate layout so that we recache the volatility of the widget.
	if ( bWasPlayingAnimation && IsPlayingAnimation() == false )
	{
		Invalidate(EInvalidateWidget::Volatility);
	}

	UWorld* World = GetWorld();
	if (World)
	{
		// Update any latent actions we have for this actor
		World->GetLatentActionManager().ProcessLatentActions(this, InDeltaTime);
	}
}

void UUserWidget::CancelLatentActions()
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetLatentActionManager().RemoveActionsForObject(this);
		World->GetTimerManager().ClearAllTimersForObject(this);
	}
}

void UUserWidget::StopAnimationsAndLatentActions()
{
	StopAllAnimations();
	CancelLatentActions();
}

void UUserWidget::ListenForInputAction( FName ActionName, TEnumAsByte< EInputEvent > EventType, bool bConsume, FOnInputAction Callback )
{
	if ( !InputComponent )
	{
		InitializeInputComponent();
	}

	if ( InputComponent )
	{
		FInputActionBinding NewBinding( ActionName, EventType.GetValue() );
		NewBinding.bConsumeInput = bConsume;
		NewBinding.ActionDelegate.GetDelegateForManualSet().BindUObject( this, &ThisClass::OnInputAction, Callback );

		InputComponent->AddActionBinding( NewBinding );
	}
}

void UUserWidget::StopListeningForInputAction( FName ActionName, TEnumAsByte< EInputEvent > EventType )
{
	if ( InputComponent )
	{
		for ( int32 ExistingIndex = InputComponent->GetNumActionBindings() - 1; ExistingIndex >= 0; --ExistingIndex )
		{
			const FInputActionBinding& ExistingBind = InputComponent->GetActionBinding( ExistingIndex );
			if ( ExistingBind.GetActionName() == ActionName && ExistingBind.KeyEvent == EventType )
			{
				InputComponent->RemoveActionBinding( ExistingIndex );
			}
		}
	}
}

void UUserWidget::StopListeningForAllInputActions()
{
	if ( InputComponent )
	{
		for ( int32 ExistingIndex = InputComponent->GetNumActionBindings() - 1; ExistingIndex >= 0; --ExistingIndex )
		{
			InputComponent->RemoveActionBinding( ExistingIndex );
		}

		UnregisterInputComponent();

		InputComponent->ClearActionBindings();
		InputComponent->MarkPendingKill();
		InputComponent = nullptr;
	}
}

bool UUserWidget::IsListeningForInputAction( FName ActionName ) const
{
	bool bResult = false;
	if ( InputComponent )
	{
		for ( int32 ExistingIndex = InputComponent->GetNumActionBindings() - 1; ExistingIndex >= 0; --ExistingIndex )
		{
			const FInputActionBinding& ExistingBind = InputComponent->GetActionBinding( ExistingIndex );
			if ( ExistingBind.GetActionName() == ActionName )
			{
				bResult = true;
				break;
			}
		}
	}

	return bResult;
}

void UUserWidget::RegisterInputComponent()
{
	if ( InputComponent )
	{
		if ( APlayerController* Controller = GetOwningPlayer() )
		{
			Controller->PushInputComponent(InputComponent);
		}
	}
}

void UUserWidget::UnregisterInputComponent()
{
	if ( InputComponent )
	{
		if ( APlayerController* Controller = GetOwningPlayer() )
		{
			Controller->PopInputComponent(InputComponent);
		}
	}
}

void UUserWidget::SetInputActionPriority( int32 NewPriority )
{
	if ( InputComponent )
	{
		Priority = NewPriority;
		InputComponent->Priority = Priority;
	}
}

void UUserWidget::SetInputActionBlocking( bool bShouldBlock )
{
	if ( InputComponent )
	{
		bStopAction = bShouldBlock;
		InputComponent->bBlockInput = bStopAction;
	}
}

void UUserWidget::OnInputAction( FOnInputAction Callback )
{
	if ( GetIsEnabled() )
	{
		Callback.ExecuteIfBound();
	}
}

void UUserWidget::InitializeInputComponent()
{
	if ( APlayerController* Controller = GetOwningPlayer() )
	{
		InputComponent = NewObject< UInputComponent >( this, NAME_None, RF_Transient );
		InputComponent->bBlockInput = bStopAction;
		InputComponent->Priority = Priority;
		Controller->PushInputComponent( InputComponent );
	}
	else
	{
		FMessageLog("PIE").Info(FText::Format(LOCTEXT("NoInputListeningWithoutPlayerController", "Unable to listen to input actions without a player controller in {0}."), FText::FromName(GetClass()->GetFName())));
	}
}

void UUserWidget::UpdateCanTick() 
{
	TSharedPtr<SObjectWidget> SafeGCWidget = MyGCWidget.Pin();
	UWorld* World = GetWorld();

	if(SafeGCWidget.IsValid() && World)
	{
		// Default to never tick, only recompute for auto
		bool bCanTick = false;
		if (TickFrequency == EWidgetTickFrequency::Auto)
		{
			// Note: WidgetBPClass can be NULL in a cooked build, if the Blueprint has been nativized (in that case, it will be a UDynamicClass type).
			UWidgetBlueprintGeneratedClass* WidgetBPClass = Cast<UWidgetBlueprintGeneratedClass>(GetClass());
			bCanTick |= !WidgetBPClass || WidgetBPClass->ClassRequiresNativeTick();
			bCanTick |= bHasScriptImplementedTick;
			bCanTick |= World->GetLatentActionManager().GetNumActionsForObject(this) != 0;
			bCanTick |= ActiveSequencePlayers.Num() > 0;
		}

		SafeGCWidget->SetCanTick(bCanTick);
	}
}

int32 UUserWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const
{
	if ( bHasScriptImplementedPaint )
	{
		FPaintContext Context(AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
		OnPaint( Context );

		return FMath::Max(LayerId, Context.MaxLayer);
	}

	return LayerId;
}

void UUserWidget::SetMinimumDesiredSize(FVector2D InMinimumDesiredSize)
{
	if (MinimumDesiredSize != InMinimumDesiredSize)
	{
		MinimumDesiredSize = InMinimumDesiredSize;

		TSharedPtr<SWidget> CachedWidget = GetCachedWidget();
		if (CachedWidget.IsValid())
		{
			CachedWidget->Invalidate(EInvalidateWidget::Layout);
		}
	}
}

bool UUserWidget::NativeIsInteractable() const
{
	return IsInteractable();
}

bool UUserWidget::NativeSupportsKeyboardFocus() const
{
	return bIsFocusable;
}

FReply UUserWidget::NativeOnFocusReceived( const FGeometry& InGeometry, const FFocusEvent& InFocusEvent )
{
	return OnFocusReceived( InGeometry, InFocusEvent ).NativeReply;
}

void UUserWidget::NativeOnFocusLost( const FFocusEvent& InFocusEvent )
{
	OnFocusLost( InFocusEvent );
}

void UUserWidget::NativeOnFocusChanging(const FWeakWidgetPath& PreviousFocusPath, const FWidgetPath& NewWidgetPath, const FFocusEvent& InFocusEvent)
{
	TSharedPtr<SObjectWidget> SafeGCWidget = MyGCWidget.Pin();
	if ( SafeGCWidget.IsValid() )
	{
		const bool bDecendantNewlyFocused = NewWidgetPath.ContainsWidget(SafeGCWidget.ToSharedRef());
		if ( bDecendantNewlyFocused )
		{
			const bool bDecendantPreviouslyFocused = PreviousFocusPath.ContainsWidget(SafeGCWidget.ToSharedRef());
			if ( !bDecendantPreviouslyFocused )
			{
				NativeOnAddedToFocusPath( InFocusEvent );
			}
		}
		else
		{
			NativeOnRemovedFromFocusPath( InFocusEvent );
		}
	}
}

void UUserWidget::NativeOnAddedToFocusPath(const FFocusEvent& InFocusEvent)
{
	OnAddedToFocusPath(InFocusEvent);
}

void UUserWidget::NativeOnRemovedFromFocusPath(const FFocusEvent& InFocusEvent)
{
	OnRemovedFromFocusPath(InFocusEvent);
}

FNavigationReply UUserWidget::NativeOnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent, const FNavigationReply& InDefaultReply)
{
	// No Blueprint Support At This Time

	return InDefaultReply;
}

FReply UUserWidget::NativeOnKeyChar( const FGeometry& InGeometry, const FCharacterEvent& InCharEvent )
{
	return OnKeyChar( InGeometry, InCharEvent ).NativeReply;
}

FReply UUserWidget::NativeOnPreviewKeyDown( const FGeometry& InGeometry, const FKeyEvent& InKeyEvent )
{
	return OnPreviewKeyDown( InGeometry, InKeyEvent ).NativeReply;
}

FReply UUserWidget::NativeOnKeyDown( const FGeometry& InGeometry, const FKeyEvent& InKeyEvent )
{
	return OnKeyDown( InGeometry, InKeyEvent ).NativeReply;
}

FReply UUserWidget::NativeOnKeyUp( const FGeometry& InGeometry, const FKeyEvent& InKeyEvent )
{
	return OnKeyUp( InGeometry, InKeyEvent ).NativeReply;
}

FReply UUserWidget::NativeOnAnalogValueChanged( const FGeometry& InGeometry, const FAnalogInputEvent& InAnalogEvent )
{
	return OnAnalogValueChanged( InGeometry, InAnalogEvent ).NativeReply;
}

FReply UUserWidget::NativeOnMouseButtonDown( const FGeometry& InGeometry, const FPointerEvent& InMouseEvent )
{
	return OnMouseButtonDown( InGeometry, InMouseEvent ).NativeReply;
}

FReply UUserWidget::NativeOnPreviewMouseButtonDown( const FGeometry& InGeometry, const FPointerEvent& InMouseEvent )
{
	return OnPreviewMouseButtonDown( InGeometry, InMouseEvent ).NativeReply;
}

FReply UUserWidget::NativeOnMouseButtonUp( const FGeometry& InGeometry, const FPointerEvent& InMouseEvent )
{
	return OnMouseButtonUp(InGeometry, InMouseEvent).NativeReply;
}

FReply UUserWidget::NativeOnMouseMove( const FGeometry& InGeometry, const FPointerEvent& InMouseEvent )
{
	return OnMouseMove( InGeometry, InMouseEvent ).NativeReply;
}

void UUserWidget::NativeOnMouseEnter( const FGeometry& InGeometry, const FPointerEvent& InMouseEvent )
{
	OnMouseEnter( InGeometry, InMouseEvent );
}

void UUserWidget::NativeOnMouseLeave( const FPointerEvent& InMouseEvent )
{
	OnMouseLeave( InMouseEvent );
}

FReply UUserWidget::NativeOnMouseWheel( const FGeometry& InGeometry, const FPointerEvent& InMouseEvent )
{
	return OnMouseWheel( InGeometry, InMouseEvent ).NativeReply;
}

FReply UUserWidget::NativeOnMouseButtonDoubleClick( const FGeometry& InGeometry, const FPointerEvent& InMouseEvent )
{
	return OnMouseButtonDoubleClick( InGeometry, InMouseEvent ).NativeReply;
}

void UUserWidget::NativeOnDragDetected( const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation )
{
	OnDragDetected( InGeometry, InMouseEvent, OutOperation);
}

void UUserWidget::NativeOnDragEnter( const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation )
{
	OnDragEnter( InGeometry, InDragDropEvent, InOperation );
}

void UUserWidget::NativeOnDragLeave( const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation )
{
	OnDragLeave( InDragDropEvent, InOperation );
}

bool UUserWidget::NativeOnDragOver( const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation )
{
	return OnDragOver( InGeometry, InDragDropEvent, InOperation );
}

bool UUserWidget::NativeOnDrop( const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation )
{
	return OnDrop( InGeometry, InDragDropEvent, InOperation );
}

void UUserWidget::NativeOnDragCancelled( const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation )
{
	OnDragCancelled( InDragDropEvent, InOperation );
}

FReply UUserWidget::NativeOnTouchGesture( const FGeometry& InGeometry, const FPointerEvent& InGestureEvent )
{
	return OnTouchGesture( InGeometry, InGestureEvent ).NativeReply;
}

FReply UUserWidget::NativeOnTouchStarted( const FGeometry& InGeometry, const FPointerEvent& InGestureEvent )
{
	return OnTouchStarted( InGeometry, InGestureEvent ).NativeReply;
}

FReply UUserWidget::NativeOnTouchMoved( const FGeometry& InGeometry, const FPointerEvent& InGestureEvent )
{
	return OnTouchMoved( InGeometry, InGestureEvent ).NativeReply;
}

FReply UUserWidget::NativeOnTouchEnded( const FGeometry& InGeometry, const FPointerEvent& InGestureEvent )
{
	return OnTouchEnded( InGeometry, InGestureEvent ).NativeReply;
}

FReply UUserWidget::NativeOnMotionDetected( const FGeometry& InGeometry, const FMotionEvent& InMotionEvent )
{
	return OnMotionDetected( InGeometry, InMotionEvent ).NativeReply;
}

FReply UUserWidget::NativeOnTouchForceChanged(const FGeometry& InGeometry, const FPointerEvent& InTouchEvent)
{
	return OnTouchForceChanged(InGeometry, InTouchEvent).NativeReply;
}

FCursorReply UUserWidget::NativeOnCursorQuery( const FGeometry& InGeometry, const FPointerEvent& InCursorEvent )
{
	return FCursorReply::Unhandled();
}

FNavigationReply UUserWidget::NativeOnNavigation(const FGeometry& InGeometry, const FNavigationEvent& InNavigationEvent)
{
	return FNavigationReply::Escape();
}
	
void UUserWidget::NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	OnMouseCaptureLost();
}

bool UUserWidget::ShouldSerializeWidgetTree(const ITargetPlatform* TargetPlatform) const
{
	// Never save the widget tree of something on the CDO.
	if (HasAllFlags(RF_ClassDefaultObject))
	{
		return false;
	}

	// We preserve widget trees on Archetypes (that are not the CDO).
	if (HasAllFlags(RF_ArchetypeObject))
	{
		if (UWidgetBlueprintGeneratedClass* BPWidgetClass = Cast<UWidgetBlueprintGeneratedClass>(GetClass()))
		{
			if (BPWidgetClass->HasTemplate())
			{
				return true;
			}
		}
	}

	// We preserve widget trees if you're a sub-object of an archetype that is going to serialize it's
	// widget tree.
	for (const UObject* It = GetOuter(); It; It = It->GetOuter())
	{
		if (It->HasAllFlags(RF_ArchetypeObject))
		{
			if (const UUserWidget* OuterWidgetArchetype = Cast<UUserWidget>(It))
			{
				if (OuterWidgetArchetype->ShouldSerializeWidgetTree(TargetPlatform))
				{
					return true;
				}
			}
		}
	}

	return false;
}

bool UUserWidget::IsAsset() const
{
	// This stops widget archetypes from showing up in the content browser
	return false;
}

void UUserWidget::PreSave(const class ITargetPlatform* TargetPlatform)
{
	if ( WidgetTree )
	{
		if ( ShouldSerializeWidgetTree(TargetPlatform) )
		{
			bCookedWidgetTree = true;
			WidgetTree->ClearFlags(RF_Transient);
		}
		else
		{
			bCookedWidgetTree = false;
			WidgetTree->SetFlags(RF_Transient);
		}
	}
	else
	{
		bCookedWidgetTree = false;
		if (ShouldSerializeWidgetTree(TargetPlatform))
		{
			UE_LOG(LogUMG, Error, TEXT("PreSave: Null Widget Tree - %s"), *GetFullName());
		}
	}

	// Remove bindings that are no longer contained in the class.
	if ( UWidgetBlueprintGeneratedClass* BGClass = Cast<UWidgetBlueprintGeneratedClass>(GetClass()) )
	{
		RemoveObsoleteBindings(BGClass->NamedSlots);
	}

	Super::PreSave(TargetPlatform);
}

void UUserWidget::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITOR
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		UUserWidget* DefaultWidget = Cast<UUserWidget>(GetClass()->GetDefaultObject());
		bHasScriptImplementedTick = DefaultWidget->bHasScriptImplementedTick;
		bHasScriptImplementedPaint = DefaultWidget->bHasScriptImplementedPaint;
	}
#else
	if ( HasAnyFlags(RF_ArchetypeObject) && !HasAllFlags(RF_ClassDefaultObject) )
	{
		if ( UWidgetBlueprintGeneratedClass* WidgetClass = Cast<UWidgetBlueprintGeneratedClass>(GetClass()) )
		{
			WidgetClass->SetTemplate(this);
		}
	}
#endif
}

void UUserWidget::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FEditorObjectVersion::GUID);

	if ( Ar.IsLoading() )
	{
		if ( Ar.UE4Ver() < VER_UE4_USERWIDGET_DEFAULT_FOCUSABLE_FALSE )
		{
			bIsFocusable = bSupportsKeyboardFocus_DEPRECATED;
		}
	}

#if UE_BUILD_DEBUG
	if ( Ar.IsCooking() )
	{
		if ( HasAllFlags(RF_ArchetypeObject) && !HasAllFlags(RF_ClassDefaultObject) )
		{
			if ( bCookedWidgetTree )
			{
				UE_LOG(LogUMG, Display, TEXT("Widget Class %s - Saving Cooked Template"), *GetClass()->GetName());
			}
			else
			{
				UE_LOG(LogUMG, Warning, TEXT("Widget Class %s - Unable To Cook Template"), *GetClass()->GetName());
			}
		}
	}
#endif
}

/////////////////////////////////////////////////////

UUserWidget* UUserWidget::CreateWidgetInstance(UWidget& OwningWidget, TSubclassOf<UUserWidget> UserWidgetClass, FName WidgetName)
{
	UUserWidget* ParentUserWidget = Cast<UUserWidget>(&OwningWidget);
	if (!ParentUserWidget && OwningWidget.GetOuter())
	{
		// If we were given a UWidget, the nearest parent UserWidget is the outer of the UWidget's WidgetTree outer
		ParentUserWidget = Cast<UUserWidget>(OwningWidget.GetOuter()->GetOuter());
	}

	if (ensure(ParentUserWidget && ParentUserWidget->WidgetTree))
	{
		UUserWidget* NewWidget = CreateInstanceInternal(ParentUserWidget->WidgetTree, UserWidgetClass, WidgetName, ParentUserWidget->GetWorld(), ParentUserWidget->GetOwningLocalPlayer());
#if WITH_EDITOR
		if (NewWidget)
		{
			NewWidget->SetDesignerFlags(OwningWidget.GetDesignerFlags());
		}
#endif
		return NewWidget;
	}

	return nullptr;
}

UUserWidget* UUserWidget::CreateWidgetInstance(UWidgetTree& OwningWidgetTree, TSubclassOf<UUserWidget> UserWidgetClass, FName WidgetName)
{
	if (UUserWidget* OwningUserWidget = Cast<UUserWidget>(OwningWidgetTree.GetOuter()))
	{
		return CreateWidgetInstance(*OwningUserWidget, UserWidgetClass, WidgetName);
	}

	return CreateInstanceInternal(&OwningWidgetTree, UserWidgetClass, WidgetName, nullptr, nullptr);
}

UUserWidget* UUserWidget::CreateWidgetInstance(APlayerController& OwnerPC, TSubclassOf<UUserWidget> UserWidgetClass, FName WidgetName)
{
	if (!OwnerPC.IsLocalPlayerController())
	{
		const FText FormatPattern = LOCTEXT("NotLocalPlayer", "Only Local Player Controllers can be assigned to widgets. {PlayerController} is not a Local Player Controller.");
		FFormatNamedArguments FormatPatternArgs;
		FormatPatternArgs.Add(TEXT("PlayerController"), FText::FromName(OwnerPC.GetFName()));
		FMessageLog("PIE").Error(FText::Format(FormatPattern, FormatPatternArgs));
	}
	else if (!OwnerPC.Player)
	{
		const FText FormatPattern = LOCTEXT("NoPlayer", "CreateWidget cannot be used on Player Controller with no attached player. {PlayerController} has no Player attached.");
		FFormatNamedArguments FormatPatternArgs;
		FormatPatternArgs.Add(TEXT("PlayerController"), FText::FromName(OwnerPC.GetFName()));
		FMessageLog("PIE").Error(FText::Format(FormatPattern, FormatPatternArgs));
	}
	else if (UWorld* World = OwnerPC.GetWorld())
	{
		UGameInstance* GameInstance = World->GetGameInstance();
		UObject* Outer = GameInstance ? StaticCast<UObject*>(GameInstance) : StaticCast<UObject*>(World);
		return CreateInstanceInternal(Outer, UserWidgetClass, WidgetName, World, CastChecked<ULocalPlayer>(OwnerPC.Player));
	}
	return nullptr;
}

UUserWidget* UUserWidget::CreateWidgetInstance(UGameInstance& GameInstance, TSubclassOf<UUserWidget> UserWidgetClass, FName WidgetName)
{
	return CreateInstanceInternal(&GameInstance, UserWidgetClass, WidgetName, GameInstance.GetWorld(), GameInstance.GetFirstGamePlayer());
}

UUserWidget* UUserWidget::CreateWidgetInstance(UWorld& World, TSubclassOf<UUserWidget> UserWidgetClass, FName WidgetName)
{
	if (UGameInstance* GameInstance = World.GetGameInstance())
	{
		return CreateWidgetInstance(*GameInstance, UserWidgetClass, WidgetName);
	}
	return CreateInstanceInternal(&World, UserWidgetClass, WidgetName, &World, World.GetFirstLocalPlayerFromController());
}

UUserWidget* UUserWidget::CreateInstanceInternal(UObject* Outer, TSubclassOf<UUserWidget> UserWidgetClass, FName InstanceName, UWorld* World, ULocalPlayer* LocalPlayer)
{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	// Only do this on a non-shipping or test build.
	if (!CreateWidgetHelpers::ValidateUserWidgetClass(UserWidgetClass))
	{
		return nullptr;
	}
#endif

#if !UE_BUILD_SHIPPING
	// In non-shipping builds, ensure that users are allowed to dynamic construct this widget.
	if (UWidgetBlueprintGeneratedClass* BPClass = Cast<UWidgetBlueprintGeneratedClass>(UserWidgetClass))
	{
		if (World && World->IsGameWorld())
		{
			ensureMsgf(BPClass->bAllowDynamicCreation, TEXT("This Widget Blueprint's 'Support Dynamic Creation' option either defaults to Off or was explictly turned off.  If you need to create this widget at runtime, turn this option on."));
		}
	}
#endif

#if !UE_BUILD_SHIPPING
	// Check if the world is being torn down before we create a widget for it.
	if (World)
	{
		// Look for indications that widgets are being created for a dead and dying world.
		ensureMsgf(!World->bIsTearingDown, TEXT("Widget Class %s - Attempting to be created while tearing down the world."), *UserWidgetClass->GetName());
	}
#endif

	if (!Outer)
	{
		FMessageLog("PIE").Error(FText::Format(LOCTEXT("OuterNull", "Unable to create the widget {0}, no outer provided."), FText::FromName(UserWidgetClass->GetFName())));
		return nullptr;
	}

	UUserWidget* NewWidget = nullptr;
	UWidgetBlueprintGeneratedClass* WBGC = Cast<UWidgetBlueprintGeneratedClass>(UserWidgetClass);
	if (WBGC && WBGC->HasTemplate())
	{
		if (UUserWidget* Template = WBGC->GetTemplate())
		{
#if UE_BUILD_DEBUG
			UE_LOG(LogUMG, Log, TEXT("Widget Class %s - Using Fast CreateWidget Path."), *UserWidgetClass->GetName());
#endif

			FObjectInstancingGraph ObjectInstancingGraph;
			NewWidget = NewObject<UUserWidget>(Outer, UserWidgetClass, InstanceName, RF_Transactional, Template, false, &ObjectInstancingGraph);
		}
#if !WITH_EDITOR && (UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT)
		else
		{

			UE_LOG(LogUMG, Error, TEXT("Widget Class %s - Using Slow CreateWidget path because no template found."), *UserWidgetClass->GetName());
		}
#endif
	}
#if !WITH_EDITOR && (UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT)
	else
	{
		// Nativized widget blueprint class types (UDynamicClass) do not currently support the fast path (see FWidgetBlueprintCompiler::CanAllowTemplate), so we bypass the runtime warning in that case.
		const bool bIsDynamicClass = Cast<UDynamicClass>(UserWidgetClass) != nullptr;
		if (!bIsDynamicClass)
		{
			UE_LOG(LogUMG, Warning, TEXT("Widget Class %s - Using Slow CreateWidget path because this class could not be templated."), *UserWidgetClass->GetName());
		}
	}
#endif

	if (!NewWidget)
	{
		NewWidget = NewObject<UUserWidget>(Outer, UserWidgetClass, InstanceName, RF_Transactional);
	}
	
	if (LocalPlayer)
	{
		NewWidget->SetPlayerContext(FLocalPlayerContext(LocalPlayer, World));
	}

	NewWidget->Initialize();

	return NewWidget;
}


void UUserWidget::OnLatentActionsChanged(UObject* ObjectWhichChanged, ELatentActionChangeType ChangeType)
{
	if (UUserWidget* WidgetThatChanged = Cast<UUserWidget>(ObjectWhichChanged))
	{
		TSharedPtr<SObjectWidget> SafeGCWidget = WidgetThatChanged->MyGCWidget.Pin();
		if (SafeGCWidget.IsValid())
		{
			bool bCanTick = SafeGCWidget->GetCanTick();

			WidgetThatChanged->UpdateCanTick();

			if (SafeGCWidget->GetCanTick() && !bCanTick)
			{
				// If the widget can now tick, recache the volatility of the widget.
				WidgetThatChanged->Invalidate(EInvalidateWidget::LayoutAndVolatility);
			}
		}
	}
}


/////////////////////////////////////////////////////

bool CreateWidgetHelpers::ValidateUserWidgetClass(const UClass* UserWidgetClass)
{
	if (UserWidgetClass == nullptr)
	{
		FMessageLog("PIE").Error(LOCTEXT("WidgetClassNull", "CreateWidget called with a null class."));
		return false;
	}

	if (!UserWidgetClass->IsChildOf(UUserWidget::StaticClass()))
	{
		const FText FormatPattern = LOCTEXT("NotUserWidget", "CreateWidget can only be used on UUserWidget children. {UserWidgetClass} is not a UUserWidget.");
		FFormatNamedArguments FormatPatternArgs;
		FormatPatternArgs.Add(TEXT("UserWidgetClass"), FText::FromName(UserWidgetClass->GetFName()));
		FMessageLog("PIE").Error(FText::Format(FormatPattern, FormatPatternArgs));
		return false;
	}

	if (UserWidgetClass->HasAnyClassFlags(CLASS_Abstract | CLASS_NewerVersionExists | CLASS_Deprecated))
	{
		const FText FormatPattern = LOCTEXT("NotValidClass", "Abstract, Deprecated or Replaced classes are not allowed to be used to construct a user widget. {UserWidgetClass} is one of these.");
		FFormatNamedArguments FormatPatternArgs;
		FormatPatternArgs.Add(TEXT("UserWidgetClass"), FText::FromName(UserWidgetClass->GetFName()));
		FMessageLog("PIE").Error(FText::Format(FormatPattern, FormatPatternArgs));
		return false;
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
