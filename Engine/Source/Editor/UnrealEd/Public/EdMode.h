// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "UObject/GCObject.h"
#include "UnrealWidget.h"
#include "EditorComponents.h"
#include "EngineGlobals.h"
#include "EditorModeRegistry.h"
#include "Tools/UEdMode.h"
#include "Templates/SharedPointer.h"

class FCanvas;
class FEditorModeTools;
class FEditorViewportClient;
class FModeTool;
class FModeToolkit;
class FPrimitiveDrawInterface;
class FSceneView;
class FViewport;
class UTexture2D;
struct FConvexVolume;
struct FViewportClick;

enum EModeTools : int8;
class FEditorViewportClient;
class HHitProxy;
struct FViewportClick;
class FModeTool;
class FEditorViewportClient;
struct FViewportClick;

/**
 * Base class for all editor modes.
 */
class UNREALED_API FEdMode : public TSharedFromThis<FEdMode>, public FGCObject, public FEditorCommonDrawHelper
{
public:
	/** Friends so it can access mode's internals on construction */
	friend class FEditorModeRegistry;

	FEdMode();
	virtual ~FEdMode();

	virtual void Initialize() {}

	virtual bool MouseEnter( FEditorViewportClient* ViewportClient,FViewport* Viewport,int32 x, int32 y );

	virtual bool MouseLeave( FEditorViewportClient* ViewportClient,FViewport* Viewport );

	virtual bool MouseMove(FEditorViewportClient* ViewportClient,FViewport* Viewport,int32 x, int32 y);

	virtual bool ReceivedFocus(FEditorViewportClient* ViewportClient,FViewport* Viewport);

	virtual bool LostFocus(FEditorViewportClient* ViewportClient,FViewport* Viewport);

	/**
	 * Called when the mouse is moved while a window input capture is in effect
	 *
	 * @param	InViewportClient	Level editor viewport client that captured the mouse input
	 * @param	InViewport			Viewport that captured the mouse input
	 * @param	InMouseX			New mouse cursor X coordinate
	 * @param	InMouseY			New mouse cursor Y coordinate
	 *
	 * @return	true if input was handled
	 */
	virtual bool CapturedMouseMove( FEditorViewportClient* InViewportClient, FViewport* InViewport, int32 InMouseX, int32 InMouseY );

	/** Process all captured mouse moves that occurred during the current frame */
	virtual bool ProcessCapturedMouseMoves( FEditorViewportClient* InViewportClient, FViewport* InViewport, const TArrayView<FIntPoint>& CapturedMouseMoves ) { return false; }

	virtual bool InputKey(FEditorViewportClient* ViewportClient,FViewport* Viewport,FKey Key,EInputEvent Event);
	virtual bool InputAxis(FEditorViewportClient* InViewportClient,FViewport* Viewport,int32 ControllerId,FKey Key,float Delta,float DeltaTime);
	virtual bool InputDelta(FEditorViewportClient* InViewportClient,FViewport* InViewport,FVector& InDrag,FRotator& InRot,FVector& InScale);
	virtual bool StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport);
	virtual bool EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport);
	// Added for handling EDIT Command...
	virtual EEditAction::Type GetActionEditDuplicate() { return EEditAction::Skip; }
	virtual EEditAction::Type GetActionEditDelete() { return EEditAction::Skip; }
	virtual EEditAction::Type GetActionEditCut() { return EEditAction::Skip; }
	virtual EEditAction::Type GetActionEditCopy() { return EEditAction::Skip; }
	virtual EEditAction::Type GetActionEditPaste() { return EEditAction::Skip; }
	virtual bool ProcessEditDuplicate() { return false; }
	virtual bool ProcessEditDelete() { return false; }
	virtual bool ProcessEditCut() { return false; }
	virtual bool ProcessEditCopy() { return false; }
	virtual bool ProcessEditPaste() { return false; }

	virtual void Tick(FEditorViewportClient* ViewportClient,float DeltaTime);

	virtual bool IsCompatibleWith(FEditorModeID OtherModeID) const { return false; }
	virtual void ActorMoveNotify() {}
	virtual void ActorsDuplicatedNotify(TArray<AActor*>& PreDuplicateSelection, TArray<AActor*>& PostDuplicateSelection, bool bOffsetLocations) {}
	virtual void ActorSelectionChangeNotify();
	virtual void ActorPropChangeNotify() {}
	virtual void MapChangeNotify() {}
	virtual bool ShowModeWidgets() const { return 1; }

	/** If the EdMode is handling InputDelta (i.e., returning true from it), this allows a mode to indicated whether or not the Widget should also move. */
	virtual bool AllowWidgetMove() { return true; }
	
	/** Check to see if the current widget mode can be cycled */
	virtual bool CanCycleWidgetMode() const { return true; }

	/** If the Edmode is handling its own mouse deltas, it can disable the MouseDeltaTacker */
	virtual bool DisallowMouseDeltaTracking() const { return false; }

	/** 
	 * Lets each mode/tool specify a pivot point around which the camera should orbit
	 * @param	OutPivot	The custom pivot point returned by the mode/tool
	 * @return	true if a custom pivot point was specified, false otherwise.
	 */
	virtual bool GetPivotForOrbit(FVector& OutPivot) const { return false; }

	/**
	 * Get a cursor to override the default with, if any.
	 * @return true if the cursor was overridden.
	 */
	virtual bool GetCursor(EMouseCursor::Type& OutCursor) const { return false; }

	/** Get override cursor visibility settings */
	virtual bool GetOverrideCursorVisibility(bool& bWantsOverride, bool& bHardwareCursorVisible, bool bSoftwareCursorVisible) const { return false; }

	/** Called before mouse movement is converted to drag/rot */
	virtual bool PreConvertMouseMovement(FEditorViewportClient* InViewportClient) { return false; }

	/** Called after mouse movement is converted to drag/rot */
	virtual bool PostConvertMouseMovement(FEditorViewportClient* InViewportClient) { return false;}

	virtual bool ShouldDrawBrushWireframe( AActor* InActor ) const { return true; }
	virtual bool GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData);
	virtual bool GetCustomInputCoordinateSystem( FMatrix& InMatrix, void* InData ) { return 0; }

	/** If Rotation Snap should be enabled for this mode*/ 
	virtual bool IsSnapRotationEnabled();

	/** If this mode should override the snap rotation
	* @param	Rotation		The Rotation Override
	*
	* @return					True if you have overridden the value
	*/
	virtual bool SnapRotatorToGridOverride(FRotator& Rotation){ return false; };

	/**
	 * Allows each mode to customize the axis pieces of the widget they want drawn.
	 *
	 * @param	InwidgetMode	The current widget mode
	 *
	 * @return					A bitfield comprised of AXIS_* values
	 */
	virtual EAxisList::Type GetWidgetAxisToDraw( FWidget::EWidgetMode InWidgetMode ) const;

	/**
	 * Allows each mode/tool to determine a good location for the widget to be drawn at.
	 */
	virtual FVector GetWidgetLocation() const;

	/**
	 * Lets the mode determine if it wants to draw the widget or not.
	 */
	virtual bool ShouldDrawWidget() const;
	virtual void UpdateInternalData() {}

	virtual FVector GetWidgetNormalFromCurrentAxis( void* InData );

	virtual void AddReferencedObjects( FReferenceCollector& Collector ) override {}

	virtual void Enter();
	virtual void Exit();
	virtual UTexture2D* GetVertexTexture();
	
	/**
	 * Lets each tool determine if it wants to use the editor widget or not.  If the tool doesn't want to use it,
	 * it will be fed raw mouse delta information (not snapped or altered in any way).
	 */
	virtual bool UsesTransformWidget() const;

	/**
	 * Lets each mode selectively exclude certain widget types.
	 */
	virtual bool UsesTransformWidget(FWidget::EWidgetMode CheckMode) const;

	virtual void PostUndo() {}

	/** 
	 * Check to see if this EdMode wants to disallow AutoSave
	 * @return true if AutoSave can be applied right now
	 */
	virtual bool CanAutoSave() const { return true; }

	/**
	 * Lets each mode/tool handle box selection in its own way.
	 *
	 * @param	InBox	The selection box to use, in worldspace coordinates.
	 * @return		true if something was selected/deselected, false otherwise.
	 */
	virtual bool BoxSelect( FBox& InBox, bool InSelect = true );

	/**
	 * Lets each mode/tool handle frustum selection in its own way.
	 *
	 * @param	InFrustum	The selection box to use, in worldspace coordinates.
	 * @return	true if something was selected/deselected, false otherwise.
	 */
	virtual bool FrustumSelect( const FConvexVolume& InFrustum, FEditorViewportClient* InViewportClient, bool InSelect = true );

	virtual void SelectNone();
	virtual void SelectionChanged() {}

	virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click);

	/**
	 * Allows an editor mode to override the bounding box used to focus the viewport on a selection
	 * 
	 * @param Actor			The selected actor that is being considered for focus
	 * @param PrimitiveComponent	The component in the actor being considered for focus
	 * @param InOutBox		The box that should be computed for the actor and component
	 * @return bool			true if the mode overrides the box and populated InOutBox, false if it did not populate InOutBox
	 */
	virtual bool ComputeBoundingBoxForViewportFocus(AActor* Actor, UPrimitiveComponent* PrimitiveComponent, FBox& InOutBox) const { return false; }

	/** Handling SelectActor */
	virtual bool Select( AActor* InActor, bool bInSelected ) { return 0; }

	/** Check to see if an actor can be selected in this mode - no side effects */
	virtual bool IsSelectionAllowed( AActor* InActor, bool bInSelection ) const { return true; }

	/** @return True if this mode allows the viewport to use a drag tool */
	virtual bool AllowsViewportDragTool() const { return true; }

	/** Returns the editor mode identifier. */
	FEditorModeID GetID() const { return Info.ID; }

	/** Returns the editor mode information. */
	const FEditorModeInfo& GetModeInfo() const { return Info; }

	// Tools

	void SetCurrentTool( EModeTools InID );
	void SetCurrentTool( FModeTool* InModeTool );
	FModeTool* FindTool( EModeTools InID );

	const TArray<FModeTool*>& GetTools() const		{ return Tools; }

	virtual void CurrentToolChanged() {}

	/** Returns the current tool. */
	//@{
	FModeTool* GetCurrentTool()				{ return CurrentTool; }
	const FModeTool* GetCurrentTool() const	{ return CurrentTool; }
	//@}

	/** @name Current widget axis. */
	//@{
	void SetCurrentWidgetAxis(EAxisList::Type InAxis)		{ CurrentWidgetAxis = InAxis; }
	EAxisList::Type GetCurrentWidgetAxis() const			{ return CurrentWidgetAxis; }
	//@}

	/** @name Rendering */
	//@{
	/** Draws translucent polygons on brushes and volumes. */
	virtual void Render(const FSceneView* View,FViewport* Viewport,FPrimitiveDrawInterface* PDI);
	//void DrawGridSection(int32 ViewportLocX,int32 ViewportGridY,FVector* A,FVector* B,float* AX,float* BX,int32 Axis,int32 AlphaCase,FSceneView* View,FPrimitiveDrawInterface* PDI);

	/** Overlays the editor hud (brushes, drag tools, static mesh vertices, etc*. */
	virtual void DrawHUD(FEditorViewportClient* ViewportClient,FViewport* Viewport,const FSceneView* View,FCanvas* Canvas);
	//@}

	/**
	 * Called when attempting to duplicate the selected actors by alt+dragging,
	 * return true to prevent normal duplication.
	 */
	virtual bool HandleDragDuplicate() { return false; }

	/**
	 * Called when the mode wants to draw brackets around selected objects
	 */
	virtual void DrawBrackets( FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas );

	/** True if this mode uses a toolkit mode (eventually they all should) */
	virtual bool UsesToolkits() const;

	/** Gets the toolkit created by this mode */
	TSharedPtr<FModeToolkit> GetToolkit() { return Toolkit; }

	/** Returns the world this toolkit is editing */
	UWorld* GetWorld() const;

	/** Returns the owning mode manager for this mode */
	class FEditorModeTools* GetModeManager() const;

	/** 
	 * Called when the editor mode should rebuild its toolbar 
	 *
	 * @param ToolbarBuilder	The builder which should be used to add toolbar widgets
	 */
	virtual void BuildModeToolbar(class FToolBarBuilder& ToolbarBuilder) {}

	// Property Widgets

	/**
	 * Lets each mode selectively enable widgets for editing properties tagged with 'Show 3D Widget' metadata.
	 */
	virtual bool UsesPropertyWidgets() const { return false; }

	// @TODO: Find a better home for these?
	/** Value of UPROPERTY can be edited with a widget in the editor (translation, rotation) */
	static const FName MD_MakeEditWidget;
	/** Specifies a function used for validation of the current value of a property.  The function returns a string that is empty if the value is valid, or contains an error description if the value is invalid */
	static const FName MD_ValidateWidgetUsing;
	/** Returns true if this structure can support creating a widget in the editor */
	static bool CanCreateWidgetForStructure(const UStruct* InPropStruct);
	/** Returns true if this property can support creating a widget in the editor */
	static bool CanCreateWidgetForProperty(FProperty* InProp);
	/** See if we should create a widget for the supplied property when selecting an actor instance */
	static bool ShouldCreateWidgetForProperty(FProperty* InProp);

public:

	/** Request that this mode be deleted at the next convenient opportunity (FEditorModeTools::Tick) */
	void RequestDeletion() { bPendingDeletion = true; }
	
	/** returns true if this mode is to be deleted at the next convenient opportunity (FEditorModeTools::Tick) */
	bool IsPendingDeletion() const { return bPendingDeletion; }

private:
	/** true if this mode is pending removal from its owner */
	bool bPendingDeletion;

	/** Called whenever a mode type is unregistered */
	void OnModeUnregistered(FEditorModeID ModeID);

protected:
	/** The current axis that is being dragged on the widget. */
	EAxisList::Type CurrentWidgetAxis;

	/** Optional array of tools for this mode. */
	TArray<FModeTool*> Tools;

	/** The tool that is currently active within this mode. */
	FModeTool* CurrentTool;

	/** Information pertaining to this mode. Assigned by FEditorModeRegistry. */
	FEditorModeInfo Info;

	/** Editor Mode Toolkit that is associated with this toolkit mode */
	TSharedPtr<class FModeToolkit> Toolkit;

	/** Pointer back to the mode tools that we are registered with */
	FEditorModeTools* Owner;

	// Property Widgets
public:
	/** Structure that holds info about our optional property widget */
	struct FPropertyWidgetInfo
	{
		FString PropertyName;
		int32 PropertyIndex;
		FName PropertyValidationName;
		FString DisplayName;
		bool bIsTransform;

		FPropertyWidgetInfo()
			: PropertyIndex(INDEX_NONE)
			, bIsTransform(false)
		{
		}
		
		void GetTransformAndColor(UObject* BestSelectedItem, bool bIsSelected, FTransform& OutLocalTransform, FString& OutValidationMessage, FColor& OutDrawColor) const;
	};

protected:
	/**
	 * Returns the first selected Actor, or NULL if there is no selection.
	 */
	AActor* GetFirstSelectedActorInstance() const;

	/**
	 * Gets an array of property widget info structures for the given struct/class type for the given container.
	 *
	 * @param InStruct The type of structure/class to access widget info structures for.
	 * @param InContainer The container of the given type.
	 * @param OutInfos An array of widget info structures (output).
	 */
	void GetPropertyWidgetInfos(const UStruct* InStruct, const void* InContainer, TArray<FPropertyWidgetInfo>& OutInfos) const;

	/** Finds the best item to display widgets for (preferring selected components over selected actors) */
	virtual UObject* GetItemToTryDisplayingWidgetsFor(FTransform& OutWidgetToWorld) const;

	/** Name of the property currently being edited */
	FString EditedPropertyName;

	/** If the property being edited is an array property, this is the index of the element we're currently dealing with */
	int32 EditedPropertyIndex;

	/** Indicates  */
	bool bEditedPropertyIsTransform;
};

/*------------------------------------------------------------------------------
	Default.
------------------------------------------------------------------------------*/

/**
 * The default editing mode. 
 */
class FEdModeDefault : public FEdMode
{
public:
	FEdModeDefault();

	// FEdMode interface
	virtual bool UsesPropertyWidgets() const override { return true; }
	// End of FEdMode interface
};

