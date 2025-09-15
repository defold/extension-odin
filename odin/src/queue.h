#pragma once

#include <dmsdk/sdk.h>

struct RpcMessage {
    uint64_t room_ref;
    uint8_t *bytes;
    uint32_t bytes_length;
};

struct MessageQueue
{
    dmArray<RpcMessage*>    m_Messages;
    dmMutex::HMutex         m_Mutex;
};


typedef void (*HandleRpcMessageFn)(RpcMessage* message);

MessageQueue* QueueCreate();
void QueueDestroy(MessageQueue* queue);
void QueuePush(MessageQueue* queue, RpcMessage* message);
void QueueFlush(MessageQueue* queue, HandleRpcMessageFn fn);
