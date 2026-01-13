#!/usr/bin/python3

from tools.utils.logs import logger
from tools.emulator.kb_client import KbClient

kb = KbClient()
logger.success("Start up keyboard and you can inject")
kb.event_loop()