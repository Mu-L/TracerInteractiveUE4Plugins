// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayNodes/SequencerSectionCategoryNode.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "SSequencer.h"
#include "SKeyNavigationButtons.h"


namespace SequencerSectionCategoryNodeConstants
{
	float NodePadding = 2.f;
}


/* FSequencerDisplayNode interface
 *****************************************************************************/

bool FSequencerSectionCategoryNode::CanRenameNode() const
{
	return false;
}

TSharedRef<SWidget> FSequencerSectionCategoryNode::GetCustomOutlinerContent()
{
	return SNew(SBox)
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SKeyNavigationButtons, AsShared())
			]
		];
}


FText FSequencerSectionCategoryNode::GetDisplayName() const
{
	return DisplayName;
}


float FSequencerSectionCategoryNode::GetNodeHeight() const 
{
	return SequencerLayoutConstants::CategoryNodeHeight + SequencerSectionCategoryNodeConstants::NodePadding*2;
}


FNodePadding FSequencerSectionCategoryNode::GetNodePadding() const
{
	return FNodePadding(0.f);//FNodePadding(SequencerSectionCategoryNodeConstants::NodePadding);
}


ESequencerNode::Type FSequencerSectionCategoryNode::GetType() const
{
	return ESequencerNode::Category;
}


void FSequencerSectionCategoryNode::SetDisplayName(const FText& NewDisplayName)
{
	check(false);
}

FSlateFontInfo FSequencerSectionCategoryNode::GetDisplayNameFont() const
{
	bool bAllAnimated = false;
	for (TSharedRef<FSequencerDisplayNode> ChildNode : GetChildNodes())
	{
		if (ChildNode->GetType() == ESequencerNode::KeyArea)
		{
			const FSequencerSectionKeyAreaNode& KeyAreaNode = static_cast<const FSequencerSectionKeyAreaNode&>(ChildNode.Get());
			for (const TSharedRef<IKeyArea>& KeyArea : KeyAreaNode.GetAllKeyAreas())
			{
				FMovieSceneChannel* Channel = KeyArea->ResolveChannel();
				if (!Channel || Channel->GetNumKeys() == 0)
				{
					return FSequencerDisplayNode::GetDisplayNameFont();
				}
				else
				{
					bAllAnimated = true;
				}
			}
		}
	}
	if (bAllAnimated == true)
	{
		return FEditorStyle::GetFontStyle("Sequencer.AnimationOutliner.ItalicFont");
	}
	return FSequencerDisplayNode::GetDisplayNameFont();
}

