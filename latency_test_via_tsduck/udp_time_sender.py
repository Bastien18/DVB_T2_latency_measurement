import socket
import time
import signal
import sys

udp_ip = "127.0.0.1"
udp_port = 9005

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

def signal_handler(sig, frame):
    sock.close()
    sys.exit(0)

signal.signal(signal.SIGINT, signal_handler)

while True:
    # TODO: make it MONOTONIC: testbed is in the smae PC
    timestamp = "time=" + str(int(time.time() * 1000))
    sock.sendto(timestamp.encode(), (udp_ip, udp_port))
    time.sleep(0.01)
