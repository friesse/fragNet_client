#pragma once
#include "networking_shared.h"
#include "steam/steam_gameserver.h"

class ServerGC;
class GCMessageWrite;

class NetworkingServer
{
public:
    NetworkingServer(ServerGC* serverGC);
    bool IsClientConnected(uint64_t steamId) const;
    bool IsSteamConnected() const;
    void Update();
    void ClientConnected(uint64_t steamId, const void* ticket, uint32_t ticketSize);
    void ClientDisconnected(uint64_t steamId);
    void SendMessage(uint64_t steamId, const GCMessageWrite& message);
private:
    void OnP2PSessionRequest(P2PSessionRequest_t* param);
    void OnP2PSessionConnectFail(P2PSessionConnectFail_t* param);

    CCallback<NetworkingServer, P2PSessionRequest_t, true> m_callbackP2PSessionRequest;
    CCallback<NetworkingServer, P2PSessionConnectFail_t, true> m_callbackP2PSessionFail;

    ServerGC* const m_serverGC;
    uint64_t m_serverSteamId{};
    std::unordered_set<uint64_t> m_clients;
};