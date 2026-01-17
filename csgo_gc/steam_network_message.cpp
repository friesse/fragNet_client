#include "steam_network_message.hpp"
#include <stdlib.h>

NetworkMessage::NetworkMessage(MsgGCTemplate* type) {
	Platform::Print("struct type: %i | size: %i\n", type->get_type(), type->get_size());
	struct_type = type->get_type();
	data = malloc(type->get_size());
	written_data += type->get_size();
}

NetworkMessage::NetworkMessage(MsgGCTemplate* type, void* message, uint32 msgsize) {
	// We waste CPU cycles getting the struct_type again.
	memcpy(&struct_type,
		message,
		sizeof(struct_type)); // get first 2 bytes for the struct size

	memcpy(&written_data,
		(void*)((uintptr_t)message + sizeof(struct_type)),
		sizeof(written_data)); // get 4 bytes for the struct size

	data = malloc(written_data);
	memcpy(data,
		(void*)((uintptr_t)message + sizeof(struct_type) + sizeof(written_data)),
		written_data); // get the written_data amount of bytes for the struct data

	memcpy(&written_msg_data,
		(void*)((uintptr_t)message + sizeof(struct_type) + sizeof(written_data) + written_data),
		sizeof(written_msg_data)); // get 4 bytes for the custom message size

	// if there's something written to the msg_data copy it
	if (written_msg_data > 0) {
		msg_data = malloc(written_msg_data);

		memcpy(msg_data,
			(void*)((uintptr_t)message + sizeof(struct_type) + sizeof(written_data) + written_data + sizeof(written_msg_data)),
			written_msg_data); // get the last bytes for the custom message size
	}

	Platform::Print("struct type: %i | size: %i\n", struct_type, written_data);
}


NetworkMessage::~NetworkMessage() {
	//if (data != nullptr)
	//	free(data);
	//delete[] data;
	written_data = 0;
	data = nullptr;

	//if (msg_data != nullptr)
	//	free(msg_data);
	//delete[] msg_data;
	written_msg_data = 0;
	msg_data = nullptr;
}

uint32 NetworkMessage::WriteMessage(SNetSocket_t packet, bool reliable) {
	uint32 message_size = GetMessageSize();
	char* msg = new char[message_size]();

	memcpy((void*)msg,
		&struct_type, sizeof(struct_type));

	memcpy((void*)(msg + sizeof(struct_type)),
		&written_data, sizeof(written_data)); // set first 4 bytes to the struct size

	memcpy((void*)(msg + sizeof(struct_type) + sizeof(written_data)),
		data, written_data); // set the written_data amount of bytes to the struct

	memcpy((void*)(msg + sizeof(struct_type) + sizeof(written_data) + written_data),
		&written_msg_data, sizeof(written_msg_data)); // set 4 bytes to the custom message size

	// if there's something written to the msg_data copy it
	if (written_msg_data > 0) {

		memcpy((void*)(msg + sizeof(struct_type) + sizeof(written_data) + written_data + sizeof(written_msg_data)),
			data, written_data); // set the last bytes to the custom message size
	}

	Platform::Print("Sent to the server a network message with type: %i | alt: %i\n", struct_type, *((uint16*)msg));
	SteamNetworking()->SendDataOnSocket(packet, msg, message_size, reliable);
	delete[] msg;
	return message_size;
}

uint32 NetworkMessage::GetMessageSize() {
	return sizeof(struct_type) + sizeof(written_data) + written_data + sizeof(written_msg_data) + written_msg_data;
}

uint32 NetworkMessage::WriteBytes(void* bytes, uint32 amount) {
	memcpy((void*)((uintptr_t)msg_data + written_msg_data), bytes, amount);
	written_msg_data += amount;
	return written_msg_data;
}

uint16 NetworkMessage::get_type(void* message, uint32 msgsize) {
	uint16 type = 0;
	if (msgsize >= 2) {
		memcpy(&type, message, 2);
	}
	return type;
}