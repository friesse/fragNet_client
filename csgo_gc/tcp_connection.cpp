// tcp_connection.cpp
#include "steam_network_message.hpp"
#include "tcp_connection.h"
#include "platform.h"
#include <cstring>
#include "networking_shared.h"

#ifdef _WIN32
namespace {
    struct WinsockInit {
        WinsockInit() {
            WSADATA wsaData;
            WSAStartup(MAKEWORD(2, 2), &wsaData);
        }
        ~WinsockInit() {
            WSACleanup();
        }
    } g_winsockInit;
}
#endif

TCPConnection::TCPConnection(uint64_t steamId, const char* address, int port)
    : m_steamId(steamId)
    , m_address(address)
    , m_port(port)
    , m_socket(0)
    , m_connected(false)
    , m_SocketStatusCallback()
    , m_lastheartbeat(0)
    , m_sentheartbeat(false)
    , m_clientGC(nullptr)
    , m_authverified(false)
{
}

TCPConnection::~TCPConnection()
{
    CCDisconnect();
}

bool TCPConnection::CCConnect()
{
    if (m_connected) {
        Platform::Print("Already connected to %s:%d\n", m_address.c_str(), m_port);
        return true;
    }

    struct hostent* host = gethostbyname(m_address.c_str());
    if (!host) {
        Platform::Print("Failed to resolve hostname %s\n", m_address.c_str());
        return false;
    }

    char* ip_address = inet_ntoa(*(struct in_addr*)host->h_addr_list[0]);
    uint32_t uip = htonl(inet_addr(ip_address));
    Platform::Print("Connecting to (%lu) %s:%d...\n", uip, m_address.c_str(), m_port);

    m_authverified = false;
    m_sentheartbeat = false;
    time(&m_lastheartbeat); // Extreme disconnect and connect loop happens if we don't set the heartbeat here...
    SteamNetworking()->CreateConnectionSocket(uip, CCServer::DEFAULT_PORT, 5);
    m_SocketStatusCallback.Register(this, &TCPConnection::SocketStatusCallback);

    return true;
}

void TCPConnection::Update()
{
    if (!m_socket) {
        return;
    }

    int socketStatus = k_ESNetSocketStateInvalid;
    SteamNetworking()->GetSocketInfo(m_socket, NULL, &socketStatus, NULL, NULL);

    if (socketStatus != k_ESNetSocketStateConnected) {
        Platform::Print("socketStatus: %i\n", socketStatus);
        return;
    }

    uint32 msgSize;
    while (SteamNetworking()->IsDataAvailableOnSocket(m_socket, &msgSize)) {
        Platform::Print("We have received data! Message Size: %i\n", msgSize);

        std::vector<uint8_t> buffer(msgSize);
        if (!SteamNetworking()->RetrieveDataFromSocket(m_socket, buffer.data(), msgSize, &msgSize)) {
            Platform::Print("Failed to retrieve data from socket\n");
            continue;
        }

        // is it CCProto?
        uint32_t messageType;
        memcpy(&messageType, buffer.data(), sizeof(uint32_t));
        bool isCCProto = (messageType & CCProtoMask) != 0;

        // check chunk count
        uint32_t chunkCount = 0;
        if (msgSize >= sizeof(uint32_t) * 3 && isCCProto) {
            memcpy(&chunkCount, buffer.data() + sizeof(uint32_t) * 2, sizeof(uint32_t));
            //Platform::Print("Message type: %u, Is CCProto: %d, Chunk count: %u\n", messageType & ~CCProtoMask, isCCProto, chunkCount);
        }

        if (chunkCount > 1) {
            //Platform::Print("Handling multi-chunk message, chunk count: %u\n", chunkCount);
            HandleChunkMsg(buffer, msgSize);
        }
        else {
            //Platform::Print("Processing single chunk message\n");
            ProcessChunkMsg(buffer, msgSize);
        }

    }

    //Heartbeat
    time_t stime = time(NULL);
    if (m_sentheartbeat && m_lastheartbeat != 0 && m_lastheartbeat + 15 <= stime) {
        CCDisconnect();

        // We lost connection to the GC!
        if (m_reconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
            m_reconnectAttempts++;
            CCConnect();
        }
        else {
            Platform::Print("Max reconnection attempts reached. Please check your connection or credentials and restart your game.\n");
        }
    } 
    else if (!m_sentheartbeat && m_lastheartbeat + 5 <= stime) {
        m_sentheartbeat = true;

        // sending heartbeat using GCMessageWrite :)
        CMsgGC_CC_GCHeartbeat heartbeat;
        GCMessageWrite message(k_EMsgGC_CC_GCHeartbeat, heartbeat, true);
        NetSendMessage(message);
    }
}

void TCPConnection::ProcessMessage(GCMessageRead& message)
{
    switch (message.TypeCCUnmasked())
    {
    case k_EMsgGC_CC_GCConfirmAuth:
    {
        CMsgGC_CC_GCConfirmAuth confirmMsg;
        if (!message.ReadProtobuf(confirmMsg)) {
            Platform::Print("Failed to parse GCConfirmAuth message\n");
            return;
        }

        if (confirmMsg.auth_result() == k_EBeginAuthSessionResultInvalidTicket) {
            Platform::Print("Authentication rejected - you are probably not whitelisted!\n");
            CCDisconnect();
            return;
        }

        m_authverified = true;
        SteamUser()->CancelAuthTicket(m_tempauthhandle);
        m_tempauthhandle = k_HAuthTicketInvalid;

        CMsgGC_CC_CL2GC_BuildMatchmakingHelloRequest buildMatchmakingHelloRequest;
        buildMatchmakingHelloRequest.set_steam_id(m_steamId);
        GCMessageWrite helloMsg(k_EMsgGC_CC_CL2GC_BuildMatchmakingHelloRequest, buildMatchmakingHelloRequest, true);
        NetSendMessage(helloMsg);
        Platform::Print("Sent BuildMatchmakingHelloRequest\n");
        break;
    }

    case k_EMsgGC_CC_GC2CL_BuildMatchmakingHello:
    {
        CMsgGC_CC_GC2CL_BuildMatchmakingHello matchmakingMsg;
        if (!message.ReadProtobuf(matchmakingMsg)) {
            Platform::Print("Failed to parse BuildMatchmakingHello message\n");
            return;
        }

        if (m_clientGC) {
            m_clientGC->HandleMatchmakingHello(matchmakingMsg);
        }
        break;
    }

    case k_EMsgGC_CC_GC2CL_SOCacheSubscribed:
    {
        CMsgSOCacheSubscribed cacheMsg;
        if (!message.ReadProtobuf(cacheMsg)) {
            Platform::Print("Failed to parse SOCacheSubscribed message\n");
            return;
        }

        if (m_clientGC) {
            m_clientGC->HandleCacheSubscription(cacheMsg);
        }
        Platform::Print("Received k_EMsgGC_CC_GC2CL_SOCacheSubscribed message\n");
        break;
    }




    case k_EMsgGC_CC_GC2CL_SOSingleObject:
    {
        Platform::Print("Received k_EMsgGC_CC_GC2CL_SOSingleObject\n");
        CMsgSOSingleObject soSingleObject;
        if (!message.ReadProtobuf(soSingleObject)) {
            Platform::Print("Failed to parse SOSingleObject message\n");
            return;
        }

        if (m_clientGC) {
            m_clientGC->HandleSOSingleObject(soSingleObject);
        }

        break;
    }

    case k_EMsgGC_CC_GC2CL_SOMultipleObjects:
    {
        Platform::Print("Received k_EMsgGC_CC_GC2CL_SOMultipleObjects\n");
        CMsgSOMultipleObjects soMultipleObjects;
        if (!message.ReadProtobuf(soMultipleObjects)) {
            Platform::Print("Failed to parse SOMultipleObjects message\n");
            return;
        }

        if (m_clientGC) {
            m_clientGC->HandleSOMultipleObjects(soMultipleObjects);
        }

        break;
    }

    // INVENTORY ACTIONS

    case k_EMsgGC_CC_DeleteItem:
    {
        Platform::Print("Received k_EMsgGC_CC_DeleteItem\n");
        CMsgSOSingleObject deletedItem;
        if (!message.ReadProtobuf(deletedItem)) {
            Platform::Print("Failed to parse SOSingleObject message (DeleteItem)\n");
            return;
        }

        if (m_clientGC) {
            m_clientGC->HandleDeleteItem(deletedItem);
        }

        break;
    }




    // OTHERS

    case k_EMsgGC_CC_GC2CL_UnlockCrateResponse:
    {
        CMsgSOSingleObject unlockCrateResponse;
        if (!message.ReadProtobuf(unlockCrateResponse)) {
            Platform::Print("Failed to parse UnlockCrateResponse message\n");
            return;
        }

        if (m_clientGC) {
            m_clientGC->HandleUnlockCrateResponse(unlockCrateResponse);
        }
        break;
    }

    case k_EMsgGC_CC_GC2CL_ViewPlayersProfileResponse:
    {
        CMsgGC_CC_GC2CL_ViewPlayersProfileResponse profileMsg;
        if (!message.ReadProtobuf(profileMsg)) {
            Platform::Print("Failed to parse ViewPlayersProfileResponse message\n");
            return;
        }

        if (m_clientGC) {
            m_clientGC->HandleViewPlayersProfileResponse(profileMsg);
        }
        break;
    }

    case k_EMsgGC_CC_GC2CL_ClientCommendPlayerQueryResponse:
    {
        CMsgGC_CC_ClientCommendPlayer commendMsg;
        if (!message.ReadProtobuf(commendMsg)) {
            Platform::Print("Failed to parse ClientCommendPlayerQueryResponse message\n");
            return;
        }

        if (m_clientGC) {
            m_clientGC->HandleClientCommendPlayerQueryResponse(commendMsg);
        }
        break;
    }

    case k_EMsgGC_CC_GC2CL_ClientReportResponse:
    {
        CMsgGC_CC_GC2CL_ClientReportResponse reportMsg;
        if (!message.ReadProtobuf(reportMsg)) {
            Platform::Print("Failed to parse ClientReportResponse message\n");
            return;
        }

        if (m_clientGC) {
            m_clientGC->HandleClientReportResponse(reportMsg);
        }
        break;
    }

    case k_EMsgGC_CC_GC2CL_StorePurchaseInitResponse:
    {
        CMsgGC_CC_GC2CL_StorePurchaseInitResponse storePurchaseInitResponse;
        if (!message.ReadProtobuf(storePurchaseInitResponse)) {
            Platform::Print("Failed to parse ClientReportResponse message\n");
            return;
        }

        if (m_clientGC) {
            m_clientGC->HandleStorePurchaseInitResponse(storePurchaseInitResponse);
        }
        break;
    }

    case k_EMsgGC_CC_GCHeartbeat:
    {
        CMsgGC_CC_GCHeartbeat heartbeatMsg;
        if (!message.ReadProtobuf(heartbeatMsg)) {
            Platform::Print("Failed to parse GCHeartbeat message\n");
            return;
        }
        time(&m_lastheartbeat);
        m_sentheartbeat = false;
        break;
    }

    default:
        Platform::Print("Received unknown message type: %u\n", message.TypeUnmasked());
        break;
    }
}

void TCPConnection::HandleChunkMsg(const std::vector<uint8_t>& buffer, uint32_t msgSize)
{
    const size_t typeSize = sizeof(uint32_t);
    const size_t headerSizeSize = sizeof(uint32_t);
    const size_t chunkCountSize = sizeof(uint32_t);
    const size_t fullHeaderSize = typeSize + headerSizeSize + chunkCountSize;

    uint32_t messageType;
    memcpy(&messageType, buffer.data(), typeSize);
    uint32_t unmaskedType = messageType & ~CCProtoMask;

    uint32_t chunkCount;
    memcpy(&chunkCount, buffer.data() + typeSize + headerSizeSize, chunkCountSize);

    // Get or create chunked message entry
    auto& chunkedMsg = m_pendingMessages[unmaskedType];
    if (chunkedMsg.chunks.empty()) 
    {
        Platform::Print("Starting new chunked message for type %u with %u chunks\n", unmaskedType, chunkCount);
        chunkedMsg.chunks.resize(chunkCount);
        chunkedMsg.expectedChunks = chunkCount;
        chunkedMsg.receivedChunks = 0;
        chunkedMsg.messageType = messageType;
    }

    size_t chunkIndex = chunkedMsg.receivedChunks;
    Platform::Print("Receiving chunk %zu of %u for type %u\n", chunkIndex + 1, chunkCount, unmaskedType);

    // for first chunk:
    if (chunkIndex == 0) 
    {
        // copy header from first chunk
        chunkedMsg.chunks[chunkIndex].assign(buffer.begin(), buffer.begin() + fullHeaderSize);

        // add payload
        chunkedMsg.chunks[chunkIndex].insert
        (
            chunkedMsg.chunks[chunkIndex].end(),
            buffer.begin() + fullHeaderSize,
            buffer.end()
        );
    }
    else // for subsequent chunks:
    {
        // add payload only, we dont want headers in our protobufs
        chunkedMsg.chunks[chunkIndex].assign
        (
            buffer.begin() + fullHeaderSize,
            buffer.end()
        );
    }

    chunkedMsg.receivedChunks++;

    // we have all chunks
    if (chunkedMsg.receivedChunks == chunkedMsg.expectedChunks) 
    {
        Platform::Print("All chunks received (%u), assembling message\n", chunkCount);

        // FINALLY put together this clusterfuck
        
        // add header
        std::vector<uint8_t> completeBuffer = chunkedMsg.chunks[0];

        // add full payload
        for (size_t i = 1; i < chunkedMsg.chunks.size(); i++)
        {
            completeBuffer.insert
            (
                completeBuffer.end(),
                chunkedMsg.chunks[i].begin(),
                chunkedMsg.chunks[i].end()
            );
        }

        Platform::Print("Assembled message size: %zu\n", completeBuffer.size());
        ProcessChunkMsg(completeBuffer, completeBuffer.size());
        m_pendingMessages.erase(unmaskedType);
    }
}

void TCPConnection::ProcessChunkMsg(const std::vector<uint8_t>& buffer, uint32_t msgSize)
{
    uint32_t messageType;
    memcpy(&messageType, buffer.data(), sizeof(uint32_t));

    if (messageType & CCProtoMask)
    {
        const size_t typeSize = sizeof(uint32_t);
        const size_t headerSizeSize = sizeof(uint32_t);
        const size_t chunkCountSize = sizeof(uint32_t);
        const size_t fullHeaderSize = typeSize + headerSizeSize + chunkCountSize;

        std::vector<uint8_t> strippedBuffer;

        // copy type + header for proper gcmessageread format
        strippedBuffer.assign
        (
            buffer.begin(),
            buffer.begin() +
            typeSize +
            headerSizeSize
        );

        // copy payload
        strippedBuffer.insert
        (
            strippedBuffer.end(),
            buffer.begin() +
            fullHeaderSize,
            buffer.end()
        );

        GCMessageRead message(0, strippedBuffer.data(), strippedBuffer.size());

        if (!message.IsValid()) {
            Platform::Print("Received invalid message\n");
            return;
        }

        ProcessMessage(message);
    }
    else
    {
        Platform::Print("Received invalid message\n");
        return;
    }
}

void TCPConnection::NetSendMessage(const GCMessageWrite& message)
{
    if (!m_connected)
    {
        // not connected to a server
        return;
    }

    const uint8_t* data = static_cast<const uint8_t*>(message.Data());
    Platform::Print("Sending message - Size: %u\n", message.Size());
    Platform::Print("First 8 bytes: %02x %02x %02x %02x %02x %02x %02x %02x\n",
        data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);

    bool result = SteamNetworking()->SendDataOnSocket(
        m_socket,
        const_cast<void*>(message.Data()),
        message.Size(),
        k_EP2PSendReliable);

    if (!result) {
        // what the
        Platform::Print("Failed to send message!\n");
    }
}

void TCPConnection::CCDisconnect()
{
    /*if (m_socket != INVALID_SOCK) {
#ifdef _WIN32
        closesocket(m_socket);
#else
        close(m_socket);
#endif
        m_socket = INVALID_SOCK;
    }
    m_connected = false;*/

    m_SocketStatusCallback.Unregister();

    SteamNetworking()->DestroySocket(m_socket, true);
    m_socket = 0; // Just to be sure, in case something else sets it.
    m_connected = false;

    if (m_tempauthhandle != k_HAuthTicketInvalid) {
        SteamUser()->CancelAuthTicket(m_tempauthhandle);
        m_tempauthhandle = k_HAuthTicketInvalid;
    }

    m_pendingMessages.clear();
}

void TCPConnection::SocketStatusCallback(SocketStatusCallback_t* pParam) 
{
    if (pParam->m_eSNetSocketState != k_ESNetSocketStateConnected)
    {
        Platform::Print("Networking: socket status %i\n", pParam->m_eSNetSocketState);

        // This means we timeout'd...
        if (pParam->m_eSNetSocketState == k_ESNetSocketStateInvalid || pParam->m_eSNetSocketState >= k_ESNetSocketStateTimeoutDuringConnect)
        {
            CCDisconnect();
            CCConnect(); // Reconnect...
        }
        return;
    }

    m_socket = pParam->m_hSocket;
    m_connected = true; // yep
    Platform::Print("Networking: we have connected to the GC!\n");

    char auth_ticket[512];
    uint32 auth_ticket_size;
    m_tempauthhandle = SteamUser()->GetAuthSessionTicket(auth_ticket, sizeof(auth_ticket), &auth_ticket_size);

    CMsgGC_CC_GCWelcome gcwelcome;
    gcwelcome.set_steam_id(m_steamId);
    gcwelcome.set_auth_ticket(std::string(auth_ticket, auth_ticket_size));
    gcwelcome.set_auth_ticket_size(auth_ticket_size);

    // write dat
    GCMessageWrite message{ k_EMsgGC_CC_GCWelcome, gcwelcome, true };

    Platform::Print("Message details - Type: %u (masked with regular protobuf: %u), Size: %u\n",
        k_EMsgGC_CC_GCWelcome,
        k_EMsgGC_CC_GCWelcome | CCProtoMask,
        message.Size());

    NetSendMessage(message);
}