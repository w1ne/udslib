#!/usr/bin/env python3
# Copyright (c) 2026 Andrii Shylenko
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

import socket
import select
import struct
import time
import sys

# PCAP Global Header
# Magic(4), Maj(2), Min(2), Zone(4), Sig(4), Snap(4), Net(4)
# Net=147 (DLT_USER0) - Custom payload
PCAP_GLOBAL_HEADER = struct.pack('<IHHIIII', 0xa1b2c3d4, 2, 4, 0, 0, 65535, 147)

def main():
    if len(sys.argv) < 4:
        print("Usage: proxy.py <listen_port> <target_ip> <target_port> <pcap_file>")
        return

    listen_port = int(sys.argv[1])
    target_ip = sys.argv[2]
    target_port = int(sys.argv[3])
    pcap_filename = sys.argv[4]

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('127.0.0.1', listen_port))
    
    # We need a way to send back to client, but UDP is connectionless.
    # We will assume client sends to us, we forward to server.
    # Server replies to us, we forward to last known client address.
    
    client_addr = None
    
    # To talk to server
    sock_server = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    log_file = open(pcap_filename, 'wb')
    log_file.write(PCAP_GLOBAL_HEADER)
    log_file.flush()

    print(f"Proxy listening on {listen_port}, forwarding to {target_ip}:{target_port}")
    print(f"Capturing to {pcap_filename} (DLT_USER0)")

    inputs = [sock, sock_server]

    try:
        while True:
            readable, _, _ = select.select(inputs, [], [])
            ts = time.time()
            ts_sec = int(ts)
            ts_usec = int((ts - ts_sec) * 1000000)

            for s in readable:
                if s is sock:
                    # Received from Client
                    data, addr = s.recvfrom(4096)
                    client_addr = addr
                    # Forward to Server
                    sock_server.sendto(data, (target_ip, target_port))
                    
                    # Log (Client -> Server)
                    # We can prepend a 1-byte direction or just dump data. 
                    # DLT_USER0 is raw data. 
                    # Let's save just the data.
                    # PktHeader: Sec(4)+Usec(4)+Incl(4)+Orig(4)
                    pkthdr = struct.pack('<IIII', ts_sec, ts_usec, len(data), len(data))
                    log_file.write(pkthdr)
                    log_file.write(data)
                    log_file.flush()
                    # print(f"C->S {len(data)} bytes")

                elif s is sock_server:
                    # Received from Server
                    data, addr = s.recvfrom(4096)
                    # Forward to Client
                    if client_addr:
                        sock.sendto(data, client_addr)
                        
                    # Log (Server -> Client)
                    pkthdr = struct.pack('<IIII', ts_sec, ts_usec, len(data), len(data))
                    log_file.write(pkthdr)
                    log_file.write(data)
                    log_file.flush()
                    # print(f"S->C {len(data)} bytes")
                    
    except KeyboardInterrupt:
        print("Stopping proxy...")
        log_file.close()

if __name__ == "__main__":
    main()
