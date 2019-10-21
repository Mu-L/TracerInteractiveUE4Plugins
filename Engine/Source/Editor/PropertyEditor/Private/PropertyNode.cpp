// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "PropertyNode.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/MetaData.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "Components/ActorComponent.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/UserDefinedStruct.h"
#include "UnrealEdGlobals.h"
#include "ScopedTransaction.h"
#include "PropertyRestriction.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/ScopeExit.h"
#include "Editor.h"
#include "ObjectPropertyNode.h"
#include "PropertyHandleImpl.h"
#include "EditorSupportDelegates.h"
#include "UObject/ConstructorHelpers.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "PropertyNode"

FPropertySettings& FPropertySettings::Get()
{
	static FPropertySettings Settings;

	return Settings;
}

FPropertySettings::FPropertySettings()
	: bShowFriendlyPropertyNames( true )
	, bExpandDistributions( false )
	, bShowHiddenProperties(false)
{
	GConfig->GetBool(TEXT("PropertySettings"), TEXT("ShowHiddenProperties"), bShowHiddenProperties, GEditorPerProjectIni);
	GConfig->GetBool(TEXT("PropertySettings"), TEXT("ShowFriendlyPropertyNames"), bShowFriendlyPropertyNames, GEditorPerProjectIni);
	GConfig->GetBool(TEXT("PropertySettings"), TEXT("ExpandDistributions"), bExpandDistributions, GEditorPerProjectIni);
}

DEFINE_LOG_CATEGORY(LogPropertyNode);

static FObjectPropertyNode* NotifyFindObjectItemParent(FPropertyNode* InNode)
{
	FObjectPropertyNode* Result = NULL;
	check(InNode);
	FPropertyNode* ParentNode = InNode->GetParentNode(); 
	if (ParentNode)
	{
		Result = ParentNode->FindObjectItemParent();
	}
	return Result;
}

FPropertyNode::FPropertyNode(void)
	: ParentNode(NULL)
	, Property(NULL)
	, ArrayOffset(0)
	, ArrayIndex(-1)
	, MaxChildDepthAllowed(FPropertyNodeConstants::NoDepthRestrictions)
	, PropertyNodeFlags (EPropertyNodeFlags::NoFlags)
	, bRebuildChildrenRequested( false )
	, bChildrenRebuilt(false)
	, PropertyPath(TEXT(""))
	, bIsEditConst(false)
	, bUpdateEditConstState(true)
	, bDiffersFromDefault(false)
	, bUpdateDiffersFromDefault(true)
{
}


FPropertyNode::~FPropertyNode(void)
{
	DestroyTree();
}



void FPropertyNode::InitNode( const FPropertyNodeInitParams& InitParams )
{
	//Dismantle the previous tree
	DestroyTree();

	//tree hierarchy
	check(InitParams.ParentNode.Get() != this);
	ParentNode = InitParams.ParentNode.Get();
	ParentNodeWeakPtr = InitParams.ParentNode;

	if (ParentNode)
	{
		//default to parents max child depth
		MaxChildDepthAllowed = ParentNode->MaxChildDepthAllowed;
		//if limitless or has hit the full limit
		if (MaxChildDepthAllowed > 0)
		{
			--MaxChildDepthAllowed;
		}
	}

	//Property Data
	Property		= InitParams.Property;
	ArrayOffset		= InitParams.ArrayOffset;
	ArrayIndex		= InitParams.ArrayIndex;

	// Property is advanced if it is marked advanced or the entire class is advanced and the property not marked as simple
	bool bAdvanced = Property.IsValid() ? ( Property->HasAnyPropertyFlags(CPF_AdvancedDisplay) || ( !Property->HasAnyPropertyFlags( CPF_SimpleDisplay ) && Property->GetOwnerClass() && Property->GetOwnerClass()->HasAnyClassFlags( CLASS_AdvancedDisplay ) ) ): false;

	PropertyNodeFlags = EPropertyNodeFlags::NoFlags;

	//default to copying from the parent
	if (ParentNode)
	{
		if (ParentNode->HasNodeFlags(EPropertyNodeFlags::ShowCategories) != 0)
		{
			SetNodeFlags(EPropertyNodeFlags::ShowCategories, true);
		}
		else
		{
			SetNodeFlags(EPropertyNodeFlags::ShowCategories, false);
		}

		// We are advanced if our parent is advanced or our property is marked as advanced
		SetNodeFlags(EPropertyNodeFlags::IsAdvanced, ParentNode->HasNodeFlags(EPropertyNodeFlags::IsAdvanced) || bAdvanced );
	}
	else
	{
		SetNodeFlags(EPropertyNodeFlags::ShowCategories, InitParams.bCreateCategoryNodes );
	}

	SetNodeFlags(EPropertyNodeFlags::ShouldShowHiddenProperties, InitParams.bForceHiddenPropertyVisibility);
	SetNodeFlags(EPropertyNodeFlags::ShouldShowDisableEditOnInstance, InitParams.bCreateDisableEditOnInstanceNodes);

	//Custom code run prior to setting property flags
	//needs to happen after the above SetNodeFlags calls so that ObjectPropertyNode can properly respond to CollapseCategories
	InitBeforeNodeFlags();

	bool bIsEditInlineNew = false;
	bool bShowInnerObjectProperties = false;
	if ( !Property.IsValid() )
	{
		// Disable all flags if no property is bound.
		SetNodeFlags(EPropertyNodeFlags::SingleSelectOnly | EPropertyNodeFlags::EditInlineNew | EPropertyNodeFlags::ShowInnerObjectProperties, false);
	}
	else
	{
		const bool GotReadAddresses = GetReadAddressUncached( *this, false, nullptr, false );
		const bool bSingleSelectOnly = GetReadAddressUncached( *this, true, nullptr);
		SetNodeFlags(EPropertyNodeFlags::SingleSelectOnly, bSingleSelectOnly);

		UProperty* MyProperty = Property.Get();

		const bool bIsObjectOrInterface = Cast<UObjectPropertyBase>(MyProperty) || Cast<UInterfaceProperty>(MyProperty);

		// true if the property can be expanded into the property window; that is, instead of seeing
		// a pointer to the object, you see the object's properties.
		static const FName Name_EditInline("EditInline");
		static const FName Name_ShowInnerProperties("ShowInnerProperties");

		bIsEditInlineNew = bIsObjectOrInterface && GotReadAddresses && MyProperty->HasMetaData(Name_EditInline);
		bShowInnerObjectProperties = bIsObjectOrInterface && MyProperty->HasMetaData(Name_ShowInnerProperties);

		if(bIsEditInlineNew)
		{
			SetNodeFlags(EPropertyNodeFlags::EditInlineNew, true);
		}
		else if(bShowInnerObjectProperties)
		{
			SetNodeFlags(EPropertyNodeFlags::ShowInnerObjectProperties, true);
		}

		//Get the property max child depth
		static const FName Name_MaxPropertyDepth("MaxPropertyDepth");
		if (Property->HasMetaData(Name_MaxPropertyDepth))
		{
			int32 NewMaxChildDepthAllowed = Property->GetINTMetaData(Name_MaxPropertyDepth);
			//Ensure new depth is valid.  Otherwise just let the parent specified value stand
			if (NewMaxChildDepthAllowed > 0)
			{
				//if there is already a limit on the depth allowed, take the minimum of the allowable depths
				if (MaxChildDepthAllowed >= 0)
				{
					MaxChildDepthAllowed = FMath::Min(MaxChildDepthAllowed, NewMaxChildDepthAllowed);
				}
				else
				{
					//no current limit, go ahead and take the new limit
					MaxChildDepthAllowed = NewMaxChildDepthAllowed;
				}
			}
		}
	}

	InitExpansionFlags();

	UProperty* MyProperty = Property.Get();

	bool bRequiresValidation = bIsEditInlineNew || bShowInnerObjectProperties || ( MyProperty && (MyProperty->IsA<UArrayProperty>() || MyProperty->IsA<USetProperty>() || MyProperty->IsA<UMapProperty>() ));

	// We require validation if our parent also needs validation (if an array parent was resized all the addresses of children are invalid)
	bRequiresValidation |= (GetParentNode() && GetParentNode()->HasNodeFlags( EPropertyNodeFlags::RequiresValidation ) != 0);

	SetNodeFlags( EPropertyNodeFlags::RequiresValidation, bRequiresValidation );

	if ( InitParams.bAllowChildren )
	{
		RebuildChildren();
	}

	PropertyPath = FPropertyNode::CreatePropertyPath(this->AsShared())->ToString();
}

/**
 * Used for rebuilding a sub portion of the tree
 */
void FPropertyNode::RebuildChildren()
{
	CachedReadAddresses.Reset();

	bool bDestroySelf = false;
	DestroyTree(bDestroySelf);

	if (MaxChildDepthAllowed != 0)
	{
		//the case where we don't want init child nodes is when an Item has children that we don't want to display
		//the other option would be to make each node "Read only" under that item.
		//The example is a material assigned to a static mesh.
		if (HasNodeFlags(EPropertyNodeFlags::CanBeExpanded) && (ChildNodes.Num() == 0))
		{
			InitChildNodes();
		}
	}

	//see if they support some kind of edit condition
	if (Property.IsValid() && Property->GetBoolMetaData(TEXT("FullyExpand")))
	{
		bool bExpand = true;
		bool bRecurse = true;
	}

	// Children have been rebuilt, clear any pending rebuild requests
	bRebuildChildrenRequested = false;
	bChildrenRebuilt = true;

	// Notify any listener that children have been rebuilt
	OnRebuildChildren.ExecuteIfBound();
}

void FPropertyNode::AddChildNode(TSharedPtr<FPropertyNode> InNode)
{
	ChildNodes.Add(InNode); 
}

void FPropertyNode::ClearCachedReadAddresses( bool bRecursive )
{
	CachedReadAddresses.Reset();

	if( bRecursive )
	{
		for( int32 ChildIndex = 0; ChildIndex < ChildNodes.Num(); ++ChildIndex )
		{
			ChildNodes[ChildIndex]->ClearCachedReadAddresses( bRecursive );
		}
	}
}


// Follows the chain of items upwards until it finds the object window that houses this item.
FComplexPropertyNode* FPropertyNode::FindComplexParent()
{
	FPropertyNode* Cur = this;
	FComplexPropertyNode* Found = NULL;

	while( true )
	{
		Found = Cur->AsComplexNode();
		if( Found )
		{
			break;
		}
		Cur = Cur->GetParentNode();
		if( !Cur )
		{
			// There is a break in the parent chain
			break;
		}
	}

	return Found;
}


// Follows the chain of items upwards until it finds the object window that houses this item.
const FComplexPropertyNode* FPropertyNode::FindComplexParent() const
{
	const FPropertyNode* Cur = this;
	const FComplexPropertyNode* Found = NULL;

	while( true )
	{
		Found = Cur->AsComplexNode();
		if( Found )
		{
			break;
		}
		Cur = Cur->GetParentNode();
		if( !Cur )
		{
			// There is a break in the parent chain
			break;
		}

	}

	return Found;
}

class FObjectPropertyNode* FPropertyNode::FindObjectItemParent()
{
	auto ComplexParent = FindComplexParent();
	if (!ComplexParent)
	{
		return nullptr;
	}

	if (FObjectPropertyNode* ObjectNode = ComplexParent->AsObjectNode())
	{
		return ObjectNode;
	}
	else if (FPropertyNode* ParentNodePtr = ComplexParent->GetParentNode())
	{
		return ParentNodePtr->FindObjectItemParent();
	}
	return nullptr;
}

const class FObjectPropertyNode* FPropertyNode::FindObjectItemParent() const
{
	const auto ComplexParent = FindComplexParent();
	if (!ComplexParent)
	{
		return nullptr;
	}

	if (const FObjectPropertyNode* ObjectNode = ComplexParent->AsObjectNode())
	{
		return ObjectNode;
	}
	else if (const FPropertyNode* ParentNodePtr = ComplexParent->GetParentNode())
	{
		return ParentNodePtr->FindObjectItemParent();
	}
	return nullptr;
}

/**
 * Follows the top-most object window that contains this property window item.
 */
FObjectPropertyNode* FPropertyNode::FindRootObjectItemParent()
{
	// not every type of change to property values triggers a proper refresh of the hierarchy, so find the topmost container window and trigger a refresh manually.
	FObjectPropertyNode* TopmostObjectItem=NULL;

	FObjectPropertyNode* NextObjectItem = FindObjectItemParent();
	while ( NextObjectItem != NULL )
	{
		TopmostObjectItem = NextObjectItem;
		FPropertyNode* NextObjectParent = NextObjectItem->GetParentNode();
		if ( NextObjectParent != NULL )
		{
			NextObjectItem = NextObjectParent->FindObjectItemParent();
		}
		else
		{
			break;
		}
	}

	return TopmostObjectItem;
}


bool FPropertyNode::DoesChildPropertyRequireValidation(UProperty* InChildProp)
{
	return InChildProp != nullptr && (Cast<UObjectProperty>(InChildProp) != nullptr || Cast<UStructProperty>(InChildProp) != nullptr);
}

/** 
 * Used to see if any data has been destroyed from under the property tree.  Should only be called by PropertyWindow::OnIdle
 */
EPropertyDataValidationResult FPropertyNode::EnsureDataIsValid()
{
	bool bValidateChildren = !HasNodeFlags(EPropertyNodeFlags::SkipChildValidation);
	bool bValidateChildrenKeyNodes = false;		// by default, we don't check this, since it's just for Map properties

	// If we have rebuilt children since last EnsureDataIsValid call let the caller know
	if (bChildrenRebuilt)
	{
		bChildrenRebuilt = false;
		return EPropertyDataValidationResult::ChildrenRebuilt;
	}

	// The root must always be validated
	if( GetParentNode() == NULL || HasNodeFlags(EPropertyNodeFlags::RequiresValidation) != 0 )
	{
		CachedReadAddresses.Reset();

		//Figure out if an array mismatch can be ignored
		bool bIgnoreAllMismatch = false;
		//make sure that force depth-limited trees don't cause a refresh
		bIgnoreAllMismatch |= (MaxChildDepthAllowed==0);

		//check my property
		if (Property.IsValid())
		{
			UProperty* MyProperty = Property.Get();
			UStruct* OwnerStruct = MyProperty->GetOwnerStruct();

			if (!OwnerStruct || OwnerStruct->Children == nullptr)
			{
				//verify that the property is not part of an invalid trash class, treat it as an invalid object if it is which will cause a refresh
				return EPropertyDataValidationResult::ObjectInvalid;
			}

			//verify that the number of container children is correct
			UArrayProperty* ArrayProperty = Cast<UArrayProperty>(MyProperty);
			USetProperty* SetProperty = Cast<USetProperty>(MyProperty);
			UMapProperty* MapProperty = Cast<UMapProperty>(MyProperty);
			UStructProperty* StructProperty = Cast<UStructProperty>(MyProperty);

			//default to unknown array length
			int32 NumArrayChildren = -1;
			//assume all arrays have the same length
			bool bArraysHaveEqualNum = true;
			//assume all arrays match the number of property window children
			bool bArraysMatchChildNum = true;

			bool bArrayHasNewItem = false;

			UProperty* ContainerElementProperty = MyProperty;

			if (ArrayProperty)
			{
				ContainerElementProperty = ArrayProperty->Inner;
			}
			else if (SetProperty)
			{
				ContainerElementProperty = SetProperty->ElementProp;
			}
			else if (MapProperty)
			{
				// Need to attempt to validate both the key and value properties...
				bValidateChildrenKeyNodes = DoesChildPropertyRequireValidation(MapProperty->KeyProp);

				ContainerElementProperty = MapProperty->ValueProp;
			}

			bValidateChildren = DoesChildPropertyRequireValidation(ContainerElementProperty);

			//verify that the number of object children are the same too
			UObjectPropertyBase* ObjectProperty = Cast<UObjectPropertyBase>(MyProperty);
			//check to see, if this an object property, whether the contents are NULL or not.
			//This is the check to see if an object property was changed from NULL to non-NULL, or vice versa, from non-property window code.
			bool bObjectPropertyNull = true;

			//Edit inline properties can change underneath the window
			bool bIgnoreChangingChildren = !(HasNodeFlags(EPropertyNodeFlags::EditInlineNew) || HasNodeFlags(EPropertyNodeFlags::ShowInnerObjectProperties)) ;
			//ignore this node if the consistency check should happen for the children
			bool bIgnoreStaticArray = (Property->ArrayDim > 1) && (ArrayIndex == -1);

			//if this node can't possibly have children (or causes a circular reference loop) then ignore this as a object property
			if (bIgnoreChangingChildren || bIgnoreStaticArray || HasNodeFlags(EPropertyNodeFlags::NoChildrenDueToCircularReference))
			{
				//this will bypass object property consistency checks
				ObjectProperty = NULL;
			}

			FReadAddressList ReadAddresses;
			const bool bSuccess = GetReadAddress( ReadAddresses );
			//make sure we got the addresses correctly
			if (!bSuccess)
			{
				UE_LOG( LogPropertyNode, Verbose, TEXT("Object is invalid %s"), *Property->GetName() );
				return EPropertyDataValidationResult::ObjectInvalid;
			}

			// If an object property with ShowInnerProperties changed object values out from under the property
			bool bShowInnerObjectPropertiesObjectChanged = false;

			//check for null, if we find one, there is a problem.
			for (int32 Scan = 0; Scan < ReadAddresses.Num(); ++Scan)
			{
				uint8* Addr = ReadAddresses.GetAddress(Scan);
				//make sure the data still exists
				if (Addr==NULL)
				{
					UE_LOG( LogPropertyNode, Verbose, TEXT("Object is invalid %s"), *Property->GetName() );
					return EPropertyDataValidationResult::ObjectInvalid;
				}

				if( ArrayProperty && !bIgnoreAllMismatch)
				{
					//ensure that array structures have the proper number of children
					int32 ArrayNum = FScriptArrayHelper::Num(Addr);
					//if first child
					if (NumArrayChildren == -1)
					{
						NumArrayChildren = ArrayNum;
					}
					bArrayHasNewItem = GetNumChildNodes() < ArrayNum;
					//make sure multiple arrays match
					bArraysHaveEqualNum = bArraysHaveEqualNum && (NumArrayChildren == ArrayNum);
					//make sure the array matches the number of property node children
					bArraysMatchChildNum = bArraysMatchChildNum && (GetNumChildNodes() == ArrayNum);
				}

				if (SetProperty && !bIgnoreAllMismatch)
				{
					// like arrays, ensure that set structures have the proper number of children
					int32 SetNum = FScriptSetHelper::Num(Addr);

					if (NumArrayChildren == -1)
					{
						NumArrayChildren = SetNum;
					}

					bArrayHasNewItem = GetNumChildNodes() < SetNum;
					bArraysHaveEqualNum = bArraysHaveEqualNum && (NumArrayChildren == SetNum);
					bArraysMatchChildNum = bArraysMatchChildNum && (GetNumChildNodes() == SetNum);
				}

				if (MapProperty && !bIgnoreAllMismatch)
				{
					int32 MapNum = FScriptMapHelper::Num(Addr);

					if (NumArrayChildren == -1)
					{
						NumArrayChildren = MapNum;
					}

					bArrayHasNewItem = GetNumChildNodes() < MapNum;
					bArraysHaveEqualNum = bArraysHaveEqualNum && (NumArrayChildren == MapNum);
					bArraysMatchChildNum = bArraysMatchChildNum && (GetNumChildNodes() == MapNum);
				}

				if (ObjectProperty && !bIgnoreAllMismatch)
				{
					UObject* Obj = ObjectProperty->GetObjectPropertyValue(Addr);

					if (!bShowInnerObjectPropertiesObjectChanged && HasNodeFlags(EPropertyNodeFlags::ShowInnerObjectProperties|EPropertyNodeFlags::EditInlineNew) && ChildNodes.Num() == 1)
					{
						bool bChildObjectFound = false;
						// should never have more than one node (0 is ok if the object property is null)
						check(ChildNodes.Num() == 1);
						bool bNeedRebuild = false;
						FObjectPropertyNode* ChildObjectNode = ChildNodes[0]->AsObjectNode();
						for(int32 ObjectIndex = 0; ObjectIndex < ChildObjectNode->GetNumObjects(); ++ObjectIndex)
						{
							if(Obj == ChildObjectNode->GetUObject(ObjectIndex))
							{
								bChildObjectFound = true;
								break;
							}
						}
						bShowInnerObjectPropertiesObjectChanged = !bChildObjectFound;
					}

					if (Obj != NULL)
					{
						bObjectPropertyNull = false;
						break;
					}

					
				}
			}

			//if all arrays match each other but they do NOT match the property structure, cause a rebuild
			if (bArraysHaveEqualNum && !bArraysMatchChildNum)
			{
				RebuildChildren();

				if( bArrayHasNewItem && ChildNodes.Num() )
				{
					TSharedPtr<FPropertyNode> LastChildNode = ChildNodes.Last();
					// Don't expand huge children
					if( LastChildNode->GetNumChildNodes() > 0 && LastChildNode->GetNumChildNodes() < 10 )
					{
						// Expand the last item for convenience since generally the user will want to edit the new value they added.
						LastChildNode->SetNodeFlags(EPropertyNodeFlags::Expanded, true);
					}
				}

				return EPropertyDataValidationResult::ArraySizeChanged;
			}

			if(bShowInnerObjectPropertiesObjectChanged)
			{
				RebuildChildren();
				return EPropertyDataValidationResult::EditInlineNewValueChanged;
			}

			const bool bHasChildren = (GetNumChildNodes() != 0);
			// If the object property is not null and has no children, its children need to be rebuilt
			// If the object property is null and this node has children, the node needs to be rebuilt
			if (!HasNodeFlags(EPropertyNodeFlags::ShowInnerObjectProperties) && ObjectProperty && ((!bObjectPropertyNull && !bHasChildren) || (bObjectPropertyNull && bHasChildren)))
			{
				RebuildChildren();
				return EPropertyDataValidationResult::PropertiesChanged;
			}
		}
	}

	if( bRebuildChildrenRequested )
	{
		RebuildChildren();
		// If this property is editinline and not edit const then its editinline new and we can optimize some of the refreshing in some cases.  Otherwise we need to refresh all properties in the view
		return HasNodeFlags(EPropertyNodeFlags::ShowInnerObjectProperties) || (HasNodeFlags(EPropertyNodeFlags::EditInlineNew) && !IsEditConst()) ? EPropertyDataValidationResult::EditInlineNewValueChanged : EPropertyDataValidationResult::PropertiesChanged;
	}
	
	EPropertyDataValidationResult FinalResult = EPropertyDataValidationResult::DataValid;

	// Validate children and/or their key nodes.
	if (bValidateChildren || bValidateChildrenKeyNodes)
	{
		for (int32 Scan = 0; Scan < ChildNodes.Num(); ++Scan)
		{
			TSharedPtr<FPropertyNode>& ChildNode = ChildNodes[Scan];
			check(ChildNode.IsValid());

			if (bValidateChildren)
			{
				EPropertyDataValidationResult ChildDataResult = ChildNode->EnsureDataIsValid();
				if (FinalResult == EPropertyDataValidationResult::DataValid && ChildDataResult != EPropertyDataValidationResult::DataValid)
				{
					FinalResult = ChildDataResult;
				}
			}

			// If the child property has a key node that needs validation, validate it here
			TSharedPtr<FPropertyNode>& ChildKeyNode = ChildNode->GetPropertyKeyNode();
			if (bValidateChildrenKeyNodes && ChildKeyNode.IsValid())
			{
				EPropertyDataValidationResult ChildDataResult = ChildKeyNode->EnsureDataIsValid();
				if (FinalResult == EPropertyDataValidationResult::DataValid && ChildDataResult != EPropertyDataValidationResult::DataValid)
				{
					FinalResult = ChildDataResult;
				}
			}
		}
	}

	return FinalResult;
}

/**
 * Sets the flags used by the window and the root node
 * @param InFlags - flags to turn on or off
 * @param InOnOff - whether to toggle the bits on or off
 */
void FPropertyNode::SetNodeFlags (const EPropertyNodeFlags::Type InFlags, const bool InOnOff)
{
	if (InOnOff)
	{
		PropertyNodeFlags |= InFlags;
	}
	else
	{
		PropertyNodeFlags &= (~InFlags);
	}
}

bool FPropertyNode::GetChildNode(const int32 ChildArrayIndex, TSharedPtr<FPropertyNode>& OutChildNode)
{
	OutChildNode = nullptr;

	for (auto Child = ChildNodes.CreateIterator(); Child; ++Child)
	{
		if (Child->IsValid() && (*Child)->ArrayIndex == ChildArrayIndex)
		{
			OutChildNode = *Child;
			return true;
		}
	}

	return false;
}

bool FPropertyNode::GetChildNode(const int32 ChildArrayIndex, TSharedPtr<FPropertyNode>& OutChildNode) const
{
	OutChildNode = nullptr;

	for (auto Child = ChildNodes.CreateConstIterator(); Child; ++Child)
	{
		if (Child->IsValid() && (*Child)->ArrayIndex == ChildArrayIndex)
		{
			OutChildNode = *Child;
			return true;
		}
	}

	return false;
}

TSharedPtr<FPropertyNode> FPropertyNode::FindChildPropertyNode( const FName InPropertyName, bool bRecurse )
{
	// Search Children
	for(int32 ChildIndex=0; ChildIndex<ChildNodes.Num(); ChildIndex++)
	{
		TSharedPtr<FPropertyNode>& ChildNode = ChildNodes[ChildIndex];

		if( ChildNode->GetProperty() && ChildNode->GetProperty()->GetFName() == InPropertyName )
		{
			return ChildNode;
		}
		else if( bRecurse )
		{
			TSharedPtr<FPropertyNode> PropertyNode = ChildNode->FindChildPropertyNode(InPropertyName, bRecurse );

			if( PropertyNode.IsValid() )
			{
				return PropertyNode;
			}
		}
	}

	// Return nullptr if not found...
	return nullptr;
}

/**
 * Returns whether this window's property is read only or has the CPF_EditConst flag.
 */
bool FPropertyNode::IsPropertyConst() const
{
	bool bIsPropertyConst = (HasNodeFlags(EPropertyNodeFlags::IsReadOnly) != 0);
	if (!bIsPropertyConst && Property != nullptr)
	{
		bIsPropertyConst = (Property->PropertyFlags & CPF_EditConst) ? true : false;	
	}

	return bIsPropertyConst;
}

/** @return whether this window's property is constant (can't be edited by the user) */
bool FPropertyNode::IsEditConst() const
{
	if( bUpdateEditConstState )
	{
		// Ask the objects whether this property can be changed
		const FObjectPropertyNode* ObjectPropertyNode = FindObjectItemParent();

		bIsEditConst = IsPropertyConst();
		if(!bIsEditConst && Property != nullptr && ObjectPropertyNode)
		{
			// travel up the chain to see if this property's owner struct is editconst - if it is, so is this property
			FPropertyNode* NextParent = ParentNode;
			while(NextParent != nullptr && Cast<UStructProperty>(NextParent->GetProperty()) != NULL)
			{
				if(NextParent->IsEditConst())
				{
					bIsEditConst = true;
					break;
				}
				NextParent = NextParent->ParentNode;
			}

			if(!bIsEditConst)
			{
				for(TPropObjectConstIterator CurObjectIt(ObjectPropertyNode->ObjectConstIterator()); CurObjectIt; ++CurObjectIt)
				{
					const TWeakObjectPtr<UObject> CurObject = *CurObjectIt;
					if(CurObject.IsValid())
					{
						if(!CurObject->CanEditChange(Property.Get()))
						{
							// At least one of the objects didn't like the idea of this property being changed.
							bIsEditConst = true;
							break;
						}
					}
				}
			}
		}

		bUpdateEditConstState = false;
	}


	return bIsEditConst;
}


/**
 * Appends my path, including an array index (where appropriate)
 */
bool FPropertyNode::GetQualifiedName( FString& PathPlusIndex, const bool bWithArrayIndex, const FPropertyNode* StopParent, bool bIgnoreCategories ) const
{
	bool bAddedAnything = false;
	if( ParentNodeWeakPtr.IsValid() && StopParent != ParentNode )
	{
		bAddedAnything = ParentNode->GetQualifiedName(PathPlusIndex, bWithArrayIndex, StopParent, bIgnoreCategories);
		if( bAddedAnything )
		{
			PathPlusIndex += TEXT(".");
		}
	}

	if( Property.IsValid() )
	{
		bAddedAnything = true;
		Property->AppendName(PathPlusIndex);
	}

	if ( bWithArrayIndex && (ArrayIndex != INDEX_NONE) )
	{
		bAddedAnything = true;
		PathPlusIndex += TEXT("[");
		PathPlusIndex.AppendInt(ArrayIndex);
		PathPlusIndex += TEXT("]");
	}

	return bAddedAnything;
}

bool FPropertyNode::GetReadAddressUncached( FPropertyNode& InPropertyNode,
									bool InRequiresSingleSelection,
									FReadAddressListData* OutAddresses,
									bool bComparePropertyContents,
									bool bObjectForceCompare,
									bool bArrayPropertiesCanDifferInSize ) const
{
	if (ParentNodeWeakPtr.IsValid())
	{
		return ParentNode->GetReadAddressUncached( InPropertyNode, InRequiresSingleSelection, OutAddresses, bComparePropertyContents, bObjectForceCompare, bArrayPropertiesCanDifferInSize );
	}

	return false;
}

bool FPropertyNode::GetReadAddressUncached( FPropertyNode& InPropertyNode, FReadAddressListData& OutAddresses ) const
{
	if (ParentNodeWeakPtr.IsValid())
	{
		return ParentNode->GetReadAddressUncached( InPropertyNode, OutAddresses );
	}
	return false;
}

bool FPropertyNode::GetReadAddress(bool InRequiresSingleSelection,
								   FReadAddressList& OutAddresses,
								   bool bComparePropertyContents,
								   bool bObjectForceCompare,
								   bool bArrayPropertiesCanDifferInSize)

{

	// @todo PropertyEditor Nodes which require validation cannot be cached
	if( CachedReadAddresses.Num() && !CachedReadAddresses.bRequiresCache && !HasNodeFlags(EPropertyNodeFlags::RequiresValidation) )
	{
		OutAddresses.ReadAddressListData = &CachedReadAddresses;
		return CachedReadAddresses.bAllValuesTheSame;
	}

	CachedReadAddresses.Reset();

	bool bAllValuesTheSame = false;
	if (ParentNodeWeakPtr.IsValid())
	{
		bAllValuesTheSame = GetReadAddressUncached( *this, InRequiresSingleSelection, &CachedReadAddresses, bComparePropertyContents, bObjectForceCompare, bArrayPropertiesCanDifferInSize );
		OutAddresses.ReadAddressListData = &CachedReadAddresses;
		CachedReadAddresses.bAllValuesTheSame = bAllValuesTheSame;
		CachedReadAddresses.bRequiresCache = false;
	}

	return bAllValuesTheSame;
}

/**
 * fills in the OutAddresses array with the addresses of all of the available objects.
 * @param InItem		The property to get objects from.
 * @param OutAddresses	Storage array for all of the objects' addresses.
 */
bool FPropertyNode::GetReadAddress( FReadAddressList& OutAddresses )
{
	// @todo PropertyEditor Nodes which require validation cannot be cached
	if( CachedReadAddresses.Num() && !HasNodeFlags(EPropertyNodeFlags::RequiresValidation) )
	{
		OutAddresses.ReadAddressListData = &CachedReadAddresses;
		return true;
	}

	CachedReadAddresses.Reset();

	bool bSuccess = false;
	if (ParentNodeWeakPtr.IsValid())
	{
		bSuccess = GetReadAddressUncached( *this, CachedReadAddresses );
		if( bSuccess )
		{
			OutAddresses.ReadAddressListData = &CachedReadAddresses;
		}
		CachedReadAddresses.bRequiresCache = false;
	}

	return bSuccess;
}


/**
 * Calculates the memory address for the data associated with this item's property.  This is typically the value of a UProperty or a UObject address.
 *
 * @param	StartAddress	the location to use as the starting point for the calculation; typically the address of the object that contains this property.
 *
 * @return	a pointer to a UProperty value or UObject.  (For dynamic arrays, you'd cast this value to an FArray*)
 */
uint8* FPropertyNode::GetValueBaseAddress( uint8* StartAddress )
{
	uint8* Result = NULL;

	if ( ParentNodeWeakPtr.IsValid() )
	{
		Result = ParentNode->GetValueAddress(StartAddress);
	}
	return Result;
}

/**
 * Calculates the memory address for the data associated with this item's value.  For most properties, identical to GetValueBaseAddress.  For items corresponding
 * to dynamic array elements, the pointer returned will be the location for that element's data. 
 *
 * @param	StartAddress	the location to use as the starting point for the calculation; typically the address of the object that contains this property.
 *
 * @return	a pointer to a UProperty value or UObject.  (For dynamic arrays, you'd cast this value to whatever type is the Inner for the dynamic array)
 */
uint8* FPropertyNode::GetValueAddress( uint8* StartAddress )
{
	return GetValueBaseAddress( StartAddress );
}


/*-----------------------------------------------------------------------------
	FPropertyItemValueDataTrackerSlate
-----------------------------------------------------------------------------*/
/**
 * Calculates and stores the address for both the current and default value of
 * the associated property and the owning object.
 */
class FPropertyItemValueDataTrackerSlate
{
public:
	/**
	 * A union which allows a single address to be represented as a pointer to a uint8
	 * or a pointer to a UObject.
	 */
	union FPropertyValueRoot
	{
		UObject*	OwnerObject;
		uint8*		ValueAddress;
	};

	void Reset(FPropertyNode* InPropertyNode, UObject* InOwnerObject)
	{
		OwnerObject = InOwnerObject;
		PropertyNode = InPropertyNode;
		bHasDefaultValue = false;
		InnerInitialize();
	}

	void InnerInitialize()
	{
	{
			PropertyValueRoot.OwnerObject = NULL;
			PropertyDefaultValueRoot.OwnerObject = NULL;
			PropertyValueAddress = NULL;
			PropertyValueBaseAddress = NULL;
			PropertyDefaultBaseAddress = NULL;
			PropertyDefaultAddress = NULL;
		}

		PropertyValueRoot.OwnerObject = OwnerObject.Get();
		check(PropertyNode);
		UProperty* Property = PropertyNode->GetProperty();
		check(Property);
		check(PropertyValueRoot.OwnerObject);

		FPropertyNode* ParentNode		= PropertyNode->GetParentNode();

		// if the object specified is a class object, transfer to the CDO instead
		if ( Cast<UClass>(PropertyValueRoot.OwnerObject) != NULL )
		{
			PropertyValueRoot.OwnerObject = Cast<UClass>(PropertyValueRoot.OwnerObject)->GetDefaultObject();
		}

		UArrayProperty* ArrayProp = Cast<UArrayProperty>(Property);
		UArrayProperty* OuterArrayProp = Cast<UArrayProperty>(Property->GetOuter());

		USetProperty* SetProp = Cast<USetProperty>(Property);
		USetProperty* OuterSetProp = Cast<USetProperty>(Property->GetOuter());

		UMapProperty* MapProp = Cast<UMapProperty>(Property);
		UMapProperty* OuterMapProp = Cast<UMapProperty>(Property->GetOuter());

		// calculate the values for the current object
		{
			PropertyValueBaseAddress = (OuterArrayProp == NULL && OuterSetProp == NULL && OuterMapProp == NULL)
				? PropertyNode->GetValueBaseAddress(PropertyValueRoot.ValueAddress)
				: ParentNode->GetValueBaseAddress(PropertyValueRoot.ValueAddress);

			PropertyValueAddress = PropertyNode->GetValueAddress(PropertyValueRoot.ValueAddress);
		}

		if( IsValidTracker() )
		{
			 bHasDefaultValue = Private_HasDefaultValue();
			// calculate the values for the default object
			if ( bHasDefaultValue )
			{
				PropertyDefaultValueRoot.OwnerObject = PropertyValueRoot.OwnerObject ? PropertyValueRoot.OwnerObject->GetArchetype() : NULL;
				PropertyDefaultBaseAddress = (OuterArrayProp == NULL && OuterSetProp == NULL && OuterMapProp == NULL)
					? PropertyNode->GetValueBaseAddress(PropertyDefaultValueRoot.ValueAddress)
					: ParentNode->GetValueBaseAddress(PropertyDefaultValueRoot.ValueAddress);
				PropertyDefaultAddress = PropertyNode->GetValueAddress(PropertyDefaultValueRoot.ValueAddress);

				//////////////////////////
				// If this is a container property, we must take special measures to use the base address of the property's value; for instance,
				// the array property's PropertyDefaultBaseAddress points to an FScriptArray*, while PropertyDefaultAddress points to the 
				// FScriptArray's Data pointer.
				if ( ArrayProp != NULL || SetProp != NULL || MapProp != NULL )
				{
					PropertyValueAddress = PropertyValueBaseAddress;
					PropertyDefaultAddress = PropertyDefaultBaseAddress;
				}
			}
		}
	}

	/**
	 * Constructor
	 *
	 * @param	InPropItem		the property window item this struct will hold values for
	 * @param	InOwnerObject	the object which contains the property value
	 */
	FPropertyItemValueDataTrackerSlate( FPropertyNode* InPropertyNode, UObject* InOwnerObject )
		: OwnerObject( InOwnerObject )
		, PropertyNode(InPropertyNode)
		, bHasDefaultValue(false)
	{
		InnerInitialize();
	}

	/**
	 * @return Whether or not this tracker has a valid address to a property and object
	 */
	bool IsValidTracker() const
	{
		return PropertyValueBaseAddress != 0 && OwnerObject.IsValid();
	}

	/**
	 * @return	a pointer to the subobject root (outer-most non-subobject) of the owning object.
	 */
	UObject* GetTopLevelObject()
	{
		check(PropertyNode);
		FObjectPropertyNode* RootNode = PropertyNode->FindRootObjectItemParent();
		check(RootNode);
	
		TArray<UObject*> RootObjects;
		for ( TPropObjectIterator Itor( RootNode->ObjectIterator() ) ; Itor ; ++Itor )
		{
			TWeakObjectPtr<UObject> Object = *Itor;

			if( Object.IsValid() )
			{
				RootObjects.Add(Object.Get());
			}
		}
	
		UObject* Result;
		for ( Result = PropertyValueRoot.OwnerObject; Result; Result = Result->GetOuter() )
		{
			if ( RootObjects.Contains(Result) )
			{
				break;
			}
		}
		
		if( !Result )
		{
			// The result is not contained in the root so it is the top level object
			Result = PropertyValueRoot.OwnerObject;
		}
		return Result;
	}

	/**
	 * Whether or not we have a default value
	 */
	bool HasDefaultValue() const { return bHasDefaultValue; }

	
	/**
	 * @return The property node we are inspecting
	 */
	FPropertyNode* GetPropertyNode() const { return PropertyNode; }

	/**
	 * @return The address of the property's value.
	 */
	uint8* GetPropertyValueAddress() const { return PropertyValueAddress; }

	/**
	 * @return The base address of the property's default value.
	 */
	uint8* GetPropertyDefaultBaseAddress() const { return PropertyDefaultBaseAddress; }

	/**
	 * @return The address of the property's default value.
	 */
	uint8* GetPropertyDefaultAddress() const { return PropertyDefaultAddress; }

	/**
	 * @return The address of the owning object's archetype
	 */
	FPropertyValueRoot GetPropertyValueRoot() const { return PropertyValueRoot; }
private:
		/**
	 * Determines whether the property bound to this struct exists in the owning object's archetype.
	 *
	 * @return	true if this property exists in the owning object's archetype; false if the archetype is e.g. a
	 *			CDO for a base class and this property is declared in the owning object's class.
	 */
	bool Private_HasDefaultValue() const
	{
		bool bResult = false;

		if( IsValidTracker() )
		{
			check(PropertyValueBaseAddress);
			check(PropertyValueRoot.OwnerObject);
			UObject* ParentDefault = PropertyValueRoot.OwnerObject->GetArchetype();
			check(ParentDefault);
			if (PropertyValueRoot.OwnerObject->GetClass() == ParentDefault->GetClass())
			{
				// if the archetype is of the same class, then we must have a default
				bResult = true;
			}
			else
			{
				// Find the member property which contains this item's property
			FPropertyNode* MemberPropertyNode = PropertyNode;
			for ( ;MemberPropertyNode != NULL; MemberPropertyNode = MemberPropertyNode->GetParentNode() )
			{
				UProperty* MemberProperty = MemberPropertyNode->GetProperty();
				if ( MemberProperty != NULL )
				{
					if ( Cast<UClass>(MemberProperty->GetOuter()) != NULL )
					{
						break;
					}
				}
			}
				if ( MemberPropertyNode != NULL && MemberPropertyNode->GetProperty())
			{
					// we check to see that this property is in the defaults class
					bResult = MemberPropertyNode->GetProperty()->IsInContainer(ParentDefault->GetClass());
				}
			}
		}

		return bResult;
	}

private:
	TWeakObjectPtr<UObject> OwnerObject;

	/** The property node we are inspecting */
	FPropertyNode* PropertyNode;


	/** The address of the owning object */
	FPropertyValueRoot PropertyValueRoot;

	/**
	 * The address of the owning object's archetype
	 */
	FPropertyValueRoot PropertyDefaultValueRoot;

	/**
	 * The address of this property's value.
	 */
	uint8* PropertyValueAddress;

	/**
	 * The base address of this property's value.  i.e. for dynamic arrays, the location of the FScriptArray which
	 * contains the array property's value
	 */
	uint8* PropertyValueBaseAddress;

	/**
	 * The base address of this property's default value (see other comments for PropertyValueBaseAddress)
	 */
	uint8* PropertyDefaultBaseAddress;

	/**
	 * The address of this property's default value.
	 */
	uint8* PropertyDefaultAddress;

	/** Whether or not we have a default value */
	bool bHasDefaultValue;
};


/* ==========================================================================================================
	FPropertyItemComponentCollector

	Given a property and the address for that property's data, searches for references to components and
	keeps a list of any that are found.
========================================================================================================== */
/**
 * Given a property and the address for that property's data, searches for references to components and keeps a list of any that are found.
 */
struct FPropertyItemComponentCollector
{
	/** contains the property to search along with the value address to use */
	const FPropertyItemValueDataTrackerSlate& ValueTracker;

	/** holds the list of instanced objects found */
	TArray<UObject*> Components;

	/** Whether or not we have an edit inline new */
	bool bContainsEditInlineNew;

	/** Constructor */
	FPropertyItemComponentCollector( const FPropertyItemValueDataTrackerSlate& InValueTracker )
	: ValueTracker(InValueTracker)
	, bContainsEditInlineNew( false )
	{
		check(ValueTracker.GetPropertyNode());
		FPropertyNode* PropertyNode = ValueTracker.GetPropertyNode();
		check(PropertyNode);
		UProperty* Prop = PropertyNode->GetProperty();
		if ( PropertyNode->GetArrayIndex() == INDEX_NONE )
		{
			// either the associated property is not an array property, or it's the header for the property (meaning the entire array)
			for ( int32 ArrayIndex = 0; ArrayIndex < Prop->ArrayDim; ArrayIndex++ )
			{
				ProcessProperty(Prop, ValueTracker.GetPropertyValueAddress() + ArrayIndex * Prop->ElementSize);
			}
		}
		else
		{
			// single element of either a dynamic or static array
			ProcessProperty(Prop, ValueTracker.GetPropertyValueAddress());
		}
	}

	/**
	 * Routes the processing to the appropriate method depending on the type of property.
	 *
	 * @param	Property				the property to process
	 * @param	PropertyValueAddress	the address of the property's value
	 */
	void ProcessProperty( UProperty* Property, uint8* PropertyValueAddress )
	{
		if ( Property != NULL )
		{
			bContainsEditInlineNew |= Property->HasMetaData(TEXT("EditInline")) && ((Property->PropertyFlags & CPF_EditConst) == 0);

			if ( ProcessObjectProperty(Cast<UObjectPropertyBase>(Property), PropertyValueAddress) )
			{
				return;
			}
			if ( ProcessStructProperty(Cast<UStructProperty>(Property), PropertyValueAddress) )
			{
				return;
			}
			if ( ProcessInterfaceProperty(Cast<UInterfaceProperty>(Property), PropertyValueAddress) )
			{
				return;
			}
			if ( ProcessDelegateProperty(Cast<UDelegateProperty>(Property), PropertyValueAddress) )
			{
				return;
			}
			if ( ProcessMulticastDelegateProperty(Cast<UMulticastDelegateProperty>(Property), PropertyValueAddress) )
			{
				return;
			}
			if ( ProcessArrayProperty(Cast<UArrayProperty>(Property), PropertyValueAddress) )
			{
				return;
			}
			if ( ProcessSetProperty(Cast<USetProperty>(Property), PropertyValueAddress) )
			{
				return;
			}
			if ( ProcessMapProperty(Cast<UMapProperty>(Property), PropertyValueAddress) )
			{
				return;
			}
		}
	}
private:

	/**
	 * UArrayProperty version - invokes ProcessProperty on the array's Inner member for each element in the array.
	 *
	 * @param	ArrayProp				the property to process
	 * @param	PropertyValueAddress	the address of the property's value
	 *
	 * @return	true if the property was handled by this method
	 */
	bool ProcessArrayProperty( UArrayProperty* ArrayProp, uint8* PropertyValueAddress )
	{
		bool bResult = false;

		if ( ArrayProp != NULL )
		{
			FScriptArray* ArrayValuePtr = ArrayProp->GetPropertyValuePtr(PropertyValueAddress);

			uint8* ArrayValue = (uint8*)ArrayValuePtr->GetData();
			for ( int32 ArrayIndex = 0; ArrayIndex < ArrayValuePtr->Num(); ArrayIndex++ )
			{
				ProcessProperty(ArrayProp->Inner, ArrayValue + ArrayIndex * ArrayProp->Inner->ElementSize);
			}

			bResult = true;
		}

		return bResult;
	}

	/**
	 * USetProperty version - invokes ProcessProperty on the each item in the set
	 *
	 * @param	SetProp					the property to process
	 * @param	PropertyValueAddress	the address of the property's value
	 *
	 * @return	true if the property was handled by this method
	 */
	bool ProcessSetProperty( USetProperty* SetProp, uint8* PropertyValueAddress )
	{
		bool bResult = false;

		if (SetProp != NULL)
		{
			FScriptSet* SetValuePtr = SetProp->GetPropertyValuePtr(PropertyValueAddress);

			FScriptSetLayout SetLayout = SetValuePtr->GetScriptLayout(SetProp->ElementProp->ElementSize, SetProp->ElementProp->GetMinAlignment());
			int32 ItemsLeft = SetValuePtr->Num();

			for (int32 Index = 0; ItemsLeft > 0; ++Index)
			{
				if (SetValuePtr->IsValidIndex(Index))
				{
					--ItemsLeft;
					ProcessProperty(SetProp->ElementProp, (uint8*)SetValuePtr->GetData(Index, SetLayout));
				}
			}

			bResult = true;
		}

		return bResult;
	}

	/**
	 * UMapProperty version - invokes ProcessProperty on each item in the map
	 *
	 * @param	MapProp					the property to process
	 * @param	PropertyValueAddress	the address of the property's value
	 *
	 * @return	true if the property was handled by this method
	 */
	bool ProcessMapProperty( UMapProperty* MapProp, uint8* PropertyValueAddress )
	{
		bool bResult = false;

		if (MapProp != NULL)
		{
			FScriptMap* MapValuePtr = MapProp->GetPropertyValuePtr(PropertyValueAddress);

			FScriptMapLayout MapLayout = MapValuePtr->GetScriptLayout(MapProp->KeyProp->ElementSize, MapProp->KeyProp->GetMinAlignment(), MapProp->ValueProp->ElementSize, MapProp->ValueProp->GetMinAlignment());
			int32 ItemsLeft = MapValuePtr->Num();

			for (int32 Index = 0; ItemsLeft > 0; ++Index)
			{
				if (MapValuePtr->IsValidIndex(Index))
				{
					--ItemsLeft;

					uint8* Data = (uint8*)MapValuePtr->GetData(Index, MapLayout);

					ProcessProperty(MapProp->KeyProp, MapProp->KeyProp->ContainerPtrToValuePtr<uint8>(Data));
					ProcessProperty(MapProp->ValueProp, MapProp->ValueProp->ContainerPtrToValuePtr<uint8>(Data));
				}
			}

			bResult = true;
		}

		return bResult;
	}

	/**
	 * UStructProperty version - invokes ProcessProperty on each property in the struct
	 *
	 * @param	StructProp				the property to process
	 * @param	PropertyValueAddress	the address of the property's value
	 *
	 * @return	true if the property was handled by this method
	 */
	bool ProcessStructProperty( UStructProperty* StructProp, uint8* PropertyValueAddress )
	{
		bool bResult = false;

		if ( StructProp != NULL )
		{
			for ( UProperty* Prop = StructProp->Struct->PropertyLink; Prop; Prop = Prop->PropertyLinkNext )
			{
				for ( int32 ArrayIndex = 0; ArrayIndex < Prop->ArrayDim; ArrayIndex++ )
				{
					ProcessProperty(Prop, Prop->ContainerPtrToValuePtr<uint8>(PropertyValueAddress, ArrayIndex));
				}
			}
			bResult = true;
		}

		return bResult;
	}

	/**
	 * UObjectProperty version - if the object located at the specified address is instanced, adds the component the list.
	 *
	 * @param	ObjectProp				the property to process
	 * @param	PropertyValueAddress	the address of the property's value
	 *
	 * @return	true if the property was handled by this method
	 */
	bool ProcessObjectProperty( UObjectPropertyBase* ObjectProp, uint8* PropertyValueAddress )
	{
		bool bResult = false;

		if ( ObjectProp != NULL )
		{
			UObject* ObjValue = ObjectProp->GetObjectPropertyValue(PropertyValueAddress);
			if (ObjectProp->PropertyFlags & CPF_InstancedReference)
			{
				Components.AddUnique(ObjValue);
			}

			bResult = true;
		}

		return bResult;
	}

	/**
	 * UInterfaceProperty version - if the FScriptInterface located at the specified address contains a reference to an instance, add the component to the list.
	 *
	 * @param	InterfaceProp			the property to process
	 * @param	PropertyValueAddress	the address of the property's value
	 *
	 * @return	true if the property was handled by this method
	 */
	bool ProcessInterfaceProperty( UInterfaceProperty* InterfaceProp, uint8* PropertyValueAddress )
	{
		bool bResult = false;

		if ( InterfaceProp != NULL )
		{
			FScriptInterface* InterfaceValue = InterfaceProp->GetPropertyValuePtr(PropertyValueAddress);

			UObject* InterfaceObj = InterfaceValue->GetObject();
			if (InterfaceObj && InterfaceObj->IsDefaultSubobject())
			{
				Components.AddUnique(InterfaceValue->GetObject());
			}
			bResult = true;
		}

		return bResult;
	}

	/**
	 * UDelegateProperty version - if the FScriptDelegate located at the specified address contains a reference to an instance, add the component to the list.
	 *
	 * @param	DelegateProp			the property to process
	 * @param	PropertyValueAddress	the address of the property's value
	 *
	 * @return	true if the property was handled by this method
	 */
	bool ProcessDelegateProperty( UDelegateProperty* DelegateProp, uint8* PropertyValueAddress )
	{
		bool bResult = false;

		if ( DelegateProp != NULL )
		{
			FScriptDelegate* DelegateValue = DelegateProp->GetPropertyValuePtr(PropertyValueAddress);
			if (DelegateValue->GetUObject() && DelegateValue->GetUObject()->IsDefaultSubobject())
			{
				Components.AddUnique(DelegateValue->GetUObject());
			}

			bResult = true;
		}

		return bResult;
	}

	/**
	 * UMulticastDelegateProperty version - if the FMulticastScriptDelegate located at the specified address contains a reference to an instance, add the component to the list.
	 *
	 * @param	MulticastDelegateProp	the property to process
	 * @param	PropertyValueAddress	the address of the property's value
	 *
	 * @return	true if the property was handled by this method
	 */
	bool ProcessMulticastDelegateProperty( UMulticastDelegateProperty* MulticastDelegateProp, uint8* PropertyValueAddress )
	{
		bool bResult = false;

		if ( MulticastDelegateProp != NULL )
		{
			if (const FMulticastScriptDelegate* MulticastDelegateValue = MulticastDelegateProp->GetMulticastDelegate(PropertyValueAddress))
			{
				TArray<UObject*> AllObjects = MulticastDelegateValue->GetAllObjects();
				for (TArray<UObject*>::TConstIterator CurObjectIt(AllObjects); CurObjectIt; ++CurObjectIt)
				{
					if ((*CurObjectIt)->IsDefaultSubobject())
					{
						Components.AddUnique((*CurObjectIt));
					}
				}
			}

			bResult = true;
		}

		return bResult;
	}

};

bool FPropertyNode::GetDiffersFromDefaultForObject( FPropertyItemValueDataTrackerSlate& ValueTracker, UProperty* InProperty )
{	
	check( InProperty );

	bool bDiffersFromDefaultForObject = false;

	if ( ValueTracker.IsValidTracker() && ValueTracker.HasDefaultValue() && GetParentNode() != NULL )
	{
		if (ValueTracker.GetPropertyDefaultBaseAddress() != NULL)
		{
			//////////////////////////
			// Check the property against its default.
			// If the property is an object property, we have to take special measures.
			UArrayProperty* OuterArrayProperty = Cast<UArrayProperty>(InProperty->GetOuter());
			USetProperty* OuterSetProperty = Cast<USetProperty>(InProperty->GetOuter());
			UMapProperty* OuterMapProperty = Cast<UMapProperty>(InProperty->GetOuter());

			if ( OuterArrayProperty != NULL )
			{
				// make sure we're not trying to compare against an element that doesn't exist
				if (GetArrayIndex() >= FScriptArrayHelper::Num(ValueTracker.GetPropertyDefaultBaseAddress()) )
				{
					bDiffersFromDefaultForObject = true;
				}
			}
			else if (OuterSetProperty != NULL)
			{
				FScriptSetHelper SetHelper(OuterSetProperty, ValueTracker.GetPropertyDefaultBaseAddress());
				bool bIsValidIndex = ArrayIndex >= 0 && ArrayIndex < SetHelper.Num();
				if (!bIsValidIndex)
				{
					bDiffersFromDefaultForObject = true;
				}
			}
			else if (OuterMapProperty != NULL)
			{
				FScriptMapHelper MapHelper(OuterMapProperty, ValueTracker.GetPropertyDefaultBaseAddress());
				bool bIsValidIndex = ArrayIndex >= 0 && ArrayIndex < MapHelper.Num();
				if (!bIsValidIndex)
				{
					bDiffersFromDefaultForObject = true;
				}
			}
		}

		// The property is a simple field.  Compare it against the enclosing object's default for that property.
		if ( !bDiffersFromDefaultForObject)
		{
			uint32 PortFlags = 0;
			UObjectPropertyBase* ObjectProperty = Cast<UObjectPropertyBase>(InProperty);
			if (InProperty->ContainsInstancedObjectProperty())
			{
				// Use PPF_DeepCompareInstances for component objects
				if (ObjectProperty)
				{
					PortFlags |= PPF_DeepCompareInstances;
				}
			}

			if ( ValueTracker.GetPropertyValueAddress() == NULL || ValueTracker.GetPropertyDefaultAddress() == NULL )
			{
				// if either are NULL, we had a dynamic array somewhere in our parent chain and the array doesn't
				// have enough elements in either the default or the object
				bDiffersFromDefaultForObject = true;
			}
			else if ( GetArrayIndex() == INDEX_NONE && InProperty->ArrayDim > 1 )
			{
				for ( int32 Idx = 0; !bDiffersFromDefaultForObject && Idx < InProperty->ArrayDim; Idx++ )
				{
					bDiffersFromDefaultForObject = !InProperty->Identical(
						ValueTracker.GetPropertyValueAddress() + Idx * InProperty->ElementSize,
						ValueTracker.GetPropertyDefaultAddress() + Idx * InProperty->ElementSize,
						PortFlags
						);
				}
			}
			else
			{
				uint8* PropertyValueAddr = ValueTracker.GetPropertyValueAddress();
				uint8* DefaultPropertyValueAddr = ValueTracker.GetPropertyDefaultAddress();

				if( PropertyValueAddr != NULL && DefaultPropertyValueAddr != NULL )
				{
					bDiffersFromDefaultForObject = !InProperty->Identical(
						PropertyValueAddr,
						DefaultPropertyValueAddr,
						PortFlags
						);
				}
			}
		}
	}

	return bDiffersFromDefaultForObject;
}

/**
 * If there is a property, sees if it matches.  Otherwise sees if the entire parent structure matches
 */
bool FPropertyNode::GetDiffersFromDefault()
{
	if( bUpdateDiffersFromDefault )
	{
		bUpdateDiffersFromDefault = false;
		bDiffersFromDefault = false;

		FObjectPropertyNode* ObjectNode = FindObjectItemParent();
		if(ObjectNode && Property.IsValid() && !IsEditConst())
		{
			// Get an iterator for the enclosing objects.
			for(int32 ObjIndex = 0; ObjIndex < ObjectNode->GetNumObjects(); ++ObjIndex)
			{
				UObject* Object = ObjectNode->GetUObject(ObjIndex);

				TSharedPtr<FPropertyItemValueDataTrackerSlate> ValueTracker = GetValueTracker(Object, ObjIndex);

				if(ValueTracker.IsValid() && Object && GetDiffersFromDefaultForObject(*ValueTracker, Property.Get()))
				{
					// If any object being observed differs from the result then there is no need to keep searching
					bDiffersFromDefault = true;
					break;
				}
			}
		}
	}

	return bDiffersFromDefault;
}

FString FPropertyNode::GetDefaultValueAsStringForObject( FPropertyItemValueDataTrackerSlate& ValueTracker, UObject* InObject, UProperty* InProperty, bool bUseDisplayName)
{
	check( InObject );
	check( InProperty );

	bool bDiffersFromDefaultForObject = false;
	FString DefaultValue;

	// special case for Object class - no defaults to compare against
	if ( InObject != UObject::StaticClass() && InObject != UObject::StaticClass()->GetDefaultObject() )
	{
		if ( ValueTracker.IsValidTracker() && ValueTracker.HasDefaultValue() )
		{
			//////////////////////////
			// Check the property against its default.
			// If the property is an object property, we have to take special measures.
			UArrayProperty* OuterArrayProperty = Cast<UArrayProperty>(InProperty->GetOuter());
			USetProperty* OuterSetProperty = Cast<USetProperty>(InProperty->GetOuter());
			UMapProperty* OuterMapProperty = Cast<UMapProperty>(InProperty->GetOuter());

			// The property is a simple field.  Compare it against the enclosing object's default for that property.
			if ( !bDiffersFromDefaultForObject)
			{
				uint32 PortFlags = bUseDisplayName ? PPF_PropertyWindow : PPF_None;
				UObjectPropertyBase* ObjectProperty = Cast<UObjectPropertyBase>(InProperty);
				if (InProperty->ContainsInstancedObjectProperty())
				{
					// Use PPF_DeepCompareInstances for component objects
					if (ObjectProperty)
					{
						PortFlags |= PPF_DeepCompareInstances;
					}
				}

				if ( ValueTracker.GetPropertyDefaultAddress() == NULL )
				{
					// no default available, fall back on the default value for our primitive:
					uint8* TempComplexPropAddr = (uint8*)FMemory::Malloc(InProperty->GetSize(), InProperty->GetMinAlignment());
					InProperty->InitializeValue(TempComplexPropAddr);
					ON_SCOPE_EXIT
					{
						InProperty->DestroyValue(TempComplexPropAddr);
						FMemory::Free(TempComplexPropAddr);
					};
					
					InProperty->ExportText_Direct(DefaultValue, TempComplexPropAddr, TempComplexPropAddr, nullptr, PPF_None);
				}
				else if ( GetArrayIndex() == INDEX_NONE && InProperty->ArrayDim > 1 )
				{
					UArrayProperty::ExportTextInnerItem(DefaultValue, InProperty, ValueTracker.GetPropertyDefaultAddress(), InProperty->ArrayDim,
														ValueTracker.GetPropertyDefaultAddress(), InProperty->ArrayDim, nullptr, PortFlags);
				}
				else
				{
					// Port flags will cause enums to display correctly
					InProperty->ExportTextItem( DefaultValue, ValueTracker.GetPropertyDefaultAddress(), ValueTracker.GetPropertyDefaultAddress(), InObject, PortFlags, NULL );
				}
			}
		}
	}

	return DefaultValue;
}

FString FPropertyNode::GetDefaultValueAsString(bool bUseDisplayName)
{
	FObjectPropertyNode* ObjectNode = FindObjectItemParent();
	FString DefaultValue;
	if ( ObjectNode && Property.IsValid() )
	{
		// Get an iterator for the enclosing objects.
		for ( int32 ObjIndex = 0; ObjIndex < ObjectNode->GetNumObjects(); ++ObjIndex )
		{
			UObject* Object = ObjectNode->GetUObject( ObjIndex );
			TSharedPtr<FPropertyItemValueDataTrackerSlate> ValueTracker = GetValueTracker(Object, ObjIndex);

			if( Object && ValueTracker.IsValid() )
			{
				FString NodeDefaultValue = GetDefaultValueAsStringForObject( *ValueTracker, Object, Property.Get(), bUseDisplayName );
				if ( DefaultValue.Len() > 0 && NodeDefaultValue.Len() > 0)
				{
					DefaultValue += TEXT(", ");
				}
				DefaultValue += NodeDefaultValue;
			}
		}
	}

	return DefaultValue;
}

FText FPropertyNode::GetResetToDefaultLabel()
{
	FString DefaultValue = GetDefaultValueAsString();
	FText OutLabel = GetDisplayName();
	if ( DefaultValue.Len() )
	{
		const int32 MaxValueLen = 60;

		if ( DefaultValue.Len() > MaxValueLen )
		{
			DefaultValue = DefaultValue.Left( MaxValueLen );
			DefaultValue += TEXT( "..." );
		}

		return FText::Format(NSLOCTEXT("FPropertyNode", "ResetToDefaultLabelFmt", "{0}: {1}"), OutLabel, FText::FromString(DefaultValue));
	}

	return OutLabel;
}

bool FPropertyNode::IsReorderable()
{
	UProperty* NodeProperty = GetProperty();
	if (NodeProperty == nullptr)
	{
		return false;
	}
	// It is reorderable if the parent is an array and metadata doesn't prohibit it
	const UArrayProperty* OuterArrayProp = Cast<UArrayProperty>(NodeProperty->GetOuter());

	static const FName Name_DisableReordering("EditFixedOrder");
	static const FName NAME_ArraySizeEnum("ArraySizeEnum");
	return OuterArrayProp != nullptr 
		&& !OuterArrayProp->HasMetaData(Name_DisableReordering)
		&& !IsEditConst()
		&& !OuterArrayProp->HasMetaData(NAME_ArraySizeEnum)
		&& !FApp::IsGame();
}

/**
 * Helper function to obtain the display name for an enum property
 * @param InEnum		The enum whose metadata to pull from
 * @param DisplayName	The name of the enum value to adjust
 *
 * @return	true if the DisplayName has been changed
 */
bool FPropertyNode::AdjustEnumPropDisplayName( UEnum *InEnum, FString& DisplayName ) const
{
	// see if we have alternate text to use for displaying the value
	UMetaData* PackageMetaData = InEnum->GetOutermost()->GetMetaData();
	if ( PackageMetaData )
	{
		FName AltDisplayName = FName(*(DisplayName+TEXT(".DisplayName")));
		FString ValueText = PackageMetaData->GetValue(InEnum, AltDisplayName);
		if ( ValueText.Len() > 0 )
		{
			// use the alternate text for this enum value
			DisplayName = ValueText;
			return true;
		}
	}	

	//DisplayName has been unmodified
	return false;
}

/**Walks up the hierachy and return true if any parent node is a favorite*/
bool FPropertyNode::IsChildOfFavorite (void) const
{
	for (const FPropertyNode* TestParentNode = GetParentNode(); TestParentNode != NULL; TestParentNode = TestParentNode->GetParentNode())
	{
		if (TestParentNode->HasNodeFlags(EPropertyNodeFlags::IsFavorite))
		{
			return true;
		}
	}
	return false;
}

/**
 * Destroys all node within the hierarchy
 */
void FPropertyNode::DestroyTree(const bool bInDestroySelf)
{
	ChildNodes.Empty();
}

/**
 * Marks windows as visible based on the filter strings (EVEN IF normally NOT EXPANDED)
 */
void FPropertyNode::FilterNodes( const TArray<FString>& InFilterStrings, const bool bParentSeenDueToFiltering )
{
	//clear flags first.  Default to hidden
	SetNodeFlags(EPropertyNodeFlags::IsSeenDueToFiltering | EPropertyNodeFlags::IsSeenDueToChildFiltering | EPropertyNodeFlags::IsParentSeenDueToFiltering, false);
	SetNodeFlags(EPropertyNodeFlags::IsBeingFiltered, InFilterStrings.Num() > 0 );

	//FObjectPropertyNode* ParentPropertyNode = FindObjectItemParent();
	//@todo slate property window
	bool bMultiObjectOnlyShowDiffering = false;/*TopPropertyWindow->HasFlags(EPropertyWindowFlags::ShowOnlyDifferingItems) && (ParentPropertyNode->GetNumObjects()>1)*/;

	if (InFilterStrings.Num() > 0 /*|| (TopPropertyWindow->HasFlags(EPropertyWindowFlags::ShowOnlyModifiedItems)*/ || bMultiObjectOnlyShowDiffering)
	{
		//if filtering, default to NOT-seen
		bool bPassedFilter = false;	//assuming that we aren't filtered

		//see if this is a filter-able primitive
		FText DisplayName = GetDisplayName();
		const FString& DisplayNameStr = DisplayName.ToString();
		TArray <FString> AcceptableNames;
		AcceptableNames.Add(DisplayNameStr);

		//get the basic name as well of the property
		UProperty* TheProperty = GetProperty();
		if (TheProperty && (TheProperty->GetName() != DisplayNameStr))
		{
			AcceptableNames.Add(TheProperty->GetName());
		}

		bPassedFilter = IsFilterAcceptable(AcceptableNames, InFilterStrings);

		if (bPassedFilter)
		{
			SetNodeFlags(EPropertyNodeFlags::IsSeenDueToFiltering, true);
		} 
		SetNodeFlags(EPropertyNodeFlags::IsParentSeenDueToFiltering, bParentSeenDueToFiltering);
	}
	else
	{
		//indicating that this node should not be force displayed, but opened normally
		SetNodeFlags(EPropertyNodeFlags::IsParentSeenDueToFiltering, true);
	}

	//default to doing only one pass
	//bool bCategoryOrObject = (GetObjectNode()) || (GetCategoryNode()!=NULL);
	int32 StartRecusionPass = HasNodeFlags(EPropertyNodeFlags::IsSeenDueToFiltering) ? 1 : 0;
	//Pass 1, if a pass 1 exists (object or category), is to see if there are any children that pass the filter, if any do, trim the tree to the leaves.
	//	This will stop categories from showing ALL properties if they pass the filter AND a child passes the filter
	//Pass 0, if no child exists that passes the filter OR this node didn't pass the filter
	for (int32 RecursionPass = StartRecusionPass; RecursionPass >= 0; --RecursionPass)
	{
		for (int32 scan = 0; scan < ChildNodes.Num(); ++scan)
		{
			TSharedPtr<FPropertyNode>& ScanNode = ChildNodes[scan];
			check(ScanNode.IsValid());
			//default to telling the children this node is NOT visible, therefore if not in the base pass, only filtered nodes will survive the filtering process.
			bool bChildParamParentVisible = false;
			//if we're at the base pass, tell the children the truth about visibility
			if (RecursionPass == 0)
			{
				bChildParamParentVisible = bParentSeenDueToFiltering || HasNodeFlags(EPropertyNodeFlags::IsSeenDueToFiltering);
			}
			ScanNode->FilterNodes(InFilterStrings, bChildParamParentVisible);

			if (ScanNode->HasNodeFlags(EPropertyNodeFlags::IsSeenDueToFiltering | EPropertyNodeFlags::IsSeenDueToChildFiltering))
			{
				SetNodeFlags(EPropertyNodeFlags::IsSeenDueToChildFiltering, true);
			}
		}
		//now that we've tried a pass at our children, if any of them have been successfully seen due to filtering, just quit now
		if (HasNodeFlags(EPropertyNodeFlags::IsSeenDueToChildFiltering))
		{
			break;
		}
	}

	
}

void FPropertyNode::ProcessSeenFlags(const bool bParentAllowsVisible )
{
	// Set initial state first
	SetNodeFlags(EPropertyNodeFlags::IsSeen, false);
	SetNodeFlags(EPropertyNodeFlags::IsSeenDueToChildFavorite, false );

	bool bAllowChildrenVisible;

	if ( AsObjectNode() )
	{
		bAllowChildrenVisible = true;
	}
	else
	{
		//can't show children unless they are seen due to child filtering
		bAllowChildrenVisible = !!HasNodeFlags(EPropertyNodeFlags::IsSeenDueToChildFiltering);	
	}

	//process children
	for (int32 scan = 0; scan < ChildNodes.Num(); ++scan)
	{
		TSharedPtr<FPropertyNode>& ScanNode = ChildNodes[scan];
		check(ScanNode.IsValid());
		ScanNode->ProcessSeenFlags(bParentAllowsVisible && bAllowChildrenVisible );	//both parent AND myself have to allow children
	}

	if (HasNodeFlags(EPropertyNodeFlags::IsSeenDueToFiltering | EPropertyNodeFlags::IsSeenDueToChildFiltering))
	{
		SetNodeFlags(EPropertyNodeFlags::IsSeen, true); 
	}
	else 
	{
		//Finally, apply the REAL IsSeen
		SetNodeFlags(EPropertyNodeFlags::IsSeen, bParentAllowsVisible && HasNodeFlags(EPropertyNodeFlags::IsParentSeenDueToFiltering));
	}
}

/**
 * Marks windows as visible based their favorites status
 */
void FPropertyNode::ProcessSeenFlagsForFavorites(void)
{
	if( !HasNodeFlags(EPropertyNodeFlags::IsFavorite) ) 
	{
		bool bAnyChildFavorites = false;
		//process children
		for (int32 scan = 0; scan < ChildNodes.Num(); ++scan)
		{
			TSharedPtr<FPropertyNode>& ScanNode = ChildNodes[scan];
			check(ScanNode.IsValid());
			ScanNode->ProcessSeenFlagsForFavorites();
			bAnyChildFavorites = bAnyChildFavorites || ScanNode->HasNodeFlags(EPropertyNodeFlags::IsFavorite | EPropertyNodeFlags::IsSeenDueToChildFavorite);
		}
		if (bAnyChildFavorites)
		{
			SetNodeFlags(EPropertyNodeFlags::IsSeenDueToChildFavorite, true);
		}
	}
}


void FPropertyNode::NotifyPreChange( UProperty* PropertyAboutToChange, FNotifyHook* InNotifyHook )
{
	TSharedRef<FEditPropertyChain> PropertyChain = BuildPropertyChain( PropertyAboutToChange );

	// Call through to the property window's notify hook.
	if( InNotifyHook )
	{
		if ( PropertyChain->Num() == 0 )
		{
			InNotifyHook->NotifyPreChange( PropertyAboutToChange );
		}
		else
		{
			InNotifyHook->NotifyPreChange( &PropertyChain.Get() );
		}
	}

	FObjectPropertyNode* ObjectNode = FindObjectItemParent();
	if( ObjectNode )
	{
		UProperty* CurProperty = PropertyAboutToChange;

		// Call PreEditChange on the object chain.
		while ( true )
		{
			for( TPropObjectIterator Itor( ObjectNode->ObjectIterator() ) ; Itor ; ++Itor )
			{
				UObject* Object = Itor->Get();
				if ( ensure( Object ) && PropertyChain->Num() == 0 )
				{
					Object->PreEditChange( Property.Get() );

				}
				else if( ensure( Object ) )
				{
					Object->PreEditChange( *PropertyChain );
				}
			}

			// Pass this property to the parent's PreEditChange call.
			CurProperty = ObjectNode->GetStoredProperty();
			FObjectPropertyNode* PreviousObjectNode = ObjectNode;

			// Traverse up a level in the nested object tree.
			ObjectNode = NotifyFindObjectItemParent( ObjectNode );
			if ( !ObjectNode )
			{
				// We've hit the root -- break.
				break;
			}
			else if ( PropertyChain->Num() > 0 )
			{
				PropertyChain->SetActivePropertyNode( CurProperty->GetOwnerProperty() );
				for ( FPropertyNode* BaseItem = PreviousObjectNode; BaseItem && BaseItem != ObjectNode; BaseItem = BaseItem->GetParentNode())
				{
					UProperty* ItemProperty = BaseItem->GetProperty();
					if ( ItemProperty == NULL )
					{
						// if this property item doesn't have a Property, skip it...it may be a category item or the virtual
						// item used as the root for an inline object
						continue;
					}

					// skip over property window items that correspond to a single element in a static array, or
					// the inner property of another UProperty (e.g. UArrayProperty->Inner)
					if ( BaseItem->ArrayIndex == INDEX_NONE && ItemProperty->GetOwnerProperty() == ItemProperty )
					{
						PropertyChain->SetActiveMemberPropertyNode(ItemProperty);
					}
				}
			}
		}
	}

	// Broadcast the change to any listeners
	BroadcastPropertyPreChangeDelegates();
}

void FPropertyNode::NotifyPostChange( FPropertyChangedEvent& InPropertyChangedEvent, class FNotifyHook* InNotifyHook )
{
	TSharedRef<FEditPropertyChain> PropertyChain = BuildPropertyChain( InPropertyChangedEvent.Property );
	
	// remember the property that was the chain's original active property; this will correspond to the outermost property of struct/array that was modified
	UProperty* const OriginalActiveProperty = PropertyChain->GetActiveMemberNode()->GetValue();

	FObjectPropertyNode* ObjectNode = FindObjectItemParent();
	if( ObjectNode )
	{
		ObjectNode->InvalidateCachedState();

		UProperty* CurProperty = InPropertyChangedEvent.Property;

		// Fire ULevel::LevelDirtiedEvent when falling out of scope.
		FScopedLevelDirtied	LevelDirtyCallback;

		// Call PostEditChange on the object chain.
		while ( true )
		{
			int32 CurrentObjectIndex = 0;
			for( TPropObjectIterator Itor( ObjectNode->ObjectIterator() ) ; Itor ; ++Itor )
			{
				UObject* Object = Itor->Get();
				if ( PropertyChain->Num() == 0 )
				{
					//copy 
					FPropertyChangedEvent ChangedEvent = InPropertyChangedEvent;
					if (CurProperty != InPropertyChangedEvent.Property)
					{
						//parent object node property.  Reset other internals and leave the event type as unspecified
						ChangedEvent = FPropertyChangedEvent(CurProperty, InPropertyChangedEvent.ChangeType);
					}
					ChangedEvent.ObjectIteratorIndex = CurrentObjectIndex;
					if( Object )
					{
						Object->PostEditChangeProperty( ChangedEvent );
					}
				}
				else
				{
					FPropertyChangedEvent ChangedEvent = InPropertyChangedEvent;
					if (CurProperty != InPropertyChangedEvent.Property)
					{
						//parent object node property.  Reset other internals and leave the event type as unspecified
						ChangedEvent = FPropertyChangedEvent(CurProperty, InPropertyChangedEvent.ChangeType);
					}
					FPropertyChangedChainEvent ChainEvent(*PropertyChain, ChangedEvent);
					ChainEvent.ObjectIteratorIndex = CurrentObjectIndex;
					if( Object )
					{
						Object->PostEditChangeChainProperty(ChainEvent);
					}
				}
				LevelDirtyCallback.Request();
				++CurrentObjectIndex;
			}


			// Pass this property to the parent's PostEditChange call.
			CurProperty = ObjectNode->GetStoredProperty();
			FObjectPropertyNode* PreviousObjectNode = ObjectNode;

			// Traverse up a level in the nested object tree.
			ObjectNode = NotifyFindObjectItemParent( ObjectNode );
			if ( !ObjectNode )
			{
				// We've hit the root -- break.
				break;
			}
			else if ( PropertyChain->Num() > 0 )
			{
				PropertyChain->SetActivePropertyNode(CurProperty->GetOwnerProperty());
				for ( FPropertyNode* BaseItem = PreviousObjectNode; BaseItem && BaseItem != ObjectNode; BaseItem = BaseItem->GetParentNode())
				{
					UProperty* ItemProperty = BaseItem->GetProperty();
					if ( ItemProperty == NULL )
					{
						// if this property item doesn't have a Property, skip it...it may be a category item or the virtual
						// item used as the root for an inline object
						continue;
					}

					// skip over property window items that correspond to a single element in a static array, or
					// the inner property of another UProperty (e.g. UArrayProperty->Inner)
					if ( BaseItem->GetArrayIndex() == INDEX_NONE && ItemProperty->GetOwnerProperty() == ItemProperty )
					{
						PropertyChain->SetActiveMemberPropertyNode(ItemProperty);
					}
				}
			}
		}
	}

	// Broadcast the change to any listeners
	BroadcastPropertyChangedDelegates();

	// Call through to the property window's notify hook.
	if( InNotifyHook )
	{
		if ( PropertyChain->Num() == 0 )
		{
			InNotifyHook->NotifyPostChange( InPropertyChangedEvent, InPropertyChangedEvent.Property );
		}
		else
		{
			PropertyChain->SetActiveMemberPropertyNode( OriginalActiveProperty );
			PropertyChain->SetActivePropertyNode( InPropertyChangedEvent.Property);
		
			InPropertyChangedEvent.SetActiveMemberProperty(OriginalActiveProperty);
			InNotifyHook->NotifyPostChange( InPropertyChangedEvent, &PropertyChain.Get() );
		}
	}


	if( OriginalActiveProperty )
	{
		//if i have metadata forcing other property windows to rebuild
		const FString& MetaData = OriginalActiveProperty->GetMetaData(TEXT("ForceRebuildProperty"));

		if( MetaData.Len() > 0 )
		{
			// We need to find the property node beginning at the root/parent, not at our own node.
			ObjectNode = FindObjectItemParent();
			check(ObjectNode != NULL);

			TSharedPtr<FPropertyNode> ForceRebuildNode = ObjectNode->FindChildPropertyNode( FName(*MetaData), true );

			if( ForceRebuildNode.IsValid() )
			{
				ForceRebuildNode->RequestRebuildChildren();
			}
		}
	}

	// The value has changed so the cached value could be invalid
	// Need to recurse here as we might be editing a struct with child properties that need re-caching
	ClearCachedReadAddresses(true);

	// Redraw viewports
	FEditorSupportDelegates::RedrawAllViewports.Broadcast();
}


void FPropertyNode::BroadcastPropertyChangedDelegates()
{
	PropertyValueChangedEvent.Broadcast();

	// Walk through the parents and broadcast
	FPropertyNode* LocalParentNode = GetParentNode();
	while( LocalParentNode )
	{
		if( LocalParentNode->OnChildPropertyValueChanged().IsBound() )
		{
			LocalParentNode->OnChildPropertyValueChanged().Broadcast();
		}

		LocalParentNode = LocalParentNode->GetParentNode();
	}

}

void FPropertyNode::BroadcastPropertyPreChangeDelegates()
{
	PropertyValuePreChangeEvent.Broadcast();

	// Walk through the parents and broadcast
	FPropertyNode* LocalParentNode = GetParentNode();
	while (LocalParentNode)
	{
		if (LocalParentNode->OnChildPropertyValuePreChange().IsBound())
		{
			LocalParentNode->OnChildPropertyValuePreChange().Broadcast();
		}

		LocalParentNode = LocalParentNode->GetParentNode();
	}

}

void FPropertyNode::BroadcastPropertyResetToDefault()
{
	PropertyResetToDefaultEvent.Broadcast();
}

void FPropertyNode::SetOnRebuildChildren( FSimpleDelegate InOnRebuildChildren )
{
	OnRebuildChildren = InOnRebuildChildren;
}

TSharedPtr< FPropertyItemValueDataTrackerSlate > FPropertyNode::GetValueTracker( UObject* Object, uint32 ObjIndex )
{
	ensure( AsItemPropertyNode() );

	TSharedPtr< FPropertyItemValueDataTrackerSlate > RetVal;

	if( Object && Object != UObject::StaticClass() && Object != UObject::StaticClass()->GetDefaultObject() )
	{
		if( !ObjectDefaultValueTrackers.IsValidIndex(ObjIndex) )
		{
			uint32 NumToAdd = (ObjIndex - ObjectDefaultValueTrackers.Num()) + 1;
			while( NumToAdd > 0 )
			{
				ObjectDefaultValueTrackers.Add( TSharedPtr<FPropertyItemValueDataTrackerSlate> () );
				--NumToAdd;
			}
		}

		TSharedPtr<FPropertyItemValueDataTrackerSlate>& ValueTracker = ObjectDefaultValueTrackers[ObjIndex];
		if( !ValueTracker.IsValid() )
		{
			ValueTracker = MakeShareable( new FPropertyItemValueDataTrackerSlate( this, Object ) );
		}
		else
		{
			ValueTracker->Reset(this, Object);
		}
		RetVal = ValueTracker;

	}

	return RetVal;

}

TSharedRef<FEditPropertyChain> FPropertyNode::BuildPropertyChain( UProperty* InProperty )
{
	TSharedRef<FEditPropertyChain> PropertyChain( MakeShareable( new FEditPropertyChain ) );

	FPropertyNode* ItemNode = this;

	FComplexPropertyNode* ComplexNode = FindComplexParent();
	UProperty* MemberProperty = InProperty;

	do
	{
		if (ItemNode == ComplexNode)
		{
			MemberProperty = PropertyChain->GetHead()->GetValue();
		}

		UProperty* TheProperty	= ItemNode->GetProperty();
		if ( TheProperty )
		{
			// Skip over property window items that correspond to a single element in a static array,
			// or the inner property of another UProperty (e.g. UArrayProperty->Inner).
			if ( ItemNode->GetArrayIndex() == INDEX_NONE && TheProperty->GetOwnerProperty() == TheProperty )
			{
				PropertyChain->AddHead( TheProperty );
			}
		}
		ItemNode = ItemNode->GetParentNode();
	} 
	while( ItemNode != NULL );

	// If the modified property was a property of the object at the root of this property window, the member property will not have been set correctly
	if (ItemNode == ComplexNode)
	{
		MemberProperty = PropertyChain->GetHead()->GetValue();
	}

	PropertyChain->SetActivePropertyNode( InProperty );
	PropertyChain->SetActiveMemberPropertyNode( MemberProperty );

	return PropertyChain;
}

FPropertyChangedEvent& FPropertyNode::FixPropertiesInEvent(FPropertyChangedEvent& Event)
{
	ensure(Event.Property);

	auto PropertyChain = BuildPropertyChain(Event.Property);
	auto MemberProperty = PropertyChain->GetActiveMemberNode() ? PropertyChain->GetActiveMemberNode()->GetValue() : NULL;
	if (ensure(MemberProperty))
	{
		Event.SetActiveMemberProperty(MemberProperty);
	}

	return Event;
}

void FPropertyNode::SetInstanceMetaData(const FName& Key, const FString& Value)
{
	InstanceMetaData.Add(Key, Value);
}

const FString* FPropertyNode::GetInstanceMetaData(const FName& Key) const
{
	return InstanceMetaData.Find(Key);
}

const TMap<FName, FString>* FPropertyNode::GetInstanceMetaDataMap() const
{
	return &InstanceMetaData;
}

bool FPropertyNode::ParentOrSelfHasMetaData(const FName& MetaDataKey) const
{
	return (Property.IsValid() && Property->HasMetaData(MetaDataKey)) || (ParentNode && ParentNode->ParentOrSelfHasMetaData(MetaDataKey));
}

void FPropertyNode::InvalidateCachedState()
{
	bUpdateDiffersFromDefault = true;
	bUpdateEditConstState = true;

	for( TSharedPtr<FPropertyNode>& ChildNode : ChildNodes )
	{
		ChildNode->InvalidateCachedState();
	}
}

/**
 * Does the string compares to ensure this Name is acceptable to the filter that is passed in
 * @return		Return True if this property should be displayed.  False if it should be culled
 */
bool FPropertyNode::IsFilterAcceptable(const TArray<FString>& InAcceptableNames, const TArray<FString>& InFilterStrings)
{
	bool bCompleteMatchFound = true;
	if (InFilterStrings.Num())
	{
		//we have to make sure one name matches all criteria
		for (int32 TestNameIndex = 0; TestNameIndex < InAcceptableNames.Num(); ++TestNameIndex)
		{
			bCompleteMatchFound = true;

			FString TestName = InAcceptableNames[TestNameIndex];
			for (int32 scan = 0; scan < InFilterStrings.Num(); scan++)
			{
				if (!TestName.Contains(InFilterStrings[scan])) 
				{
					bCompleteMatchFound = false;
					break;
				}
			}
			if (bCompleteMatchFound)
			{
				break;
			}
		}
	}
	return bCompleteMatchFound;
}

void FPropertyNode::PropagateContainerPropertyChange( UObject* ModifiedObject, const void* OriginalContainerAddr, EPropertyArrayChangeType::Type ChangeType, int32 Index, TMap<UObject*, bool>* PropagationResult, int32 SwapIndex /*= INDEX_NONE*/)
{
	check(OriginalContainerAddr);

	UProperty* NodeProperty = GetProperty();
	UArrayProperty* ArrayProperty = NULL;
	USetProperty* SetProperty = NULL;
	UMapProperty* MapProperty = NULL;

	FPropertyNode* ParentPropertyNode = GetParentNode();
	
	UProperty* ConvertedProperty = NULL;

	if (ChangeType == EPropertyArrayChangeType::Add || ChangeType == EPropertyArrayChangeType::Clear)
	{
		ConvertedProperty = NodeProperty;
	}
	else
	{
		ConvertedProperty = Cast<UProperty>(NodeProperty->GetOuter());
	}

	ArrayProperty = Cast<UArrayProperty>(ConvertedProperty);
	SetProperty = Cast<USetProperty>(ConvertedProperty);
	MapProperty = Cast<UMapProperty>(ConvertedProperty);

	check(ArrayProperty || SetProperty || MapProperty);

	TArray<UObject*> ArchetypeInstances, ObjectsToChange;
	FPropertyNode* SubobjectPropertyNode = NULL;
	UObject* Object = ModifiedObject;

	if (Object->HasAnyFlags(RF_ClassDefaultObject|RF_ArchetypeObject))
	{
		// Object is a default suobject, collect all instances.
		Object->GetArchetypeInstances(ArchetypeInstances);
	}
	else if (Object->HasAnyFlags(RF_DefaultSubObject) && Object->GetOuter()->HasAnyFlags(RF_ClassDefaultObject|RF_ArchetypeObject))
	{
		// Object is a default subobject of a default object. Get the subobject property node and use its owner instead.
		for (SubobjectPropertyNode = FindObjectItemParent(); SubobjectPropertyNode && !SubobjectPropertyNode->GetProperty(); SubobjectPropertyNode = SubobjectPropertyNode->GetParentNode());
		if (SubobjectPropertyNode != NULL)
		{
			// Switch the object to the owner default object and collect its instances.
			Object = Object->GetOuter();	
			Object->GetArchetypeInstances(ArchetypeInstances);
		}
	}

	ObjectsToChange.Push(Object);

	while (ObjectsToChange.Num() > 0)
	{
		check(ObjectsToChange.Num() > 0);

		// Pop the first object to change
		UObject* ObjToChange = ObjectsToChange[0];
		UObject* ActualObjToChange = NULL;
		ObjectsToChange.RemoveAt(0);
		
		if (SubobjectPropertyNode)
		{
			// If the original object is a subobject, get the current object's subobject too.
			// In this case we're not going to modify ObjToChange but its default subobject.
			ActualObjToChange = *(UObject**)SubobjectPropertyNode->GetValueBaseAddress((uint8*)ObjToChange);
		}
		else
		{
			ActualObjToChange = ObjToChange;
		}

		if (ActualObjToChange != ModifiedObject)
		{
			uint8* Addr = NULL;

			if (ChangeType == EPropertyArrayChangeType::Add || ChangeType == EPropertyArrayChangeType::Clear)
			{
				Addr = GetValueBaseAddress((uint8*)ActualObjToChange);
			}
			else
			{
				Addr = ParentPropertyNode->GetValueBaseAddress((uint8*)ActualObjToChange);
			}

			if (Addr != nullptr)
			{
				checkf(OriginalContainerAddr != Addr, TEXT("PropagateContainerPropertyChange tried to propagate a change onto itself!"));
				const bool bIsDefaultContainerContent = ConvertedProperty->Identical(OriginalContainerAddr, Addr);

				// Return instance changes result to caller
				if (PropagationResult != nullptr)
				{
					PropagationResult->Add(ActualObjToChange, bIsDefaultContainerContent);
				}

				if (ArrayProperty)
				{
					FScriptArrayHelper ArrayHelper(ArrayProperty, Addr);

					// Check if the original value was the default value and change it only then
					if (bIsDefaultContainerContent)
					{
						int32 ElementToInitialize = -1;
						switch (ChangeType)
						{
						case EPropertyArrayChangeType::Add:
							ElementToInitialize = ArrayHelper.AddValue();
							break;
						case EPropertyArrayChangeType::Clear:
							ArrayHelper.EmptyValues();
							break;
						case EPropertyArrayChangeType::Insert:
							ArrayHelper.InsertValues(ArrayIndex, 1);
							ElementToInitialize = ArrayIndex;
							break;
						case EPropertyArrayChangeType::Delete:
							ArrayHelper.RemoveValues(ArrayIndex, 1);
							break;
						case EPropertyArrayChangeType::Duplicate:
							ArrayHelper.InsertValues(ArrayIndex, 1);
							// Copy the selected item's value to the new item.
							NodeProperty->CopyCompleteValue(ArrayHelper.GetRawPtr(ArrayIndex), ArrayHelper.GetRawPtr(ArrayIndex + 1));
							Object->InstanceSubobjectTemplates();
							break;
						case EPropertyArrayChangeType::Swap:
							if (SwapIndex != INDEX_NONE)
							{
								ArrayHelper.SwapValues(Index, SwapIndex);
							}
							break;
						}
					}
				}	// End Array

				else if (SetProperty)
				{
					FScriptSetHelper SetHelper(SetProperty, Addr);

					// Check if the original value was the default value and change it only then
					if (bIsDefaultContainerContent)
					{
						int32 ElementToInitialize = -1;
						switch (ChangeType)
						{
						case EPropertyArrayChangeType::Add:
							ElementToInitialize = SetHelper.AddDefaultValue_Invalid_NeedsRehash();
							SetHelper.Rehash();
							break;
						case EPropertyArrayChangeType::Clear:
							SetHelper.EmptyElements();
							break;
						case EPropertyArrayChangeType::Insert:
							check(false);	// Insert is not supported for sets
							break;
						case EPropertyArrayChangeType::Delete:
							SetHelper.RemoveAt(SetHelper.FindInternalIndex(ArrayIndex));
							SetHelper.Rehash();
							break;
						case EPropertyArrayChangeType::Duplicate:
							check(false);	// Duplicate not supported on sets
							break;
						}
					}
				}	// End Set
				else if (MapProperty)
				{
					FScriptMapHelper MapHelper(MapProperty, Addr);

					// Check if the original value was the default value and change it only then
					if (bIsDefaultContainerContent)
					{
						int32 ElementToInitialize = -1;
						switch (ChangeType)
						{
						case EPropertyArrayChangeType::Add:
							ElementToInitialize = MapHelper.AddDefaultValue_Invalid_NeedsRehash();
							MapHelper.Rehash();
							break;
						case EPropertyArrayChangeType::Clear:
							MapHelper.EmptyValues();
							break;
						case EPropertyArrayChangeType::Insert:
							check(false);	// Insert is not supported for maps
							break;
						case EPropertyArrayChangeType::Delete:
							MapHelper.RemoveAt(MapHelper.FindInternalIndex(ArrayIndex));
							MapHelper.Rehash();
							break;
						case EPropertyArrayChangeType::Duplicate:
							check(false);	// Duplicate is not supported for maps
							break;
						}
					}
				}	// End Map
			}
		}

		for (int32 i=0; i < ArchetypeInstances.Num(); ++i)
		{
			UObject* Obj = ArchetypeInstances[i];

			if (Obj->GetArchetype() == ObjToChange)
			{
				ObjectsToChange.Push(Obj);
				ArchetypeInstances.RemoveAt(i--);
			}
		}
	}
}

void FPropertyNode::PropagatePropertyChange( UObject* ModifiedObject, const TCHAR* NewValue, const FString& PreviousValue )
{
	TArray<UObject*> ArchetypeInstances, ObjectsToChange;
	FPropertyNode* SubobjectPropertyNode = NULL;
	UObject* Object = ModifiedObject;

	if (Object->HasAnyFlags(RF_ClassDefaultObject|RF_ArchetypeObject))
	{
		// Object is a default subobject, collect all instances.
		Object->GetArchetypeInstances(ArchetypeInstances);
	}
	else if (Object->HasAnyFlags(RF_DefaultSubObject) && Object->GetOuter()->HasAnyFlags(RF_ClassDefaultObject|RF_ArchetypeObject))
	{
		// Object is a default subobject of a default object. Get the subobject property node and use its owner instead.
		for (SubobjectPropertyNode = FindObjectItemParent(); SubobjectPropertyNode && !SubobjectPropertyNode->GetProperty(); SubobjectPropertyNode = SubobjectPropertyNode->GetParentNode());
		if (SubobjectPropertyNode != NULL)
		{
			// Switch the object to the owner default object and collect its instances.
			Object = Object->GetOuter();	
			Object->GetArchetypeInstances(ArchetypeInstances);
		}
	}

	static FName FNAME_EditableWhenInherited = GET_MEMBER_NAME_CHECKED(UActorComponent,bEditableWhenInherited);
	if (GetProperty()->GetFName() == FNAME_EditableWhenInherited && ModifiedObject->IsA<UActorComponent>() && FString(TEXT("False")) == NewValue)
	{
		FBlueprintEditorUtils::HandleDisableEditableWhenInherited(ModifiedObject, ArchetypeInstances);
	}

	FPropertyNode*  Parent          = GetParentNode();
	UProperty*      ParentProp      = Parent->GetProperty();
	UArrayProperty* ParentArrayProp = Cast<UArrayProperty>(ParentProp);
	UMapProperty*   ParentMapProp   = Cast<UMapProperty>(ParentProp);
	USetProperty*	ParentSetProp	= Cast<USetProperty>(ParentProp);
	UProperty*      Prop            = GetProperty();

	if (ParentArrayProp && ParentArrayProp->Inner != Prop)
	{
		ParentArrayProp = nullptr;
	}

	if (ParentMapProp && ParentMapProp->KeyProp != Prop && ParentMapProp->ValueProp != Prop)
	{
		ParentMapProp = nullptr;
	}

	if (ParentSetProp && ParentSetProp->ElementProp != Prop)
	{
		ParentSetProp = nullptr;
	}

	ObjectsToChange.Push(Object);

	while (ObjectsToChange.Num() > 0)
	{
		check(ObjectsToChange.Num() > 0);

		// Pop the first object to change
		UObject* ObjToChange = ObjectsToChange[0];
		UObject* ActualObjToChange = NULL;
		ObjectsToChange.RemoveAt(0);
		
		if (SubobjectPropertyNode)
		{
			// If the original object is a subobject, get the current object's subobject too.
			// In this case we're not going to modify ObjToChange but its default subobject.
			ActualObjToChange = *(UObject**)SubobjectPropertyNode->GetValueBaseAddress((uint8*)ObjToChange);
		}
		else
		{
			ActualObjToChange = ObjToChange;
		}

		if (ActualObjToChange != ModifiedObject)
		{
			uint8* DestSimplePropAddr = GetValueBaseAddress( (uint8*)ActualObjToChange );
			if (DestSimplePropAddr != nullptr)
			{
				UProperty* ComplexProperty = Prop;
				FPropertyNode* ComplexPropertyNode = this;
				if (ParentArrayProp || ParentMapProp || ParentSetProp)
				{
					ComplexProperty = ParentProp;
					ComplexPropertyNode = ParentNode;
				}
				
				uint8* DestComplexPropAddr = ComplexPropertyNode->GetValueBaseAddress((uint8*)ActualObjToChange);
				uint8* ModifiedComplexPropAddr = ComplexPropertyNode->GetValueBaseAddress((uint8*)ModifiedObject);

				bool bShouldImport = false;
				{
					uint8* TempComplexPropAddr = (uint8*)FMemory::Malloc(ComplexProperty->GetSize(), ComplexProperty->GetMinAlignment());
					ComplexProperty->InitializeValue(TempComplexPropAddr);
					ON_SCOPE_EXIT
					{
						ComplexProperty->DestroyValue(TempComplexPropAddr);
						FMemory::Free(TempComplexPropAddr);
					};

					// Importing the previous value into the temporary property can potentially affect shared state (such as FText display string values), so we back-up the current value 
					// before we do this, so that we can restore it once we've checked whether the two properties are identical
					// This ensures that shared state keeps the correct value, even if the destination property itself isn't imported (or only partly imported, as is the case with arrays/maps/sets)
					FString CurrentValue;
					ComplexProperty->ExportText_Direct(CurrentValue, ModifiedComplexPropAddr, ModifiedComplexPropAddr, ModifiedObject, PPF_None);
					ComplexProperty->ImportText(*PreviousValue, TempComplexPropAddr, PPF_None, ModifiedObject);
					bShouldImport = ComplexProperty->Identical(DestComplexPropAddr, TempComplexPropAddr, PPF_None);
					ComplexProperty->ImportText(*CurrentValue, TempComplexPropAddr, PPF_None, ModifiedObject);
				}

				// Only import if the value matches the previous value of the property that changed
				if (bShouldImport)
				{
					Prop->ImportText(NewValue, DestSimplePropAddr, PPF_None, ActualObjToChange);
				}
			}
		}

		for (int32 InstanceIndex = 0; InstanceIndex < ArchetypeInstances.Num(); ++InstanceIndex)
		{
			UObject* Obj = ArchetypeInstances[InstanceIndex];

			if (Obj->GetArchetype() == ObjToChange)
			{
				ObjectsToChange.Push(Obj);
				ArchetypeInstances.RemoveAt(InstanceIndex--);
			}
		}
	}
}

void FPropertyNode::AddRestriction( TSharedRef<const class FPropertyRestriction> Restriction )
{
	Restrictions.AddUnique(Restriction);
}

bool FPropertyNode::IsHidden(const FString& Value, TArray<FText>* OutReasons) const
{
	bool bIsHidden = false;
	for( auto It = Restrictions.CreateConstIterator() ; It ; ++It )
	{
		TSharedRef<const FPropertyRestriction> Restriction = (*It);
		if( Restriction->IsValueHidden(Value) )
		{
			bIsHidden = true;
			if (OutReasons)
			{
				OutReasons->Add(Restriction->GetReason());
			}
			else
			{
				break;
			}
		}
	}

	return bIsHidden;
}

bool FPropertyNode::IsDisabled(const FString& Value, TArray<FText>* OutReasons) const
{
	bool bIsDisabled = false;
	for (const TSharedRef<const FPropertyRestriction>& Restriction : Restrictions)
	{
		if( Restriction->IsValueDisabled(Value) )
		{
			bIsDisabled = true;
			if (OutReasons)
			{
				OutReasons->Add(Restriction->GetReason());
			}
			else
			{
				break;
			}
		}
	}

	return bIsDisabled;
}

bool FPropertyNode::IsRestricted(const FString& Value, TArray<FText>& OutReasons) const
{
	const bool bIsHidden = IsHidden(Value, &OutReasons);
	const bool bIsDisabled = IsDisabled(Value, &OutReasons);
	return (bIsHidden || bIsDisabled);
}

bool FPropertyNode::GenerateRestrictionToolTip(const FString& Value, FText& OutTooltip) const
{
	static FText ToolTipFormat = NSLOCTEXT("PropertyRestriction", "TooltipFormat ", "{0}{1}");
	static FText MultipleRestrictionsToolTopAdditionFormat = NSLOCTEXT("PropertyRestriction", "MultipleRestrictionToolTipAdditionFormat ", "({0} restrictions...)");

	TArray<FText> Reasons;
	const bool bRestricted = IsRestricted(Value, Reasons);

	FText Ret;
	if( bRestricted && Reasons.Num() > 0 )
	{
		if( Reasons.Num() > 1 )
		{
			FText NumberOfRestrictions = FText::AsNumber(Reasons.Num());

			OutTooltip = FText::Format(ToolTipFormat, Reasons[0], FText::Format(MultipleRestrictionsToolTopAdditionFormat,NumberOfRestrictions));
		}
		else
		{
			OutTooltip = FText::Format(ToolTipFormat, Reasons[0], FText());
		}
	}
	return bRestricted;
}

#undef LOCTEXT_NAMESPACE
