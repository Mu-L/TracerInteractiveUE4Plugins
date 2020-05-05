// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnlinePurchaseGooglePlay.h"
#include "OnlineSubsystemGooglePlay.h"
#include "OnlineStoreGooglePlay.h"
#include "OnlineError.h"
#include "Misc/Base64.h"
#include "Async/TaskGraphInterfaces.h"
#include <jni.h>
#include "Android/AndroidJavaEnv.h"
#include "Async/TaskGraphInterfaces.h"

#define LOCTEXT_NAMESPACE "OnlineSubsystemGooglePlay"
#define GOOGLEPLAYUSER TEXT("GooglePlayUser")

FGoogleTransactionData::FGoogleTransactionData(const FString& InOfferId, const FString& InProductToken, const FString& InReceiptData, const FString& InSignature)
	: OfferId(InOfferId)
	, TransactionIdentifier(InProductToken)
	, CombinedTransactionData(InReceiptData, InSignature)
{
	if (TransactionIdentifier.IsEmpty())
	{
		ErrorStr = TEXT("Receipt does not contain purchase token");
	}
	else if (CombinedTransactionData.ReceiptData.IsEmpty())
	{
		ErrorStr = TEXT("Receipt does not contain receipt data");
	}
	else if (CombinedTransactionData.Signature.IsEmpty())
	{
		ErrorStr = TEXT("Receipt does not contain signature data");
	}
}

FOnlinePurchaseGooglePlay::FOnlinePurchaseGooglePlay(FOnlineSubsystemGooglePlay* InSubsystem)
	: bQueryingReceipts(false)
	, Subsystem(InSubsystem)
{
	UE_LOG_ONLINE_PURCHASE(Verbose, TEXT( "FOnlinePurchaseGooglePlay::FOnlinePurchaseGooglePlay" ));
}

FOnlinePurchaseGooglePlay::FOnlinePurchaseGooglePlay()
{
	UE_LOG_ONLINE_PURCHASE(Verbose, TEXT( "FOnlinePurchaseGooglePlay::FOnlinePurchaseGooglePlay" ));
}

FOnlinePurchaseGooglePlay::~FOnlinePurchaseGooglePlay()
{
	if (Subsystem)
	{
		Subsystem->ClearOnGooglePlayProcessPurchaseCompleteDelegate_Handle(ProcessPurchaseResultDelegateHandle);
		Subsystem->ClearOnGooglePlayQueryExistingPurchasesCompleteDelegate_Handle(QueryExistingPurchasesCompleteDelegateHandle);
	}
}

void FOnlinePurchaseGooglePlay::Init()
{
	UE_LOG_ONLINE_PURCHASE(Verbose, TEXT("FOnlinePurchaseGooglePlay::Init"));

	FOnGooglePlayProcessPurchaseCompleteDelegate PurchaseCompleteDelegate = FOnGooglePlayProcessPurchaseCompleteDelegate::CreateThreadSafeSP(this, &FOnlinePurchaseGooglePlay::OnTransactionCompleteResponse);
	ProcessPurchaseResultDelegateHandle = Subsystem->AddOnGooglePlayProcessPurchaseCompleteDelegate_Handle(PurchaseCompleteDelegate);

	FOnGooglePlayQueryExistingPurchasesCompleteDelegate QueryExistingPurchasesCompleteDelegate = FOnGooglePlayQueryExistingPurchasesCompleteDelegate::CreateThreadSafeSP(this, &FOnlinePurchaseGooglePlay::OnQueryExistingPurchasesComplete);
	QueryExistingPurchasesCompleteDelegateHandle = Subsystem->AddOnGooglePlayQueryExistingPurchasesCompleteDelegate_Handle(QueryExistingPurchasesCompleteDelegate);
}

bool FOnlinePurchaseGooglePlay::IsAllowedToPurchase(const FUniqueNetId& UserId)
{
	UE_LOG_ONLINE_PURCHASE(Verbose, TEXT("FOnlinePurchaseGooglePlay::IsAllowedToPurchase"));
	extern bool AndroidThunkCpp_Iap_IsAllowedToMakePurchases();
	return AndroidThunkCpp_Iap_IsAllowedToMakePurchases();
}

void FOnlinePurchaseGooglePlay::Checkout(const FUniqueNetId& UserId, const FPurchaseCheckoutRequest& CheckoutRequest, const FOnPurchaseCheckoutComplete& Delegate)
{
	bool bStarted = false;
	FText ErrorMessage;

	TSharedRef<FOnlinePurchasePendingTransactionGooglePlay> RequestedTransaction = MakeShareable(new FOnlinePurchasePendingTransactionGooglePlay(CheckoutRequest, UserId, EPurchaseTransactionState::NotStarted, Delegate));

	if (IsAllowedToPurchase(UserId))
	{
		const FString UserIdStr = GOOGLEPLAYUSER;
		const TSharedRef<FOnlinePurchasePendingTransactionGooglePlay>* UserPendingTransaction = PendingTransactions.Find(UserIdStr);
		if (UserPendingTransaction == nullptr)
		{
			FOnlineStoreGooglePlayV2Ptr StoreInterface = StaticCastSharedPtr<FOnlineStoreGooglePlayV2>(Subsystem->GetStoreV2Interface());
			if (StoreInterface.IsValid())
			{
				TSharedRef<FOnlinePurchasePendingTransactionGooglePlay> PendingTransaction = PendingTransactions.Add(UserIdStr, RequestedTransaction);

				int32 NumOffers = CheckoutRequest.PurchaseOffers.Num();
				if (NumOffers > 0)
				{
					const FPurchaseCheckoutRequest::FPurchaseOfferEntry& Offer = CheckoutRequest.PurchaseOffers[0];

					extern bool AndroidThunkCpp_Iap_BeginPurchase(const FString&);
					bStarted = AndroidThunkCpp_Iap_BeginPurchase(Offer.OfferId);
					UE_LOG_ONLINE_PURCHASE(Display, TEXT("Created Transaction? - %s"),
						bStarted ? TEXT("Created a transaction.") : TEXT("Failed to create a transaction."));

					RequestedTransaction->PendingPurchaseInfo.TransactionState = bStarted ? EPurchaseTransactionState::Processing : EPurchaseTransactionState::Failed;
					if (NumOffers > 1)
					{
						UE_LOG_ONLINE_PURCHASE(Warning, TEXT("GooglePlay supports purchasing one offer at a time, %d were requested and ignored"), NumOffers - 1);
					}
				}
				else
				{
					ErrorMessage = NSLOCTEXT("GooglePlayPurchase", "ErrorNoOffersSpecified", "Failed to checkout, no offers given.");
					RequestedTransaction->PendingPurchaseInfo.TransactionState = EPurchaseTransactionState::Failed;
				}
			}
		}
		else
		{
			ErrorMessage = NSLOCTEXT("GooglePlayPurchase", "ErrorTransactionInProgress", "Failed to checkout, user has in progress transaction.");
			RequestedTransaction->PendingPurchaseInfo.TransactionState = EPurchaseTransactionState::Failed;
		}
	}
	else
	{
		ErrorMessage = NSLOCTEXT("GooglePlayPurchase", "ErrorPurchaseNotAllowed", "Failed to checkout, user not allowed to purchase.");
		RequestedTransaction->PendingPurchaseInfo.TransactionState = EPurchaseTransactionState::Failed;
	}

	if (!bStarted)
	{
		TSharedRef<FPurchaseReceipt> FailReceipt = RequestedTransaction->GenerateReceipt();

		Subsystem->ExecuteNextTick([ErrorMessage, FailReceipt, Delegate]()
		{
			FOnlineError Error(ErrorMessage);
			Delegate.ExecuteIfBound(Error, FailReceipt);
		});
	}
}

void FOnlinePurchaseGooglePlay::FinalizePurchase(const FUniqueNetId& UserId, const FString& ReceiptId)
{
	UE_LOG_ONLINE_PURCHASE(Verbose, TEXT("FOnlinePurchaseGooglePlay::FinalizePurchase %s %s"), *UserId.ToString(), *ReceiptId);
	extern void AndroidThunkCpp_Iap_ConsumePurchase(const FString&);
	AndroidThunkCpp_Iap_ConsumePurchase(ReceiptId);
}

void FOnlinePurchaseGooglePlay::RedeemCode(const FUniqueNetId& UserId, const FRedeemCodeRequest& RedeemCodeRequest, const FOnPurchaseRedeemCodeComplete& Delegate)
{
	// Not supported
	FOnlineError Result;
	Delegate.ExecuteIfBound(Result, MakeShareable(new FPurchaseReceipt()));
}

void FOnlinePurchaseGooglePlay::QueryReceipts(const FUniqueNetId& UserId, bool bRestoreReceipts, const FOnQueryReceiptsComplete& Delegate)
{
	bool bSuccess = false;
	bool bTriggerDelegate = true;
	if (!bQueryingReceipts)
	{
		/**
		 * bRestoreReceipts is irrelevant because GooglePlay requires the client to consume purchase in order to make a new purchase
		 * There is no concept of restore here, any purchase query will reveal any non consumed purchases. They will remain in the list until the the game
		 * consumes it via FinalizePurchase().
		 */
		bQueryingReceipts = true;
		QueryReceiptsComplete = Delegate;

		extern bool AndroidThunkCpp_Iap_QueryExistingPurchases();
		if (AndroidThunkCpp_Iap_QueryExistingPurchases())
		{
			bTriggerDelegate = false;
			bSuccess = true;
		}
		else
		{
			UE_LOG_ONLINE_PURCHASE(Verbose, TEXT("FOnlinePurchaseGooglePlay::QueryReceipts failed to start query"));
		}
	}
	else
	{
		UE_LOG_ONLINE_PURCHASE(Verbose, TEXT("FOnlinePurchaseGooglePlay::QueryReceipts already in progress."));
	}

	if (bTriggerDelegate)
	{
		Subsystem->ExecuteNextTick([this, Delegate, bSuccess]() {
			FOnlineError Result(bSuccess);
			Delegate.ExecuteIfBound(Result);
			bQueryingReceipts = false;
			QueryReceiptsComplete.Unbind();
		});
	}
}

void FOnlinePurchaseGooglePlay::GetReceipts(const FUniqueNetId& UserId, TArray<FPurchaseReceipt>& OutReceipts) const
{
	OutReceipts.Empty();

	// Add the cached list of user purchases
	FString UserIdStr = GOOGLEPLAYUSER;
	const TArray<TSharedRef<FPurchaseReceipt>>* UserCompletedTransactionsPtr = CompletedTransactions.Find(UserIdStr);
	if (UserCompletedTransactionsPtr != nullptr)
	{
		const TArray<TSharedRef<FPurchaseReceipt>>& UserCompletedTransactions = *UserCompletedTransactionsPtr;
		for (int32 Idx = 0; Idx < UserCompletedTransactions.Num(); Idx++)
		{
			UE_LOG_ONLINE_PURCHASE(Verbose, TEXT("FOnlinePurchaseGooglePlay::GetReceipts - Adding UserCompletedTransaction to OutReceipts"));
			const TSharedRef<FPurchaseReceipt>& Transaction = UserCompletedTransactions[Idx];
			OutReceipts.Add(*Transaction);
		}
	}
	
	// Add purchases completed while "offline"
	for (int32 Idx = 0; Idx < OfflineTransactions.Num(); Idx++)
	{
		UE_LOG_ONLINE_PURCHASE(Verbose, TEXT("FOnlinePurchaseGooglePlay::GetReceipts - Adding OfflineTransaction to OutReceipts"));
		OutReceipts.Add(*OfflineTransactions[Idx]);
	}

	UE_LOG_ONLINE_PURCHASE(Log, TEXT("FOnlinePurchaseGooglePlay::GetReceipts - Final Number of outreciepts OutReceipts - %d"), OutReceipts.Num());
}

void FOnlinePurchaseGooglePlay::OnTransactionCompleteResponse(EGooglePlayBillingResponseCode InResponseCode, const FGoogleTransactionData& InTransactionData)
{
	UE_LOG_ONLINE_PURCHASE(Log, TEXT("FOnlinePurchaseGooglePlay::OnTransactionCompleteResponse %s"), ToString(InResponseCode));
	UE_LOG_ONLINE_PURCHASE(Log, TEXT("3... Transaction: %s"), *InTransactionData.ToDebugString());
	EPurchaseTransactionState Result = ConvertGPResponseCodeToPurchaseTransactionState(InResponseCode);

	FString UserIdStr = GOOGLEPLAYUSER;
	const TSharedRef<FOnlinePurchasePendingTransactionGooglePlay>* UserPendingTransactionPtr = PendingTransactions.Find(UserIdStr);
	if(UserPendingTransactionPtr != nullptr)
	{
		const TSharedRef<FOnlinePurchasePendingTransactionGooglePlay> UserPendingTransaction = *UserPendingTransactionPtr;
		const FString& ErrorStr = InTransactionData.GetErrorStr();

		if (Result == EPurchaseTransactionState::Canceled && !InTransactionData.GetOfferId().IsEmpty())
		{
			// When result is cancelled, but there is a sku in the transaction data, this is a deferred transaction
			Result = EPurchaseTransactionState::Deferred;
		}

		if (Result == EPurchaseTransactionState::Purchased || Result == EPurchaseTransactionState::Restored)
		{
			if (!UserPendingTransaction->AddCompletedOffer(Result, InTransactionData))
			{
				UE_LOG_ONLINE_PURCHASE(Verbose, TEXT("Offer not found in pending transaction"));
				Result = EPurchaseTransactionState::Failed;
			}
		}

		UserPendingTransaction->PendingPurchaseInfo.TransactionState = Result;
		UserPendingTransaction->PendingPurchaseInfo.TransactionId = InTransactionData.GetTransactionIdentifier();

		FOnlineError FinalResult;
		switch (Result)
		{
			case EPurchaseTransactionState::Failed:
				FinalResult.SetFromErrorCode(TEXT("com.epicgames.purchase.failure"));
				FinalResult.ErrorMessage = !ErrorStr.IsEmpty() ? FText::FromString(ErrorStr) : LOCTEXT("GooglePlayTransactionFailed", "Transaction Failed");
				break;
			case EPurchaseTransactionState::Canceled:
				FinalResult.SetFromErrorCode(TEXT("com.epicgames.catalog_helper.user_cancelled"));
				FinalResult.ErrorMessage = !ErrorStr.IsEmpty() ? FText::FromString(ErrorStr) : LOCTEXT("GooglePlayTransactionCancel", "Transaction Canceled");
				break;
			case EPurchaseTransactionState::Purchased:
				FinalResult.bSucceeded = true;
				break;
			case EPurchaseTransactionState::Deferred:
				FinalResult.SetFromErrorCode(TEXT("com.epicgames.purchase.deferred"));
				FinalResult.ErrorMessage = !ErrorStr.IsEmpty() ? FText::FromString(ErrorStr) : LOCTEXT("GooglePlayTransactionDeferred", "Transaction Deferred");
				break;
			case EPurchaseTransactionState::Invalid:
				FinalResult.SetFromErrorCode(TEXT("com.epicgames.purchase.invalid"));
				FinalResult.ErrorMessage = !ErrorStr.IsEmpty() ? FText::FromString(ErrorStr) : LOCTEXT("GooglePlayInvalidState", "Invalid purchase result");
				UserPendingTransaction->PendingPurchaseInfo.TransactionState = EPurchaseTransactionState::Invalid;
				break;
			default:
				UE_LOG_ONLINE_PURCHASE(Warning, TEXT("Unexpected state after purchase %d"), (int)Result);
				FinalResult.SetFromErrorCode(TEXT("com.epicgames.purchase.unexpected_state"));
				FinalResult.ErrorMessage = !ErrorStr.IsEmpty() ? FText::FromString(ErrorStr) : LOCTEXT("GooglePlayUnexpectedState", "Unexpected purchase result");
				UserPendingTransaction->PendingPurchaseInfo.TransactionState = EPurchaseTransactionState::Failed;
				break;
		}

		TSharedRef<FPurchaseReceipt> FinalReceipt = UserPendingTransaction->GenerateReceipt();

		TArray< TSharedRef<FPurchaseReceipt> >& UserCompletedTransactions = CompletedTransactions.FindOrAdd(UserIdStr);

		PendingTransactions.Remove(UserIdStr);

		// If this is a deferred transaction, we will process it as an "offline" transaction, so don't complete
		if (Result != EPurchaseTransactionState::Deferred)
		{
			UserCompletedTransactions.Add(FinalReceipt);
		}

		UserPendingTransaction->CheckoutCompleteDelegate.ExecuteIfBound(FinalResult, FinalReceipt);
	}
	else
	{
		// Need to populate offlineTransactions here. 
		// Transactions that come in during login or other non explicit purchase moments are added to a receipts list for later redemption
		UE_LOG_ONLINE_PURCHASE(Log, TEXT("Pending transaction completed offline"));
		if (Result == EPurchaseTransactionState::Restored || Result == EPurchaseTransactionState::Purchased)
		{
			TSharedRef<FPurchaseReceipt> OfflineReceipt = FOnlinePurchasePendingTransactionGooglePlay::GenerateReceipt(InTransactionData);
			OfflineTransactions.Add(OfflineReceipt);

			// Queue this user to be updated about this next-tick on the game-thread, if they're not mid-purchase
			TWeakPtr<FOnlinePurchaseGooglePlay, ESPMode::ThreadSafe> WeakThis(AsShared());
			Subsystem->ExecuteNextTick([WeakThis]()
			{
				FOnlinePurchaseGooglePlayPtr StrongThis = WeakThis.Pin();
				if (StrongThis.IsValid())
				{
					StrongThis->TriggerOnUnexpectedPurchaseReceiptDelegates(*FUniqueNetIdGooglePlay::EmptyId());
				}
			});
		}
	}
}

void FOnlinePurchaseGooglePlay::OnQueryExistingPurchasesComplete(EGooglePlayBillingResponseCode InResponseCode, const TArray<FGoogleTransactionData>& InExistingPurchases)
{
	UE_LOG_ONLINE_PURCHASE(Log, TEXT("FOnlinePurchaseGooglePlay::OnQueryExistingPurchasesComplete Response: %s Num: %d"), ToString(InResponseCode), InExistingPurchases.Num());

	if (bQueryingReceipts)
	{
		bool bSuccess = (InResponseCode == EGooglePlayBillingResponseCode::Ok);
		if (bSuccess)
		{
			EPurchaseTransactionState Result = ConvertGPResponseCodeToPurchaseTransactionState(InResponseCode);
			for (const FGoogleTransactionData& Purchase : InExistingPurchases)
			{
				UE_LOG_ONLINE_PURCHASE(Log, TEXT("Adding existing receipt %s"), *Purchase.ToDebugString());
				TSharedRef<FPurchaseReceipt> OfflineReceipt = FOnlinePurchasePendingTransactionGooglePlay::GenerateReceipt(Purchase);
				OfflineTransactions.Add(OfflineReceipt);
			}
		}
		else
		{
			UE_LOG_ONLINE_PURCHASE(Log, TEXT("OnQueryExistingPurchasesComplete failed"));
		}

		Subsystem->ExecuteNextTick([this, bSuccess]()
		{
			FOnlineError Result(bSuccess);
			QueryReceiptsComplete.ExecuteIfBound(Result);
			bQueryingReceipts = false;
			QueryReceiptsComplete.Unbind();
		});
	}
	else
	{
		UE_LOG_ONLINE_PURCHASE(Warning, TEXT("FOnlinePurchaseGooglePlay::OnQueryExistingPurchasesComplete unexpected call"));
	}
}

void FOnlinePurchaseGooglePlay::FinalizeReceiptValidationInfo(const FUniqueNetId& UserId, FString& InReceiptValidationInfo, const FOnFinalizeReceiptValidationInfoComplete& Delegate)
{
	FOnlineError DefaultSuccess(true);
	Delegate.ExecuteIfBound(DefaultSuccess, InReceiptValidationInfo);
}

TSharedRef<FPurchaseReceipt> FOnlinePurchasePendingTransactionGooglePlay::GenerateReceipt()
{
	TSharedRef<FPurchaseReceipt> Receipt = MakeShareable(new FPurchaseReceipt());
	
	Receipt->TransactionState = PendingPurchaseInfo.TransactionState;
	Receipt->TransactionId = PendingPurchaseInfo.TransactionId;
	
	if(PendingPurchaseInfo.TransactionState == EPurchaseTransactionState::Purchased ||
	   PendingPurchaseInfo.TransactionState == EPurchaseTransactionState::Restored)
	{
		Receipt->ReceiptOffers = PendingPurchaseInfo.ReceiptOffers;
	}
	else
	{
		// Add the requested offers to the receipt in the event of an incomplete purchase.
		for(const auto& RequestedOffer : CheckoutRequest.PurchaseOffers)
		{
			Receipt->AddReceiptOffer(RequestedOffer.OfferNamespace, RequestedOffer.OfferId, RequestedOffer.Quantity);
		}
	}
	
	return Receipt;
}

TSharedRef<FPurchaseReceipt> FOnlinePurchasePendingTransactionGooglePlay::GenerateReceipt(const FGoogleTransactionData& Transaction)
{
	TSharedRef<FPurchaseReceipt> Receipt = MakeShareable(new FPurchaseReceipt());
	
	Receipt->TransactionState = Transaction.GetErrorStr().IsEmpty() ? EPurchaseTransactionState::Purchased : EPurchaseTransactionState::Failed;
	Receipt->TransactionId = Transaction.GetTransactionIdentifier();
	
	if (Receipt->TransactionState == EPurchaseTransactionState::Purchased ||
		Receipt->TransactionState == EPurchaseTransactionState::Restored)
	{
		FPurchaseReceipt::FReceiptOfferEntry ReceiptEntry(TEXT(""), Transaction.GetOfferId(), 1);

		int32 Idx = ReceiptEntry.LineItems.AddZeroed();

		FPurchaseReceipt::FLineItemInfo& LineItem = ReceiptEntry.LineItems[Idx];

		LineItem.ItemName = Transaction.GetOfferId();
		LineItem.UniqueId = Transaction.GetTransactionIdentifier();
		LineItem.ValidationInfo = Transaction.GetCombinedReceiptData();

		Receipt->AddReceiptOffer(ReceiptEntry);
	}
	
	return Receipt;
}

bool FOnlinePurchasePendingTransactionGooglePlay::AddCompletedOffer(EPurchaseTransactionState Result, const FGoogleTransactionData& Transaction)
{
	for (int32 OfferIdx = 0; OfferIdx < CheckoutRequest.PurchaseOffers.Num(); ++OfferIdx)
	{
		const FPurchaseCheckoutRequest::FPurchaseOfferEntry& Offer = CheckoutRequest.PurchaseOffers[OfferIdx];
		if (Transaction.GetOfferId() == Offer.OfferId)
		{
			FPurchaseReceipt::FReceiptOfferEntry Receipt(TEXT(""), Transaction.GetOfferId(), 1);

			int32 Idx = Receipt.LineItems.AddZeroed();
			
			FPurchaseReceipt::FLineItemInfo& LineItem = Receipt.LineItems[Idx];
			
			LineItem.ItemName = Transaction.GetOfferId();
			LineItem.UniqueId = Transaction.GetTransactionIdentifier();
			LineItem.ValidationInfo = Transaction.GetCombinedReceiptData();
			
			PendingPurchaseInfo.AddReceiptOffer(Receipt);
			return true;
		}
	}
	
	return false;
}

JNI_METHOD void Java_com_epicgames_ue4_GooglePlayStoreHelper_nativeQueryExistingPurchasesComplete(JNIEnv* jenv, jobject thiz, jsize responseCode, jobjectArray ProductIDs, jobjectArray ProductTokens, jobjectArray ReceiptsData, jobjectArray Signatures)
{
	TArray<FGoogleTransactionData> ExistingPurchaseInfo;

	EGooglePlayBillingResponseCode EGPResponse = (EGooglePlayBillingResponseCode)responseCode;

	bool bWasSuccessful = (EGPResponse == EGooglePlayBillingResponseCode::Ok);
	if (jenv && bWasSuccessful)
	{
		jsize NumProducts = jenv->GetArrayLength(ProductIDs);
		jsize NumProductTokens = jenv->GetArrayLength(ProductTokens);
		jsize NumReceipts = jenv->GetArrayLength(ReceiptsData);
		jsize NumSignatures = jenv->GetArrayLength(Signatures);

		ensure((NumProducts == NumProductTokens) && (NumProducts == NumReceipts) && (NumProducts == NumSignatures));

		for (jsize Idx = 0; Idx < NumProducts; Idx++)
		{
			// Build the product information strings.
			const auto OfferId = FJavaHelper::FStringFromLocalRef(jenv, (jstring)jenv->GetObjectArrayElement(ProductIDs, Idx));
			const auto ProductToken = FJavaHelper::FStringFromLocalRef(jenv, (jstring)jenv->GetObjectArrayElement(ProductTokens, Idx));
			const auto ReceiptData = FJavaHelper::FStringFromLocalRef(jenv, (jstring)jenv->GetObjectArrayElement(ReceiptsData, Idx));
			const auto SignatureData = FJavaHelper::FStringFromLocalRef(jenv, (jstring)jenv->GetObjectArrayElement(Signatures, Idx));

			FGoogleTransactionData ExistingPurchase(OfferId, ProductToken, ReceiptData, SignatureData);
			ExistingPurchaseInfo.Add(ExistingPurchase);

			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("\Existing Product Identifier: %s"), *ExistingPurchase.ToDebugString());
		}
	}

	DECLARE_CYCLE_STAT(TEXT("FSimpleDelegateGraphTask.QueryExistingPurchases"), STAT_FSimpleDelegateGraphTask_QueryExistingPurchases, STATGROUP_TaskGraphTasks);

	FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(
		FSimpleDelegateGraphTask::FDelegate::CreateLambda([=]()
	{
		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Query existing purchases was completed %s\n"), bWasSuccessful ? TEXT("successfully") : TEXT("unsuccessfully"));
		if (IOnlineSubsystem* const OnlineSub = IOnlineSubsystem::Get(GOOGLEPLAY_SUBSYSTEM))
		{
			FOnlineSubsystemGooglePlay* const OnlineSubGP = static_cast<FOnlineSubsystemGooglePlay* const>(OnlineSub);
			if (OnlineSubGP)
			{
				OnlineSubGP->TriggerOnGooglePlayQueryExistingPurchasesCompleteDelegates(EGPResponse, ExistingPurchaseInfo);
			}
		}
	}),
		GET_STATID(STAT_FSimpleDelegateGraphTask_QueryExistingPurchases),
		nullptr,
		ENamedThreads::GameThread
		);
}

JNI_METHOD void Java_com_epicgames_ue4_GooglePlayStoreHelper_nativeRestorePurchasesComplete(JNIEnv* jenv, jobject thiz, jsize responseCode, jobjectArray ProductIDs, jobjectArray ProductTokens, jobjectArray ReceiptsData, jobjectArray Signatures)
{
	TArray<FGoogleTransactionData> RestoredPurchaseInfo;

	EGooglePlayBillingResponseCode EGPResponse = (EGooglePlayBillingResponseCode)responseCode;
	bool bWasSuccessful = (EGPResponse == EGooglePlayBillingResponseCode::Ok);

	if (jenv && bWasSuccessful)
	{
		jsize NumProducts = jenv->GetArrayLength(ProductIDs);
		jsize NumProductTokens = jenv->GetArrayLength(ProductTokens);
		jsize NumReceipts = jenv->GetArrayLength(ReceiptsData);
		jsize NumSignatures = jenv->GetArrayLength(Signatures);

		ensure((NumProducts == NumProductTokens) && (NumProducts == NumReceipts) && (NumProducts == NumSignatures));

		for (jsize Idx = 0; Idx < NumProducts; Idx++)
		{
			const auto OfferId = FJavaHelper::FStringFromLocalRef(jenv, (jstring)jenv->GetObjectArrayElement(ProductIDs, Idx));
			const auto ProductToken = FJavaHelper::FStringFromLocalRef(jenv, (jstring)jenv->GetObjectArrayElement(ProductTokens, Idx));
			const auto ReceiptData = FJavaHelper::FStringFromLocalRef(jenv, (jstring)jenv->GetObjectArrayElement(ReceiptsData, Idx));
			const auto SignatureData = FJavaHelper::FStringFromLocalRef(jenv, (jstring)jenv->GetObjectArrayElement(Signatures, Idx));

			FGoogleTransactionData RestoredPurchase(OfferId, ProductToken, ReceiptData, SignatureData);
			RestoredPurchaseInfo.Add(RestoredPurchase);

			FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Restored Transaction: %s"), *RestoredPurchase.ToDebugString());
		}
	}

	DECLARE_CYCLE_STAT(TEXT("FSimpleDelegateGraphTask.RestorePurchases"), STAT_FSimpleDelegateGraphTask_RestorePurchases, STATGROUP_TaskGraphTasks);

	FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(
		FSimpleDelegateGraphTask::FDelegate::CreateLambda([=]()
	{
		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Restoring In-App Purchases was completed  %s\n"), bWasSuccessful ? TEXT("successfully") : TEXT("unsuccessfully"));

		if (IOnlineSubsystem* const OnlineSub = IOnlineSubsystem::Get(GOOGLEPLAY_SUBSYSTEM))
		{
			FOnlineSubsystemGooglePlay* const OnlineSubGP = static_cast<FOnlineSubsystemGooglePlay* const>(OnlineSub);
			if (OnlineSubGP)
			{
				OnlineSubGP->TriggerOnGooglePlayRestorePurchasesCompleteDelegates(EGPResponse, RestoredPurchaseInfo);
			}
		}
	}),
		GET_STATID(STAT_FSimpleDelegateGraphTask_RestorePurchases),
		nullptr,
		ENamedThreads::GameThread
		);
}

JNI_METHOD void Java_com_epicgames_ue4_GooglePlayStoreHelper_nativePurchaseComplete(JNIEnv* jenv, jobject thiz, jsize responseCode, jstring productId, jstring productToken, jstring receiptData, jstring signature)
{
	FString ProductId, ProductToken, ReceiptData, Signature;
	EGooglePlayBillingResponseCode EGPResponse = (EGooglePlayBillingResponseCode)responseCode;

	bool bWasSuccessful = (EGPResponse == EGooglePlayBillingResponseCode::Ok);

	//Store off results, because we will use them to determine if this is a deferred transaction.
	ProductId = FJavaHelper::FStringFromParam(jenv, productId);
	ProductToken = FJavaHelper::FStringFromParam(jenv, productToken);
	ReceiptData = FJavaHelper::FStringFromParam(jenv, receiptData);
	Signature = FJavaHelper::FStringFromParam(jenv, signature);


	FGoogleTransactionData TransactionData(ProductId, ProductToken, ReceiptData, Signature);

	FPlatformMisc::LowLevelOutputDebugStringf(TEXT("1... Response: %s, Transaction %s"), ToString(EGPResponse), *TransactionData.ToDebugString());

	DECLARE_CYCLE_STAT(TEXT("FSimpleDelegateGraphTask.ProcessIapResult"), STAT_FSimpleDelegateGraphTask_ProcessIapResult, STATGROUP_TaskGraphTasks);

	FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(
		FSimpleDelegateGraphTask::FDelegate::CreateLambda([=]()
	{
		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("In-App Purchase was completed  %s\n"), bWasSuccessful ? TEXT("successfully") : TEXT("unsuccessfully"));
		if (IOnlineSubsystem* const OnlineSub = IOnlineSubsystem::Get(GOOGLEPLAY_SUBSYSTEM))
		{
			FOnlineSubsystemGooglePlay* const OnlineSubGP = static_cast<FOnlineSubsystemGooglePlay* const>(OnlineSub);
			if (OnlineSubGP)
			{
				FPlatformMisc::LowLevelOutputDebugStringf(TEXT("2... Response %s Transaction %s"), ToString(EGPResponse), *TransactionData.ToDebugString());
				OnlineSubGP->TriggerOnGooglePlayProcessPurchaseCompleteDelegates(EGPResponse, TransactionData);
			}
		}
	}),
		GET_STATID(STAT_FSimpleDelegateGraphTask_ProcessIapResult),
		nullptr,
		ENamedThreads::GameThread
		);
}

#undef LOCTEXT_NAMESPACE
