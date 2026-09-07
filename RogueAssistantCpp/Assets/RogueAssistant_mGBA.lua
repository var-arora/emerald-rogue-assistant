-- Emerald Rogue Assistant mGBA bridge protocol 1.0
-- Connects mGBA 0.10.5 or later to the assistant on the same computer.

local BRIDGE_HOST = "127.0.0.1"
local BRIDGE_PORT = 30125 -- ROGUE_ASSISTANT_BRIDGE_PORT

local Bridge = {}

local PROTOCOL_MAJOR = 1
local PROTOCOL_MINOR = 0
local SCRIPT_VERSION = 1
local MAX_BODY_LENGTH = 1024 * 1024
local MAX_QUEUE_FRAMES = 256
local MAX_QUEUE_BYTES = 4 * 1024 * 1024
local MAX_DIAGNOSTIC_LENGTH = 1024
local MAX_IO_BYTES_PER_FRAME = 256 * 1024
local MAX_OPERATIONS_PER_FRAME = 64
local RECEIVE_CHUNK_SIZE = 64 * 1024

local MESSAGE = {
    CLIENT_HELLO = 1,
    SERVER_HELLO = 2,
    READ_REQUEST = 3,
    WRITE_REQUEST = 4,
    READ_RESULT = 5,
    WRITE_RESULT = 6,
    ERROR = 7,
    CLOSE = 8,
}

local ERROR = {
    UNSUPPORTED_PROTOCOL = 1,
    BUSY = 2,
    MALFORMED_FRAME = 3,
    INVALID_REQUEST_ID = 4,
    INVALID_ADDRESS = 5,
    INVALID_SIZE = 6,
    QUEUE_FULL = 7,
    INTERNAL_ERROR = 8,
}

local READABLE_RANGES = {
    {0x02000000, 0x02040000},
    {0x03000000, 0x03008000},
    {0x05000000, 0x05000400},
    {0x06000000, 0x06018000},
    {0x07000000, 0x07000400},
    {0x08000000, 0x0E000000},
}

local WRITABLE_RANGES = {
    {0x02000000, 0x02040000},
    {0x03000000, 0x03008000},
}

local function isKnownMessageType(messageType)
    return messageType >= MESSAGE.CLIENT_HELLO and messageType <= MESSAGE.CLOSE
end

local function requiresZeroRequestId(messageType)
    return messageType == MESSAGE.CLIENT_HELLO
        or messageType == MESSAGE.SERVER_HELLO
        or messageType == MESSAGE.CLOSE
end

local function requiresNonzeroRequestId(messageType)
    return messageType == MESSAGE.READ_REQUEST
        or messageType == MESSAGE.WRITE_REQUEST
        or messageType == MESSAGE.READ_RESULT
        or messageType == MESSAGE.WRITE_RESULT
end

local function isRangeAllowed(address, size, ranges)
    if type(address) ~= "number" or type(size) ~= "number" or size <= 0 then
        return false
    end
    local rangeEnd = address + size
    if address < 0 or rangeEnd > 0x100000000 or rangeEnd < address then
        return false
    end
    for _, range in ipairs(ranges) do
        if address >= range[1] and rangeEnd <= range[2] then
            return true
        end
    end
    return false
end

local function encodeFrame(messageType, requestId, payload)
    payload = payload or ""
    requestId = requestId or 0
    local bodyLength = 8 + #payload
    if not isKnownMessageType(messageType) or bodyLength > MAX_BODY_LENGTH then
        return nil, "invalid bridge frame"
    end
    if (requiresZeroRequestId(messageType) and requestId ~= 0)
        or (requiresNonzeroRequestId(messageType) and requestId == 0) then
        return nil, "invalid bridge request ID"
    end
    return string.pack("<I4I1I1I2I4", bodyLength, messageType, 0, 0, requestId) .. payload
end

local function encodeClientHello()
    local payload = "RAB1" .. string.pack("<I2I2I4", PROTOCOL_MAJOR, PROTOCOL_MINOR, SCRIPT_VERSION)
    return assert(encodeFrame(MESSAGE.CLIENT_HELLO, 0, payload))
end

local function encodeError(requestId, code, diagnostic)
    diagnostic = tostring(diagnostic or "")
    if #diagnostic > MAX_DIAGNOSTIC_LENGTH then
        diagnostic = string.sub(diagnostic, 1, MAX_DIAGNOSTIC_LENGTH)
    end
    return encodeFrame(MESSAGE.ERROR, requestId,
        string.pack("<I2I2", code, #diagnostic) .. diagnostic)
end

local function newDecoder()
    return {
        buffer = "",
        frames = {},
        queuedBytes = 0,
        error = nil,
    }
end

local function failDecoder(decoder, diagnostic)
    decoder.error = diagnostic
    decoder.buffer = ""
    return false, diagnostic
end

local function feedDecoder(decoder, bytes)
    if decoder.error then
        return false, decoder.error
    end
    decoder.buffer = decoder.buffer .. bytes
    while true do
        if #decoder.buffer < 4 then
            return true
        end
        local bodyLength = string.unpack("<I4", decoder.buffer)
        if bodyLength < 8 then
            return failDecoder(decoder, "bridge frame body is too short")
        end
        if bodyLength > MAX_BODY_LENGTH then
            return failDecoder(decoder, "bridge frame body exceeds 1 MiB")
        end
        local wireLength = 4 + bodyLength
        if #decoder.buffer < wireLength then
            return true
        end

        local messageType, flags, reserved, requestId = string.unpack("<I1I1I2I4", decoder.buffer, 5)
        if not isKnownMessageType(messageType) then
            return failDecoder(decoder, "unknown bridge message type")
        end
        if flags ~= 0 or reserved ~= 0 then
            return failDecoder(decoder, "bridge flags and reserved bytes must be zero")
        end
        if (requiresZeroRequestId(messageType) and requestId ~= 0)
            or (requiresNonzeroRequestId(messageType) and requestId == 0) then
            return failDecoder(decoder, "invalid bridge request ID")
        end
        if #decoder.frames >= MAX_QUEUE_FRAMES
            or decoder.queuedBytes + wireLength > MAX_QUEUE_BYTES then
            return failDecoder(decoder, "bridge receive queue is full")
        end

        table.insert(decoder.frames, {
            messageType = messageType,
            requestId = requestId,
            payload = string.sub(decoder.buffer, 13, wireLength),
        })
        decoder.queuedBytes = decoder.queuedBytes + wireLength
        decoder.buffer = string.sub(decoder.buffer, wireLength + 1)
    end
end

local function pollDecoder(decoder)
    local frames = decoder.frames
    decoder.frames = {}
    decoder.queuedBytes = 0
    return frames
end

local function newState()
    return {
        socket = nil,
        phase = "disconnected",
        stopped = false,
        decoder = newDecoder(),
        operations = {},
        operationBytes = 0,
        outgoing = {},
        outgoingBytes = 0,
        lastConnectSecond = nil,
        lastConnectionError = nil,
    }
end

local function queueWire(state, wire)
    if #state.outgoing >= MAX_QUEUE_FRAMES
        or state.outgoingBytes + #wire > MAX_QUEUE_BYTES then
        return false, "bridge send queue is full"
    end
    table.insert(state.outgoing, {data = wire, offset = 1})
    state.outgoingBytes = state.outgoingBytes + #wire
    return true
end

local function queueFrame(state, messageType, requestId, payload)
    local wire, err = encodeFrame(messageType, requestId, payload)
    if not wire then
        return false, err
    end
    return queueWire(state, wire)
end

local function queueError(state, requestId, code, diagnostic)
    local wire, err = encodeError(requestId, code, diagnostic)
    if not wire then
        return false, err
    end
    return queueWire(state, wire)
end

local function detachSocketCallbacks(connection)
    if connection then
        -- In mGBA 0.10.5, close() leaves the socket's OS number (descriptor) in
        -- the Lua object. When Lua later frees the object, it closes that number
        -- again. The OS may have reused it for our next connection by then.
        -- Remove callbacks and let Lua free the object, closing it only once.
        if connection._onframecb then
            callbacks:remove(connection._onframecb)
            connection._onframecb = nil
        end
        connection._callbacks = {}
    end
end

local function closeSocket(state)
    local connection = state.socket
    state.socket = nil
    state.phase = "disconnected"
    state.decoder = newDecoder()
    state.operations = {}
    state.operationBytes = 0
    state.outgoing = {}
    state.outgoingBytes = 0
    detachSocketCallbacks(connection)
    connection = nil
    collectgarbage("collect")
end

local function failConnection(state, diagnostic)
    local expectedClose = state.phase == "closing" or state.stopped
    if not expectedClose and diagnostic and diagnostic ~= state.lastConnectionError then
        console:error("Emerald Rogue Assistant connection error: " .. tostring(diagnostic))
        state.lastConnectionError = diagnostic
    end
    closeSocket(state)
end

local function beginProtocolClose(state, diagnostic)
    local queued = queueError(state, 0, ERROR.MALFORMED_FRAME, diagnostic)
    if queued then
        queued = queueFrame(state, MESSAGE.CLOSE, 0, "")
    end
    if not queued then
        failConnection(state, diagnostic)
        return
    end
    state.phase = "closing"
end

local function enqueueOperation(state, operation)
    local size = operation.size
    if #state.operations >= MAX_QUEUE_FRAMES
        or state.operationBytes + size > MAX_QUEUE_BYTES then
        return queueError(state, operation.requestId, ERROR.QUEUE_FULL, "mGBA operation queue is full")
    end
    table.insert(state.operations, operation)
    state.operationBytes = state.operationBytes + size
    return true
end

local function processServerHello(state, frame)
    if #frame.payload ~= 12 then
        beginProtocolClose(state, "invalid ServerHello payload")
        return
    end
    local status, reserved, major, minor =
        string.unpack("<I1I1I2I2I2I2I2", frame.payload)
    if reserved ~= 0 then
        beginProtocolClose(state, "invalid ServerHello reserved byte")
        return
    end
    if status ~= 0 or major ~= PROTOCOL_MAJOR or minor ~= PROTOCOL_MINOR then
        console:error("Emerald Rogue Assistant did not accept the connection.")
        state.phase = "closing"
        return
    end
    state.phase = "connected"
    state.lastConnectionError = nil
    console:log("Emerald Rogue Assistant connected.")
end

local function processIncomingFrame(state, frame)
    if state.phase == "awaiting_hello" then
        if frame.messageType ~= MESSAGE.SERVER_HELLO or frame.requestId ~= 0 then
            beginProtocolClose(state, "expected ServerHello")
            return
        end
        processServerHello(state, frame)
        return
    end

    if frame.messageType == MESSAGE.ERROR then
        if #frame.payload < 4 then
            beginProtocolClose(state, "invalid Error payload")
            return
        end
        local code, diagnosticLength = string.unpack("<I2I2", frame.payload)
        if diagnosticLength ~= #frame.payload - 4 or diagnosticLength > MAX_DIAGNOSTIC_LENGTH then
            beginProtocolClose(state, "invalid Error diagnostic length")
            return
        end
        local diagnostic = string.sub(frame.payload, 5)
        console:error(string.format("Emerald Rogue Assistant connection error %d: %s", code, diagnostic))
        if frame.requestId == 0 then
            state.phase = "closing"
        end
        return
    end

    if frame.messageType == MESSAGE.CLOSE and frame.requestId == 0 and #frame.payload == 0 then
        local queued = queueFrame(state, MESSAGE.CLOSE, 0, "")
        if not queued then
            failConnection(state, "cannot queue orderly bridge close")
            return
        end
        state.phase = "closing"
        return
    end

    if state.phase ~= "connected" then
        beginProtocolClose(state, "unexpected message before bridge handshake")
        return
    end

    if frame.messageType == MESSAGE.READ_REQUEST then
        if #frame.payload ~= 8 then
            beginProtocolClose(state, "invalid ReadRequest payload")
            return
        end
        local address, size = string.unpack("<I4I4", frame.payload)
        if size > MAX_BODY_LENGTH - 8 then
            if not queueError(state, frame.requestId, ERROR.INVALID_SIZE, "read is too large") then
                failConnection(state, "cannot queue invalid-size response")
            end
        elseif not isRangeAllowed(address, size, READABLE_RANGES) then
            if not queueError(state, frame.requestId, ERROR.INVALID_ADDRESS,
                "read address is outside GBA memory") then
                failConnection(state, "cannot queue invalid-address response")
            end
        else
            local queued = enqueueOperation(state, {
                kind = "read",
                requestId = frame.requestId,
                address = address,
                size = size,
                offset = 0,
                parts = {},
            })
            if not queued then
                failConnection(state, "cannot queue read response")
            end
        end
        return
    end

    if frame.messageType == MESSAGE.WRITE_REQUEST then
        if #frame.payload < 8 then
            beginProtocolClose(state, "invalid WriteRequest payload")
            return
        end
        local address, size = string.unpack("<I4I4", frame.payload)
        local data = string.sub(frame.payload, 9)
        if size ~= #data or size == 0 then
            if not queueError(state, frame.requestId, ERROR.INVALID_SIZE,
                "write byte count does not match payload") then
                failConnection(state, "cannot queue invalid-size response")
            end
        elseif not isRangeAllowed(address, size, WRITABLE_RANGES) then
            if not queueError(state, frame.requestId, ERROR.INVALID_ADDRESS,
                "write address is outside EWRAM/IWRAM") then
                failConnection(state, "cannot queue invalid-address response")
            end
        else
            local queued = enqueueOperation(state, {
                kind = "write",
                requestId = frame.requestId,
                address = address,
                size = size,
                data = data,
                offset = 0,
            })
            if not queued then
                failConnection(state, "cannot queue write response")
            end
        end
        return
    end

    beginProtocolClose(state, "unexpected bridge message from Emerald Rogue Assistant")
end

local function processReceivedBytes(state, bytes)
    local ok, err = feedDecoder(state.decoder, bytes)
    if not ok then
        beginProtocolClose(state, err)
        return
    end
    for _, frame in ipairs(pollDecoder(state.decoder)) do
        if state.phase == "disconnected" then
            return
        end
        processIncomingFrame(state, frame)
    end
end

local function receiveAvailable(state)
    local receivedThisFrame = 0
    while state.socket and receivedThisFrame < MAX_IO_BYTES_PER_FRAME do
        local maximum = math.min(RECEIVE_CHUNK_SIZE, MAX_IO_BYTES_PER_FRAME - receivedThisFrame)
        local bytes, err = state.socket:receive(maximum)
        if bytes then
            receivedThisFrame = receivedThisFrame + #bytes
            processReceivedBytes(state, bytes)
        elseif err == socket.ERRORS.AGAIN then
            return
        else
            failConnection(state, err or "connection closed")
            return
        end
    end
end

local function writeAligned(address, data, startIndex, count)
    local index = startIndex
    local remaining = count
    while remaining > 0 do
        local currentAddress = address + index - 1
        if currentAddress % 4 == 0 and remaining >= 4 then
            local value = string.unpack("<I4", data, index)
            emu:write32(currentAddress, value)
            index = index + 4
            remaining = remaining - 4
        elseif currentAddress % 2 == 0 and remaining >= 2 then
            local value = string.unpack("<I2", data, index)
            emu:write16(currentAddress, value)
            index = index + 2
            remaining = remaining - 2
        else
            emu:write8(currentAddress, string.byte(data, index))
            index = index + 1
            remaining = remaining - 1
        end
    end
end

local function prepareOperationResponse(operation)
    if operation.response then
        return
    end
    if operation.failure then
        operation.response = assert(encodeError(
            operation.requestId, ERROR.INTERNAL_ERROR, operation.failure))
    elseif operation.kind == "read" then
        operation.response = assert(encodeFrame(
            MESSAGE.READ_RESULT, operation.requestId, table.concat(operation.parts)))
    else
        operation.response = assert(encodeFrame(MESSAGE.WRITE_RESULT, operation.requestId, ""))
    end
end

local function processOperations(state)
    if state.phase ~= "connected" then
        return
    end
    local bytesProcessed = 0
    local operationsProcessed = 0
    while #state.operations > 0 and operationsProcessed < MAX_OPERATIONS_PER_FRAME do
        local operation = state.operations[1]
        if operation.response then
            local queued = queueWire(state, operation.response)
            if not queued then
                return
            end
            state.operationBytes = state.operationBytes - operation.size
            table.remove(state.operations, 1)
            operationsProcessed = operationsProcessed + 1
        else
            local remaining = operation.size - operation.offset
            local budget = MAX_IO_BYTES_PER_FRAME - bytesProcessed
            if budget <= 0 then
                return
            end
            local count = math.min(remaining, budget)
            local ok, result
            if operation.kind == "read" then
                ok, result = pcall(function()
                    return emu:readRange(operation.address + operation.offset, count)
                end)
                if ok and type(result) == "string" and #result == count then
                    table.insert(operation.parts, result)
                else
                    operation.failure = "mGBA could not read the requested memory"
                end
            else
                ok = pcall(function()
                    writeAligned(operation.address, operation.data, operation.offset + 1, count)
                end)
                if not ok then
                    operation.failure = "mGBA could not write the requested memory"
                end
            end
            operation.offset = operation.offset + count
            bytesProcessed = bytesProcessed + count
            if operation.failure or operation.offset == operation.size then
                prepareOperationResponse(operation)
            end
        end
    end
end

local function flushOutgoing(state)
    if not state.socket then
        return
    end
    local sentThisFrame = 0
    while #state.outgoing > 0 and sentThisFrame < MAX_IO_BYTES_PER_FRAME do
        local item = state.outgoing[1]
        local last = math.min(#item.data, item.offset + MAX_IO_BYTES_PER_FRAME - sentThisFrame - 1)
        local sentLast, err = state.socket:send(item.data, item.offset, last)
        if not sentLast then
            if err == socket.ERRORS.AGAIN then
                return
            end
            failConnection(state, err or "send failed")
            return
        end
        if sentLast < item.offset then
            return
        end
        local sent = sentLast - item.offset + 1
        item.offset = sentLast + 1
        state.outgoingBytes = state.outgoingBytes - sent
        sentThisFrame = sentThisFrame + sent
        if item.offset > #item.data then
            table.remove(state.outgoing, 1)
        end
    end
end

local function shouldReconnect(state, now)
    if state.stopped or state.socket then
        return false
    end
    if state.lastConnectSecond == nil or now < state.lastConnectSecond
        or now - state.lastConnectSecond >= 1 then
        state.lastConnectSecond = now
        return true
    end
    return false
end

local function connect(state)
    local connection = socket.tcp()
    connection:add("received", function()
        if state.socket == connection then
            receiveAvailable(state)
        end
    end)
    connection:add("error", function(_, err)
        if state.socket == connection then
            failConnection(state, err)
        end
    end)
    local ok, err = connection:connect(BRIDGE_HOST, BRIDGE_PORT)
    if not ok then
        -- Failed connections can also close the same socket number twice.
        -- Let mGBA close it when Lua frees the object.
        detachSocketCallbacks(connection)
        connection = nil
        collectgarbage("collect")
        if err ~= state.lastConnectionError then
            console:log("Waiting for Emerald Rogue Assistant on port " .. BRIDGE_PORT .. ".")
            state.lastConnectionError = err
        end
        return
    end
    state.socket = connection
    state.phase = "awaiting_hello"
    state.decoder = newDecoder()
    state.outgoing = {}
    state.outgoingBytes = 0
    assert(queueWire(state, encodeClientHello()))
end

local function onFrame(state)
    if state.stopped then
        return
    end
    if not state.socket then
        if shouldReconnect(state, os.time()) then
            connect(state)
        end
        return
    end
    processOperations(state)
    flushOutgoing(state)
    if state.socket and state.phase == "closing" and #state.outgoing == 0 then
        closeSocket(state)
    end
end

local function stop(state)
    if state.stopped then
        return
    end
    state.stopped = true
    if state.socket then
        queueFrame(state, MESSAGE.CLOSE, 0, "")
        flushOutgoing(state)
    end
    closeSocket(state)
end

local function restart(state, now)
    closeSocket(state)
    state.stopped = false
    state.lastConnectSecond = now or os.time()
    state.lastConnectionError = nil
end

local function guardCallback(state, name, callback)
    return function(...)
        local arguments = table.pack(...)
        local ok, diagnostic = xpcall(function()
            callback(table.unpack(arguments, 1, arguments.n))
        end, debug.traceback)
        if not ok then
            console:error("The Emerald Rogue Assistant script stopped after an error in " .. name .. ":\n"
                .. tostring(diagnostic))
            stop(state)
        end
    end
end

Bridge.host = BRIDGE_HOST
Bridge.port = BRIDGE_PORT
Bridge.message = MESSAGE
Bridge.error = ERROR
Bridge.encodeFrame = encodeFrame
Bridge.encodeClientHello = encodeClientHello
Bridge.encodeError = encodeError
Bridge.newDecoder = newDecoder
Bridge.feedDecoder = feedDecoder
Bridge.pollDecoder = pollDecoder
Bridge.newState = newState
Bridge.queueWire = queueWire
Bridge.processReceivedBytes = processReceivedBytes
Bridge.processOperations = processOperations
Bridge.flushOutgoing = flushOutgoing
Bridge.writeAligned = writeAligned
Bridge.shouldReconnect = shouldReconnect
Bridge.connect = connect
Bridge.isRangeAllowed = isRangeAllowed
Bridge.restart = restart
Bridge.stop = stop
Bridge.readableRanges = READABLE_RANGES
Bridge.writableRanges = WRITABLE_RANGES

if rawget(_G, "ROGUE_ASSISTANT_TEST") then
    return Bridge
end

local state = newState()
console:log("The Emerald Rogue Assistant script is running.")
callbacks:add("frame", guardCallback(state, "frame", function() onFrame(state) end))
callbacks:add("start", guardCallback(state, "start", function() restart(state) end))
callbacks:add("reset", guardCallback(state, "reset", function() restart(state) end))
callbacks:add("shutdown", guardCallback(state, "shutdown", function() stop(state) end))
callbacks:add("stop", guardCallback(state, "stop", function() stop(state) end))
