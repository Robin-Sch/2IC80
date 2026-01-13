#!/usr/bin/python3
import dbus
import dbus.service
import dbus.mainloop.glib
from bluetooth import *
from tools.utils.logs import logger
from tools.emulator.kb_device import KbDevice

class KbServer(dbus.service.Object):

    def __init__(self, mac_address: str):
        logger.status(msg="Setting up service...")
        # set up as a dbus service
        bus_name = dbus.service.BusName(
            "org.trapedev.btkbservice", bus=dbus.SystemBus())
        dbus.service.Object.__init__(
            self, bus_name, "/org/trapedev/btkbservice")
        # create and setup our device
        self.device = KbDevice(mac_address=mac_address)
        # start to try to connect
        self.device.connect()

    @dbus.service.method('org.trapedev.btkbservice', in_signature='yay')
    def send_keys(self, modifier_byte, keys):
        logger.notice(keys)
        state = [ 0xA1, 1, 0, 0, 0, 0, 0, 0, 0, 0 ]
        state[2] = int(modifier_byte)
        count = 4
        for key_code in keys:
            if(count < 10):
                state[count] = int(key_code)
            count += 1
        self.device.send_string(state)
