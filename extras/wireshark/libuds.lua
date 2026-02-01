-- LibUDS Wireshark Dissector
-- Provides human-readable decoding of UDS Service Data Units (SDUs)

local libuds_p = Proto("libuds", "LibUDS Protocol")

local f_sid = ProtoField.uint8("libuds.sid", "Service ID", base.HEX)
local f_sub = ProtoField.uint8("libuds.sub", "Sub-function", base.HEX)
local f_nrc = ProtoField.uint8("libuds.nrc", "NRC", base.HEX)
local f_data = ProtoField.bytes("libuds.data", "Data")

libuds_p.fields = { f_sid, f_sub, f_nrc, f_data }

local sids = {
    [0x10] = "DiagnosticSessionControl",
    [0x11] = "ECUReset",
    [0x14] = "ClearDiagnosticInformation",
    [0x19] = "ReadDTCInformation",
    [0x22] = "ReadDataByIdentifier",
    [0x23] = "ReadMemoryByAddress",
    [0x27] = "SecurityAccess",
    [0x28] = "CommunicationControl",
    [0x29] = "Authentication",
    [0x2E] = "WriteDataByIdentifier",
    [0x31] = "RoutineControl",
    [0x34] = "RequestDownload",
    [0x36] = "TransferData",
    [0x37] = "RequestTransferExit",
    [0x3D] = "WriteMemoryByAddress",
    [0x3E] = "TesterPresent",
    [0x85] = "ControlDTCSetting",
    -- Responses
    [0x50] = "DiagnosticSessionControl Response",
    [0x51] = "ECUReset Response",
    [0x59] = "ReadDTCInformation Response",
    [0x62] = "ReadDataByIdentifier Response",
    [0x67] = "SecurityAccess Response",
    [0x68] = "CommunicationControl Response",
    [0x71] = "RoutineControl Response",
    [0x74] = "RequestDownload Response",
    [0x76] = "TransferData Response",
    [0x7F] = "NegativeResponse"
}

local nrcs = {
    [0x10] = "GeneralReject",
    [0x11] = "ServiceNotSupported",
    [0x12] = "SubFunctionNotSupported",
    [0x13] = "IncorrectMessageLengthOrInvalidFormat",
    [0x14] = "ResponseTooLong",
    [0x21] = "BusyRepeatRequest",
    [0x22] = "ConditionsNotCorrect",
    [0x24] = "RequestSequenceError",
    [0x31] = "RequestOutOfRange",
    [0x33] = "SecurityAccessDenied",
    [0x35] = "InvalidKey",
    [0x36] = "ExceedNumberOfAttempts",
    [0x37] = "RequiredTimeDelayNotExpired",
    [0x78] = "ResponsePending"
}

function libuds_p.dissector(buffer, pinfo, tree)
    pinfo.cols.protocol = "LibUDS"
    local subtree = tree:add(libuds_p, buffer(), "LibUDS Protocol")
    
    local sid = buffer(0,1):uint()
    local sid_name = sids[sid] or "Unknown Service"
    
    pinfo.cols.info = sid_name
    subtree:add(f_sid, buffer(0,1)):append_text(" (" .. sid_name .. ")")
    
    if sid == 0x7F then
        local nrc_sid = buffer(1,1):uint()
        local nrc = buffer(2,1):uint()
        local nrc_name = nrcs[nrc] or "Unknown NRC"
        subtree:add(f_sid, buffer(1,1)):append_text(" (Original SID: " .. (sids[nrc_sid] or "Unknown") .. ")")
        subtree:add(f_nrc, buffer(2,1)):append_text(" (" .. nrc_name .. ")")
        pinfo.cols.info = "NegativeResponse: " .. nrc_name
    elseif sid >= 0x40 and sid <= 0x7E then
        -- Positive Response
        if buffer:len() > 1 then
           subtree:add(f_data, buffer(1))
        end
    else
        -- Request
        if buffer:len() > 1 then
            subtree:add(f_sub, buffer(1,1))
            if buffer:len() > 2 then
                subtree:add(f_data, buffer(2))
            end
        end
    end
end

-- Hook into common UDP/TCP ports for local development/simulation
-- local udp_table = DissectorTable.get("udp.port")
-- udp_table:add(1337, libuds_p)
