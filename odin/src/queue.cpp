#include "queue.h"

#include <dmsdk/sdk.h>

MessageQueue* QueueCreate()
{
    struct MessageQueue* queue = (struct MessageQueue*)malloc(sizeof(MessageQueue));
    queue->m_Mutex = dmMutex::New();
    return queue;
}

void QueueDestroy(MessageQueue* queue)
{
    dmMutex::Delete(queue->m_Mutex);
    queue->m_Mutex = 0;

    for(uint32_t i = 0; i != queue->m_Messages.Size(); ++i)
    {
        RpcMessage* msg = queue->m_Messages[i];
        free(msg->bytes);
        free(msg);
    }
    queue->m_Messages.SetSize(0);
}

void QueuePush(MessageQueue* queue, RpcMessage* message)
{
    DM_MUTEX_SCOPED_LOCK(queue->m_Mutex);
    if (queue->m_Messages.Full())
    {
        queue->m_Messages.OffsetCapacity(8);
    }
    queue->m_Messages.Push(message);
}

void QueueFlush(MessageQueue* queue, HandleRpcMessageFn fn)
{
    if (queue->m_Messages.Empty())
    {
        return;
    }

    dmArray<RpcMessage*> tmp;
    {
        DM_MUTEX_SCOPED_LOCK(queue->m_Mutex);
        tmp.Swap(queue->m_Messages);
    }

    for(uint32_t i = 0; i != tmp.Size(); ++i)
    {
        RpcMessage* msg = tmp[i];
        fn(msg);
        free(msg->bytes);
        free(msg);
    }
}

