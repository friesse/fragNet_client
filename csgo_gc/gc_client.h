#pragma once

#include "gc_shared.h"
#include "networking_client.h"
#include "gc_const_csgo.h"
#include "tcp_connection.h"

class TCPConnection;

using ItemMap = std::unordered_map<uint64_t, CSOEconItem>;

class ClientGC final : public SharedGC
{
public:
    ClientGC(uint64_t steamId);
    ~ClientGC();

    uint64_t GetSteamID() const { return m_steamId; }

    void HandleMessage(uint32_t type, const void *data, uint32_t size);

    void Update();

    // called from net code
    void SendSOCacheToGameServer();
    void HandleNetMessage(GCMessageRead &messageRead);

    // passed to net code
    void SetAuthTicket(uint32_t handle, const void *data, uint32_t size);
    void ClearAuthTicket(uint32_t handle);

private:
    // send to the local game and the game server we're connected to (if we're connected)
    void SendMessageToGame(bool sendToGameServer, uint32_t type, const google::protobuf::MessageLite &message);

    void OnClientHello(GCMessageRead &messageRead);
    //void AdjustItemEquippedState(GCMessageRead &messageRead);
    void ClientPlayerDecalSign(GCMessageRead &messageRead);
    //void UseItemRequest(GCMessageRead &messageRead);
    void ClientRequestJoinServerData(GCMessageRead &messageRead);
    //void SetItemPositions(GCMessageRead &messageRead);
    //void IncrementKillCountAttribute(GCMessageRead &messageRead);
    //void ApplySticker(GCMessageRead &messageRead);

    //void UnlockCrate(GCMessageRead &messageRead);
    //void NameItem(GCMessageRead &messageRead);
    //void NameBaseItem(GCMessageRead &messageRead);
    //void RemoveItemName(GCMessageRead &messageRead);
    //void DeleteItem(GCMessageRead& messageRead);

    void BuildCSWelcome(CMsgCStrike15Welcome& message);
    void BuildMatchmakingHello(CMsgGCCStrike15_v2_MatchmakingGC2ClientHello &message);
    void BuildClientWelcome(CMsgClientWelcome &message, const CMsgCStrike15Welcome &csWelcome,
                            const CMsgGCCStrike15_v2_MatchmakingGC2ClientHello &matchmakingHello);
    //void SendRankUpdate();

    uint32_t AccountId() const { return m_steamId & 0xffffffff; }

    const uint64_t m_steamId;
    NetworkingClient m_networking{ this };

    //Inventory m_inventory;

    TCPConnection* m_gcConnection;

    // BuildMatchmakingHello
    bool m_hasMatchmakingData{ false };
    bool m_pendingOnClientHello{ false };
    CMsgGC_CC_GC2CL_BuildMatchmakingHello m_serverMatchmakingData;

    // SOCacheSubscribed
    bool m_hasSOCacheData{ false };
    CMsgSOCacheSubscribed m_serverSOCacheData;



// CC MESSAGES
private:
    void CheckAndSendWelcomeMessages();
    void SendWelcomeMessages();

    void HandleUnlockCrate(GCMessageRead& messageRead);
    void HandleCraft(GCMessageRead& messageRead);
    void HandleItemAcknowledged(GCMessageRead& messageRead);
    void HandleAdjustItemEquippedState(GCMessageRead& messageRead);
    void HandleDeleteItemRequest(GCMessageRead& messageRead);
    void HandleUseItemRequest(GCMessageRead& messageRead);
    void HandleApplySticker(GCMessageRead& messageRead);
    void HandleNameItem(GCMessageRead& messageRead);
    void HandleNameBaseItem(GCMessageRead& messageRead);
    void HandleRemoveItemName(GCMessageRead& messageRead);

    void StoreGetUserData(GCMessageRead& messageRead);
    void HandleStorePurchaseInit(GCMessageRead& messageRead);

    void HandleViewPlayersProfileRequest(GCMessageRead& messageRead);
    void HandleClientCommendPlayerQuery(GCMessageRead& messageRead);
    void HandleClientCommendPlayer(GCMessageRead& messageRead);
    void HandleClientReportPlayer(GCMessageRead& messageRead);

    // inventory items
    ItemMap m_items;
    std::vector<CSOEconDefaultEquippedDefinitionInstanceClient> m_defaultEquips;
public:
    void HandleMatchmakingHello(const CMsgGC_CC_GC2CL_BuildMatchmakingHello& serverMsg);
    void HandleCacheSubscription(const CMsgSOCacheSubscribed& message);
    void StoreCacheSubscription(const CMsgSOCacheSubscribed& message);
    void BuildCacheSubscription(CMsgSOCacheSubscribed& message, bool server);

    void HandleViewPlayersProfileResponse(const CMsgGC_CC_GC2CL_ViewPlayersProfileResponse& message);
    void HandleClientCommendPlayerQueryResponse(const CMsgGC_CC_ClientCommendPlayer& message);
    void HandleClientReportResponse(const CMsgGC_CC_GC2CL_ClientReportResponse& message);

    void HandleUnlockCrateResponse(const CMsgSOSingleObject& message);
    void HandleCraftResponse(const CMsgGC_CC_GC2CL_CraftResponse& message);
    void HandleDeleteItem(const CMsgSOSingleObject& message);

    void HandleStorePurchaseInitResponse(const CMsgGC_CC_GC2CL_StorePurchaseInitResponse& message);

    void HandleSOSingleObject(const CMsgSOSingleObject& message);
    void HandleSOMultipleObjects(const CMsgSOMultipleObjects& message);
};
