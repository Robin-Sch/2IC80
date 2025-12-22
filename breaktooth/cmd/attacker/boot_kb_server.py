#!/usr/bin/python3
import sys
from gi.repository import GLib
from dbus.mainloop.glib import DBusGMainLoop
from tools.emulator.kb_server import KbServer
from bluetooth import *

if len(sys.argv) < 2:
    print("You should have to set the target mac address of bluetooth device.")
    sys.exit(1)

try:
    if not os.geteuid() == 0:
        sys.exit("Only root can run this script")

    DBusGMainLoop(set_as_default=True)
    myservice = KbServer(mac_address=sys.argv[1])
    loop = GLib.MainLoop()
    loop.run()
except KeyboardInterrupt:
    sys.exit()