#!/usr/bin/env python3
import struct
import sys
import json
import os
from datetime import datetime

# --- HTML TEMPLATE ---
HTML_TEMPLATE = """
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>UDSLib Session Analyzer</title>
    <style>
        :root {
            --bg-color: #1e1e1e;
            --panel-bg: #252526;
            --header-bg: #333333;
            --text-color: #d4d4d4;
            --accent: #007acc;
            --highlight: #264f78;
            --border: #3e3e42;
            --row-hover: #2a2d2e;
            --tx-color: #c586c0; /* Purple-ish */
            --rx-color: #4ec9b0; /* Teal-ish */
        }
        body {
            background-color: var(--bg-color);
            color: var(--text-color);
            font-family: 'Consolas', 'Monaco', monospace;
            margin: 0;
            display: flex;
            flex-direction: column;
            height: 100vh;
            overflow: hidden;
        }
        header {
            background-color: var(--header-bg);
            padding: 10px 20px;
            display: flex;
            justify-content: space-between;
            align-items: center;
            border-bottom: 1px solid var(--border);
        }
        h1 { margin: 0; font-size: 16px; color: #569cd6; }
        .meta { font-size: 12px; color: #858585; }
        
        #main-container {
            display: flex;
            flex-direction: column;
            flex: 1;
            overflow: hidden;
        }
        
        #table-container {
            flex: 1;
            overflow-y: auto;
            border-bottom: 1px solid var(--border);
        }
        
        table {
            width: 100%;
            border-collapse: collapse;
            font-size: 13px;
        }
        th {
            background-color: var(--panel-bg);
            position: sticky;
            top: 0;
            text-align: left;
            padding: 5px 10px;
            border-right: 1px solid var(--border);
            border-bottom: 1px solid var(--border);
            color: #dcdcaa;
        }
        td {
            padding: 4px 10px;
            border-right: 1px solid #2d2d2d;
            white-space: nowrap;
            cursor: pointer;
        }
        tr:hover { background-color: var(--row-hover); }
        tr.selected { background-color: var(--highlight) !important; color: white; }
        
        .tx { color: var(--tx-color); }
        .rx { color: var(--rx-color); }
        
        #details-panel {
            height: 200px;
            background-color: var(--panel-bg);
            padding: 15px;
            overflow-y: auto;
            border-top: 1px solid var(--border);
        }
        
        .detail-row { margin-bottom: 8px; font-size: 13px; }
        .label { color: #569cd6; width: 120px; display: inline-block; }
        .value { color: #ce9178; }
        .desc { color: #6a9955; font-style: italic; margin-left: 10px; }
        
        /* Scrollbars */
        ::-webkit-scrollbar { width: 10px; height: 10px; }
        ::-webkit-scrollbar-track { background: var(--bg-color); }
        ::-webkit-scrollbar-thumb { background: #424242; border-radius: 5px; }
        ::-webkit-scrollbar-thumb:hover { background: #4f4f4f; }
        
        .badge {
            padding: 2px 6px;
            border-radius: 4px;
            font-size: 11px;
            font-weight: bold;
        }
        .badge-req { background-color: #007acc; color: white; }
        .badge-res { background-color: #007acc; color: white; opacity: 0.8; }
        .badge-neg { background-color: #f44336; color: white; }
        
    </style>
</head>
<body>
    <header>
        <h1>UDSLib Session Analyzer <span style="font-size:0.8em; opacity:0.7">v1.9.0-dev</span></h1>
        <div class="meta">Capture: <span id="filename">sim_traffic.pcap</span> | <span id="pkt-count">0</span> packets</div>
    </header>
    
    <div id="main-container">
        <div id="table-container">
            <table id="packet-table">
                <thead>
                    <tr>
                        <th style="width: 80px">Time</th>
                        <th style="width: 60px">Dir</th>
                        <th style="width: 60px">ID</th>
                        <th style="width: 150px">Type</th>
                        <th style="width: 200px">Service / Info</th>
                        <th>Payload</th>
                    </tr>
                </thead>
                <tbody>
                    <!-- JS will populate -->
                </tbody>
            </table>
        </div>
        
        <div id="details-panel">
            <h3 style="margin-top:0; color: #dcdcaa; font-size:14px; border-bottom:1px solid #3e3e42; padding-bottom:5px;">Packet Details</h3>
            <div id="details-content">
                <div style="color: grey; padding: 20px;">Select a packet to view details.</div>
            </div>
        </div>
    </div>

    <script>
        const packets = __PACKET_DATA__;
        
        const tableBody = document.querySelector("#packet-table tbody");
        const detailsContent = document.getElementById("details-content");
        const pktCountEl = document.getElementById("pkt-count");
        
        pktCountEl.textContent = packets.length;
        
        function renderTable() {
            let html = "";
            packets.forEach((pkt, index) => {
                const dirClass = pkt.dir === "TX" ? "tx" : "rx";
                const arrow = pkt.dir === "TX" ? "&rarr;" : "&larr;";
                
                let serviceLabel = pkt.info;
                let badgeClass = "";
                
                if (pkt.service_name) {
                    serviceLabel = `<span class="badge badge-req">${pkt.service_name}</span>`;
                    if (pkt.is_response) {
                        serviceLabel = `<span class="badge badge-res">${pkt.service_name} Resp</span>`;
                    }
                    if (pkt.nrc) {
                        serviceLabel = `<span class="badge badge-neg">Neg Resp (0x7F)</span>`;
                    }
                }
                
                html += `<tr onclick="selectPacket(${index})" id="row-${index}">
                    <td>${pkt.time.toFixed(6)}</td>
                    <td class="${dirClass}" style="text-align:center">${arrow}</td>
                    <td>0x${pkt.can_id.toString(16).toUpperCase()}</td>
                    <td>${pkt.frame_type}</td>
                    <td>${serviceLabel} ${pkt.extra_info || ""}</td>
                    <td style="font-family: monospace; font-size: 12px; opacity: 0.8">${pkt.payload_str}</td>
                </tr>`;
            });
            tableBody.innerHTML = html;
        }
        
        function selectPacket(index) {
            // Highlight row
            document.querySelectorAll("tr.selected").forEach(r => r.classList.remove("selected"));
            const row = document.getElementById(`row-${index}`);
            if (row) row.classList.add("selected");
            
            const pkt = packets[index];
            
            let html = `
                <div class="detail-row"><span class="label">Time:</span> <span class="value">${pkt.time.toFixed(6)} s</span></div>
                <div class="detail-row"><span class="label">CAN ID:</span> <span class="value">0x${pkt.can_id.toString(16).toUpperCase()}</span> <span class="desc">(${pkt.dir_desc})</span></div>
                <div class="detail-row"><span class="label">Frame Type:</span> <span class="value">${pkt.frame_type}</span> <span class="desc">${pkt.frame_type_desc}</span></div>
                <div class="detail-row"><span class="label">DLC / Length:</span> <span class="value">${pkt.data_len} bytes</span></div>
                <div class="detail-row"><span class="label">Payload:</span> <span class="value" style="font-family:monospace">${pkt.payload_str}</span></div>
            `;
            
            if (pkt.analysis) {
                 html += `<div style="margin-top:10px; padding-top:10px; border-top:1px dashed #3e3e42">`;
                 for (const [key, val] of Object.entries(pkt.analysis)) {
                     html += `<div class="detail-row"><span class="label">${key}:</span> <span class="value">${val}</span></div>`;
                 }
                 html += `</div>`;
            }
            
            detailsContent.innerHTML = html;
        }
        
        renderTable();
    </script>
</body>
</html>
"""

# --- PARSER ---
SID_MAP = {
    0x10: "DiagnosticSessionControl",
    0x11: "ECUReset",
    0x22: "ReadDataByIdentifier",
    0x27: "SecurityAccess",
    0x2E: "WriteDataByIdentifier",
    0x31: "RoutineControl",
    0x3E: "TesterPresent",
    0x50: "DiagnosticSessionControl",
    0x62: "ReadDataByIdentifier",
    0x7F: "NegativeResponse"
}

def parse_isotp_frame(can_id, payload):
    if not payload: return {}
    
    pci = payload[0]
    pci_type = (pci & 0xF0) >> 4
    
    res = {
        "frame_type": "UNK",
        "frame_type_desc": "Unknown",
        "info": "",
        "payload_str": payload.hex(" ").upper(),
        "analysis": {}
    }
    
    sdu_data = None
    
    if pci_type == 0x0: # SF
        slen = pci & 0x0F
        offset = 1
        if slen == 0: 
            if len(payload) > 1:
                slen = payload[1]
                offset = 2
            else: slen = 0
            
        res["frame_type"] = "SF"
        res["frame_type_desc"] = "Single Frame"
        res["analysis"]["SF_DL"] = slen
        
        if len(payload) >= offset + slen:
             sdu_data = payload[offset : offset + slen]

    elif pci_type == 0x1: # FF
        slen = ((payload[0] & 0x0F) << 8) | payload[1]
        res["frame_type"] = "FF"
        res["frame_type_desc"] = "First Frame (Start of segmented message)"
        res["analysis"]["Total_DL"] = slen
        if len(payload) > 2:
            sdu_data = payload[2:] # Partial data

    elif pci_type == 0x2: # CF
        sn = pci & 0x0F
        res["frame_type"] = "CF"
        res["frame_type_desc"] = f"Consecutive Frame (SN={sn})"
        res["analysis"]["SN"] = sn

    elif pci_type == 0x3: # FC
        fs = pci & 0x0F
        fs_map = {0: "CTS", 1: "WAIT", 2: "OVFL"}
        res["frame_type"] = "FC"
        res["frame_type_desc"] = f"Flow Control ({fs_map.get(fs, 'UNK')})"
        res["analysis"]["Flag"] = fs_map.get(fs, "?")
        if len(payload) > 2:
            res["analysis"]["BlockSize"] = payload[1]
            res["analysis"]["STmin"] = payload[2]

    # UDS Decoding (Best Effort)
    if sdu_data and len(sdu_data) > 0:
        sid = sdu_data[0]
        if sid == 0x7F and len(sdu_data) >= 3:
             res["nrc"] = True
             res["service_name"] = SID_MAP.get(sdu_data[1], f"SID 0x{sdu_data[1]:02X}")
             res["analysis"]["NRC_Code"] = f"0x{sdu_data[2]:02X}"
             res["info"] = f"NRC: 0x{sdu_data[2]:02X}"
        else:
            is_resp = (sid >= 0x50 and sid != 0x7F)
            base_sid = sid - 0x40 if is_resp else sid
            name = SID_MAP.get(base_sid, None) or SID_MAP.get(sid, f"SID 0x{sid:02X}")
            
            res["service_name"] = name
            res["is_response"] = is_resp
            res["info"] = name

    return res

def parse_pcap_to_json(filename):
    packets = []
    
    try:
        f = open(filename, 'rb')
    except:
        return []

    with f:
        f.read(24) # Skip global
        start_time = None
        
        while True:
            pkthdr = f.read(16)
            if len(pkthdr) < 16: break
            ts_sec, ts_usec, incl_len, orig_len = struct.unpack('<IIII', pkthdr)
            pkt_data = f.read(incl_len)
            
            ts = ts_sec + (ts_usec / 1000000.0)
            if start_time is None: start_time = ts
            rel_time = ts - start_time
            
            # User0 -> Raw VCAN packet
            if len(pkt_data) >= 69:
                 can_id = struct.unpack('<I', pkt_data[0:4])[0]
                 dlen = pkt_data[68]
                 if dlen > 64: dlen = 64
                 payload = pkt_data[4 : 4 + dlen]
                 
                 arrow = "TX" # Default client
                 dir_desc = "Client -> Simulator"
                 if can_id == 0x7E8: 
                     arrow = "RX"
                     dir_desc = "Simulator -> Client"
                 
                 info = parse_isotp_frame(can_id, payload)
                 
                 pkt = {
                     "time": rel_time,
                     "can_id": can_id,
                     "dir": arrow,
                     "dir_desc": dir_desc,
                     "data_len": dlen,
                     **info
                 }
                 packets.append(pkt)
                 
    return packets

def main():
    if len(sys.argv) < 2:
        print("Usage: pcap_to_html.py <pcap> [output.html]")
        return
        
    pcap_file = sys.argv[1]
    out_file = sys.argv[2] if len(sys.argv) > 2 else "session_report.html"
    
    print(f"Parsing {pcap_file}...")
    data = parse_pcap_to_json(pcap_file)
    
    print(f"Generating {out_file} with {len(data)} packets...")
    
    json_data = json.dumps(data)
    html = HTML_TEMPLATE.replace("__PACKET_DATA__", json_data)
    html = html.replace("sim_traffic.pcap", os.path.basename(pcap_file))
    
    with open(out_file, 'w') as f:
        f.write(html)
    
    print("Done.")

if __name__ == "__main__":
    main()
