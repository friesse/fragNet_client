#define VALVE_CALLBACK_PACK_SMALL

#include "stdafx.h"
#include "networking_client.h"
#include "gc_client.h"
#include "steam/isteamclient.h"

NetworkingClient::NetworkingClient(ClientGC* clientGC)
    : m_clientGC(clientGC)
    , m_callbackP2PSessionRequest(this, &NetworkingClient::OnP2PSessionRequest)
    , m_callbackP2PSessionFail(this, &NetworkingClient::OnP2PSessionConnectFail)
{
}

void NetworkingClient::Update()
{
    uint32_t messageSize;
    bool hasPacket = SteamNetworking()->IsP2PPacketAvailable(&messageSize, NetMessageChannel);

    if (m_serverSteamId) {  // for client
        CSteamID remoteSteamId(m_serverSteamId);
        P2PSessionState_t state;
        if (SteamNetworking()->GetP2PSessionState(remoteSteamId, &state))
        {
            static int logCounter = 0;
            if (++logCounter >= 100) {  // antispam
                logCounter = 0;
                /*Platform::Print("P2P Session State: active=%d connecting=%d error=%d relay=%d\n",
                    state.m_bConnectionActive,
                    state.m_bConnecting,
                    state.m_eP2PSessionError,
                    state.m_bUsingRelay);*/
            }
        }
    }

    // Only process if we have a packet with valid size
    if (hasPacket && messageSize > 0)
    {
        std::vector<uint8_t> buffer(messageSize);
        CSteamID remoteSteamId;
        Platform::Print("Attempting to read P2P packet of size %u\n", messageSize);

        if (!SteamNetworking()->ReadP2PPacket(buffer.data(), messageSize, &messageSize, &remoteSteamId, NetMessageChannel)) {
            Platform::Print("Failed to read P2P packet\n");
            return;
        }

        uint64_t steamId = remoteSteamId.ConvertToUint64();
        Platform::Print("Read P2P packet from %llu, final size=%u\n", steamId, messageSize);

        if (messageSize >= 4) {
            uint32_t firstDword = *reinterpret_cast<uint32_t*>(buffer.data());
            Platform::Print("Message starts with: %08x\n", firstDword);
        }

        GCMessageRead messageRead{ 0, buffer.data(), messageSize };
        if (!messageRead.IsValid())
        {
            Platform::Print("Invalid message received\n");
            return;
        }

        if (HandleMessage(steamId, messageRead))
        {
            Platform::Print("Handled internal message\n");
            return;
        }

        if (!m_serverSteamId || steamId != m_serverSteamId)
        {
            Platform::Print("NetworkingClient: ignored message from %llu (not our gs %llu)\n", steamId, m_serverSteamId);
            return;
        }

        Platform::Print("Passing message to ClientGC\n");
        m_clientGC->HandleNetMessage(messageRead);
    }
}

static bool ValidateTicket(std::unordered_map<uint32_t, AuthTicket> &tickets, uint64_t steamId, const void *data, uint32_t size)
{
    Platform::Print("ValidateTicket: Validating ticket for %llu, size=%u, tickets.size=%zu\n",
        steamId, size, tickets.size());

    for (auto& pair : tickets)
    {
        Platform::Print("Checking ticket %u: stored_size=%zu\n",
            pair.first, pair.second.buffer.size());

        if (pair.second.buffer.size() == size && !memcmp(pair.second.buffer.data(), data, size))
        {
            Platform::Print("Found matching ticket!\n");
            pair.second.steamId = steamId;
            return true;
        }
    }

    Platform::Print("No matching ticket found\n");
    return false;
}

bool NetworkingClient::HandleMessage(uint64_t steamId, GCMessageRead &message)
{
    Platform::Print("Client handling message type %u from %llu\n", message.TypeUnmasked(), steamId);

    // Protobuf messages should go to server only
    if (message.IsProtobuf())
    {
        // internal messages are not protobuf based
        return false;
    }

    uint32_t typeUnmasked = message.TypeUnmasked();
    if (typeUnmasked == k_EMsgNetworkConnect)
    {
        Platform::Print("Received network connect message\n"); // Are we seeing this?
        uint64_t serverSteamId = message.ReadUint64();
        uint32_t ticketSize = message.ReadUint32();
        const void *ticket = message.ReadData(ticketSize);

        Platform::Print("Processing connect message: size=%u\n", ticketSize);

        if (!message.IsValid())
        {
            Platform::Print("NetworkingClient: ignored connection from %llu (malformed message)\n", steamId);
            return true;
        }

        if (!ValidateTicket(m_tickets, steamId, ticket, ticketSize))
        {
            Platform::Print("NetworkingClient: ignored connection from %llu (ticket mismatch)\n", steamId);
            return true;
        }

        Platform::Print("NetworkingClient: sending socache to %llu\n", serverSteamId);
        m_serverSteamId = serverSteamId;
        m_clientGC->SendSOCacheToGameServer();

        return true;
    }

    return false;
}


void NetworkingClient::NetSendMessage(const GCMessageWrite& message)
{
    if (!m_serverSteamId)
    {
        // not connected to a server
        return;
    }

    CSteamID steamId(m_serverSteamId);
    bool result = SteamNetworking()->SendP2PPacket(
        steamId,
        message.Data(),
        message.Size(),
        k_EP2PSendReliable,
        NetMessageChannel);

    assert(result);
}

void NetworkingClient::SetAuthTicket(uint32_t handle, const void *data, uint32_t size)
{
    Platform::Print("SetAuthTicket: handle=%u size=%u\n", handle, size);
    AuthTicket &ticket = m_tickets[handle];

    ticket.steamId = 0;
    ticket.buffer.resize(size);
    memcpy(ticket.buffer.data(), data, size);
}

void NetworkingClient::ClearAuthTicket(uint32_t handle)
{
    auto it = m_tickets.find(handle);
    if (it == m_tickets.end())
    {
        //assert(false);
        Platform::Print("NetworkingClient: tried to clear a nonexistent auth ticket???\n");
        return;
    }

    if (it->second.steamId)
    {
        Platform::Print("NetworkingClient: closing p2p session with %llu\n", it->second.steamId);

        // Close P2P session using old API
        CSteamID remoteSteamId(it->second.steamId);
        SteamNetworking()->CloseP2PSessionWithUser(remoteSteamId);

        if (it->second.steamId == m_serverSteamId)
        {
            Platform::Print("NetworkingClient: clearing gs identity\n");
            m_serverSteamId = 0;
        }
    }

    m_tickets.erase(it);
}

//for older sdk

void NetworkingClient::OnP2PSessionRequest(P2PSessionRequest_t* param)
{
    /*if (!param->m_steamIDRemote.BGameServerAccount())
    {
        // csgo_gc related connections come from gameservers
        return;
    }*/

    // accept the connection, we should receive the k_EMsgNetworkConnect message
    SteamNetworking()->AcceptP2PSessionWithUser(param->m_steamIDRemote);
}

void NetworkingClient::OnP2PSessionConnectFail(P2PSessionConnectFail_t* param)
{
    Platform::Print("NetworkingClient::OnP2PSessionConnectFail: error %d\n", param->m_eP2PSessionError);
}
