#!/usr/bin/python3

import dbus
import dbus.service
import dbus.mainloop.glib
import time
import pyudev
import tools.emulator.rules.keymap as keymap
from evdev import *
from tools.utils.logs import logger

class KbClient():
    
    def __init__(self):
        self.state = [
            0xA1, # this is an input report
            0x01, # Usage report = Keyboard
            # Bit array for Modifier keys
            [
                0,  # Right GUI - Windows Key
                0,  # Right ALT
                0,  # Right Shift
                0,  # Right Control
                0,  # Left GUI
                0,  # Left ALT
                0,  # Left Shift
                0,  # Left Control
            ],
            0x00,  # Vendor reserved
            0x00,  # rest is space for 6 keys
            0x00,
            0x00,
            0x00,
            0x00,
            0x00
        ]
        logger.status("Setting up DBus Client...")
        self.bus = dbus.SystemBus()
        self.btkservice = self.bus.get_object(
            bus_name="org.trapedev.btkbservice",
            object_path="/org/trapedev/btkbservice"
        )
        self.iface = dbus.Interface(self.btkservice, "org.trapedev.btkbservice")
        logger.status("Waiting for keyboard...")
        self.dev = self.get_keyboard_device()
    
    def get_keyboard_device(self):
        context = pyudev.Context()
        devs = context.list_devices(subsystem="input")
        for dev in devs:
            if "ID_INPUT_KEYBOARD" in dev.properties and dev.device_node is not None:
                logger.status("Found keyboard: " + dev.device_node)
                return InputDevice(dev.device_node)
        logger.error("Keyboard not found, waiting 3 seconds and retrying...")
        time.sleep(3)
        return self.get_keyboard_device()
    
    def change_state(self, event):
        evdev_code = ecodes.KEY[event.code]
        modkey_element = keymap.modkey(evdev_code)

        if modkey_element > 0:
            if self.state[2][modkey_element] == 0:
                self.state[2][modkey_element] = 1
            else:
                self.state[2][modkey_element] = 0
        else:
            # Get the keycode of the key
            hex_key = keymap.convert(ecodes.KEY[event.code])
            # Loop through elements 4 to 9 of the inport report structure
            for i in range(4, 10):
                if self.state[i] == hex_key and event.value == 0:
                    # Code 0 so we need to depress it
                    self.state[i] = 0x00
                elif self.state[i] == 0x00 and event.value == 1:
                    # if the current space if empty and the key is being pressed
                    self.state[i] = hex_key
                    break

    # poll for keyboard events
    def event_loop(self):
        for event in self.dev.read_loop():
            logger.notice(event)
            # only bother if we hit a key and its an up or down event
            if event.type == ecodes.EV_KEY and event.value < 2:
                self.change_state(event)
                self.send_input()

    # forward keyboard events to the dbus service
    def send_input(self):
        bin_str = ""
        element = self.state[2]
        for bit in element:
            bin_str += str(bit)
        a = self.state
        self.iface.send_keys(int(bin_str, 2), self.state[4:10])