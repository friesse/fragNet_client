#include "stdafx.h"
#include "networking_server.h"
#include "gc_server.h"

NetworkingServer::NetworkingServer(ServerGC* serverGC)
    : m_serverGC(serverGC)
    , m_callbackP2PSessionRequest(this, &NetworkingServer::OnP2PSessionRequest)
    , m_callbackP2PSessionFail(this, &NetworkingServer::OnP2PSessionConnectFail)
{
    if (!SteamNetworking()) { // :(
        throw std::runtime_error("SteamNetworking interface not available!");
    }
}

bool NetworkingServer::IsClientConnected(uint64_t steamId) const {
    return m_clients.find(steamId) != m_clients.end();
}

bool NetworkingServer::IsSteamConnected() const
{
    if (!SteamGameServer()) {
        Platform::Print("SteamGameServer() returned null\n");
        return false;
    }

    if (!SteamGameServer()->BLoggedOn()) {
        Platform::Print("SteamGameServer()->BLoggedOn() returned false\n");
        return false;
    }

    return true;
}

void NetworkingServer::Update()
{
    if (!IsSteamConnected()) {
        return;  // Skip update if Steam isn't ready
    }

    uint32_t messageSize = 0;

    // only try to read if IsP2PPacketAvailable returns true AND gives us not zeros
    if (SteamGameServerNetworking()->IsP2PPacketAvailable(&messageSize, NetMessageChannel) && messageSize > 0)
    {
        std::vector<uint8_t> buffer(messageSize);
        CSteamID remoteSteamId;

        if (!SteamGameServerNetworking()->ReadP2PPacket(buffer.data(), messageSize, &messageSize, &remoteSteamId, NetMessageChannel)) {
            Platform::Print("Failed to read P2P packet of size %u\n", messageSize);
            return;
        }

        uint64_t steamId = remoteSteamId.ConvertToUint64();
        Platform::Print("Read packet from %llu\n", steamId);

        // Check connection state
        P2PSessionState_t state;
        if (SteamGameServerNetworking()->GetP2PSessionState(remoteSteamId, &state)) {
            Platform::Print("Connection state with %llu: active=%d connecting=%d error=%d relay=%d\n",
                steamId, state.m_bConnectionActive, state.m_bConnecting,
                state.m_eP2PSessionError, state.m_bUsingRelay);
        }

        auto it = m_clients.find(steamId);
        if (it == m_clients.end())
        {
            Platform::Print("NetworkingServer: ignored message from %llu (no session)\n", steamId);
            return;
        }

        m_serverGC->HandleNetMessage(steamId, buffer.data(), messageSize);
    }
}

void NetworkingServer::ClientConnected(uint64_t steamId, const void* ticket, uint32_t ticketSize)
{
    CSteamID remoteSteamId(steamId);

    if (!IsSteamConnected()) {
        Platform::Print("Steam not connected, deferring client connection for %llu\n", steamId);
        return;
    }

    // Accept the session first
    if (!SteamGameServerNetworking()->AcceptP2PSessionWithUser(remoteSteamId)) {
        Platform::Print("Failed to accept P2P session with %llu\n", steamId);
        return;
    }
    Platform::Print("Accepted P2P session with %llu\n", steamId);

    // Add to clients list before waiting
    auto result = m_clients.insert(steamId);
    if (!result.second)
    {
        Platform::Print("got ClientConnected for %llu but they're already on the list! ignoring\n", steamId);
        return;
    }

    uint64_t serverSteamId = SteamGameServer()->GetSteamID().ConvertToUint64();
    // send a message, if the client has csgo_gc installed they will
    // reply with their so cache and we'll add them to our list
    GCMessageWrite messageWrite{ k_EMsgNetworkConnect };
    messageWrite.WriteUint64(serverSteamId);
    messageWrite.WriteUint32(ticketSize);
    messageWrite.WriteData(ticket, ticketSize);

    Platform::Print("Sending initial connection message to %llu, size=%zu\n", steamId, messageWrite.Size());

    bool sendResult = SteamGameServerNetworking()->SendP2PPacket(
        remoteSteamId,
        messageWrite.Data(),
        messageWrite.Size(),
        k_EP2PSendReliable,
        NetMessageChannel);

    if (!sendResult) {
        Platform::Print("Failed to send initial connection packet!\n");
        m_clients.erase(steamId);
        return;
    }
    Platform::Print("Successfully sent initial connection packet\n");
}

void NetworkingServer::ClientDisconnected(uint64_t steamId)
{
    auto it = m_clients.find(steamId);
    if (it == m_clients.end())
    {
        Platform::Print("got ClientDisconnected for %llu but they're not on the list! ignoring\n", steamId);
        return;
    }

    m_clients.erase(it);
    CSteamID remoteSteamId(steamId);
    SteamGameServerNetworking()->CloseP2PSessionWithUser(remoteSteamId);
}

void NetworkingServer::SendMessage(uint64_t steamId, const GCMessageWrite& message)
{
    auto it = m_clients.find(steamId);
    if (it == m_clients.end())
    {
        Platform::Print("No csgo_gc session with %llu, not sending message!!!\n", steamId);
        return;
    }

    CSteamID remoteSteamId(steamId);

    // Check connection state before sending
    P2PSessionState_t state;
    if (!SteamGameServerNetworking()->GetP2PSessionState(remoteSteamId, &state)) {
        Platform::Print("Failed to get P2P session state for %llu\n", steamId);
        return;
    }

    if (!state.m_bConnectionActive) {
        Platform::Print("P2P connection not active for %llu\n", steamId);
        return;
    }

    Platform::Print("Sending message of size %zu to %llu\n", message.Size(), steamId);

    bool result = SteamGameServerNetworking()->SendP2PPacket(
        remoteSteamId,
        message.Data(),
        message.Size(),
        k_EP2PSendReliable,
        NetMessageChannel);

    if (!result) {
        Platform::Print("Failed to send P2P packet to %llu\n", steamId);
    }
}

void NetworkingServer::OnP2PSessionRequest(P2PSessionRequest_t* param)
{
    uint64_t steamId = param->m_steamIDRemote.ConvertToUint64();
    Platform::Print("Received P2P session request from %llu\n", steamId);

    // Always accept the session request initially
    if (!SteamGameServerNetworking()->AcceptP2PSessionWithUser(param->m_steamIDRemote)) {
        Platform::Print("Failed to accept P2P session from %llu\n", steamId);
        return;
    }

    Platform::Print("Accepted P2P session from %llu\n", steamId);
}

void NetworkingServer::OnP2PSessionConnectFail(P2PSessionConnectFail_t* param)
{
    // don't do anything, rely on the auth session
    Platform::Print("OnP2PSessionConnectFail: error %d\n", param->m_eP2PSessionError);
}