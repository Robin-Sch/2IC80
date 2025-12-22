import time
import subprocess
from tools.utils.logs import logger

def sleep_detector(bt_addr):
    cmd = ["sudo", "l2ping", "-c", "1", "-f", bt_addr]
    logger.status(f"Sending a l2ping echo request to {bt_addr} to detect Bluetooth sleep mode.")
    
    while True:
        ret = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if ret.returncode == 0:
            logger.success("Detect the sleep mode !!")
            break
        logger.status("Not in sleep, try again.")
        time.sleep(1)
