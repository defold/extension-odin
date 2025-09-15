local M = {}

local subscribers = {}

local function on_event(self, event, data, msgid)
	if event == "rpc_error" then
		print("on_event (rpc_error)", event, data, msgid)
	else
		for url,_ in pairs(subscribers) do
			if go.exists(url) then
				msg.post(url, event, data)
			else
				print("Removed listener", url)
				subscribers[url] = nil
			end
		end
	end
end

function M.init(cb)
	local ok = odin.init(on_event)
	if not ok then
		print("Error while initializing odin")
	end
end

function M.subscribe(url)
	url = url or msg.url()
	subscribers[url] = true
end

function M.unsubscribe(url)
	url = url or msg.url()
	subscribers[url] = nil
end

function M.create_room(room_id, user_id)
	-- https://docs.4players.io/voice/introduction/access-keys#generating-access-keys
	local access_key = "AdJBwSsjczaDmrEuJvfBXJhAQPw63aWnBRX2WWGQSHc4"
	return odin.create_room(room_id, user_id, access_key)
end

function M.close_room()
	return odin.close_room()
end

function M.send(data)
	return odin.send(json.encode(data))
end

return M