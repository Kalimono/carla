"""
Simple UDP mode switcher for CarlaInteractiveMirror
Press keys to switch modes instantly.
"""

import socket
# import keyboard
import time

UDP_IP = "127.0.0.1"
UDP_PORT = 9876

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)



print("Mirror Mode Switcher")
print("=" * 50)
print("Press '1' for Pan Mode")
print("Press '2' for ZoomOut Mode")
print("Press 'ESC' to exit")
print("=" * 50)

def switch_to_pan():
    sock.sendto(b"mode:0", (UDP_IP, UDP_PORT))
    print("[Mode] Switched to Pan Mode")

def switch_to_zoomout():
    sock.sendto(b"mode:1", (UDP_IP, UDP_PORT))
    print("[Mode] Switched to ZoomOut Mode")
switch_to_zoomout()
# Register hotkeys
# keyboard.add_hotkey('1', switch_to_pan)
# keyboard.add_hotkey('2', switch_to_zoomout)

print("\nReady! Press keys to switch modes...")

# try:
#     # keyboard.wait('esc')
# except KeyboardInterrupt:
#     pass

print("\nExiting...")
sock.close()
