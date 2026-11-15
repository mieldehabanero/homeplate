"""Host USB transport for Docker: fixed 115200 baud, local reset per connection.

Port 14421 enters the ROM loader; port 14422 boots the application. No firmware
or configuration is read or written here. Run with an existing host pyserial.
"""

import argparse
import select
import socket
import time

import serial


def serve(device, address):
    with serial.Serial(device, 115200, timeout=0.05, exclusive=True) as usb:
        usb.dtr = False
        usb.rts = False
        listeners = []
        try:
            for port in (14421, 14422):
                listener = socket.socket()
                listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                listener.bind((address, port))
                listener.listen(1)
                listeners.append(listener)
            print("USB bridge ready: 14421 bootloader, 14422 application", flush=True)
            while True:
                readable, _, _ = select.select(listeners, [], [])
                listener = readable[0]
                with listener.accept()[0] as client:
                    client.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                    bootloader = listener.getsockname()[1] == 14421
                    usb.dtr = False
                    usb.rts = True
                    time.sleep(0.1)
                    usb.reset_input_buffer()
                    usb.dtr = bootloader
                    usb.rts = False
                    time.sleep(0.1)
                    usb.dtr = False
                    print("Connected: " + ("bootloader" if bootloader else "application"), flush=True)
                    try:
                        while True:
                            incoming, _, _ = select.select([client], [], [], 0.001)
                            if incoming:
                                data = client.recv(65536)
                                if not data:
                                    break
                                usb.write(data)
                            if usb.in_waiting:
                                client.sendall(usb.read(usb.in_waiting))
                    except (ConnectionError, BrokenPipeError):
                        pass
                    print("Disconnected", flush=True)
        finally:
            for listener in listeners:
                listener.close()
            usb.dtr = False
            usb.rts = False


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("device")
    parser.add_argument("--bind", default="127.0.0.1")
    args = parser.parse_args()
    serve(args.device, args.bind)
