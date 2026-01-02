#!/usr/bin/env python3
"""
Test script to control rear-view mirror crop offsets via UDP
Sends left/right crop offset values to CARLA spectator pawn

Usage: python test_udp_mirror_control.py
"""

import socket
import time

# UDP configuration
UDP_IP = "127.0.0.1"
UDP_PORT = 8888

# Create UDP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

print(f"Sending UDP mirror control to {UDP_IP}:{UDP_PORT}")
print("Press Ctrl+C to stop")

try:
    # Sweep animation: move mirrors from left to right and back
    step = 0
    direction = 1
    
    while True:
        # Calculate offset (0.0 to 1.0)
        progress = step / 100.0
        
        # Create message in format: "left:X,right:Y"
        # Both mirrors move together in this example
        message = f"left:{progress:.2f},right:{progress:.2f}"
        
        # Send UDP packet
        sock.sendto(message.encode(), (UDP_IP, UDP_PORT))
        print(f"\rSent: {message}", end="", flush=True)
        
        # Update step
        step += direction
        if step >= 100:
            direction = -1
        elif step <= 0:
            direction = 1
        
        # 20Hz update rate
        time.sleep(0.05)
        
except KeyboardInterrupt:
    print("\nStopping...")
finally:
    sock.close()
    print("UDP socket closed")
