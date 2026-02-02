import ctypes
import os

# Load the shared library
# Assuming it's in the build directory relative to the repository root
_lib_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "build", "libudslib.so"))
if not os.path.exists(_lib_path):
    raise ImportError(f"UDSLib shared library not found at {_lib_path}. Please run 'make udslib_shared' in the build directory.")
_lib = ctypes.CDLL(_lib_path)

# --- Type Definitions ---
# ... (existing types) ...

# typedef void (*uds_response_cb)(struct uds_ctx *ctx, uint8_t sid, const uint8_t *data, uint16_t len);
uds_response_cb = ctypes.CFUNCTYPE(None, ctypes.POINTER(UdsCtx), uint8_t, ctypes.POINTER(uint8_t), uint16_t)

# --- Configuration & Context Structures ---

class UdsConfig(ctypes.Structure):
    _fields_ = [
        ("ecu_address", uint8_t),
        ("get_time_ms", uds_get_time_fn),
        ("fn_log", uds_log_fn),
        ("fn_tp_send", uds_tp_send_fn),
        ("p2_ms", uint16_t),
        ("p2_star_ms", uint32_t),
        ("fn_reset", ctypes.c_void_p),
        ("fn_comm_control", ctypes.c_void_p),
        ("fn_security_seed", ctypes.c_void_p),
        ("fn_security_key", ctypes.c_void_p),
        ("rx_buffer", ctypes.POINTER(uint8_t)),
        ("rx_buffer_size", uint16_t),
        ("tx_buffer", ctypes.POINTER(uint8_t)),
        ("tx_buffer_size", uint16_t),
        ("strict_compliance", bool_t),
        ("log_level", uint8_t),
        # did_table and other advanced fields would go here
    ]

# ... (UdsCtx fields update) ...
UdsCtx._fields_ = [
    ("config", ctypes.POINTER(UdsConfig)),
    ("state", uint8_t),
    ("active_session", uint8_t),
    ("security_level", uint8_t),
    ("last_msg_time", uint32_t),
    ("p2_timer_start", uint32_t),
    ("p2_msg_pending", bool_t),
    ("p2_star_active", bool_t),
    ("response_pending", bool_t),
    ("client_cb", ctypes.c_void_p),
    ("pending_sid", uint8_t),
    ("comm_state", uint8_t),
    ("suppress_pos_resp", bool_t),
    ("p2_ms", uint16_t),
    ("p2_star_ms", uint32_t),
    ("flash_sequence", uint8_t),
]

# --- API Function Prototypes ---
_lib.uds_init.argtypes = [ctypes.POINTER(UdsCtx), ctypes.POINTER(UdsConfig)]
_lib.uds_init.restype = ctypes.c_int

_lib.uds_process.argtypes = [ctypes.POINTER(UdsCtx)]
_lib.uds_process.restype = None

_lib.uds_input_sdu.argtypes = [ctypes.POINTER(UdsCtx), ctypes.POINTER(uint8_t), uint16_t]
_lib.uds_input_sdu.restype = None

_lib.uds_client_request.argtypes = [ctypes.POINTER(UdsCtx), uint8_t, ctypes.POINTER(uint8_t), uint16_t, uds_response_cb]
_lib.uds_client_request.restype = ctypes.c_int

# --- Pythonic Wrapper Class ---

class UDSLib:
    def __init__(self, config_dict):
        self.ctx = UdsCtx()
        self.config = UdsConfig()
        
        self._get_time = uds_get_time_fn(config_dict['get_time_ms'])
        self._tp_send = uds_tp_send_fn(config_dict['fn_tp_send'])
        self._log = uds_log_fn(config_dict.get('fn_log', lambda lv, msg: None))
        self._resp_cbs = [] # Keep refs to response callbacks
        
        self.rx_buf = (uint8_t * 4096)()
        self.tx_buf = (uint8_t * 4096)()
        
        self.config.get_time_ms = self._get_time
        self.config.fn_tp_send = self._tp_send
        self.config.fn_log = self._log
        self.config.rx_buffer = ctypes.cast(self.rx_buf, ctypes.POINTER(uint8_t))
        self.config.rx_buffer_size = 4096
        self.config.tx_buffer = ctypes.cast(self.tx_buf, ctypes.POINTER(uint8_t))
        self.config.tx_buffer_size = 4096
        self.config.strict_compliance = config_dict.get('strict_compliance', False)
        self.config.log_level = config_dict.get('log_level', 1) # Default INFO
        
        res = _lib.uds_init(ctypes.byref(self.ctx), ctypes.byref(self.config))
        if res != 0:
            raise Exception(f"UDSLib init failed with code {res}")

    def process(self):
        _lib.uds_process(ctypes.byref(self.ctx))

    def input_sdu(self, data):
        data_arr = (uint8_t * len(data))(*data)
        _lib.uds_input_sdu(ctypes.byref(self.ctx), data_arr, len(data))

    def client_request(self, sid, data, callback=None):
        data_arr = (uint8_t * len(data))(*data) if data else None
        
        c_callback = None
        if callback:
            def _internal_cb(ctx, sid, data_ptr, length):
                payload = [data_ptr[i] for i in range(length)]
                callback(sid, payload)
            c_callback = uds_response_cb(_internal_cb)
            self._resp_cbs.append(c_callback) # Prevent GC
            
        return _lib.uds_client_request(ctypes.byref(self.ctx), sid, data_arr, len(data) if data else 0, c_callback)

# --- Simple Demo (if run as script) ---
if __name__ == "__main__":
    import time
    
    def log_cb(level, msg):
        print(f"[C-LOG] {level}: {msg.decode('utf-8')}")
        
    def tp_send_cb(ctx, data, length):
        payload = [data[i] for i in range(length)]
        print(f"[PY-TP] Sending SDU: {bytes(payload).hex(' ').upper()}")
        return 0

    uds = UDSLib({
        'get_time_ms': lambda: int(time.time() * 1000),
        'fn_tp_send': tp_send_cb,
        'fn_log': log_cb
    })
    
    print("Feeding TesterPresent...")
    uds.input_sdu([0x3E, 0x00])
    uds.process()
