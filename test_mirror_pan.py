"""
Test script for CarlaInteractiveMirror UDP pan control.
Sends pan values that bounce between 0.0 and 1.0 continuously.
"""

import socket
import time
import math

# Configuration
UDP_IP = "127.0.0.1"  # localhost
UDP_PORT = 9876       # Default port from CarlaInteractiveMirror
UPDATE_RATE = 30      # Updates per second
BOUNCE_SPEED = 0.5    # How fast to bounce (lower = slower)

def send_pan_command(sock, pan_value):
    """Send a pan command via UDP."""
    # Clamp value to 0.0-1.0 range
    pan_value = max(0.0, min(1.0, pan_value))
    
    # Format: "pan:0.5"
    command = f"pan:{pan_value:.3f}"
    sock.sendto(command.encode('utf-8'), (UDP_IP, UDP_PORT))
    print(f"Sent: {command}")

def main():
    # Create UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    
    print(f"Starting mirror pan test on {UDP_IP}:{UDP_PORT}")
    print(f"Press Ctrl+C to stop")
    print("-" * 50)
    
    start_time = time.time()
    frame_duration = 1.0 / UPDATE_RATE
    
    try:
        while True:
            frame_start = time.time()
            
            # Calculate pan value using sine wave for smooth bouncing
            # Starts at 1.0 (default), bounces to 0.0, then back to 1.0
            elapsed = time.time() - start_time
            pan_value = (math.sin(elapsed * BOUNCE_SPEED + math.pi/2) + 1.0) / 2.0
            
            # Send the command
            send_pan_command(sock, pan_value)
            
            # Sleep to maintain update rate
            frame_time = time.time() - frame_start
            sleep_time = max(0, frame_duration - frame_time)
            time.sleep(sleep_time)
            
    except KeyboardInterrupt:
        print("\n" + "-" * 50)
        print("Test stopped")
    finally:
        sock.close()

if __name__ == "__main__":
    main()
