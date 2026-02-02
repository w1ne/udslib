import socket
import struct
import subprocess
import time
import os
import signal

# VCAN Packet Format: ID (4 bytes), Data (8 bytes), Len (1 byte)
VCAN_FORMAT = "<I8sB"

class UDSTestClient:
    def __init__(self, target_ip="127.0.0.1", port=5000):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(2.0)
        self.target = (target_ip, port)
        self.tx_id = 0x7E0 # Client TX -> Server RX
        self.rx_id = 0x7E8 # Server TX -> Client RX

    def send_can(self, can_id, data):
        data = data.ljust(8, b'\x00')
        pkt = struct.pack(VCAN_FORMAT, can_id, data, len(data.strip(b'\x00')) or len(data))
        # Note: strip(b'\x00') might be wrong for data ending in 00, but for simple tests its ok.
        # Let's fix it to use explicit length.
        # Actually, let's just use the real length passed.
        pkt = struct.pack(VCAN_FORMAT, can_id, data[:8], len(data))
        self.sock.sendto(pkt, self.target)

    def recv_can(self):
        try:
            pkt, _ = self.sock.recvfrom(struct.calcsize(VCAN_FORMAT))
            can_id, data, length = struct.unpack(VCAN_FORMAT, pkt)
            return can_id, data[:length]
        except socket.timeout:
            return None, None

    def uds_request(self, payload):
        # ISO-TP SF or MF implementation
        if len(payload) <= 7:
            isotp_frame = bytes([len(payload)]) + payload
            print(f"  [TX] SF: {isotp_frame.hex()}")
            self.send_can(self.tx_id, isotp_frame)
        else:
            # First Frame
            ff = struct.pack(">H", 0x1000 | len(payload)) + payload[:6]
            print(f"  [TX] FF: {ff.hex()}")
            self.send_can(self.tx_id, ff)
            
            # Wait for Flow Control
            cid, data = self.recv_can()
            print(f"  [RX] FC: {data.hex() if data else 'TIMEOUT'}")
            if not cid or (data[0] & 0xF0) != 0x30:
                print("  [ERR] Flow Control failure")
                return None
            
            # Consecutive Frames
            pos = 6
            idx = 1
            while pos < len(payload):
                chunk = payload[pos:pos+7]
                cf = bytes([0x20 | (idx & 0x0F)]) + chunk
                print(f"  [TX] CF: {cf.hex()}")
                self.send_can(self.tx_id, cf)
                pos += 7
                idx += 1
        
        # Wait for response
        start = time.time()
        full_resp = bytearray()
        expected_len = 0
        
        while time.time() - start < 2.0:
            cid, data = self.recv_can()
            if cid == self.rx_id:
                print(f"  [RX] DATA: {data.hex()}")
                if (data[0] & 0xF0) == 0x00: # Single Frame
                    return list(data[1:1 + (data[0] & 0x0F)])
                elif (data[0] & 0xF0) == 0x10: # First Frame
                    expected_len = ((data[0] & 0x0F) << 8) | data[1]
                    full_resp.extend(data[2:])
                    # Send Flow Control
                    self.send_can(self.tx_id, bytes([0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]))
                elif (data[0] & 0xF0) == 0x20: # Consecutive Frame
                    full_resp.extend(data[1:])
                    if len(full_resp) >= expected_len:
                        return list(full_resp[:expected_len])
                elif (data[0] & 0xF0) == 0x7F and data[3] == 0x78:
                    print("  [MSG] Response Pending...")
                    start = time.time() # Reset timeout
        print(f"  [ERR] Request timed out or invalid response")
        return None

def test_full_sequence():
    # 1. Start Simulator
    print("[TEST] Starting ECU Simulator...")
    search_paths = [
        "./build_quality/examples/host_sim/uds_host_sim", 
        "./build/examples/host_sim/uds_host_sim",
        "./examples/host_sim/uds_host_sim"
    ]
    sim_path = next((p for p in search_paths if os.path.exists(p)), None)
    if not sim_path:
        print("[ERROR] Simulator not found")
        exit(1)
    
    sim_proc = subprocess.Popen([sim_path, "5001"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(1)
    client = UDSTestClient(port=5001)
    
    try:
        # Step 1: Default -> Extended Session (0x10)
        print("[TEST] 1. Diagnostic Session Control (10 03)")
        assert client.uds_request(bytes([0x10, 0x03])) == [0x50, 0x03, 0x00, 0x32, 0x01, 0xF4]
        
        # Step 2: Authentication (0x29)
        print("[TEST] 2. Authentication (29 02 - Certificate)")
        assert client.uds_request(bytes([0x29, 0x02, 0xDE, 0xAD])) == [0x69, 0x02, 0x01]
        
        # Step 3: Security Access (0x27)
        print("[TEST] 3. Security Access (27 01/02)")
        resp = client.uds_request(bytes([0x27, 0x01]))
        assert resp == [0x67, 0x01, 0xDE, 0xAD, 0xBE, 0xEF]
        assert client.uds_request(bytes([0x27, 0x02, 0xDF, 0xAE, 0xBF, 0xF0])) == [0x67, 0x02]
        
        # Step 4: Data Services (0x22/0x2E)
        print("[TEST] 4. Data Services (22 F1 90 / 2E 01 23)")
        # 22 F1 90 - VIN (MF Response)
        resp = client.uds_request(bytes([0x22, 0xF1, 0x90]))
        assert bytes(resp[3:]).decode() == "UDSLIB_SIM_001"
        # 2E 01 23 - Write Customer Name
        data_to_write = b"TEST_CLIENT_001".ljust(16, b"\x00")
        assert client.uds_request(bytes([0x2E, 0x01, 0x23]) + data_to_write) == [0x6E, 0x01, 0x23]
        
        # Step 5: Maintenance & DTC (0x14/0x19/0x85)
        print("[TEST] 5. Maintenance / DTC Services (19 01 / 85 01 / 14 FF FF FF)")
        assert client.uds_request(bytes([0x19, 0x01, 0xFF])) == [0x59, 0x01, 0x01, 0x01, 0x00, 0x02]
        assert client.uds_request(bytes([0x85, 0x01])) == [0xC5, 0x01]
        assert client.uds_request(bytes([0x14, 0xFF, 0xFF, 0xFF])) == [0x54]
        
        # Step 6: Flash Engine (0x31/0x34/0x36/0x37)
        print("[TEST] 6. Flash Engine (31 / 34 / 36 / 37)")
        # 31 01 FF 00 (Erase)
        assert client.uds_request(bytes([0x31, 0x01, 0xFF, 0x00])) == [0x71, 0x01, 0xFF, 0x00, 0x00]
        # 34 (Download)
        assert client.uds_request(bytes([0x34, 0x00, 0x44, 0x11, 0x22, 0x33, 0x44, 0x00, 0x00, 0x10, 0x00])) == [0x74, 0x20, 0x04, 0x00]
        # 36 (Transfer)
        assert client.uds_request(bytes([0x36, 0x01, 0xAA, 0xBB])) == [0x76, 0x01]
        # 37 (Exit)
        assert client.uds_request(bytes([0x37])) == [0x77]
        
        # Step 7: Comm Control (0x28) & Tester Present (0x3E)
        print("[TEST] 7. Comm Control & Tester Present")
        assert client.uds_request(bytes([0x28, 0x00, 0x01])) == [0x68, 0x00]
        assert client.uds_request(bytes([0x3E, 0x00])) == [0x7E, 0x00]
        
        # Step 8: ECU Reset (0x11)
        print("[TEST] 8. ECU Reset (11 01)")
        assert client.uds_request(bytes([0x11, 0x01])) == [0x51, 0x01]

        # Step 9: Memory Services (23 / 3D)
        print("[TEST] 9. Memory Services (23 / 3D)")
        # 3D 12 00 10 01 AB (Write addr 0x0010 size 1 byte val 0xAB)
        # Format 0x12 -> Size=1 byte, Addr=2 bytes
        # 3D 12 00 10 01 AB
        assert client.uds_request(bytes([0x3D, 0x12, 0x00, 0x10, 0x01, 0xAB])) == [0x7D, 0x12]
        
        # 23 12 00 10 01 (Read addr 0x0010 size 1 byte)
        assert client.uds_request(bytes([0x23, 0x12, 0x00, 0x10, 0x01])) == [0x63, 0xAB]

        print("\n--- ALL 15 SERVICES (Including Memory) VERIFIED VIA INTEGRATION ---")

    finally:
        os.kill(sim_proc.pid, signal.SIGTERM)
        sim_proc.wait()

if __name__ == "__main__":
    test_full_sequence()
