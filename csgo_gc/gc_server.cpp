#include "stdafx.h"
#include "gc_server.h"
#include "gc_const.h"
#include "gc_const_csgo.h"
#include "graffiti.h"

const char *MessageName(uint32_t type);

ServerGC::ServerGC()
    : m_networking{ this }
{
    // Initialize SteamGameServer
    if (!SteamGameServer_Init(
        0,                              // IP address (0 for localhost)
        0,                              // Steam port 
        27015,                          // Game port (use default CS:GO port)
        0,                              // Query port
        eServerModeNoAuthentication,    // Server mode for bots
        "1.35.3.6"))                    // Version
    {
        throw std::runtime_error("Failed to initialize SteamGameServer!");
    }

    // Log on anonymously right away
    SteamGameServer()->LogOnAnonymous();

    uint64_t steamId = SteamGameServer()->GetSteamID().ConvertToUint64();
    Platform::Print("ServerGC spawned of id %llu\n", steamId);
    //Graffiti::Initialize();
}

ServerGC::~ServerGC()
{
    Platform::Print("ServerGC destroyed\n");

    // baibai
    SteamGameServer_Shutdown();
}

void ServerGC::HandleMessage(uint32_t type, const void *data, uint32_t size)
{
    GCMessageRead messageRead{ type, data, size };
    if (!messageRead.IsValid())
    {
        assert(false);
        return;
    }

    if (messageRead.IsProtobuf())
    {
        switch (messageRead.TypeUnmasked())
        {
        case k_EMsgGCServerHello:
            OnServerHello(messageRead);
            break;

        case k_EMsgGCCStrike15_v2_Server2GCClientValidate:
            // server doesn't want a response so ignore
            break;

        case k_EMsgGC_IncrementKillCountAttribute:
            IncrementKillCountAttribute(messageRead);
            break;

        default:
            Platform::Print("ServerGC::HandleMessage: unhandled protobuf message %s)\n",
                MessageName(messageRead.TypeUnmasked()));
            break;
        }
    }
}

void ServerGC::ClientConnected(uint64_t steamId, const void *ticket, uint32_t ticketSize)
{
    Platform::Print("ClientConnected: %llu\n", steamId);
    m_networking.ClientConnected(steamId, ticket, ticketSize);
}

void ServerGC::ClientDisconnected(uint64_t steamId)
{
    Platform::Print("ClientDisconnected: %llu\n", steamId);
    m_networking.ClientDisconnected(steamId);

    CMsgSOCacheUnsubscribed message;
    message.mutable_owner_soid()->set_type(SoIdTypeSteamId);
    message.mutable_owner_soid()->set_id(steamId);

    m_outgoingMessages.emplace(k_ESOMsg_CacheUnsubscribed, message);
}

void ServerGC::Update()
{
    // Check Steam connection state in Update
    if (!m_receivedHello) 
    {
        return;
    }

    // process deferred connections
    /*if (SteamGameServer() && SteamGameServer()->BLoggedOn()) {
        // todo (?) process queued up connections here?
    }*/

    m_networking.Update();
}

template<typename T>
static bool ValidateMessageOwnerSOID(GCMessageRead& messageRead, uint64_t steamId)
{
    T message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("ValidateMessageOwnerSOID %llu: parsing failed\n", steamId);
        return false;
    }

    if (message.owner_soid().type() != SoIdTypeSteamId
        || message.owner_soid().id() != steamId)
    {
        Platform::Print("ValidateMessageOwnerSOID %llu: steam id mismatch (message has %llu)\n",
            steamId, message.owner_soid().id());
        return false;
    }

    return true;
}

void ServerGC::HandleNetMessage(uint64_t steamId, const void* data, uint32_t size)
{
    Platform::Print("ServerGC handling message from %llu of size %u\n", steamId, size);
    GCMessageRead validate{ 0, data, size };
    if (!validate.IsValid())
    {
        assert(false);
        return;
    }

    uint32_t messageType = validate.TypeUnmasked();

    // idk maybe revert this
    if (!validate.IsProtobuf() && messageType == k_EMsgNetworkConnect)
    {
        uint32_t ticketSize = validate.ReadUint32();
        const void* ticket = validate.ReadData(ticketSize);
        if (validate.IsValid()) {
            m_networking.ClientConnected(steamId, ticket, ticketSize);
        }
        return;
    }

    // Handle protobuf messages
    if (!validate.IsProtobuf())
    {
        // all the allowed messages are protobuf based
        Platform::Print("ServerGC::HandleNetMessage: ignoring non protobuf message %u from %llu\n",
            messageType, steamId);
        return;
    }

    // validate the type and contents
    bool isValid = false;
    switch (messageType)
    {
    case k_ESOMsg_Create:
    case k_ESOMsg_Update:
    case k_ESOMsg_Destroy:
        isValid = ValidateMessageOwnerSOID<CMsgSOSingleObject>(validate, steamId);
        break;

    case k_ESOMsg_CacheSubscribed:
        isValid = ValidateMessageOwnerSOID<CMsgSOCacheSubscribed>(validate, steamId);
        break;

    case k_ESOMsg_UpdateMultiple:
        isValid = ValidateMessageOwnerSOID<CMsgSOMultipleObjects>(validate, steamId);
        break;

    case k_EMsgGCItemAcknowledged:
        isValid = true;
        break;
    default:
        Platform::Print("Unknown protobuf message type: %u\n", messageType);
        break;
    }

    if (!isValid)
    {
        Platform::Print("ServerGC::HandleNetMessage: ignoring net message %u from %llu\n",
            messageType, steamId);
        return;
    }

    m_outgoingMessages.emplace(data, size);
}

void ServerGC::OnServerHello(GCMessageRead &messageRead)
{
    CMsgServerHello hello;
    if (!messageRead.ReadProtobuf(hello))
    {
        Platform::Print("Parsing CMsgServerHello failed, ignoring\n");
        return;
    }

    // we don't care about anything in this message, just reply

    CMsgCStrike15Welcome csWelcome;
    csWelcome.set_gscookieid(GameServerCookieId);

    CMsgClientWelcome welcome;
    welcome.set_version(0);
    welcome.set_game_data(csWelcome.SerializeAsString());
    welcome.set_rtime32_gc_welcome_timestamp(static_cast<uint32_t>(time(nullptr)));

    m_outgoingMessages.emplace(k_EMsgGCServerWelcome, welcome);

    m_receivedHello = true;
}

void ServerGC::IncrementKillCountAttribute(GCMessageRead &messageRead)
{
    CMsgIncrementKillCountAttribute message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgIncrementKillCountAttribute failed, ignoring\n");
        return;
    }

    // just forward it to the killer
    GCMessageWrite messageWrite{ k_EMsgGC_IncrementKillCountAttribute , message };
    CSteamID killerId{ message.killer_account_id(), k_EUniversePublic, k_EAccountTypeIndividual };
    m_networking.SendMessage(killerId.ConvertToUint64(), messageWrite);
}
