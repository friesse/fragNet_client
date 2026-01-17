// tcp_connection.h
#pragma once
#include <string>
#include <steam/steam_api.h>
#include "gc_message.h"
#include "gc_client.h"
#include "cc_gcmessages.pb.h"

#ifdef _WIN32
#include <WinSock2.h>
#define INVALID_SOCK INVALID_SOCKET
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#define INVALID_SOCK -1
#endif

namespace CCServer {
    constexpr const char* DEFAULT_IP = "gc.ollum.cc";
    constexpr int DEFAULT_PORT = 21818;
}

class TCPConnection {
public:
    TCPConnection(uint64_t steamId, const char* address = CCServer::DEFAULT_IP, int port = CCServer::DEFAULT_PORT);
    ~TCPConnection();

    void Update();
    bool CCConnect();
    void NetSendMessage(const GCMessageWrite& message);
    bool CCIsConnected() const { return m_connected; }
    void CCDisconnect();
    void SetClientGC(ClientGC* clientGC) { m_clientGC = clientGC; }

private:
    uint64_t m_steamId;
    std::string m_address;
    int m_port;
    SNetSocket_t m_socket = 0;
    bool m_connected;
    time_t m_lastheartbeat;
    bool m_sentheartbeat;
    ClientGC* m_clientGC;
    bool m_authverified = false;
    HAuthTicket m_tempauthhandle;

    CCallbackManual<TCPConnection, SocketStatusCallback_t> m_SocketStatusCallback;
    void SocketStatusCallback(SocketStatusCallback_t* pParam);

    // Handle Chunks:
    struct ChunkedMessage {
        std::vector<std::vector<uint8_t>> chunks;
        uint32_t expectedChunks;
        uint32_t receivedChunks;
        uint32_t messageType;
    };
    std::map<uint32_t, ChunkedMessage> m_pendingMessages;

    // Reconnects
    int m_reconnectAttempts = 0;
    static const int MAX_RECONNECT_ATTEMPTS = 3;

    void HandleChunkMsg(const std::vector<uint8_t>& buffer, uint32_t msgSize);
    void ProcessChunkMsg(const std::vector<uint8_t>& buffer, uint32_t msgSize);
    void ProcessMessage(GCMessageRead& message);
};