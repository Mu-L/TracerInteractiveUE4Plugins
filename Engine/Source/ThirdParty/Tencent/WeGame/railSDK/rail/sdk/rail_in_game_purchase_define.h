// Copyright (c) 2016, Entropy Game Global Limited.
// All rights reserved.

#ifndef RAIL_SDK_RAIL_IN_GAME_PRUCHASE_DEFINE_H
#define RAIL_SDK_RAIL_IN_GAME_PRUCHASE_DEFINE_H

#include "rail/sdk/rail_assets_define.h"

namespace rail {
#pragma pack(push, RAIL_SDK_PACKING)

// define product id, [1, 1000000000] is used for game self
// like in-game-purchase, assert and so on
enum EnumRailProductId {
    EnumRailProductId_For_Game_Start = 1,
    EnumRailProductId_For_Game_End = 1000000000,

    EnumRailProductId_For_Platfrom_Start = 1000000001,
    EnumRailProductId_For_Platfrom_Storage_Space = 1000000001,
    EnumRailProductId_For_Platfrom_All = 1000000011
};

// in game purchase products discount type
enum PurchaseProductDiscountType {
    kPurchaseProductDiscountTypeInvalid = 0,
    kPurchaseProductDiscountTypeNone = 1,       // û���ۿ�
    kPurchaseProductDiscountTypePermanent = 2,  // �����ۿ�
    kPurchaseProductDiscountTypeTimed = 3,      // ��ʱ�ۿ�
};

// in game purchase order state
enum PurchaseProductOrderState {
    kPurchaseProductOrderStateInvalid = 0,
    kPurchaseProductOrderStateCreateOrderOk = 100,  // �µ��ɹ�
    kPurchaseProductOrderStatePayOk = 200,          // ֧���ɹ�
    kPurchaseProductOrderStateDeliverOk = 300,      // �����ɹ�
};

struct RailDiscountInfo {
    RailDiscountInfo() {
        off = 0;
        type = kPurchaseProductDiscountTypeNone;
        discount_price = 0.0;
        start_time = 0;
        end_time = 0;
    }

    float off;                         // �ۿ��ʣ�[0~1.0)֮��:
                                       //        0.15 - 15%off - 8.5��
                                       //        0.20 - 20%off - 8��
    float discount_price;              // �ۿۺ�ļ۸�,��̨����offֵ�Զ����������

    PurchaseProductDiscountType type;  // �ۿ�����
    uint32_t start_time;               // ��ʱ�ۿۿ�ʼʱ�䣬ֻ����ʱ�ۿ�������Ч
    uint32_t end_time;                 // ��ʱ�ۿ۽���ʱ�䣬ֻ����ʱ�ۿ�������Ч
};

// product info
// ������Ϣ
struct RailPurchaseProductExtraInfo {
    RailPurchaseProductExtraInfo() {}

    RailString exchange_rule;      // ���ߵĺϳɹ���
    RailString bundle_rule;        // ���ߵĴ������
};

struct RailPurchaseProductInfo {
    RailPurchaseProductInfo() {
        product_id = 0;
        is_purchasable = false;
        original_price = 0.0;
    }

    RailProductID product_id;      // ����ID
    bool is_purchasable;           // �����Ƿ���Թ���
    RailString name;               // ��������
    RailString description;        // ��������
    RailString category;           // �������
    RailString product_thumbnail;  // ����ͼƬurl
    RailPurchaseProductExtraInfo extra_info;  // ���߸�����Ϣ
    // ��is_purchasable=true��ʱ����������������Ч
    float original_price;          // ����ԭ��
    RailString currency_type;      // ��������
    RailDiscountInfo discount;     // �ۿ���Ϣ
};

namespace rail_event {

struct RailInGamePurchaseRequestAllPurchasableProductsResponse
    : RailEvent<kRailEventInGamePurchaseAllPurchasableProductsInfoReceived> {
    RailInGamePurchaseRequestAllPurchasableProductsResponse() { result = kFailure; }

    RailArray<RailPurchaseProductInfo> purchasable_products;  // ��ȡ�ɹ�ʱ��Ч������Ϊ��
};

struct RailInGamePurchaseRequestAllProductsResponse
    : RailEvent<kRailEventInGamePurchaseAllProductsInfoReceived> {
    RailInGamePurchaseRequestAllProductsResponse() { result = kFailure; }

        RailArray<RailPurchaseProductInfo> all_products;  // ��ȡ�ɹ�ʱ��Ч������Ϊ��
};

struct RailInGamePurchasePurchaseProductsResponse
    : RailEvent<kRailEventInGamePurchasePurchaseProductsResult> {
    RailInGamePurchasePurchaseProductsResponse() {
        result = kFailure;
        user_data = "";
    }

    RailString order_id;
    RailArray<RailProductItem> delivered_products;  // �����ɹ�ʱ��Ч����¼ÿ����Ʒ�ķ�������
};

struct RailInGamePurchasePurchaseProductsToAssetsResponse
    : RailEvent<kRailEventInGamePurchasePurchaseProductsToAssetsResult> {
    RailInGamePurchasePurchaseProductsToAssetsResponse() {
        result = kFailure;
        user_data = "";
    }

    RailString order_id;
    RailArray<RailAssetInfo> delivered_assets;  // �����ɹ�ʱ��Ч����¼ÿ����Ʒ�ķ�������,id
};

struct RailInGamePurchaseFinishOrderResponse :
    RailEvent<kRailEventInGamePurchaseFinishOrderResult> {
    RailInGamePurchaseFinishOrderResponse() {
        result = kFailure;
    }

    RailString order_id;
};

}  // namespace rail_event

#pragma pack(pop)
}  // namespace rail

#endif  // RAIL_SDK_RAIL_IN_GAME_PRUCHASE_DEFINE_H
