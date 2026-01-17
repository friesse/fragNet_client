#ifndef STEAM_NETWORK_MESSAGE_H
#define STEAM_NETWORK_MESSAGE_H

#include "steam/steam_api.h"

#define VIRTUAL_TYPE_SIZE(ctype, uitype) \
static constexpr uint16 type() { return uitype; } \
virtual uint16 get_type() override { return uitype; } \
virtual constexpr uint32 get_size() override { return sizeof(ctype)-sizeof(void*); }

// Messages go inbetween the pragma pack! Did it that way so the structs don't get padded!
#pragma pack(push, 1)
// ---------------------------------------------
// ------------- Start of messages -------------
// ---------------------------------------------

// Template game coordinator network message
class MsgGCTemplate {
public:
	// Defines the type of the network message, cannot be more than one of the same type!
	static constexpr uint16 type() { return 0xFFFF; }
	virtual uint16 get_type() { return 0xFFFF; }
	virtual constexpr uint32 get_size() { return sizeof(MsgGCTemplate) - sizeof(void*); }

	// You can define any variables here that you can read on received message.
};

class MsgGCWelcome : public MsgGCTemplate {
public:
	VIRTUAL_TYPE_SIZE(MsgGCWelcome, 0xBEEF);
	uint64 steam_id;
	uint32 auth_ticket_size;
	char auth_ticket[512];
};
class MsgGCConfirmAuth : public MsgGCTemplate {
public:
	VIRTUAL_TYPE_SIZE(MsgGCConfirmAuth, 0xBEFE);
	EBeginAuthSessionResult auth_result;
};
class MsgGCInventoryData : public MsgGCTemplate {
public:
	VIRTUAL_TYPE_SIZE(MsgGCInventoryData, 0xBFFE);
};
class MsgGCHeartbeat : public MsgGCTemplate {
public:
	VIRTUAL_TYPE_SIZE(MsgGCHeartbeat, 0xBEAF);
};

// ---------------------------------------------
// -------------- End of messages --------------
// ---------------------------------------------
#pragma pack(pop)

class NetworkMessage {
public:
	// Use default constructor for writing
	NetworkMessage(MsgGCTemplate* type);
	// Use this constructor with the message parameter for reading
	NetworkMessage(MsgGCTemplate* type, void* message, uint32 msgsize);

	~NetworkMessage();

	uint32 WriteMessage(SNetSocket_t packet, bool reliable);
	uint32 GetMessageSize();

	uint32 WriteBytes(void* bytes, uint32 amount);
public:
	uint16 struct_type = 0;
	uint32 written_data = 0;
	void* data = nullptr;
	uint32 written_msg_data = 0;
	void* msg_data = nullptr;

	// Get the struct type of the message
	static uint16 get_type(void* message, uint32 msgsize);
};

#endif