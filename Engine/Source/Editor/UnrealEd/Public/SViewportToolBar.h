// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineBaseTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Animation/CurveSequence.h"
#include "Editor/UnrealEdTypes.h"

class SMenuAnchor;

/**
 * A level viewport toolbar widget that is placed in a viewport
 */
class UNREALED_API SViewportToolBar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS( SViewportToolBar ){}
	SLATE_END_ARGS()

	void Construct( const FArguments& InArgs );

	/**
	 * @return The currently open pull down menu if there is one
	 */
	TWeakPtr<SMenuAnchor> GetOpenMenu() const;

	/**
	 * Sets the open menu to a new menu and closes any currently opened one
	 *
	 * @param NewMenu The new menu that is opened
	 */
	void SetOpenMenu( TSharedPtr< SMenuAnchor >& NewMenu );

	/**
	 * @return The color and opacity of this viewport. 
	 */ 
	FLinearColor OnGetColorAndOpacity() const;

	/** @return Whether the given viewmode is supported. */ 
	virtual bool IsViewModeSupported(EViewModeIndex ViewModeIndex) const;

private:
	
	/**
	 * Called when the mouse enters the toolbar area.  We fade in the toolbar when this happens
	 *
	 * @param MyGeometry	Information about the size of the toolbar
	 * @param MouseEvent	The mouse event that triggered this function
	 */
	virtual void OnMouseEnter( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent ) override;

	/**
	 * Called when the mouse leaves the toolbar area.  We fade out the toolbar when this happens
	 *
	 * @param MouseEvent	The mouse event that triggered this function
	 */
	virtual void OnMouseLeave( const FPointerEvent& MouseEvent ) override;

protected:
	/**
	* Returns the label for the "Camera" tool bar menu based on based viewport type
	*
	* @param ViewportType	The Viewport type we want the label for
	*
	* @return	Label to use for this menu label
	*/
	virtual FText GetCameraMenuLabelFromViewportType(const ELevelViewportType ViewportType) const;

	/**
	* Returns the label icon for the "Camera" tool bar menu, which changes depending on the viewport type
	*
	* @param ViewportType	The Viewport type we want the icon for
	*
	* @return	Label icon to use for this menu label
	*/
	virtual const FSlateBrush* GetCameraMenuLabelIconFromViewportType(const ELevelViewportType ViewportType) const;

private:
	/** Curve sequence for fading out the toolbar */
	FCurveSequence FadeOutSequence;
	/** Curve sequence for fading in the toolbar */
	FCurveSequence FadeInSequence;
	/** The pulldown menu that is open if any */
	TWeakPtr< SMenuAnchor > OpenedMenu;
	/** True if the mouse is inside the toolbar */
	bool bIsHovered;
};

