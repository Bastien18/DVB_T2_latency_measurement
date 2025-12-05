import socket
import time
import RPi.GPIO as GPIO
import signal 
import sys

# Socket creation for TSDuck receiving
udp_ip = "127.0.0.1"
udp_port = 9006
TS_PACKET_SIZE = 188
PID_TO_WATCH = 0x0404

# Get PID from a TS packet
def get_pid(pkt: bytes) -> int | None:
    """Extract PID from a 188-byte MPEG-TS packet."""
    # pkt[0] should always be 0x47 from tsp
    print(f"PID is {((pkt[1] & 0x1F) << 8) | pkt[2]}\n")
    return ((pkt[1] & 0x1F) << 8) | pkt[2]


def main():
    print("Wait 10sec for tsp to be launched")
    time.sleep(10)

    # Create UDP socket and bind to 127.0.0.1:9006
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((udp_ip, udp_port))

    print(f"Listening on 127.0.0.1:9006 for PID 0x{PID_TO_WATCH:04X}...")

    # Recieve first packet as starting point
    data, addr = sock.recvfrom(2048)  

    while True:
        start = time.perf_counter_ns()
        data, addr = sock.recvfrom(2048)  # receive one UDP datagram
        end = time.perf_counter_ns()
          
        print(
            f"PID 0x{PID_TO_WATCH:04X} first received at {end/1000}us "
            f"GPIO edge trigger at {start/1000}us"
            f"Delta is {(end-start)/1000}us"
        )

if __name__ == "__main__":
    main()
    
