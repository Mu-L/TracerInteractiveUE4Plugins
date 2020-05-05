// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SceneOutlinerFwd.h"
#include "Widgets/SWidget.h"
#include "Widgets/SNullWidget.h"

namespace SceneOutliner
{
	/** A const tree item visitor. Derive to implement type-specific behaviour for tree items. */
	struct ITreeItemVisitor
	{
		virtual ~ITreeItemVisitor() {}
		virtual void Visit(const FActorTreeItem& Actor) const {}
		virtual void Visit(const FWorldTreeItem& World) const {}
		virtual void Visit(const FFolderTreeItem& Folder) const {}
		virtual void Visit(const FComponentTreeItem& Folder) const {}
		virtual void Visit(const FSubComponentTreeItem& Folder) const {}
	};

	/** A non-const tree item visitor. Derive to implement type-specific behaviour for tree items. */
	struct IMutableTreeItemVisitor
	{
		virtual ~IMutableTreeItemVisitor() {}
		virtual void Visit(FActorTreeItem& Actor) const {}
		virtual void Visit(FWorldTreeItem& World) const {}
		virtual void Visit(FFolderTreeItem& Folder) const {}
		virtual void Visit(FComponentTreeItem& Folder) const {}
		virtual void Visit(FSubComponentTreeItem& Folder) const {}
	};

	/** A functional-based visitor. Allows for visitor-pattern behaviour without creating a custom type. */
	struct FFunctionalVisitor : ITreeItemVisitor
	{
		typedef TFunctionRef<void(const FActorTreeItem&)> 				FActorFunction;
		typedef TFunctionRef<void(const FWorldTreeItem&)> 				FWorldFunction;
		typedef TFunctionRef<void(const FFolderTreeItem&)> 				FFolderFunction;
		typedef TFunctionRef<void(const FComponentTreeItem&)> 			FComponentFunction;
		typedef TFunctionRef<void(const FSubComponentTreeItem&)> 		FSubComponentFunction;

		FFunctionalVisitor& Actor(FActorFunction InFunction) 					{ ActorFunction = InFunction; return *this; }
		FFunctionalVisitor& World(FWorldFunction InFunction) 					{ WorldFunction = InFunction; return *this; }
		FFunctionalVisitor& Folder(FFolderFunction InFunction)					{ FolderFunction = InFunction; return *this; }
		FFunctionalVisitor& Component(FComponentFunction InFunction)			{ ComponentFunction = InFunction; return *this; }
		FFunctionalVisitor& SubComponent(FSubComponentFunction InFunction)		{ SubComponentFunction = InFunction; return *this; }

	private:
		TOptional<FActorFunction> ActorFunction;
		TOptional<FWorldFunction> WorldFunction;
		TOptional<FFolderFunction> FolderFunction;
		TOptional<FComponentFunction> ComponentFunction;
		TOptional<FSubComponentFunction> SubComponentFunction;

		virtual void Visit(const FActorTreeItem& Item) const override 					{ if (ActorFunction) { ActorFunction.GetValue()(Item); } }
		virtual void Visit(const FWorldTreeItem& Item) const override 					{ if (WorldFunction) { WorldFunction.GetValue()(Item); } }
		virtual void Visit(const FFolderTreeItem& Item) const override					{ if (FolderFunction) { FolderFunction.GetValue()(Item); } }
		virtual void Visit(const FComponentTreeItem& Item) const override				{ if (ComponentFunction) { ComponentFunction.GetValue()(Item); } }
		virtual void Visit(const FSubComponentTreeItem& Item) const override			{ if (SubComponentFunction) { SubComponentFunction.GetValue()(Item); } }
	};

	/** A visitor specialized for getting/extracting a value from a tree item. */
	template<typename TDataType>
	struct TTreeItemGetter : ITreeItemVisitor
	{
		mutable TDataType Data;

		/** Override to extract the data from specific tree item types */
		virtual TDataType Get(const FActorTreeItem& ActorItem) const 					{ return TDataType(); }
		virtual TDataType Get(const FWorldTreeItem& WorldItem) const 					{ return TDataType(); }
		virtual TDataType Get(const FFolderTreeItem& FolderItem) const					{ return TDataType(); }
		virtual TDataType Get(const FComponentTreeItem& ComponentFunction) const		{ return TDataType(); }
		virtual TDataType Get(const FSubComponentTreeItem& CustomFunction) const		{ return TDataType(); }

		/** Return the result returned from Get() */
		FORCEINLINE const TDataType& Result() const 									{ return Data; }

	private:
		virtual void Visit(const FActorTreeItem& ActorItem) const override 				{ Data = Get(ActorItem); }
		virtual void Visit(const FWorldTreeItem& WorldItem) const override 				{ Data = Get(WorldItem); }
		virtual void Visit(const FFolderTreeItem& FolderItem) const override			{ Data = Get(FolderItem); }
		virtual void Visit(const FComponentTreeItem& ComponentFunction) const override	{ Data = Get(ComponentFunction); }
		virtual void Visit(const FSubComponentTreeItem& SubComponentFunction) const override { Data = Get(SubComponentFunction); }
	};

	/** A visitor class used to generate column cells for specific tree item types */
	struct FColumnGenerator : IMutableTreeItemVisitor
	{
		mutable TSharedPtr<SWidget> Widget;

		virtual TSharedRef<SWidget> GenerateWidget(FActorTreeItem& Item) const 			{ return SNullWidget::NullWidget; }
		virtual TSharedRef<SWidget> GenerateWidget(FWorldTreeItem& Item) const 			{ return SNullWidget::NullWidget; }
		virtual TSharedRef<SWidget> GenerateWidget(FFolderTreeItem& Item) const			{ return SNullWidget::NullWidget; }
		virtual TSharedRef<SWidget> GenerateWidget(FComponentTreeItem& Item) const		{ return SNullWidget::NullWidget; }
		virtual TSharedRef<SWidget> GenerateWidget(FSubComponentTreeItem& Item) const	{ return SNullWidget::NullWidget; }

	private:
		virtual void Visit(FActorTreeItem& Item) const override 						{ Widget = GenerateWidget(Item); }
		virtual void Visit(FWorldTreeItem& Item) const override 						{ Widget = GenerateWidget(Item); }
		virtual void Visit(FFolderTreeItem& Item) const override						{ Widget = GenerateWidget(Item); }
		virtual void Visit(FComponentTreeItem& Item) const override						{ Widget = GenerateWidget(Item); }
		virtual void Visit(FSubComponentTreeItem& Item) const override					{ Widget = GenerateWidget(Item); }
	};
	
}	// namespace SceneOutliner
