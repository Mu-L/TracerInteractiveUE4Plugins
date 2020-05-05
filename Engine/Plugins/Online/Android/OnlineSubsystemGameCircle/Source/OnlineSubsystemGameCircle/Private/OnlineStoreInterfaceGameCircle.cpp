// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlineStoreInterfaceGameCircle.h"
#include "OnlineSubsystemGameCircle.h"
#include "Async/TaskGraphInterfaces.h"
#include <jni.h>
#include "Android/AndroidPlatform.h"
#include "Android/AndroidJavaEnv.h"

////////////////////////////////////////////////////////////////////
/// Amazon Store Helper Request Response Codes
EInAppPurchaseState::Type GetInAppPurchaseStateFromAmazonResponseStatus(JNIEnv* jenv, jint responseStatus)
{
	EAmazonResponseStatus localResponse = (EAmazonResponseStatus)responseStatus;

	switch (localResponse)
	{
	case EAmazonResponseStatus::Successful:
		return EInAppPurchaseState::Success;
	case EAmazonResponseStatus::Failed:
		return EInAppPurchaseState::Failed;
	case EAmazonResponseStatus::NotSupported:
		return EInAppPurchaseState::NotAllowed;
	case EAmazonResponseStatus::AlreadyPurchased:
		return EInAppPurchaseState::AlreadyOwned;
	case EAmazonResponseStatus::InvalidSKU:
		return EInAppPurchaseState::Invalid;
	case EAmazonResponseStatus::Unknown:
	default:
		return EInAppPurchaseState::Unknown;
	}
}

////////////////////////////////////////////////////////////////////
/// FOnlineStoreGameCircle implementation

FOnlineStoreGameCircle::FOnlineStoreGameCircle(FOnlineSubsystemGameCircle* InSubsystem)
	: Subsystem( InSubsystem )
{
}


FOnlineStoreGameCircle::~FOnlineStoreGameCircle()
{
}


bool FOnlineStoreGameCircle::IsAllowedToMakePurchases()
{
	extern bool AndroidThunkCpp_Iap_IsAllowedToMakePurchases();
	return AndroidThunkCpp_Iap_IsAllowedToMakePurchases();
}


bool FOnlineStoreGameCircle::QueryForAvailablePurchases(const TArray<FString>& ProductIds, FOnlineProductInformationReadRef& InReadObject)
{
	ReadObject = InReadObject;
	ReadObject->ReadState = EOnlineAsyncTaskState::InProgress;

	TArray<bool> ConsumableFlags;
	ConsumableFlags.AddZeroed(ProductIds.Num());

	extern bool AndroidThunkCpp_Iap_QueryInAppPurchases(const TArray<FString>&, const TArray<bool>&);
	AndroidThunkCpp_Iap_QueryInAppPurchases(ProductIds, ConsumableFlags);

	return true;
}


JNI_METHOD void Java_com_epicgames_ue4_AmazonStoreHelper_nativeQueryComplete(JNIEnv* jenv, jobject thiz, jint responseStatus, jobjectArray productIDs, jobjectArray titles, jobjectArray descriptions, jobjectArray prices)
{
	TArray<FInAppPurchaseProductInfo> ProvidedProductInformation;
	EInAppPurchaseState::Type Result = GetInAppPurchaseStateFromAmazonResponseStatus(jenv, responseStatus);

	if (jenv && Result == EInAppPurchaseState::Success)
	{
		jsize NumProducts = jenv->GetArrayLength(productIDs);
		jsize NumTitles = jenv->GetArrayLength(titles);
		jsize NumDescriptions = jenv->GetArrayLength(descriptions);
		jsize NumPrices = jenv->GetArrayLength(prices);

		ensure((NumProducts == NumTitles) && (NumProducts == NumDescriptions) && (NumProducts == NumPrices));

		for (jsize Idx = 0; Idx < NumProducts; Idx++)
		{
			// Build the product information strings.

			FInAppPurchaseProductInfo NewProductInfo;

			NewProductInfo.Identifier = FJavaHelper::FStringFromLocalRef(jenv, (jstring)jenv->GetObjectArrayElement(productIDs, Idx));
			NewProductInfo.DisplayName = FJavaHelper::FStringFromLocalRef(jenv, (jstring)jenv->GetObjectArrayElement(titles, Idx));
			NewProductInfo.DisplayDescription = FJavaHelper::FStringFromLocalRef(jenv, (jstring)jenv->GetObjectArrayElement(descriptions, Idx));
			NewProductInfo.DisplayPrice = FJavaHelper::FStringFromLocalRef(jenv, (jstring)jenv->GetObjectArrayElement(prices, Idx));
			
			LexFromString(NewProductInfo.RawPrice, *NewProductInfo.DisplayPrice);

			ProvidedProductInformation.Add(NewProductInfo);

			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("\nProduct Identifier: %s, Name: %s, Description: %s, Price: %s, RawPrice: %.2f\n"),
				*NewProductInfo.Identifier,
				*NewProductInfo.DisplayName,
				*NewProductInfo.DisplayDescription,
				*NewProductInfo.DisplayPrice,
				NewProductInfo.RawPrice);
		}
	}


	DECLARE_CYCLE_STAT(TEXT("FSimpleDelegateGraphTask.ProcessQueryIapResult"), STAT_FSimpleDelegateGraphTask_ProcessQueryIapResult, STATGROUP_TaskGraphTasks);

	FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(
		FSimpleDelegateGraphTask::FDelegate::CreateLambda([=](){
			if (IOnlineSubsystem* const OnlineSub = IOnlineSubsystem::Get())
			{
				// call store implementation to process query results.
				if (FOnlineStoreGameCircle* StoreInterface = (FOnlineStoreGameCircle*)OnlineSub->GetStoreInterface().Get())
				{
					StoreInterface->ProcessQueryAvailablePurchasesResults(Result, ProvidedProductInformation);
				}				
			}
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("In-App Purchase query was completed  %s\n"), Result == EInAppPurchaseState::Success ? TEXT("successfully") : TEXT("unsuccessfully"));
		}),
		GET_STATID(STAT_FSimpleDelegateGraphTask_ProcessQueryIapResult), 
		nullptr, 
		ENamedThreads::GameThread
	);

}


void FOnlineStoreGameCircle::ProcessQueryAvailablePurchasesResults(EInAppPurchaseState::Type InResult, const TArray<FInAppPurchaseProductInfo>& AvailablePurchases)
{
	bool bSuccess = InResult == EInAppPurchaseState::Success;

	if (ReadObject.IsValid())
	{
		ReadObject->ReadState = bSuccess ? EOnlineAsyncTaskState::Done : EOnlineAsyncTaskState::Failed;
		ReadObject->ProvidedProductInformation.Insert(AvailablePurchases, 0);
	}

	TriggerOnQueryForAvailablePurchasesCompleteDelegates(bSuccess);
}


bool FOnlineStoreGameCircle::BeginPurchase(const FInAppPurchaseProductRequest& ProductRequest, FOnlineInAppPurchaseTransactionRef& InPurchaseStateObject)
{
	UE_LOG_ONLINE_STORE(Display, TEXT( "FOnlineStoreGameCircle::BeginPurchase" ));
	
	bool bCreatedNewTransaction = false;
	
	if (IsAllowedToMakePurchases())
	{
		CachedPurchaseStateObject = InPurchaseStateObject;

		extern bool AndroidThunkCpp_Iap_BeginPurchase(const FString&, const bool);
		bCreatedNewTransaction = AndroidThunkCpp_Iap_BeginPurchase(ProductRequest.ProductIdentifier, ProductRequest.bIsConsumable);
		UE_LOG_ONLINE_STORE(Display, TEXT("Created Transaction? - %s"), 
			bCreatedNewTransaction ? TEXT("Created a transaction.") : TEXT("Failed to create a transaction."));

		if (!bCreatedNewTransaction)
		{
			UE_LOG_ONLINE_STORE(Display, TEXT("FOnlineStoreGameCircle::BeginPurchase - Could not create a new transaction."));
			CachedPurchaseStateObject->ReadState = EOnlineAsyncTaskState::Failed;
			TriggerOnInAppPurchaseCompleteDelegates(EInAppPurchaseState::Invalid);
		}
		else
		{
			CachedPurchaseStateObject->ReadState = EOnlineAsyncTaskState::InProgress;
		}
	}
	else
	{
		UE_LOG_ONLINE_STORE(Display, TEXT("This device is not able to make purchases."));

		InPurchaseStateObject->ReadState = EOnlineAsyncTaskState::Failed;
		TriggerOnInAppPurchaseCompleteDelegates(EInAppPurchaseState::NotAllowed);
	}


	return bCreatedNewTransaction;
}


JNI_METHOD void Java_com_epicgames_ue4_AmazonStoreHelper_nativePurchaseComplete(JNIEnv* jenv, jobject thiz, jint responseStatus, jstring productId, jstring receiptData, jstring signature)
{
	FString ProductId, ReceiptData, Signature;
	EInAppPurchaseState::Type Result = GetInAppPurchaseStateFromAmazonResponseStatus(jenv, responseStatus);

	if (Result == EInAppPurchaseState::Success)
	{
		ProductId = FJavaHelper::FStringFromParam(jenv, productId);
		ReceiptData = FJavaHelper::FStringFromParam(jenv, receiptData);
		Signature = FJavaHelper::FStringFromParam(jenv, signature);
	}

	DECLARE_CYCLE_STAT(TEXT("FSimpleDelegateGraphTask.ProcessIapResult"), STAT_FSimpleDelegateGraphTask_ProcessIapResult, STATGROUP_TaskGraphTasks);

	FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(
		FSimpleDelegateGraphTask::FDelegate::CreateLambda([=](){
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("In-App Purchase was completed  %s\n"), Result == EInAppPurchaseState::Success ? TEXT("successfully") : TEXT("unsuccessfully"));
			if (IOnlineSubsystem* const OnlineSub = IOnlineSubsystem::Get())
			{
				// call store implementation to process query results.
				if (FOnlineStoreGameCircle* StoreInterface = (FOnlineStoreGameCircle*)OnlineSub->GetStoreInterface().Get())
				{
					StoreInterface->ProcessPurchaseResult(Result, ProductId, ReceiptData, Signature);
				}
			}
		}),
		GET_STATID(STAT_FSimpleDelegateGraphTask_ProcessIapResult), 
		nullptr, 
		ENamedThreads::GameThread
	);

}


void FOnlineStoreGameCircle::ProcessPurchaseResult(EInAppPurchaseState::Type InResult, const FString& ProductId, const FString& InReceiptData, const FString& Signature)
{
	if (CachedPurchaseStateObject.IsValid())
	{
		FInAppPurchaseProductInfo& ProductInfo = CachedPurchaseStateObject->ProvidedProductInformation;
		ProductInfo.Identifier = ProductId;
		ProductInfo.DisplayName = TEXT("n/a");
		ProductInfo.DisplayDescription = TEXT("n/a");
		ProductInfo.DisplayPrice = TEXT("n/a");
		ProductInfo.ReceiptData = InReceiptData;
		ProductInfo.TransactionIdentifier = Signature;

		CachedPurchaseStateObject->ReadState = EOnlineAsyncTaskState::Done;
	}

	TriggerOnInAppPurchaseCompleteDelegates(InResult);
}


bool FOnlineStoreGameCircle::RestorePurchases(const TArray<FInAppPurchaseProductRequest>& ConsumableProductFlags, FOnlineInAppPurchaseRestoreReadRef& InReadObject)
{
	bool bSentAQueryRequest = false;
	CachedPurchaseRestoreObject = InReadObject;

	if (IsAllowedToMakePurchases())
	{
		TArray<FString> ProductIds;
		TArray<bool> IsConsumableFlags;

		for (int i = 0; i < ConsumableProductFlags.Num(); i++)
		{
			ProductIds.Add(ConsumableProductFlags[i].ProductIdentifier);
			IsConsumableFlags.Add(ConsumableProductFlags[i].bIsConsumable);
		}

		// Send JNI request
		extern bool AndroidThunkCpp_Iap_RestorePurchases(const TArray<FString>&, const TArray<bool>&);
		bSentAQueryRequest = AndroidThunkCpp_Iap_RestorePurchases(ProductIds, IsConsumableFlags);
	}
	else
	{
		UE_LOG_ONLINE_STORE(Display, TEXT("This device is not able to make purchases."));
		TriggerOnInAppPurchaseRestoreCompleteDelegates(EInAppPurchaseState::Failed);
	}

	return bSentAQueryRequest;
}

JNI_METHOD void Java_com_epicgames_ue4_AmazonStoreHelper_nativeRestorePurchasesComplete(JNIEnv* jenv, jobject thiz, jint responseStatus, jobjectArray ProductIDs, jobjectArray ReceiptsData)
{
	TArray<FInAppPurchaseRestoreInfo> RestoredPurchaseInfo;
	EInAppPurchaseState::Type Result = GetInAppPurchaseStateFromAmazonResponseStatus(jenv, responseStatus);

	if (jenv && Result == EInAppPurchaseState::Success)
	{
		jsize NumProducts = jenv->GetArrayLength(ProductIDs);
		jsize NumReceipts = jenv->GetArrayLength(ReceiptsData);

		ensure((NumProducts == NumReceipts));

		for (jsize Idx = 0; Idx < NumProducts; Idx++)
		{
			// Build the restore product information strings.
			FInAppPurchaseRestoreInfo RestoreInfo;

			RestoreInfo.Identifier = FJavaHelper::FStringFromLocalRef(jenv, (jstring)jenv->GetObjectArrayElement(ProductIDs, Idx));
			RestoreInfo.ReceiptData = FJavaHelper::FStringFromLocalRef(jenv, (jstring)jenv->GetObjectArrayElement(ReceiptsData, Idx));
			
			RestoredPurchaseInfo.Add(RestoreInfo);

			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("\nRestored Product Identifier: %s\n"), *RestoreInfo.Identifier);
		}
	}

	DECLARE_CYCLE_STAT(TEXT("FSimpleDelegateGraphTask.RestorePurchases"), STAT_FSimpleDelegateGraphTask_RestorePurchases, STATGROUP_TaskGraphTasks);

	FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(
		FSimpleDelegateGraphTask::FDelegate::CreateLambda([=]()
	{
		bool bSuccess = Result == EInAppPurchaseState::Success;
		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Restoring In-App Purchases was completed  %s\n"), bSuccess ? TEXT("successfully") : TEXT("unsuccessfully"));
		if (IOnlineSubsystem* const OnlineSub = IOnlineSubsystem::Get())
		{
			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Sending result back to OnlineSubsystem.\n"));
			// call store implementation to process query results.
			if (FOnlineStoreGameCircle* StoreInterface = (FOnlineStoreGameCircle*)OnlineSub->GetStoreInterface().Get())
			{
				if (StoreInterface->CachedPurchaseRestoreObject.IsValid())
				{
					StoreInterface->CachedPurchaseRestoreObject->ProvidedRestoreInformation = RestoredPurchaseInfo;
					StoreInterface->CachedPurchaseRestoreObject->ReadState = bSuccess ? EOnlineAsyncTaskState::Done : EOnlineAsyncTaskState::Failed;
				}
				StoreInterface->TriggerOnInAppPurchaseRestoreCompleteDelegates(bSuccess ? EInAppPurchaseState::Restored : Result);
			}
		}
		}),
		GET_STATID(STAT_FSimpleDelegateGraphTask_RestorePurchases),
		nullptr,
		ENamedThreads::GameThread
	);
}
