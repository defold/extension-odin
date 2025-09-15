/** ODIN Voice
 * Functions and constants for interacting with ODIN Voice
 * @document
 * @namespace odin
 */

#define EXTENSION_NAME ODIN
#define LIB_NAME "ODIN"
#define MODULE_NAME "odin"

#ifndef DLIB_LOG_DOMAIN
#define DLIB_LOG_DOMAIN "odin"
#endif

#include <dmsdk/sdk.h>

#if defined(DM_PLATFORM_OSX) || defined(DM_PLATFORM_WINDOWS) || defined(DM_PLATFORM_LINUX) || defined(DM_PLATFORM_ANDROID) || defined(DM_PLATFORM_IOS)

#include "queue.h"
#include "helper.h"
#include "odin.h"
#include "msgpack.h"


#define MSGPACK_REQUEST 0
#define MSGPACK_RESPONSE 1
#define MSGPACK_NOTIFICATION 2




static void on_rpc(uint64_t room_ref, const uint8_t *bytes, uint32_t bytes_length, void *user_data);

static const char* g_OdinServer;
static dmScript::LuaCallbackInfo* g_OdinListener;
static MessageQueue* g_OdinMessageQueue;
static OdinRoom *g_OdinRoom;
static OdinConnectionPool *g_OdinConnectionPool;
static OdinConnectionPoolSettings g_OdinConnectionPoolSettings = {
  .on_datagram = NULL,
  .on_rpc = &on_rpc,
  .user_data = NULL
};



static void push_object(lua_State* L, struct msgpack_object o)
{
    switch (o.type)
    {
        case MSGPACK_OBJECT_NIL:
            lua_pushnil(L);
            break;
        case MSGPACK_OBJECT_BOOLEAN:
            lua_pushboolean(L, o.via.boolean);
            break;
        case MSGPACK_OBJECT_POSITIVE_INTEGER:
            lua_pushinteger(L, o.via.u64);
            break;
        case MSGPACK_OBJECT_NEGATIVE_INTEGER:
            lua_pushinteger(L, o.via.i64);
            break;
        case MSGPACK_OBJECT_FLOAT32:
            lua_pushnumber(L, o.via.f64);
            break;
        case MSGPACK_OBJECT_FLOAT64:
            lua_pushnumber(L, o.via.f64);
            break;
        case MSGPACK_OBJECT_STR:
            lua_pushlstring(L, o.via.str.ptr, o.via.str.size);
            break;
        case MSGPACK_OBJECT_ARRAY:
            lua_newtable(L);
            for (int i = 0; i < o.via.array.size; i++)
            {
                lua_pushnumber(L, i + 1);
                push_object(L, o.via.array.ptr[i]);
                lua_rawset(L, -3);
            }
            break;
        case MSGPACK_OBJECT_MAP:
            lua_newtable(L);
            for (int i = 0; i < o.via.map.size; i++)
            {
                struct msgpack_object_kv kv = o.via.map.ptr[i];
                push_object(L, kv.key);
                push_object(L, kv.val);
                lua_rawset(L, -3);
            }
            break;
        case MSGPACK_OBJECT_BIN:
            lua_pushlstring(L, o.via.bin.ptr, o.via.bin.size);
            break;
        case MSGPACK_OBJECT_EXT:
            dmLogInfo("Unsupported type MSGPACK_OBJECT_EXT");
            lua_pushnil(L);
            break;
    }
}

static void HandleRpcMessage(RpcMessage* message)
{
    if (!g_OdinListener)
    {
        dmLogWarning("ODIN callback is not set");
        return;
    }

    if (!dmScript::IsCallbackValid(g_OdinListener))
    {
        dmLogError("ODIN callback is not valid");
        g_OdinListener = 0;
        return;
    }

    if (!dmScript::SetupCallback(g_OdinListener))
    {
        dmLogError("ODIN callback setup failed");
        dmScript::DestroyCallback(g_OdinListener);
        g_OdinListener = 0;
        return;
    }

    /* deserialize the buffer into msgpack_object instance. */
    /* deserialized object is valid during the msgpack_zone instance alive. */
    msgpack_zone mempool;
    msgpack_zone_init(&mempool, 2048);
    msgpack_object deserialized;
    msgpack_unpack((const char*)message->bytes, message->bytes_length, NULL, &mempool, &deserialized);

    lua_State* L = dmScript::GetCallbackLuaContext(g_OdinListener);

    if (deserialized.type == MSGPACK_OBJECT_ARRAY)
    {
        msgpack_object_array array = deserialized.via.array;
        int type = array.ptr[0].via.u64;
        if (type == MSGPACK_NOTIFICATION)
        {
            // notification [type, method, params]
            msgpack_object method = array.ptr[1];
            msgpack_object params = array.ptr[2];
            lua_pushlstring(L, method.via.str.ptr, method.via.str.size);
            push_object(L, params);
            lua_pushnil(L);
        }
        else if (type == MSGPACK_RESPONSE)
        {
            // response [type, msgid, error, result]
            msgpack_object msgid = array.ptr[1];
            msgpack_object error = array.ptr[2];
            msgpack_object result = array.ptr[3];
            if (error.type != MSGPACK_OBJECT_NIL)
            {
                lua_pushstring(L, "rpc_error");
                push_object(L, error);
                lua_pushnumber(L, msgid.via.u64);
            }
            else
            {
                lua_pushstring(L, "rpc_response");
                push_object(L, result);
                lua_pushnumber(L, msgid.via.u64);
            }
        }
        else if (type == MSGPACK_REQUEST)
        {
            // request [type, msgid, method, params]
            msgpack_object msgid = array.ptr[1];
            msgpack_object method = array.ptr[2];
            msgpack_object params = array.ptr[3];

            lua_pushlstring(L, method.via.str.ptr, method.via.str.size);
            push_object(L, params);
            lua_pushnumber(L, msgid.via.u64);
        }
        else
        {
            dmLogError("Unexpected msgpack rpc type %d", type);
            lua_pushstring(L, "rpc_error");
            push_object(L, deserialized);
            lua_pushnil(L);
        }
    }
    else
    {
        dmLogError("Unexpected msgpack rpc object, expected array but was type %d", deserialized.type);
        lua_pushstring(L, "rpc_error");
        push_object(L, deserialized);
        lua_pushnil(L);
    }

    msgpack_zone_destroy(&mempool);

    // self, event, object, msgid
    int ret = lua_pcall(L, 4, 0, 0);
    if (ret != 0)
    {
        dmLogError("Error invoking ODIN listener %d (LUA_ERRRUN = %d LUA_ERRMEM = %d LUA_ERRERR = %d)", ret, LUA_ERRRUN, LUA_ERRMEM, LUA_ERRERR);
        lua_pop(L, 1);
    }
    dmScript::TeardownCallback(g_OdinListener);
}


static void on_rpc(uint64_t room_ref, const uint8_t *bytes, uint32_t bytes_length, void *user_data) {
    const char* data = (const char*)bytes;

    RpcMessage* msg = (RpcMessage*)malloc(sizeof(RpcMessage));
    msg->room_ref = room_ref;
    msg->bytes_length = bytes_length;
    msg->bytes = (uint8_t*)malloc(sizeof(uint8_t) * bytes_length);
    memcpy(msg->bytes, bytes, bytes_length);

    QueuePush(g_OdinMessageQueue, msg);
}



/*******************************************
 * LIFECYCLE
 *******************************************/

/** Initialize ODIN Voice
 * @name init
 * @function listener
 * @treturn boolean success
 */
static int Init(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 1);

    if (g_OdinConnectionPool)
    {
        dmLogWarning("already initialized");
        lua_pushboolean(L, true);
        return 1;
    }

    g_OdinListener = dmScript::CreateCallback(L, 1);

    g_OdinMessageQueue = QueueCreate();

    OdinError r = odin_initialize(ODIN_VERSION);
    if (r != ODIN_ERROR_SUCCESS)
    {
        const char* last_error = odin_error_get_last_error();
        dmLogError("Failed to initialize ODIN %s, %d", last_error, r);
        lua_pushboolean(L, false);
        return 1;
    }
    r = odin_connection_pool_create(g_OdinConnectionPoolSettings, &g_OdinConnectionPool);
    if (r != ODIN_ERROR_SUCCESS)
    {
        const char* last_error = odin_error_get_last_error();
        dmLogError("Failed to create ODIN connection pool %s %d", last_error, r);
        lua_pushboolean(L, false);
        return 1;
    }

    lua_pushboolean(L, true);
    return 1;
}

/** Create a room.
 * @name create_room
 * @string access_key
 * @string payload
 * @treturn boolean success
 */
static int CreateRoom(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 1);

    const char* access_key = luaL_checkstring(L, 1);
    const char* payload = luaL_checkstring(L, 2);
    dmLogInfo("CreateRoom access key = '%s' payload = '%s'", access_key, payload);

    OdinTokenGenerator *generator;
    OdinError r = odin_token_generator_create(access_key, &generator);
    if (r != ODIN_ERROR_SUCCESS)
    {
        const char* last_error = odin_error_get_last_error();
        dmLogError("Failed to create token generator %s %d", last_error, r);
        lua_pushboolean(L, false);
        return 1;
    }

    char token[512];
    uint32_t token_length = sizeof(token);
    r = odin_token_generator_sign(generator, payload, token, &token_length);
    if (r != ODIN_ERROR_SUCCESS)
    {
        const char* last_error = odin_error_get_last_error();
        odin_token_generator_free(generator);
        dmLogError("Failed to sign payload %s %d", last_error, r);
        lua_pushboolean(L, false);
        return 1;
    }
    odin_token_generator_free(generator);

    dmLogInfo("CreateRoom token = %s", token);

    r = odin_room_create(g_OdinConnectionPool, g_OdinServer, token, &g_OdinRoom);
    if (r != ODIN_ERROR_SUCCESS)
    {
        const char* last_error = odin_error_get_last_error();
        dmLogError("Failed to create room %s %d", last_error, r);
        lua_pushboolean(L, false);
        return 1;
    }

    lua_pushboolean(L, true);
    return 1;
}



/** Send a message.
 * @name send
 * @string data
 * @table target_peer_ids (OPTIONAL)
 * @number msgid (OPTIONAL)
 * @treturn boolean success
 */
static int SendRpc(lua_State* L)
{
    DM_LUA_STACK_CHECK(L, 1);

    size_t data_length;
    const char* data = luaL_checklstring(L, 1, &data_length);
    bool has_target_peer_ids = lua_istable(L, 2);
    uint32_t msgid = 0;
    if (lua_isnumber(L, 3))
    {
        msgid = luaL_checknumber(L, 3);
    }

    msgpack_sbuffer sbuf;
    msgpack_sbuffer_init(&sbuf);
    msgpack_packer pk;
    msgpack_packer_init(&pk, &sbuf, msgpack_sbuffer_write);

    // [0,0,"SendMessage",{"message":"foo","target_peer_ids":[1,2,3,4,5]}]
    msgpack_pack_array(&pk, 4);
    msgpack_pack_uint8(&pk, MSGPACK_REQUEST);
    msgpack_pack_uint32(&pk, msgid);
    msgpack_pack_str_with_body(&pk, "SendMessage", 11);
    msgpack_pack_map(&pk, has_target_peer_ids ? 2 : 1);
    msgpack_pack_str_with_body(&pk, "message", 7);
    msgpack_pack_bin_with_body(&pk, data, data_length);
    if (has_target_peer_ids)
    {
        msgpack_pack_str_with_body(&pk, "target_peer_ids", 15);
        size_t len = lua_objlen(L, 2);
        msgpack_pack_array(&pk, len);
        for (int i = 1; i <= len; i++)
        {
            lua_rawgeti(L, 2, i);
            lua_Number peer_id = luaL_checknumber(L, -1);
            msgpack_pack_uint32(&pk, peer_id);
        }
    }

    OdinError r = odin_room_send_rpc(g_OdinRoom, (const uint8_t*)sbuf.data, sbuf.size);

    msgpack_sbuffer_destroy(&sbuf);

    if (r != ODIN_ERROR_SUCCESS)
    {
        const char* last_error = odin_error_get_last_error();
        dmLogError("Failed to send data %s %d", last_error, r);
        lua_pushboolean(L, false);
        return 1;
    }

    lua_pushboolean(L, true);
    return 1;
}

static const luaL_reg Module_methods[] = {
    { "init", Init },
    { "create_room", CreateRoom },
    { "send", SendRpc },
    { 0, 0 }
};


static void LuaInit(lua_State* L)
{
    int top = lua_gettop(L);
    luaL_register(L, MODULE_NAME, Module_methods);
    lua_pop(L, 1);
    assert(top == lua_gettop(L));
}

dmExtension::Result AppInitializeODIN(dmExtension::AppParams* params)
{
    g_OdinServer = dmConfigFile::GetString(params->m_ConfigFile, "odin.server", "");
    return dmExtension::RESULT_OK;
}

dmExtension::Result InitializeODIN(dmExtension::Params* params)
{
    LuaInit(params->m_L);
    dmLogInfo("Registered %s Extension", MODULE_NAME);
    return dmExtension::RESULT_OK;
}

dmExtension::Result AppFinalizeODIN(dmExtension::AppParams* params)
{
    return dmExtension::RESULT_OK;
}

dmExtension::Result FinalizeODIN(dmExtension::Params* params)
{
    if (g_OdinRoom)
    {
        odin_room_free(g_OdinRoom);
        g_OdinRoom = 0;
    }

    if (g_OdinConnectionPool)
    {
        odin_connection_pool_free(g_OdinConnectionPool);
        g_OdinConnectionPool = 0;
    }

    odin_shutdown();

    if (g_OdinListener)
    {
        dmScript::DestroyCallback(g_OdinListener);
        g_OdinListener = 0;
    }

    if (g_OdinMessageQueue)
    {
        QueueDestroy(g_OdinMessageQueue);
        free(g_OdinMessageQueue);
        g_OdinMessageQueue = 0;
    }


    return dmExtension::RESULT_OK;
}


dmExtension::Result UpdateODIN(dmExtension::Params* params)
{
    QueueFlush(g_OdinMessageQueue, HandleRpcMessage);
    return dmExtension::RESULT_OK;
}

#else

static dmExtension::Result AppInitializeODIN(dmExtension::AppParams* params)
{
    dmLogWarning("Registered %s (null) Extension", MODULE_NAME);
    return dmExtension::RESULT_OK;
}

static dmExtension::Result InitializeODIN(dmExtension::Params* params)
{
    return dmExtension::RESULT_OK;
}

static dmExtension::Result AppFinalizeODIN(dmExtension::AppParams* params)
{
    return dmExtension::RESULT_OK;
}

static dmExtension::Result FinalizeODIN(dmExtension::Params* params)
{
    return dmExtension::RESULT_OK;
}

static dmExtension::Result UpdateODIN(dmExtension::Params* params)
{
    return dmExtension::RESULT_OK;
}

#endif

DM_DECLARE_EXTENSION(EXTENSION_NAME, LIB_NAME, AppInitializeODIN, AppFinalizeODIN, InitializeODIN, UpdateODIN, 0, FinalizeODIN)
