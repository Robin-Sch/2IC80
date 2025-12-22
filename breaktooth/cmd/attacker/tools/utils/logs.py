import logging

logging.basicConfig(
  level=logging.DEBUG,
  format='[%(asctime)s.%(msecs)03d] %(message)s',
  datefmt="%Y-%m-%d %H:%M:%S"
)

class Logger:
  def status(self, msg):
    logging.info(f"\033[0;33m{msg}\033[0m")
    
  def success(self, msg):
    logging.info(f"\033[0;32m{msg}\033[0m")
    
  def error(self, msg):
    logging.error(f"\033[0m{msg}\033[0m")
    
  def debug(self, msg):
    logging.debug(f"\033[0m{msg}\033[0m")
    
  def notice(self, msg):
    logging.info(f"\033[0;35m{msg}\033[0m")
    
  def info(self, msg):
    logging.info(msg)

logger = Logger()