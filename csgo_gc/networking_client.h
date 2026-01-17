#pragma once
#include "networking_shared.h"
#include "steam/isteamclient.h"
#include "steam/isteamnetworking.h"
#include "steam/steam_api.h"

class ClientGC;
class GCMessageRead;
class GCMessageWrite;
struct AuthTicket
{
    uint64_t steamId; // gameserver
    std::vector<uint8_t> buffer;
};

class NetworkingClient
{
public:
    NetworkingClient(ClientGC* clientGC);
    void Update();
    void NetSendMessage(const GCMessageWrite& message);
    void SetAuthTicket(uint32_t handle, const void* data, uint32_t size);
    void ClearAuthTicket(uint32_t handle);
private:
    bool HandleMessage(uint64_t steamId, GCMessageRead& message);

    void OnP2PSessionRequest(P2PSessionRequest_t* param);
    void OnP2PSessionConnectFail(P2PSessionConnectFail_t* param);

    CCallback<NetworkingClient, P2PSessionRequest_t, false> m_callbackP2PSessionRequest;
    CCallback<NetworkingClient, P2PSessionConnectFail_t, false> m_callbackP2PSessionFail;

    ClientGC* const m_clientGC;
    uint64_t m_serverSteamId{};
    std::unordered_map<uint32_t, AuthTicket> m_tickets;
};