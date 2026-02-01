-- LibUDS Wireshark Dissector
-- Provides human-readable decoding of UDS Service Data Units (SDUs)

local libuds_p = Proto("libuds", "LibUDS Protocol")

local f_sid = ProtoField.uint8("libuds.sid", "Service ID", base.HEX)
local f_sub = ProtoField.uint8("libuds.sub", "Sub-function", base.HEX)
local f_sub_base = ProtoField.uint8("libuds.sub_base", "Sub-function ID", base.HEX, nil, 0x7F)
local f_sub_suppress = ProtoField.bool("libuds.sub_press", "Suppress Positive Response", 8, nil, 0x80)
local f_did = ProtoField.uint16("libuds.did", "Data Identifier", base.HEX)
local f_nrc = ProtoField.uint8("libuds.nrc", "NRC", base.HEX)
local f_data = ProtoField.bytes("libuds.data", "Data")

libuds_p.fields = { f_sid, f_sub, f_sub_base, f_sub_suppress, f_nrc, f_did, f_data }

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
    [0x54] = "ClearDiagnosticInformation Response",
    [0x59] = "ReadDTCInformation Response",
    [0x62] = "ReadDataByIdentifier Response",
    [0x63] = "ReadMemoryByAddress Response",
    [0x67] = "SecurityAccess Response",
    [0x68] = "CommunicationControl Response",
    [0x69] = "Authentication Response",
    [0x6E] = "WriteDataByIdentifier Response",
    [0x71] = "RoutineControl Response",
    [0x74] = "RequestDownload Response",
    [0x76] = "TransferData Response",
    [0x77] = "RequestTransferExit Response",
    [0x7D] = "WriteMemoryByAddress Response",
    [0x7E] = "TesterPresent Response",
    [0xC5] = "ControlDTCSetting Response",
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
    [0x70] = "VehicleModeNotCorrect",
    [0x78] = "ResponsePending",
    [0x7F] = "ServiceNotSupportedInActiveSession"
}

function libuds_p.dissector(buffer, pinfo, tree)
    pinfo.cols.protocol = "LibUDS"
    local subtree = tree:add(libuds_p, buffer(), "LibUDS Protocol")
    
    local sid = buffer(0,1):uint()
    local sid_name = sids[sid] or "Unknown Service"
    
    pinfo.cols.info = sid_name
    subtree:add(f_sid, buffer(0,1)):append_text(" (" .. sid_name .. ")")
    
    if sid == 0x7F then
        if buffer:len() >= 3 then
            local nrc_sid = buffer(1,1):uint()
            local nrc = buffer(2,1):uint()
            local nrc_name = nrcs[nrc] or "Unknown NRC"
            subtree:add(f_sid, buffer(1,1)):append_text(" (Original SID: " .. (sids[nrc_sid] or "Unknown") .. ")")
            subtree:add(f_nrc, buffer(2,1)):append_text(" (" .. nrc_name .. ")")
            pinfo.cols.info = "NegativeResponse: " .. nrc_name
        end
    elseif sid >= 0x40 and sid <= 0xEF then
        -- Positive Response
        if sid == 0x62 then -- RDBI Response
            local pos = 1
            while pos + 2 <= buffer:len() do
                subtree:add(f_did, buffer(pos, 2))
                pos = pos + 2
                -- DID data decoding skipped for generic visualization
            end
        elseif buffer:len() > 1 then
           subtree:add(f_data, buffer(1))
        end
    else
        -- Request
        if sid == 0x22 then -- RDBI Request
            local pos = 1
            while pos + 2 <= buffer:len() do
                subtree:add(f_did, buffer(pos, 2))
                pos = pos + 2
            end
        elseif buffer:len() > 1 then
            subtree:add(f_sub, buffer(1,1))
            subtree:add(f_sub_base, buffer(1,1))
            subtree:add(f_sub_suppress, buffer(1,1))
            
            if buffer:len() > 2 then
                subtree:add(f_data, buffer(2))
            end
        end
    end
end
