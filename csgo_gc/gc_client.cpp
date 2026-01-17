#include "stdafx.h"
#include "gc_client.h"
#include "graffiti.h"
#include "keyvalue.h"

const char *MessageName(uint32_t type);

ClientGC::ClientGC(uint64_t steamId)
    : m_steamId{ steamId }
{
    Platform::Print("ClientGC spawned for user %llu\n", steamId);

    // also called from ServerGC's constructor
    //Graffiti::Initialize();
}

ClientGC::~ClientGC()
{
    Platform::Print("ClientGC destroyed\n");
}

void ClientGC::HandleMessage(uint32_t type, const void *data, uint32_t size)
{
    GCMessageRead messageRead{ type, data, size };
    if (!messageRead.IsValid())
    { 
        //assert(false);
        return;
    }

    if (messageRead.IsProtobuf())
    {
        switch (messageRead.TypeUnmasked())
        {
        case k_EMsgGCClientHello:
            OnClientHello(messageRead);
            break;

        case k_EMsgGCAdjustItemEquippedState:
            //AdjustItemEquippedState(messageRead); // TODO
            HandleAdjustItemEquippedState(messageRead);
            break;

        case k_EMsgGCCStrike15_v2_ClientPlayerDecalSign:
            //ClientPlayerDecalSign(messageRead); // unsupported on cc
            break;

        case k_EMsgGCUseItemRequest:
            //UseItemRequest(messageRead); // TODO
            HandleUseItemRequest(messageRead);
            break;

        case k_EMsgGCCStrike15_v2_ClientRequestJoinServerData:
            ClientRequestJoinServerData(messageRead);
            break;

        case k_EMsgGCSetItemPositions:
            //SetItemPositions(messageRead);
            HandleItemAcknowledged(messageRead);
            break;

        case k_EMsgGCApplySticker:
            //ApplySticker(messageRead); // TODO
            HandleApplySticker(messageRead);
            break;

        case k_EMsgGCCStrike15_v2_ClientRequestPlayersProfile:
            HandleViewPlayersProfileRequest(messageRead);
            break;


        case k_EMsgGCCStrike15_v2_ClientCommendPlayerQuery:
            HandleClientCommendPlayerQuery(messageRead);
            break;

        case k_EMsgGCCStrike15_v2_ClientCommendPlayer:
            HandleClientCommendPlayer(messageRead);
            break;

        case k_EMsgGCCStrike15_v2_ClientReportPlayer:
            HandleClientReportPlayer(messageRead);
            break;

        case k_EMsgGCCStrike15_v2_ClientReportServer:
            // probably not soon
            break;

        case k_EMsgGCStoreGetUserData:
            StoreGetUserData(messageRead);
            break;

        case k_EMsgGCStorePurchaseInit:
            HandleStorePurchaseInit(messageRead);
            break;

        default:
            Platform::Print("ClientGC::HandleMessage: unhandled protobuf message %s\n",
                MessageName(messageRead.TypeUnmasked()));
            break;
        }
    }
    else
    {
        switch (messageRead.TypeUnmasked())
        {
        case k_EMsgGCUnlockCrate:
            //UnlockCrate(messageRead);
            HandleUnlockCrate(messageRead); // TODO FIX IT
            break;

        case k_EMsgGCNameItem:
            //NameItem(messageRead); // TODO
            HandleNameItem(messageRead);
            break;

        case k_EMsgGCNameBaseItem:
            //NameBaseItem(messageRead); // TODO
            HandleNameBaseItem(messageRead);
            break;

        case k_EMsgGCRemoveItemName:
            //RemoveItemName(messageRead); // TODO
            HandleRemoveItemName(messageRead);
            break;

        case k_EMsgGCDelete:
            //DeleteItem(messageRead);
            HandleDeleteItemRequest(messageRead);
            break;

        case k_EMsgGCCraft:
            HandleCraft(messageRead);
            break;

        default:
            Platform::Print("ClientGC::HandleMessage: unhandled struct message %s\n",
                MessageName(messageRead.TypeUnmasked()));
            break;
        }
    }
}

void ClientGC::Update()
{
    m_networking.Update();

    if (m_gcConnection)
        m_gcConnection->Update();
}

void ClientGC::SendSOCacheToGameServer()
{
    CMsgSOCacheSubscribed message;
    BuildCacheSubscription(message, true);

    Platform::Print("Sending SOCache with steamID %llu\n", message.owner_soid().id());

    GCMessageWrite messageWrite{ k_ESOMsg_CacheSubscribed, message };
    m_networking.NetSendMessage(messageWrite);
}

void ClientGC::HandleNetMessage(GCMessageRead &messageRead)
{
    assert(messageRead.IsValid());

    if (messageRead.IsProtobuf())
    {
        switch (messageRead.TypeUnmasked())
        {
        case k_EMsgGC_IncrementKillCountAttribute:
            //IncrementKillCountAttribute(messageRead);
            return;
        }
    }

    Platform::Print("ClientGC::HandleNetMessage: unhandled protobuf message %s\n",
        MessageName(messageRead.TypeUnmasked()));
}

void ClientGC::SetAuthTicket(uint32_t handle, const void *data, uint32_t size)
{
    m_networking.SetAuthTicket(handle, data, size);
}

void ClientGC::ClearAuthTicket(uint32_t handle)
{
    m_networking.ClearAuthTicket(handle);
}

void ClientGC::SendMessageToGame(bool sendToGameServer, uint32_t type, const google::protobuf::MessageLite &message)
{
    const GCMessageWrite &messageWrite = m_outgoingMessages.emplace(type, message);

    if (sendToGameServer)
    {
        m_networking.NetSendMessage(messageWrite);
    }
}

constexpr uint32_t MakeAddress(uint32_t v1, uint32_t v2, uint32_t v3, uint32_t v4)
{
    return v4 | (v3 << 8) | (v2 << 16) | (v1 << 24);
}

void ClientGC::BuildCSWelcome(CMsgCStrike15Welcome& message)
{
    message.set_store_item_hash(136617352);
    message.set_timeplayedconsecutively(0);
    message.set_time_first_played(1329845773);
    message.set_last_time_played(1680260376);
    message.set_last_ip_address(MakeAddress(127, 0, 0, 1));
}

// FROM TCPConnection
void ClientGC::HandleMatchmakingHello(const CMsgGC_CC_GC2CL_BuildMatchmakingHello& serverMsg) 
{
    // store message
    m_serverMatchmakingData = serverMsg;
    m_hasMatchmakingData = true;
    Platform::Print("Received CMsgGC_CC_GC2CL_BuildMatchmakingHello for account %u\n", serverMsg.account_id());

    // send SOCacheSubscribedRequest
    CMsgGC_CC_CL2GC_SOCacheSubscribedRequest soCacheRequest;
    soCacheRequest.set_steam_id(m_steamId);
    GCMessageWrite cacheMsg(k_EMsgGC_CC_CL2GC_SOCacheSubscribedRequest, soCacheRequest, true);

    if (m_gcConnection && m_gcConnection->CCIsConnected()) {
        m_gcConnection->NetSendMessage(cacheMsg);
        Platform::Print("Sent SOCacheSubscribedRequest\n");
    }

    CheckAndSendWelcomeMessages();
}

void ClientGC::HandleCacheSubscription(const CMsgSOCacheSubscribed& message) 
{
    // store message in inv class
    StoreCacheSubscription(message);
    m_hasSOCacheData = true;

    Platform::Print("Received CMsgGC_CC_GC2CL_SOCacheSubscribed for account %u\n", message.owner_soid().id() & 0xFFFFFFFF);

    CheckAndSendWelcomeMessages();
}

void ClientGC::StoreCacheSubscription(const CMsgSOCacheSubscribed& message)
{
    m_serverSOCacheData = message;

    m_items.clear();
    m_defaultEquips.clear();

    for (const auto& obj : message.objects()) {
        if (obj.type_id() == SOTypeItem) {
            for (const auto& itemData : obj.object_data()) {
                CSOEconItem item;
                if (item.ParseFromString(itemData)) {
                    m_items[item.id()] = item;
                    Platform::Print("Added item ID %llu to local cache from SOCache\n", item.id());
                }
            }
        }
        else if (obj.type_id() == SOTypeDefaultEquippedDefinitionInstanceClient) {
            for (const auto& equipData : obj.object_data()) {
                CSOEconDefaultEquippedDefinitionInstanceClient defaultEquip;
                if (defaultEquip.ParseFromString(equipData)) {
                    m_defaultEquips.push_back(defaultEquip);
                    Platform::Print("Added default equip to local cache from SOCache\n");
                }
            }
        }
    }

    Platform::Print("StoreCacheSubscription: Cached %zu items and %zu default equips\n",
        m_items.size(), m_defaultEquips.size());
};

void ClientGC::BuildCacheSubscription(CMsgSOCacheSubscribed& message, bool server)
{
    message = m_serverSOCacheData;

    Platform::Print("BuildCacheSubscription: Using %zu cached items and %zu default equips\n", m_items.size(), m_defaultEquips.size());
};

void ClientGC::CheckAndSendWelcomeMessages() 
{
    if (m_hasMatchmakingData && m_hasSOCacheData) 
    {
        m_pendingOnClientHello = false;
        Platform::Print("Both MatchmakingHello and CacheSubscribed received, calling SendWelcomeMessages()\n");
        SendWelcomeMessages();
    }
}

void ClientGC::BuildMatchmakingHello(CMsgGCCStrike15_v2_MatchmakingGC2ClientHello& message)
{
    message.set_account_id(m_serverMatchmakingData.account_id());

    // global
    if (m_serverMatchmakingData.has_global_stats()) {
        *message.mutable_global_stats() = m_serverMatchmakingData.global_stats();
    }

    // ranks
    if (m_serverMatchmakingData.has_ranking()) {
        *message.mutable_ranking() = m_serverMatchmakingData.ranking();
    }

    // commends
    if (m_serverMatchmakingData.has_commendation()) {
        *message.mutable_commendation() = m_serverMatchmakingData.commendation();
    }

    message.set_vac_banned(m_serverMatchmakingData.vac_banned());

    if (m_serverMatchmakingData.has_penalty_reason()) {
        message.set_penalty_reason(m_serverMatchmakingData.penalty_reason());
    }
    if (m_serverMatchmakingData.has_penalty_seconds()) {
        message.set_penalty_seconds(m_serverMatchmakingData.penalty_seconds());
    }

    message.set_player_level(m_serverMatchmakingData.player_level());
    message.set_player_cur_xp(m_serverMatchmakingData.player_cur_xp());
    message.set_player_xp_bonus_flags(m_serverMatchmakingData.player_xp_bonus_flags());
}

void ClientGC::BuildClientWelcome(CMsgClientWelcome &message, const CMsgCStrike15Welcome &csWelcome,
                                  const CMsgGCCStrike15_v2_MatchmakingGC2ClientHello &matchmakingHello)
{
    // mikkotodo remove dox
    message.set_version(0); // this is accurate
    message.set_game_data(csWelcome.SerializeAsString());
    BuildCacheSubscription(*message.add_outofdate_subscribed_caches(), false);
    message.mutable_location()->set_latitude(65.0133006f);
    message.mutable_location()->set_longitude(25.4646212f);
    message.mutable_location()->set_country("PL"); // POLSKA GUROM
    message.set_game_data2(matchmakingHello.SerializeAsString());
    message.set_rtime32_gc_welcome_timestamp(static_cast<uint32_t>(time(nullptr)));
    message.set_currency(2); // euros
}

void ClientGC::OnClientHello(GCMessageRead &messageRead) 
{

    std::vector<uint32_t> int_values;
    std::vector<uint64_t> int64_values;

    m_gcConnection = new TCPConnection(m_steamId);
    m_gcConnection->SetClientGC(this);
    m_gcConnection->CCConnect();
    
    CMsgClientHello hello;
    if (!messageRead.ReadProtobuf(hello))
    {
        Platform::Print("Parsing CMsgClientHello failed, ignoring\n");
        return;
    }

    // just relax man
    if (!m_hasMatchmakingData) 
    {
        m_pendingOnClientHello = true;
        Platform::Print("Called OnClientHello, now waiting for CMsgGC_CC_GC2CL_BuildMatchmakingHello...\n");
        return;
    }
}

void ClientGC::SendWelcomeMessages() 
{
    CMsgCStrike15Welcome csWelcome;
    BuildCSWelcome(csWelcome);

    CMsgGCCStrike15_v2_MatchmakingGC2ClientHello mmHello;
    BuildMatchmakingHello(mmHello);

    CMsgClientWelcome clientWelcome;
    BuildClientWelcome(clientWelcome, csWelcome, mmHello);

    SendMessageToGame(false, k_EMsgGCClientWelcome, clientWelcome);
    SendMessageToGame(false, k_EMsgGCCStrike15_v2_MatchmakingGC2ClientHello, mmHello);
}





void ClientGC::HandleViewPlayersProfileResponse(const CMsgGC_CC_GC2CL_ViewPlayersProfileResponse& message) 
{
    Platform::Print("Received profile data for accountId %d\n", message.account_profiles(0).account_id());
    CMsgGCCStrike15_v2_PlayersProfile clientMessage;

    for (const auto& profile : message.account_profiles()) {
        auto* newProfile = clientMessage.add_account_profiles();
        newProfile->set_account_id(profile.account_id());

        if (profile.has_ranking()) {
            auto* ranking = newProfile->mutable_ranking();
            ranking->set_account_id(profile.ranking().account_id());
            ranking->set_rank_id(profile.ranking().rank_id());
            ranking->set_wins(profile.ranking().wins());
            ranking->set_rank_change(profile.ranking().rank_change());
        }

        if (profile.has_commendation()) {
            auto* commendation = newProfile->mutable_commendation();
            commendation->set_cmd_friendly(profile.commendation().cmd_friendly());
            commendation->set_cmd_teaching(profile.commendation().cmd_teaching());
            commendation->set_cmd_leader(profile.commendation().cmd_leader());
        }

        if (profile.has_medals()) {
            auto* medals = newProfile->mutable_medals();
            medals->set_featured_display_item_defidx(profile.medals().featured_display_item_defidx());
            for (int i = 0; i < profile.medals().display_items_defidx_size(); i++) {
                medals->add_display_items_defidx(profile.medals().display_items_defidx(i));
            }
        }

        if (profile.has_player_level()) {
            newProfile->set_player_level(profile.player_level());
            newProfile->set_player_cur_xp(profile.player_cur_xp());
        }
    }

    SendMessageToGame(false, k_EMsgGCCStrike15_v2_PlayersProfile, clientMessage);
}

void ClientGC::HandleViewPlayersProfileRequest(GCMessageRead& messageRead) 
{
    CMsgGCCStrike15_v2_ClientRequestPlayersProfile clientRequest;
    if (!messageRead.ReadProtobuf(clientRequest)) {
        return;
    }

    CMsgGC_CC_CL2GC_ViewPlayersProfileRequest viewPlayersProfileRequest;
    viewPlayersProfileRequest.set_account_id(clientRequest.account_id());
    viewPlayersProfileRequest.set_request_level(clientRequest.request_level());

    // send it
    if (m_gcConnection && m_gcConnection->CCIsConnected()) 
    {
        GCMessageWrite message(k_EMsgGC_CC_CL2GC_ViewPlayersProfileRequest, viewPlayersProfileRequest, true);
        m_gcConnection->NetSendMessage(message);
        Platform::Print("Sent profile request for account %u to GC\n", clientRequest.account_id());
    }
}

// COMMENDS
void ClientGC::HandleClientCommendPlayerQuery(GCMessageRead& messageRead) // grrrr
{
    CMsgGCCStrike15_v2_ClientCommendPlayer clientRequest;
    if (!messageRead.ReadProtobuf(clientRequest)) {
        Platform::Print("Parsing CMsgGCCStrike15_v2_ClientCommendPlayerQuery failed, ignoring\n");
        return;
    }

    CMsgGC_CC_ClientCommendPlayer gcCommendPlayerQuery;
    gcCommendPlayerQuery.set_account_id(clientRequest.account_id());

    // Forward to GC networking
    if (m_gcConnection && m_gcConnection->CCIsConnected())
    {
        GCMessageWrite message(k_EMsgGC_CC_CL2GC_ClientCommendPlayerQuery, gcCommendPlayerQuery, true);
        m_gcConnection->NetSendMessage(message);
        Platform::Print("Sent commendation query for account %u to GC\n", clientRequest.account_id());
    }
}

void ClientGC::HandleClientCommendPlayerQueryResponse(const CMsgGC_CC_ClientCommendPlayer& message)
{
    Platform::Print("Received commendation query response for accountId %u\n", message.account_id());

    // Create the message to forward to the client
    CMsgGCCStrike15_v2_ClientCommendPlayer clientCommendPlayerQueryResponse;
    clientCommendPlayerQueryResponse.set_account_id(message.account_id());

    // Copy commendation info if present
    if (message.has_commendation()) {
        auto* commendation = clientCommendPlayerQueryResponse.mutable_commendation();
        commendation->set_cmd_friendly(message.commendation().cmd_friendly());
        commendation->set_cmd_teaching(message.commendation().cmd_teaching());
        commendation->set_cmd_leader(message.commendation().cmd_leader());
    }

    // Set tokens if present
    if (message.has_tokens()) {
        clientCommendPlayerQueryResponse.set_tokens(message.tokens());
    }

    // Send the response to the game client
    SendMessageToGame(false, k_EMsgGCCStrike15_v2_ClientCommendPlayerQueryResponse, clientCommendPlayerQueryResponse);
}

void ClientGC::HandleClientCommendPlayer(GCMessageRead& messageRead)
{
    CMsgGCCStrike15_v2_ClientCommendPlayer clientRequest;
    if (!messageRead.ReadProtobuf(clientRequest)) {
        Platform::Print("Parsing CMsgGCCStrike15_v2_ClientCommendPlayer failed, ignoring\n");
        return;
    }

    CMsgGC_CC_ClientCommendPlayer gcCommendPlayer;
    gcCommendPlayer.set_account_id(clientRequest.account_id());

    // commend data
    if (clientRequest.has_commendation()) {
        auto* commendation = gcCommendPlayer.mutable_commendation();
        commendation->set_cmd_friendly(clientRequest.commendation().cmd_friendly());
        commendation->set_cmd_teaching(clientRequest.commendation().cmd_teaching());
        commendation->set_cmd_leader(clientRequest.commendation().cmd_leader());
    }

    if (m_gcConnection && m_gcConnection->CCIsConnected())
    {
        GCMessageWrite message(k_EMsgGC_CC_CL2GC_ClientCommendPlayer, gcCommendPlayer, true);
        m_gcConnection->NetSendMessage(message);
        Platform::Print("Sent commendation for account %u to GC\n", clientRequest.account_id());
    }
}





//PLAYER REPORTS
void ClientGC::HandleClientReportPlayer(GCMessageRead& messageRead)
{
    CMsgGCCStrike15_v2_ClientReportPlayer clientRequest;
    if (!messageRead.ReadProtobuf(clientRequest)) {
        Platform::Print("Parsing CMsgGCCStrike15_v2_ClientReportPlayer failed, ignoring\n");
        return;
    }

    CMsgGC_CC_CL2GC_ClientReportPlayer reportPlayerRequest;
    reportPlayerRequest.set_account_id(clientRequest.account_id());

    if (clientRequest.rpt_aimbot()) reportPlayerRequest.set_rpt_aimbot(1);
    if (clientRequest.rpt_wallhack()) reportPlayerRequest.set_rpt_wallhack(1);
    if (clientRequest.rpt_speedhack()) reportPlayerRequest.set_rpt_speedhack(1);
    if (clientRequest.rpt_teamharm()) reportPlayerRequest.set_rpt_teamharm(1);
    if (clientRequest.rpt_textabuse()) reportPlayerRequest.set_rpt_textabuse(1);
    if (clientRequest.rpt_voiceabuse()) reportPlayerRequest.set_rpt_voiceabuse(1);

    // idk what this outputs
    if (clientRequest.has_match_id()) {
        reportPlayerRequest.set_match_id(clientRequest.match_id());
    }
    //reportPlayerRequest.set_report_from_demo(clientRequest.report_from_demo()); // apparently not there?

    if (m_gcConnection && m_gcConnection->CCIsConnected())
    {
        GCMessageWrite message(k_EMsgGC_CC_CL2GC_ClientReportPlayer, reportPlayerRequest, true);
        m_gcConnection->NetSendMessage(message);
        Platform::Print("Sent player report for account %u to GC\n", clientRequest.account_id());
    }
}

void ClientGC::HandleClientReportResponse(const CMsgGC_CC_GC2CL_ClientReportResponse& message)
{
    Platform::Print("Received report response for accountId %u\n", message.account_id());

    CMsgGCCStrike15_v2_ClientReportResponse clientResponse;

    clientResponse.set_confirmation_id(message.confirmation_id());
    clientResponse.set_account_id(message.account_id());
    clientResponse.set_server_ip(message.server_ip());
    clientResponse.set_response_type(message.response_type());
    clientResponse.set_response_result(message.response_result());

    if (message.has_tokens()) {
        clientResponse.set_tokens(message.tokens());
    }

    SendMessageToGame(false, k_EMsgGCCStrike15_v2_ClientReportResponse, clientResponse);
}






/*void ClientGC::AdjustItemEquippedState(GCMessageRead& messageRead)
{
    CMsgAdjustItemEquippedState message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgAdjustItemEquippedState failed, ignoring\n");
        return;
    }

    CMsgSOMultipleObjects update;
    if (!m_inventory.EquipItem(message.item_id(), message.new_class(), message.new_slot(), update))
    {
        // no change
        //assert(false);
        return;
    }

    // let the gameserver know, too
    SendMessageToGame(true, k_ESOMsg_UpdateMultiple, update);
}*/

void ClientGC::HandleAdjustItemEquippedState(GCMessageRead& messageRead)
{
    CMsgAdjustItemEquippedState message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgAdjustItemEquippedState failed, ignoring\n");
        return;
    }

    CMsgGC_CC_CL2GC_AdjustItemEquippedState adjustItemEquippedState;
    adjustItemEquippedState.set_item_id(message.item_id());
    adjustItemEquippedState.set_new_class(message.new_class());
    adjustItemEquippedState.set_new_slot(message.new_slot());
    // idk what this is, probably not important
    if (message.has_swap())
    {
        adjustItemEquippedState.set_swap(message.swap());
    }

    Platform::Print("Sending AdjustItemEquippedState for item %llu to GC\n", message.item_id());

    if (m_gcConnection && m_gcConnection->CCIsConnected())
    {
        GCMessageWrite messageWrite(k_EMsgGC_CC_CL2GC_AdjustItemEquippedState, adjustItemEquippedState, true);
        m_gcConnection->NetSendMessage(messageWrite);
    }
}

void ClientGC::ClientPlayerDecalSign(GCMessageRead &messageRead)
{
    CMsgGCCStrike15_v2_ClientPlayerDecalSign message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgGCCStrike15_v2_ClientPlayerDecalSign failed, ignoring\n");
        return;
    }

    if (!Graffiti::SignMessage(*message.mutable_data()))
    {
        Platform::Print("Could not sign graffiti! it won't appear\n");
        return;
    }

    SendMessageToGame(false, k_EMsgGCCStrike15_v2_ClientPlayerDecalSign, message);
}

/*void ClientGC::UseItemRequest(GCMessageRead& messageRead)
{
    CMsgUseItem message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgUseItem failed, ignoring\n");
        return;
    }

    CMsgSOSingleObject destroy;
    CMsgSOMultipleObjects updateMultiple;
    //CMsgGCItemCustomizationNotification notification;

    if (m_inventory.UseItem(message.item_id(), destroy, updateMultiple))
    {
        SendMessageToGame(true, k_ESOMsg_Destroy, destroy);
        SendMessageToGame(true, k_ESOMsg_UpdateMultiple, updateMultiple);
        //SendMessageToGame(false, k_EMsgGCItemCustomizationNotification, notification);
    }
}*/

void ClientGC::HandleUseItemRequest(GCMessageRead& messageRead)
{
    CMsgUseItem message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgUseItem failed, ignoring\n");
        return;
    }

    CMsgGC_CC_CL2GC_UseItem useItemRequest;
    useItemRequest.set_item_id(message.item_id());

    if (message.has_target_steam_id()) {
        useItemRequest.set_target_steam_id(message.target_steam_id());
    }

    for (int i = 0; i < message.gift__potential_targets_size(); i++) {
        useItemRequest.add_gift__potential_targets(message.gift__potential_targets(i));
    }

    if (message.has_duel__class_lock()) {
        useItemRequest.set_duel__class_lock(message.duel__class_lock());
    }

    if (message.has_initiator_steam_id()) {
        useItemRequest.set_initiator_steam_id(message.initiator_steam_id());
    }

    Platform::Print("Sending UseItem request for item %llu to GC\n", message.item_id());

    if (m_gcConnection && m_gcConnection->CCIsConnected())
    {
        GCMessageWrite messageWrite(k_EMsgGC_CC_CL2GC_UseItem, useItemRequest, true);
        m_gcConnection->NetSendMessage(messageWrite);
    }
}

static void AddressString(uint32_t ip, uint32_t port, char *buffer, size_t bufferSize)
{
    snprintf(buffer, bufferSize,
        "%u.%u.%u.%u:%u\n",
        (ip >> 24) & 0xff,
        (ip >> 16) & 0xff,
        (ip >> 8) & 0xff,
        ip & 0xff,
        port);
}

void ClientGC::ClientRequestJoinServerData(GCMessageRead &messageRead)
{
    CMsgGCCStrike15_v2_ClientRequestJoinServerData request;
    if (!messageRead.ReadProtobuf(request))
    {
        Platform::Print("Parsing CMsgGCCStrike15_v2_ClientRequestJoinServerData failed, ignoring\n");
        return;
    }

    CMsgGCCStrike15_v2_ClientRequestJoinServerData response = request;
    response.mutable_res()->set_serverid(request.version());
    response.mutable_res()->set_direct_udp_ip(request.server_ip());
    response.mutable_res()->set_direct_udp_port(request.server_port());
    response.mutable_res()->set_reservationid(GameServerCookieId);

    char addressString[32];
    AddressString(request.server_ip(), request.server_port(), addressString, sizeof(addressString));
    response.mutable_res()->set_server_address(addressString);

    SendMessageToGame(false, k_EMsgGCCStrike15_v2_ClientRequestJoinServerData, response);
}

/*void ClientGC::SetItemPositions(GCMessageRead& messageRead)
{
    CMsgSetItemPositions message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgSetItemPositions failed, ignoring\n");
        return;
    }

    std::vector<CMsgItemAcknowledged> acknowledgements;
    acknowledgements.reserve(message.item_positions_size());

    CMsgSOMultipleObjects update;
    if (m_inventory.SetItemPositions(message, acknowledgements, update))
    {
        for (const CMsgItemAcknowledged &acknowledgement : acknowledgements)
        {
            // send these to the gameserver only
            GCMessageWrite messageWrite{ k_EMsgGCItemAcknowledged, acknowledgement };
            m_networking.NetSendMessage(messageWrite);
        }

        SendMessageToGame(true, k_ESOMsg_UpdateMultiple, update);
    }
    else
    {
        //assert(false);
    }
}*/

void ClientGC::HandleSOSingleObject(const CMsgSOSingleObject& message)
{
    Platform::Print("Processing SOSingleObject with type %u\n", message.type_id());

    if (message.type_id() == SOTypeItem) {
        CSOEconItem item;
        if (item.ParseFromString(message.object_data())) {
            m_items[item.id()] = item;
            Platform::Print("Updated local cache with item ID %llu\n", item.id());
        }
    }
    else if (message.type_id() == SOTypeDefaultEquippedDefinitionInstanceClient) {
        CSOEconDefaultEquippedDefinitionInstanceClient defaultEquip;
        if (defaultEquip.ParseFromString(message.object_data())) {
            bool found = false;
            for (auto& existingEquip : m_defaultEquips) {
                if (existingEquip.account_id() == defaultEquip.account_id() &&
                    existingEquip.class_id() == defaultEquip.class_id() &&
                    existingEquip.slot_id() == defaultEquip.slot_id()) {
                    existingEquip = defaultEquip;
                    found = true;
                    break;
                }
            }

            if (!found) {
                m_defaultEquips.push_back(defaultEquip);
            }
            Platform::Print("Updated local cache with default equip for account %u, class %u, slot %u\n",
                defaultEquip.account_id(), defaultEquip.class_id(), defaultEquip.slot_id());
        }
    }

    SendMessageToGame(true, k_ESOMsg_Update, message);
}

void ClientGC::HandleSOMultipleObjects(const CMsgSOMultipleObjects& message)
{
    Platform::Print("Processing SOMultipleObjects with %d objects\n", message.objects_modified_size());

    for (int i = 0; i < message.objects_modified_size(); i++) {
        if (message.objects_modified(i).type_id() == SOTypeItem) {
            CSOEconItem item;
            if (item.ParseFromString(message.objects_modified(i).object_data())) {
                m_items[item.id()] = item;
                Platform::Print("Updated local cache with modified item ID %llu\n", item.id());
            }
        }
        else if (message.objects_modified(i).type_id() == SOTypeDefaultEquippedDefinitionInstanceClient) {
            CSOEconDefaultEquippedDefinitionInstanceClient defaultEquip;
            if (defaultEquip.ParseFromString(message.objects_modified(i).object_data())) {
                bool found = false;
                for (auto& existingEquip : m_defaultEquips) {
                    if (existingEquip.account_id() == defaultEquip.account_id() &&
                        existingEquip.class_id() == defaultEquip.class_id() &&
                        existingEquip.slot_id() == defaultEquip.slot_id()) {
                        existingEquip = defaultEquip;
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    m_defaultEquips.push_back(defaultEquip);
                }
                Platform::Print("Updated local cache with modified default equip\n");
            }
        }
    }

    for (int i = 0; i < message.objects_added_size(); i++) {
        if (message.objects_added(i).type_id() == SOTypeItem) {
            CSOEconItem item;
            if (item.ParseFromString(message.objects_added(i).object_data())) {
                m_items[item.id()] = item;
                Platform::Print("Added new item ID %llu to local cache\n", item.id());
            }
        }
        else if (message.objects_added(i).type_id() == SOTypeDefaultEquippedDefinitionInstanceClient) {
            CSOEconDefaultEquippedDefinitionInstanceClient defaultEquip;
            if (defaultEquip.ParseFromString(message.objects_added(i).object_data())) {
                m_defaultEquips.push_back(defaultEquip);
                Platform::Print("Added new default equip to local cache\n");
            }
        }
    }

    for (int i = 0; i < message.objects_removed_size(); i++) {
        if (message.objects_removed(i).type_id() == SOTypeItem) {
            CSOEconItem item;
            if (item.ParseFromString(message.objects_removed(i).object_data())) {
                m_items.erase(item.id());
                Platform::Print("Removed item ID %llu from local cache\n", item.id());
            }
        }
        else if (message.objects_removed(i).type_id() == SOTypeDefaultEquippedDefinitionInstanceClient) {
            CSOEconDefaultEquippedDefinitionInstanceClient defaultEquip;
            if (defaultEquip.ParseFromString(message.objects_removed(i).object_data())) {
                for (auto it = m_defaultEquips.begin(); it != m_defaultEquips.end(); ++it) {
                    if (it->account_id() == defaultEquip.account_id() &&
                        it->class_id() == defaultEquip.class_id() &&
                        it->slot_id() == defaultEquip.slot_id()) {
                        m_defaultEquips.erase(it);
                        Platform::Print("Removed default equip from local cache\n");
                        break;
                    }
                }
            }
        }
    }

    SendMessageToGame(true, k_ESOMsg_UpdateMultiple, message);
}

void ClientGC::HandleItemAcknowledged(GCMessageRead& messageRead)
{
    CMsgSetItemPositions message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgSetItemPositions failed, ignoring\n");
        return;
    }

    if (message.item_positions_size() <= 0)
    {
        return;
    }

    const int BATCH_SIZE = 200;
    int totalItems = message.item_positions_size();
    int batchesSent = 0;

    for (int startIdx = 0; startIdx < totalItems; startIdx += BATCH_SIZE)
    {
        CMsgGC_CC_CL2GC_ItemAcknowledged acknowledgedMessage;

        int endIdx = startIdx + BATCH_SIZE;
        if (endIdx > totalItems)
        {
            endIdx = totalItems;
        }

        // add item ids
        for (int i = startIdx; i < endIdx; i++)
        {
            acknowledgedMessage.add_item_id(message.item_positions(i).item_id());
        }

        if (m_gcConnection && m_gcConnection->CCIsConnected())
        {
            GCMessageWrite message(k_EMsgGC_CC_CL2GC_ItemAcknowledged, acknowledgedMessage, true);
            m_gcConnection->NetSendMessage(message);
            Platform::Print("Sent acknowledgment batch %d/%d with %d items to GC\n",
                ++batchesSent, (totalItems + BATCH_SIZE - 1) / BATCH_SIZE,
                acknowledgedMessage.item_id_size());
        }
    }
    Platform::Print("Completed sending acknowledgments for %d items in %d batches to GC\n", totalItems, batchesSent);
}





/*void ClientGC::IncrementKillCountAttribute(GCMessageRead& messageRead)
{
    Platform::Print("Called IncrementKillCountAttribute\n");

    CMsgIncrementKillCountAttribute message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgIncrementKillCountAttribute failed, ignoring\n");
        return;
    }

    assert(message.event_type() == 0);

    CMsgSOSingleObject update;
    if (m_inventory.IncrementKillCountAttribute(message.item_id(), message.amount(), update))
    {
        SendMessageToGame(true, k_ESOMsg_Update, update);
    }
    else
    {
        //assert(false);
    }
}*/


/*void ClientGC::ApplySticker(GCMessageRead& messageRead)
{
    CMsgApplySticker message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgApplySticker failed, ignoring\n");
        return;
    }

    assert(!message.item_item_id() != !message.baseitem_defidx());

    CMsgSOSingleObject update, destroy;
    if (!message.sticker_item_id())
    {
        // scrape
        if (m_inventory.ScrapeSticker(message, update, destroy))
        {
            if (destroy.has_type_id())
            {
                // destroying a default item
                SendMessageToGame(true, k_ESOMsg_Destroy, destroy);
            }

            if (update.has_type_id())
            {
                // if the item got removed (handled above), nothing gets updated
                SendMessageToGame(true, k_ESOMsg_Update, update);
            }
        }
        else
        {
            //assert(false);
        }
    }
    else if (m_inventory.ApplySticker(message, update, destroy))
    {
        SendMessageToGame(true, k_ESOMsg_Destroy, destroy);
        SendMessageToGame(true, k_ESOMsg_Update, update);
    }
    else
    {
        //assert(false);
    }
}*/

void ClientGC::HandleApplySticker(GCMessageRead& messageRead)
{
    CMsgApplySticker message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgApplySticker failed, ignoring\n");
        return;
    }

    CMsgGC_CC_CL2GC_ApplySticker applyStickerRequest;
    applyStickerRequest.set_sticker_item_id(message.sticker_item_id());

    if (message.has_item_item_id()) {
        applyStickerRequest.set_item_item_id(message.item_item_id());
    }

    if (message.has_baseitem_defidx()) {
        applyStickerRequest.set_baseitem_defidx(message.baseitem_defidx());
    }

    if (message.has_sticker_slot()) {
        applyStickerRequest.set_sticker_slot(message.sticker_slot());
    }

    if (message.has_sticker_wear()) {
        applyStickerRequest.set_sticker_wear(message.sticker_wear());
    }

    Platform::Print("Sending ApplySticker request for sticker %llu to GC\n", message.sticker_item_id());

    if (m_gcConnection && m_gcConnection->CCIsConnected())
    {
        GCMessageWrite messageWrite(k_EMsgGC_CC_CL2GC_ApplySticker, applyStickerRequest, true);
        m_gcConnection->NetSendMessage(messageWrite);
    }
}

void ClientGC::StoreGetUserData(GCMessageRead& messageRead)
{
    CMsgStoreGetUserData message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgStoreGetUserData failed, ignoring\n");
        return;
    }

    KeyValue priceSheet{ "price_sheet" };
    if (!priceSheet.ParseFromFile("csgo_gc/price_sheet.txt"))
    {
        return;
    }

    std::string binaryString;
    binaryString.reserve(1 << 17);
    priceSheet.BinaryWriteToString(binaryString);

    // fuck you idiot
    CMsgStoreGetUserDataResponse response;
    response.set_result(1);
    response.set_price_sheet_version(1729); // what
    *response.mutable_price_sheet() = std::move(binaryString);

    SendMessageToGame(false, k_EMsgGCStoreGetUserDataResponse, response);
}

void ClientGC::HandleStorePurchaseInit(GCMessageRead& messageRead)
{
    CMsgGCStorePurchaseInit message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgGCStorePurchaseInit failed, ignoring\n");
        return;
    }

    if (m_gcConnection && m_gcConnection->CCIsConnected())
    {
        CMsgGC_CC_CL2GC_StorePurchaseInit forwardMessage;
        forwardMessage.set_country(message.country());
        forwardMessage.set_language(message.language());
        forwardMessage.set_currency(message.currency());

        // CGCStorePurchaseInit_LineItem
        for (int i = 0; i < message.line_items_size(); i++)
        {
            auto* lineItem = forwardMessage.add_line_items();
            lineItem->set_item_def_id(message.line_items(i).item_def_id());
            lineItem->set_quantity(message.line_items(i).quantity());
            lineItem->set_cost_in_local_currency(message.line_items(i).cost_in_local_currency());
            lineItem->set_purchase_type(message.line_items(i).purchase_type());
        }

        GCMessageWrite messageWrite(k_EMsgGC_CC_CL2GC_StorePurchaseInit, forwardMessage, true);
        m_gcConnection->NetSendMessage(messageWrite);
        Platform::Print("StorePurchaseInit forwarded to GC\n");
    }
}

void ClientGC::HandleStorePurchaseInitResponse(const CMsgGC_CC_GC2CL_StorePurchaseInitResponse& message)
{
    Platform::Print("Received StorePurchaseInitResponse from GC - Result: %d\n", message.result());

    CMsgGCStorePurchaseInitResponse clientResponse;
    clientResponse.set_result(message.result());

    // transaction id
    if (message.has_txn_id()) {
        clientResponse.set_txn_id(message.txn_id());
        Platform::Print("  Transaction ID: %llu\n", message.txn_id());
    }

    // prolly wont be used
    if (message.has_url() && !message.url().empty()) {
        clientResponse.set_url(message.url());
        Platform::Print("  URL: %s\n", message.url().c_str());
    }

    // item ids
    for (int i = 0; i < message.item_ids_size(); i++) {
        clientResponse.add_item_ids(message.item_ids(i));
        Platform::Print("  Item ID: %llu\n", message.item_ids(i));
    }

    SendMessageToGame(false, k_EMsgGCStorePurchaseInitResponse, clientResponse); // this probably isnt enough
    Platform::Print("StorePurchaseInitResponse forwarded to game client\n");
}

/*void ClientGC::UnlockCrate(GCMessageRead& messageRead)
{
    uint64_t keyId = messageRead.ReadUint64(); // unused
    uint64_t crateId = messageRead.ReadUint64();
    if (!messageRead.IsValid())
    {
        Platform::Print("Parsing CMsgGCUnlockCrate failed, ignoring\n");
        return;
    }

    Platform::Print("CASE OPENING %llu\n", crateId);

    CMsgSOSingleObject destroyCrate, newItem;

    if (m_inventory.UnlockCrate(
        crateId,
        destroyCrate,
        newItem))
    {
        // mikkotodo what does the server want to know
        SendMessageToGame(true, k_ESOMsg_Destroy, destroyCrate);
        SendMessageToGame(true, k_ESOMsg_Create, newItem);
        SendMessageToGame(false, k_EMsgGCUnlockCrateResponse, newItem);
    }
    else
    {
        //assert(false);
    }
}*/

void ClientGC::HandleUnlockCrate(GCMessageRead& messageRead)
{
    uint64_t keyId = messageRead.ReadUint64();
    uint64_t crateId = messageRead.ReadUint64();
    if (!messageRead.IsValid())
    {
        Platform::Print("Parsing k_EMsgGCUnlockCrate failed, ignoring\n");
        return;
    }

    CMsgGC_CC_CL2GC_UnlockCrate unlockCrateMessage;
    unlockCrateMessage.set_key_id(keyId);
    unlockCrateMessage.set_crate_id(crateId);

    Platform::Print("Sending unlock request for crate %llu to GC\n", crateId);

    if (m_gcConnection && m_gcConnection->CCIsConnected())
    {
        GCMessageWrite message(k_EMsgGC_CC_CL2GC_UnlockCrate, unlockCrateMessage, true);
        m_gcConnection->NetSendMessage(message);
    }
}

void ClientGC::HandleUnlockCrateResponse(const CMsgSOSingleObject& message)
{
    if (message.type_id() == SOTypeItem) {
        CSOEconItem item;
        if (item.ParseFromString(message.object_data())) {
            m_items[item.id()] = item;
            Platform::Print("Added unlocked item ID %llu to local cache\n", item.id());
        }
    }
    Platform::Print("Received crate unlock response\n");

    Platform::Print("Sending k_ESOMsg_Create to game\n");
    SendMessageToGame(true, k_ESOMsg_Create, message);
    Platform::Print("Sending k_EMsgGCUnlockCrateResponse to game\n");
    SendMessageToGame(false, k_EMsgGCUnlockCrateResponse, message);
}

void ClientGC::HandleCraft(GCMessageRead& messageRead)
{
    Platform::Print("Received k_EMsgGCCraft message\n");

    int16_t recipeDefIndex = messageRead.ReadInt16();
    uint16_t itemCount = messageRead.ReadUint16();

    Platform::Print("  Recipe DefIndex: %d\n", recipeDefIndex);
    Platform::Print("  Item Count: %u\n", itemCount);

    if (itemCount != 10) {
        Platform::Print("  Warning: Unexpected item count for trade-up: %u (expected 10)\n", itemCount);
    }

    std::vector<uint64_t> item_ids;
    item_ids.reserve(itemCount);

    for (uint16_t i = 0; i < itemCount; i++) {
        if (!messageRead.IsValid()) {
            Platform::Print("  Error: Unexpected end of message while reading item IDs\n");
            return;
        }

        uint64_t item_id = messageRead.ReadUint64();
        item_ids.push_back(item_id);
        Platform::Print("  Item ID %u: %llu (0x%llX)\n", i + 1, item_id, item_id);
    }

    if (m_gcConnection && m_gcConnection->CCIsConnected()) {
        CMsgGC_CC_CL2GC_Craft craftRequest;
        craftRequest.set_recipe_defindex(recipeDefIndex);

        for (const auto& id : item_ids) {
            craftRequest.add_item_ids(id);
        }

        GCMessageWrite message(k_EMsgGC_CC_CL2GC_Craft, craftRequest, true);
        m_gcConnection->NetSendMessage(message);
        Platform::Print("Sent craft request with recipe %d and %zu items to GC\n",
            recipeDefIndex, item_ids.size());
    }
    else {
        Platform::Print("Cannot send craft request - not connected to GC\n");
    }
}

void ClientGC::HandleCraftResponse(const CMsgGC_CC_GC2CL_CraftResponse& gcResponse)
{
    Platform::Print("Received craft response from GC\n");
    Platform::Print("  Response Index: %d\n", gcResponse.response_index());
    Platform::Print("  Response Code: %u\n", gcResponse.response_code());

    if (gcResponse.response_code() == 0) {
        Platform::Print("  Craft successful!\n");

        if (gcResponse.has_item_object()) {
            const CMsgGC_CC_GC2CL_SOSingleObject& newItemObject = gcResponse.item_object();

            CMsgSOSingleObject itemObject;
            itemObject.set_type_id(newItemObject.type_id());
            itemObject.set_object_data(newItemObject.object_data());
            if (newItemObject.has_version()) {
                itemObject.set_version(newItemObject.version());
            }
            if (newItemObject.has_owner_soid()) {
                auto* ownerSoid = itemObject.mutable_owner_soid();
                ownerSoid->set_id(newItemObject.owner_soid().id());
                ownerSoid->set_type(newItemObject.owner_soid().type());
            }

            Platform::Print("  Sending new crafted item to game client via k_ESOMsg_Create\n");
            SendMessageToGame(true, k_ESOMsg_Create, itemObject);
        }
        else {
            Platform::Print("  Craft successful but no item object received\n");
        }
    }
    else {
        Platform::Print("  Craft failed with error code: %u\n", gcResponse.response_code());
    }

    GCMessageWrite messageWrite(k_EMsgGCCraftResponse);
    int16_t responseIndex = gcResponse.response_index();
    uint32_t responseCode = gcResponse.response_code();
    messageWrite.WriteInt16(responseIndex);
    messageWrite.WriteUint32(responseCode);
    Platform::Print("  Sending craft response to game client\n");
    m_networking.NetSendMessage(messageWrite);
}

/*void ClientGC::NameItem(GCMessageRead& messageRead)
{
    uint64_t nameTagId = messageRead.ReadUint64();
    uint64_t itemId = messageRead.ReadUint64();
    messageRead.ReadData(1); // skip the sentinel
    std::string_view name = messageRead.ReadString();

    if (!messageRead.IsValid())
    {
        Platform::Print("Parsing CMsgGCNameItem failed, ignoring\n");
        return;
    }

    CMsgSOSingleObject update, destroy;
    if (m_inventory.NameItem(nameTagId, itemId, name, update, destroy))
    {
        SendMessageToGame(true, k_ESOMsg_Update, update);
        SendMessageToGame(true, k_ESOMsg_Destroy, destroy);
    }
    else
    {
        //assert(false);
    }
}*/

void ClientGC::HandleNameItem(GCMessageRead& messageRead)
{
    uint64_t nameTagId = messageRead.ReadUint64();
    uint64_t itemId = messageRead.ReadUint64();
    messageRead.ReadData(1); // skip the sentinel
    std::string_view nameView = messageRead.ReadString();
    std::string name(nameView);

    if (!messageRead.IsValid())
    {
        Platform::Print("Parsing k_EMsgGCNameItem failed, ignoring\n");
        return;
    }

    CMsgGC_CC_CL2GC_NameItem nameItemRequest;
    nameItemRequest.set_nametag_id(nameTagId);
    nameItemRequest.set_item_id(itemId);
    nameItemRequest.set_name(name);

    Platform::Print("Sending NameItem request for item %llu to GC\n", itemId);

    if (m_gcConnection && m_gcConnection->CCIsConnected())
    {
        GCMessageWrite messageWrite(k_EMsgGC_CC_CL2GC_NameItem, nameItemRequest, true);
        m_gcConnection->NetSendMessage(messageWrite);
    }
}

/*void ClientGC::NameBaseItem(GCMessageRead& messageRead)
{
    uint64_t nameTagId = messageRead.ReadUint64();
    uint32_t defIndex = messageRead.ReadUint32();
    messageRead.ReadData(1); // skip the sentinel
    std::string_view name = messageRead.ReadString();

    if (!messageRead.IsValid())
    {
        Platform::Print("Parsing CMsgGCNameBaseItem failed, ignoring\n");
        return;
    }

    CMsgSOSingleObject create, destroy;
    if (m_inventory.NameBaseItem(nameTagId, defIndex, name, create, destroy))
    {
        SendMessageToGame(true, k_ESOMsg_Create, create);
        SendMessageToGame(true, k_ESOMsg_Destroy, destroy);
    }
    else
    {
        //assert(false);
    }
}*/

void ClientGC::HandleNameBaseItem(GCMessageRead& messageRead)
{
    uint64_t nameTagId = messageRead.ReadUint64();
    uint32_t defIndex = messageRead.ReadUint32();
    messageRead.ReadData(1); // skip the sentinel
    std::string_view nameView = messageRead.ReadString();
    std::string name(nameView);

    if (!messageRead.IsValid())
    {
        Platform::Print("Parsing k_EMsgGCNameBaseItem failed, ignoring\n");
        return;
    }

    CMsgGC_CC_CL2GC_NameBaseItem nameBaseItemRequest;
    nameBaseItemRequest.set_nametag_id(nameTagId);
    nameBaseItemRequest.set_defindex(defIndex);
    nameBaseItemRequest.set_name(name);

    Platform::Print("Sending NameBaseItem request for defindex %u to GC\n", defIndex);

    if (m_gcConnection && m_gcConnection->CCIsConnected())
    {
        GCMessageWrite messageWrite(k_EMsgGC_CC_CL2GC_NameBaseItem, nameBaseItemRequest, true);
        m_gcConnection->NetSendMessage(messageWrite);
    }
}

/*void ClientGC::RemoveItemName(GCMessageRead& messageRead)
{
    uint64_t itemId = messageRead.ReadUint64();
    if (!messageRead.IsValid())
    {
        Platform::Print("Parsing CMsgGCRemoveItemName failed, ignoring\n");
        return;
    }

    CMsgSOSingleObject update, destroy;
    if (m_inventory.RemoveItemName(itemId, update, destroy))
    {
        if (update.has_type_id())
        {
            SendMessageToGame(true, k_ESOMsg_Update, update);
        }

        if (destroy.has_type_id())
        {
            SendMessageToGame(true, k_ESOMsg_Destroy, destroy);
        }
    }
    else
    {
        //assert(false);
    }
}*/

void ClientGC::HandleRemoveItemName(GCMessageRead& messageRead)
{
    uint64_t itemId = messageRead.ReadUint64();
    if (!messageRead.IsValid())
    {
        Platform::Print("Parsing k_EMsgGCRemoveItemName failed, ignoring\n");
        return;
    }

    CMsgGC_CC_CL2GC_RemoveItemName removeItemNameRequest;
    removeItemNameRequest.set_item_id(itemId);

    Platform::Print("Sending RemoveItemName request for item %llu to GC\n", itemId);

    if (m_gcConnection && m_gcConnection->CCIsConnected())
    {
        GCMessageWrite messageWrite(k_EMsgGC_CC_CL2GC_RemoveItemName, removeItemNameRequest, true);
        m_gcConnection->NetSendMessage(messageWrite);
    }
}

/*void ClientGC::DeleteItem(GCMessageRead& messageRead)
{
    uint64_t itemId = messageRead.ReadUint64();
    if (!messageRead.IsValid())
    {
        Platform::Print("Parsing k_EMsgGCDelete failed, ignoring\n");
        return;
    }

    CMsgSOSingleObject destroy;
    if (m_inventory.DeleteItem(itemId, destroy))
    {
        SendMessageToGame(true, k_ESOMsg_Destroy, destroy);
    }
    else
    {
        //assert(false);
    }
}*/

void ClientGC::HandleDeleteItemRequest(GCMessageRead& messageRead)
{
    uint64_t itemId = messageRead.ReadUint64();
    if (!messageRead.IsValid())
    {
        Platform::Print("Parsing k_EMsgGCDelete failed, ignoring\n");
        return;
    }

    CMsgGC_CC_DeleteItem deleteMessage;
    deleteMessage.set_item_id(itemId);
    Platform::Print("Sending DeleteItem request for item %llu to GC\n", itemId);

    if (m_gcConnection && m_gcConnection->CCIsConnected())
    {
        GCMessageWrite message(k_EMsgGC_CC_DeleteItem, deleteMessage, true);
        m_gcConnection->NetSendMessage(message);
    }
    else
    {
        Platform::Print("Cannot send DeleteItem request - not connected to GC\n");
    }
}

void ClientGC::HandleDeleteItem(const CMsgSOSingleObject& message)
{
    if (message.type_id() == SOTypeItem) {
        CSOEconItem item;
        if (item.ParseFromString(message.object_data())) {
            auto it = m_items.find(item.id());
            if (it != m_items.end()) {
                Platform::Print("Removing deleted item ID %llu from local cache\n", item.id());
                m_items.erase(it);
            }
        }
    }

    SendMessageToGame(true, k_ESOMsg_Destroy, message);
}

const char *MessageName(uint32_t type)
{
    switch (type)
    {
#define HANDLE_MSG(e) \
    case e: \
        return #e
        HANDLE_MSG(k_EMsgGCSystemMessage); // 4001;
        HANDLE_MSG(k_EMsgGCReplicateConVars); // 4002;
        HANDLE_MSG(k_EMsgGCConVarUpdated); // 4003;
        HANDLE_MSG(k_EMsgGCInQueue); // 4008;
        HANDLE_MSG(k_EMsgGCInviteToParty); // 4501;
        HANDLE_MSG(k_EMsgGCInvitationCreated); // 4502;
        HANDLE_MSG(k_EMsgGCPartyInviteResponse); // 4503;
        HANDLE_MSG(k_EMsgGCKickFromParty); // 4504;
        HANDLE_MSG(k_EMsgGCLeaveParty); // 4505;
        HANDLE_MSG(k_EMsgGCServerAvailable); // 4506;
        HANDLE_MSG(k_EMsgGCClientConnectToServer); // 4507;
        HANDLE_MSG(k_EMsgGCGameServerInfo); // 4508;
        HANDLE_MSG(k_EMsgGCError); // 4509;
        HANDLE_MSG(k_EMsgGCReplay_UploadedToYouTube); // 4510;
        HANDLE_MSG(k_EMsgGCLANServerAvailable); // 4511;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_Base); // 9100;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchmakingStart); // 9101;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchmakingStop); // 9102;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchmakingClient2ServerPing); // 9103;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchmakingGC2ClientUpdate); // 9104;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchmakingServerReservationResponse); // 9106;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchmakingGC2ClientReserve); // 9107;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchmakingClient2GCHello); // 9109;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchmakingGC2ClientHello); // 9110;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchmakingGC2ClientAbandon); // 9112;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchmakingOperator2GCBlogUpdate); // 9117;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ServerNotificationForUserPenalty); // 9118;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientReportPlayer); // 9119;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientReportServer); // 9120;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientCommendPlayer); // 9121;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientReportResponse); // 9122;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientCommendPlayerQuery); // 9123;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientCommendPlayerQueryResponse); // 9124;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_WatchInfoUsers); // 9126;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientRequestPlayersProfile); // 9127;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_PlayersProfile); // 9128;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_SetMyMedalsInfo); // 9129;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_PlayerOverwatchCaseUpdate); // 9131;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_PlayerOverwatchCaseAssignment); // 9132;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_PlayerOverwatchCaseStatus); // 9133;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_GC2ClientTextMsg); // 9134;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_Client2GCTextMsg); // 9135;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchEndRunRewardDrops); // 9136;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchEndRewardDropsNotification); // 9137;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientRequestWatchInfoFriends2); // 9138;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchList); // 9139;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchListRequestCurrentLiveGames); // 9140;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchListRequestRecentUserGames); // 9141;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_GC2ServerReservationUpdate); // 9142;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientVarValueNotificationInfo); // 9144;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchListRequestTournamentGames); // 9146;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchListRequestFullGameInfo); // 9147;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_GiftsLeaderboardRequest); // 9148;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_GiftsLeaderboardResponse); // 9149;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ServerVarValueNotificationInfo); // 9150;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientSubmitSurveyVote); // 9152;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_Server2GCClientValidate); // 9153;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchListRequestLiveGameForUser); // 9154;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_Client2GCEconPreviewDataBlockRequest); // 9156;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_Client2GCEconPreviewDataBlockResponse); // 9157;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_AccountPrivacySettings); // 9158;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_SetMyActivityInfo); // 9159;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchListRequestTournamentPredictions); // 9160;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_MatchListUploadTournamentPredictions); // 9161;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_DraftSummary); // 9162;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientRequestJoinFriendData); // 9163;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientRequestJoinServerData); // 9164;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientRequestNewMission); // 9165;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_GC2ClientTournamentInfo); // 9167;
        HANDLE_MSG(k_EMsgGC_GlobalGame_Subscribe); // 9168;
        HANDLE_MSG(k_EMsgGC_GlobalGame_Unsubscribe); // 9169;
        HANDLE_MSG(k_EMsgGC_GlobalGame_Play); // 9170;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_AcknowledgePenalty); // 9171;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin); // 9172;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_GC2ClientGlobalStats); // 9173;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_Client2GCStreamUnlock); // 9174;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_FantasyRequestClientData); // 9175;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_FantasyUpdateClientData); // 9176;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_GCToClientSteamdatagramTicket); // 9177;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientToGCRequestTicket); // 9178;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientToGCRequestElevate); // 9179;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_GlobalChat); // 9180;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_GlobalChat_Subscribe); // 9181;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_GlobalChat_Unsubscribe); // 9182;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientAuthKeyCode); // 9183;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_GotvSyncPacket); // 9184;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientPlayerDecalSign); // 9185;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientLogonFatalError); // 9187;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_ClientPollState); // 9188;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_Party_Register); // 9189;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_Party_Unregister); // 9190;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_Party_Search); // 9191;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_Party_Invite); // 9192;
        HANDLE_MSG(k_EMsgGCCStrike15_v2_Account_RequestCoPlays); // 9193;
        HANDLE_MSG(k_EMsgGCBase); // 1000;
        HANDLE_MSG(k_EMsgGCSetItemPosition); // 1001;
        HANDLE_MSG(k_EMsgGCCraft); // 1002;
        HANDLE_MSG(k_EMsgGCCraftResponse); // 1003;
        HANDLE_MSG(k_EMsgGCDelete); // 1004;
        HANDLE_MSG(k_EMsgGCVerifyCacheSubscription); // 1005;
        HANDLE_MSG(k_EMsgGCNameItem); // 1006;
        HANDLE_MSG(k_EMsgGCUnlockCrate); // 1007;
        HANDLE_MSG(k_EMsgGCUnlockCrateResponse); // 1008;
        HANDLE_MSG(k_EMsgGCPaintItem); // 1009;
        HANDLE_MSG(k_EMsgGCPaintItemResponse); // 1010;
        HANDLE_MSG(k_EMsgGCGoldenWrenchBroadcast); // 1011;
        HANDLE_MSG(k_EMsgGCMOTDRequest); // 1012;
        HANDLE_MSG(k_EMsgGCMOTDRequestResponse); // 1013;
        HANDLE_MSG(k_EMsgGCAddItemToSocket_DEPRECATED); // 1014;
        HANDLE_MSG(k_EMsgGCAddItemToSocketResponse_DEPRECATED); // 1015;
        HANDLE_MSG(k_EMsgGCAddSocketToBaseItem_DEPRECATED); // 1016;
        HANDLE_MSG(k_EMsgGCAddSocketToItem_DEPRECATED); // 1017;
        HANDLE_MSG(k_EMsgGCAddSocketToItemResponse_DEPRECATED); // 1018;
        HANDLE_MSG(k_EMsgGCNameBaseItem); // 1019;
        HANDLE_MSG(k_EMsgGCNameBaseItemResponse); // 1020;
        HANDLE_MSG(k_EMsgGCRemoveSocketItem_DEPRECATED); // 1021;
        HANDLE_MSG(k_EMsgGCRemoveSocketItemResponse_DEPRECATED); // 1022;
        HANDLE_MSG(k_EMsgGCCustomizeItemTexture); // 1023;
        HANDLE_MSG(k_EMsgGCCustomizeItemTextureResponse); // 1024;
        HANDLE_MSG(k_EMsgGCUseItemRequest); // 1025;
        HANDLE_MSG(k_EMsgGCUseItemResponse); // 1026;
        HANDLE_MSG(k_EMsgGCGiftedItems_DEPRECATED); // 1027;
        HANDLE_MSG(k_EMsgGCRemoveItemName); // 1030;
        HANDLE_MSG(k_EMsgGCRemoveItemPaint); // 1031;
        HANDLE_MSG(k_EMsgGCGiftWrapItem); // 1032;
        HANDLE_MSG(k_EMsgGCGiftWrapItemResponse); // 1033;
        HANDLE_MSG(k_EMsgGCDeliverGift); // 1034;
        HANDLE_MSG(k_EMsgGCDeliverGiftResponseGiver); // 1035;
        HANDLE_MSG(k_EMsgGCDeliverGiftResponseReceiver); // 1036;
        HANDLE_MSG(k_EMsgGCUnwrapGiftRequest); // 1037;
        HANDLE_MSG(k_EMsgGCUnwrapGiftResponse); // 1038;
        HANDLE_MSG(k_EMsgGCSetItemStyle); // 1039;
        HANDLE_MSG(k_EMsgGCUsedClaimCodeItem); // 1040;
        HANDLE_MSG(k_EMsgGCSortItems); // 1041;
        HANDLE_MSG(k_EMsgGC_RevolvingLootList_DEPRECATED); // 1042;
        HANDLE_MSG(k_EMsgGCLookupAccount); // 1043;
        HANDLE_MSG(k_EMsgGCLookupAccountResponse); // 1044;
        HANDLE_MSG(k_EMsgGCLookupAccountName); // 1045;
        HANDLE_MSG(k_EMsgGCLookupAccountNameResponse); // 1046;
        HANDLE_MSG(k_EMsgGCUpdateItemSchema); // 1049;
        HANDLE_MSG(k_EMsgGCRemoveCustomTexture); // 1051;
        HANDLE_MSG(k_EMsgGCRemoveCustomTextureResponse); // 1052;
        HANDLE_MSG(k_EMsgGCRemoveMakersMark); // 1053;
        HANDLE_MSG(k_EMsgGCRemoveMakersMarkResponse); // 1054;
        HANDLE_MSG(k_EMsgGCRemoveUniqueCraftIndex); // 1055;
        HANDLE_MSG(k_EMsgGCRemoveUniqueCraftIndexResponse); // 1056;
        HANDLE_MSG(k_EMsgGCSaxxyBroadcast); // 1057;
        HANDLE_MSG(k_EMsgGCBackpackSortFinished); // 1058;
        HANDLE_MSG(k_EMsgGCAdjustItemEquippedState); // 1059;
        HANDLE_MSG(k_EMsgGCCollectItem); // 1061;
        HANDLE_MSG(k_EMsgGCItemAcknowledged__DEPRECATED); // 1062;
        HANDLE_MSG(k_EMsgGC_ReportAbuse); // 1065;
        HANDLE_MSG(k_EMsgGC_ReportAbuseResponse); // 1066;
        HANDLE_MSG(k_EMsgGCNameItemNotification); // 1068;
        HANDLE_MSG(k_EMsgGCApplyConsumableEffects); // 1069;
        HANDLE_MSG(k_EMsgGCConsumableExhausted); // 1070;
        HANDLE_MSG(k_EMsgGCShowItemsPickedUp); // 1071;
        HANDLE_MSG(k_EMsgGCClientDisplayNotification); // 1072;
        HANDLE_MSG(k_EMsgGCApplyStrangePart); // 1073;
        HANDLE_MSG(k_EMsgGC_IncrementKillCountAttribute); // 1074;
        HANDLE_MSG(k_EMsgGC_IncrementKillCountResponse); // 1075;
        HANDLE_MSG(k_EMsgGCApplyPennantUpgrade); // 1076;
        HANDLE_MSG(k_EMsgGCSetItemPositions); // 1077;
        HANDLE_MSG(k_EMsgGCApplyEggEssence); // 1078;
        HANDLE_MSG(k_EMsgGCNameEggEssenceResponse); // 1079;
        HANDLE_MSG(k_EMsgGCPaintKitItem); // 1080;
        HANDLE_MSG(k_EMsgGCPaintKitBaseItem); // 1081;
        HANDLE_MSG(k_EMsgGCPaintKitItemResponse); // 1082;
        HANDLE_MSG(k_EMsgGCGiftedItems); // 1083;
        HANDLE_MSG(k_EMsgGCUnlockItemStyle); // 1084;
        HANDLE_MSG(k_EMsgGCUnlockItemStyleResponse); // 1085;
        HANDLE_MSG(k_EMsgGCApplySticker); // 1086;
        HANDLE_MSG(k_EMsgGCItemAcknowledged); // 1087;
        HANDLE_MSG(k_EMsgGCStatTrakSwap); // 1088;
        HANDLE_MSG(k_EMsgGCUserTrackTimePlayedConsecutively); // 1089;
        HANDLE_MSG(k_EMsgGCTradingBase); // 1500;
        HANDLE_MSG(k_EMsgGCTrading_InitiateTradeRequest); // 1501;
        HANDLE_MSG(k_EMsgGCTrading_InitiateTradeResponse); // 1502;
        HANDLE_MSG(k_EMsgGCTrading_StartSession); // 1503;
        HANDLE_MSG(k_EMsgGCTrading_SetItem); // 1504;
        HANDLE_MSG(k_EMsgGCTrading_RemoveItem); // 1505;
        HANDLE_MSG(k_EMsgGCTrading_UpdateTradeInfo); // 1506;
        HANDLE_MSG(k_EMsgGCTrading_SetReadiness); // 1507;
        HANDLE_MSG(k_EMsgGCTrading_ReadinessResponse); // 1508;
        HANDLE_MSG(k_EMsgGCTrading_SessionClosed); // 1509;
        HANDLE_MSG(k_EMsgGCTrading_CancelSession); // 1510;
        HANDLE_MSG(k_EMsgGCTrading_TradeChatMsg); // 1511;
        HANDLE_MSG(k_EMsgGCTrading_ConfirmOffer); // 1512;
        HANDLE_MSG(k_EMsgGCTrading_TradeTypingChatMsg); // 1513;
        HANDLE_MSG(k_EMsgGCServerBrowser_FavoriteServer); // 1601;
        HANDLE_MSG(k_EMsgGCServerBrowser_BlacklistServer); // 1602;
        HANDLE_MSG(k_EMsgGCServerRentalsBase); // 1700;
        HANDLE_MSG(k_EMsgGCItemPreviewCheckStatus); // 1701;
        HANDLE_MSG(k_EMsgGCItemPreviewStatusResponse); // 1702;
        HANDLE_MSG(k_EMsgGCItemPreviewRequest); // 1703;
        HANDLE_MSG(k_EMsgGCItemPreviewRequestResponse); // 1704;
        HANDLE_MSG(k_EMsgGCItemPreviewExpire); // 1705;
        HANDLE_MSG(k_EMsgGCItemPreviewExpireNotification); // 1706;
        HANDLE_MSG(k_EMsgGCItemPreviewItemBoughtNotification); // 1707;
        HANDLE_MSG(k_EMsgGCDev_NewItemRequest); // 2001;
        HANDLE_MSG(k_EMsgGCDev_NewItemRequestResponse); // 2002;
        HANDLE_MSG(k_EMsgGCDev_PaintKitDropItem); // 2003;
        HANDLE_MSG(k_EMsgGCStoreGetUserData); // 2500;
        HANDLE_MSG(k_EMsgGCStoreGetUserDataResponse); // 2501;
        HANDLE_MSG(k_EMsgGCStorePurchaseInit_DEPRECATED); // 2502;
        HANDLE_MSG(k_EMsgGCStorePurchaseInitResponse_DEPRECATED); // 2503;
        HANDLE_MSG(k_EMsgGCStorePurchaseFinalize); // 2504;
        HANDLE_MSG(k_EMsgGCStorePurchaseFinalizeResponse); // 2505;
        HANDLE_MSG(k_EMsgGCStorePurchaseCancel); // 2506;
        HANDLE_MSG(k_EMsgGCStorePurchaseCancelResponse); // 2507;
        HANDLE_MSG(k_EMsgGCStorePurchaseQueryTxn); // 2508;
        HANDLE_MSG(k_EMsgGCStorePurchaseQueryTxnResponse); // 2509;
        HANDLE_MSG(k_EMsgGCStorePurchaseInit); // 2510;
        HANDLE_MSG(k_EMsgGCStorePurchaseInitResponse); // 2511;
        HANDLE_MSG(k_EMsgGCBannedWordListRequest); // 2512;
        HANDLE_MSG(k_EMsgGCBannedWordListResponse); // 2513;
        HANDLE_MSG(k_EMsgGCToGCBannedWordListBroadcast); // 2514;
        HANDLE_MSG(k_EMsgGCToGCBannedWordListUpdated); // 2515;
        HANDLE_MSG(k_EMsgGCToGCDirtySDOCache); // 2516;
        HANDLE_MSG(k_EMsgGCToGCDirtyMultipleSDOCache); // 2517;
        HANDLE_MSG(k_EMsgGCToGCUpdateSQLKeyValue); // 2518;
        HANDLE_MSG(k_EMsgGCToGCIsTrustedServer); // 2519;
        HANDLE_MSG(k_EMsgGCToGCIsTrustedServerResponse); // 2520;
        HANDLE_MSG(k_EMsgGCToGCBroadcastConsoleCommand); // 2521;
        HANDLE_MSG(k_EMsgGCServerVersionUpdated); // 2522;
        HANDLE_MSG(k_EMsgGCToGCWebAPIAccountChanged); // 2524;
        HANDLE_MSG(k_EMsgGCRequestAnnouncements); // 2525;
        HANDLE_MSG(k_EMsgGCRequestAnnouncementsResponse); // 2526;
        HANDLE_MSG(k_EMsgGCRequestPassportItemGrant); // 2527;
        HANDLE_MSG(k_EMsgGCClientVersionUpdated); // 2528;
        HANDLE_MSG(k_EMsgGCClientWelcome); // 4004;
        HANDLE_MSG(k_EMsgGCServerWelcome); // 4005;
        HANDLE_MSG(k_EMsgGCClientHello); // 4006;
        HANDLE_MSG(k_EMsgGCServerHello); // 4007;
        HANDLE_MSG(k_EMsgGCClientConnectionStatus); // 4009;
        HANDLE_MSG(k_EMsgGCServerConnectionStatus); // 4010;
        HANDLE_MSG(k_EMsgGCClientHelloPartner); // 4011;
        HANDLE_MSG(k_EMsgGCClientHelloPW); // 4012;
        HANDLE_MSG(k_EMsgGCClientHelloR2); // 4013;
        HANDLE_MSG(k_EMsgGCClientHelloR3); // 4014;
        HANDLE_MSG(k_EMsgGCClientHelloR4); // 4015;
        HANDLE_MSG(k_EMsgUpdateSessionIP); // 154;
        HANDLE_MSG(k_EMsgRequestSessionIP); // 155;
        HANDLE_MSG(k_EMsgRequestSessionIPResponse); // 156;

        HANDLE_MSG(k_ESOMsg_Create);
        HANDLE_MSG(k_ESOMsg_Update);
        HANDLE_MSG(k_ESOMsg_Destroy);
        HANDLE_MSG(k_ESOMsg_CacheSubscribed);
        HANDLE_MSG(k_ESOMsg_CacheUnsubscribed);
        HANDLE_MSG(k_ESOMsg_UpdateMultiple);
        HANDLE_MSG(k_ESOMsg_CacheSubscriptionCheck);
        HANDLE_MSG(k_ESOMsg_CacheSubscriptionRefresh);
    }

    //assert(false);
    
    static char unknown_message[64];
    sprintf(unknown_message, "UNKNOWN MESSAGE %lu", type);
    return unknown_message;
}
