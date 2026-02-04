#!/usr/bin/env python3
# Copyright (c) 2026 Andrii Shylenko
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

import struct
import sys
import os

# --- Colors ---
RESET = "\033[0m"
BOLD = "\033[1m"
GREEN = "\033[32m"
BLUE = "\033[34m"
CYAN = "\033[36m"
MAGENTA = "\033[35m"
YELLOW = "\033[33m"
RED = "\033[31m"

def decode_isotp(can_id, payload):
    if not payload:
        return f"{RED}Empty{RESET}"

    pci = payload[0]
    pci_type = (pci & 0xF0) >> 4
    
    # FD Logic (Heuristic for demo, based on DLC > 8)
    is_fd = len(payload) > 8
    fd_str = f"{BOLD}FD{RESET}" if is_fd else "  "

    info = ""
    details = ""

    if pci_type == 0x0: # SF
        slen = pci & 0x0F
        if slen == 0: # CAN-FD SF
            if len(payload) > 1:
                slen = payload[1]
                details = f"Len={slen} Data={payload[2:].hex().upper()}"
            else:
                details = "Invalid FD SF"
        else:
            details = f"Len={slen} Data={payload[1:1+slen].hex().upper()}"
        info = f"{GREEN}[SF] Single Frame{RESET}"

    elif pci_type == 0x1: # FF
        slen = ((payload[0] & 0x0F) << 8) | payload[1]
        info = f"{BLUE}[FF] First Frame{RESET}"
        details = f"TotalLen={slen} Data={payload[2:].hex().upper()}..."

    elif pci_type == 0x2: # CF
        sn = pci & 0x0F
        info = f"{CYAN}[CF] Consecutive Frame{RESET}"
        details = f"SN={sn} Data={payload[1:].hex().upper()}"

    elif pci_type == 0x3: # FC
        fs = pci & 0x0F
        info = f"{MAGENTA}[FC] Flow Control{RESET}"
        fs_map = {0: "CTS", 1: "WAIT", 2: "OVFL"}
        fs_str = fs_map.get(fs, f"UNK({fs})")
        bs = payload[1] if len(payload) > 1 else 0
        st = payload[2] if len(payload) > 2 else 0
        details = f"Flag={fs_str} BS={bs} STmin={st}"

    else:
        info = f"{RED}Unknown PCI{RESET}"
        details = payload.hex().upper()

    return f"ID:{can_id:03X} {fd_str} {info:<22} {details}"

def parse_pcap(filename):
    print(f"Reading {filename}...")
    try:
        f = open(filename, 'rb')
    except FileNotFoundError:
        print(f"{RED}File not found: {filename}{RESET}")
        return

    with f:
        # Global Header
        header = f.read(24)
        if len(header) < 24:
            return

        magic, maj, min, zone, sig, snap, net = struct.unpack('<IHHIIII', header)
        
        # net = LinkType
        # 147 = DLT_USER0 (Our Proxy)
        # 0 = DLT_NULL (Loopback)
        # 1 = DLT_EN10MB (Ethernet)
        # 113 = DLT_LINUX_SLL
        
        is_user0 = (net == 147)

        while True:
            # Packet Header (16 bytes)
            pkthdr = f.read(16)
            if len(pkthdr) < 16:
                break
            
            ts_sec, ts_usec, incl_len, orig_len = struct.unpack('<IIII', pkthdr)
            
            # Packet Data
            pkt_data = f.read(incl_len)
            
            if is_user0:
                # Direct VCAN payload
                # VCAN Packet: ID(4) + Data(64) + Len(1).
                # Actually our proxy captures whatever socket receives.
                # uds_host_sim sends vcan_packet_t.
                
                # We need to extract ID and Payload from this struct.
                # Struct layout: ID(4), Data(64), Len(1) -> Total 69 bytes. 
                # Wait, struct is packed? #pragma pack(push, 1) in main.c? Yes.
                # So offset 0 = ID.
                # offset 4 = Data.
                # offset 68 = Len.
                
                # print(f"DEBUG: PktLen={len(pkt_data)} Raw={pkt_data.hex()}") 
                
                if len(pkt_data) >= 69:
                     can_id_raw = pkt_data[0:4]
                     can_id = struct.unpack('<I', can_id_raw)[0]
                     
                     data_len = pkt_data[68] # Last byte
                     
                     if data_len > 64: data_len = 64 # Safety
                     
                     payload = pkt_data[4 : 4 + data_len]
                     
                     # Direction? Pcap doesn't store direction in payload. 
                     # But we can infer from ID. 0x7E0 (Req), 0x98 (Res).
                     # Wait, 0x7E8 is server response.
                     
                     arrow = "-->"
                     if can_id == 0x7E8: arrow = "<--"
                     
                     print(f"{arrow} {decode_isotp(can_id, payload)}")
                else:
                     print(f"WARN: Short packet len={len(pkt_data)}")
            
            else:
                # Previous Heuristic Logic for System Capture (tcpdump)
                try:
                    idx_client = pkt_data.find(b'\xE0\x07\x00\x00')
                    idx_server = pkt_data.find(b'\xE8\x07\x00\x00')
                    
                    offset = -1
                    match_id = 0
                    
                    if idx_client != -1:
                        offset = idx_client
                        match_id = 0x7E0
                    elif idx_server != -1:
                        offset = idx_server
                        match_id = 0x7E8
                        
                    if offset != -1:
                        data_offset = offset + 4
                        # Assume struct data is there. We read from end? 
                        # Or just read 64 bytes? 
                        # Let's try reading the length byte at offset + 68
                        len_offset = offset + 4 + 64
                        if len(pkt_data) > len_offset:
                            dlc = pkt_data[len_offset]
                            actual_payload = pkt_data[data_offset : data_offset + dlc]
                            
                            arrow = "-->" if match_id == 0x7E0 else "<--"
                            print(f"{arrow} {decode_isotp(match_id, actual_payload)}")
                except:
                    pass

def main():
    if len(sys.argv) < 2:
        print("Usage: read_pcap.py <file.pcap>")
        return
    parse_pcap(sys.argv[1])

if __name__ == "__main__":
    main()
