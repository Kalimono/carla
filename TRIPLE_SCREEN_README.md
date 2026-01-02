# CARLA Triple-Screen Panoramic View with UDP-Controlled Mirrors

## Overview
Custom spectator pawn implementation for CARLA that provides:
- Triple-screen panoramic view (left 90°, center forward, right 90°)
- Rear-view mirrors on left/right displays (200x600 pixels)
- Real-time UDP control of mirror crop offsets
- **Asynchronous UDP receiver to prevent frame rate interference**

## Display Layout
```
┌─────────────┬─────────────┬─────────────┐
│   LEFT      │   CENTER    │   RIGHT     │
│   90° ←     │   FORWARD   │   90° →     │
│             │             │             │
│  ┌────┐     │             │     ┌────┐  │
│  │REAR│     │             │     │REAR│  │
│  │VIEW│     │             │     │VIEW│  │
│  └────┘     │             │     └────┘  │
└─────────────┴─────────────┴─────────────┘
     33.3%         33.3%         33.3%
```

## Screen Resolution
- Target: Single 1920x1080 display
- Each view: ~640x1080 pixels
- Mirrors: 200x600 pixels each
- Render targets: Fixed 1920x1080 (prevents aspect ratio distortion)

## UDP Communication

### Protocol
- **Port**: 8888
- **Format**: `left:X,right:Y` (plain text)
- **Values**: 0.0 to 1.0 (crop offset along horizontal axis)
- **Example**: `left:0.5,right:0.7`

### Asynchronous Architecture
The UDP receiver runs in a **separate thread** to prevent blocking the game thread:

1. **FUdpMirrorReceiver** (FRunnable implementation):
   - Runs in dedicated thread (`UdpMirrorReceiverThread`)
   - Uses **blocking socket** with 100ms timeout (efficient wait)
   - Thread-safe data access via `FCriticalSection`
   - Continuously receives UDP packets in background

2. **Game Thread Integration**:
   - Polls receiver at 50Hz (every 0.02s) via `Tick()`
   - Retrieves mirror offsets using thread-safe `GetMirrorOffsets()`
   - Updates Slate brush UV regions to reposition mirrors
   - **Zero blocking** - no direct socket operations in game thread

### Testing UDP Control
Use the provided test script:
```bash
python3 /home/smarteye/carla/test_udp_mirror_control.py
```

The script sweeps mirror positions from 0.0 to 1.0 and back at 20Hz.

## Implementation Details

### Key Files
1. **CarlaSpectatorPawn.h**
   - FUdpMirrorReceiver class (lines 25-60)
   - UDP receiver thread members
   - Mirror crop offset properties (EditAnywhere for debugging)

2. **CarlaSpectatorPawn.cpp**
   - FUdpMirrorReceiver implementation (lines 25-180)
   - Async UDP methods (lines 553-628)
   - Slate UI construction (CreateTripleScreenWidget)

3. **Carla.Build.cs**
   - Dependencies: Sockets, Networking modules

### Scene Capture Components
```cpp
ForwardSceneCapture   // Center view (forward)
LeftSceneCapture      // Left view (90° left)
RightSceneCapture     // Right view (90° right)
LeftRearSceneCapture  // Left mirror (180° rear)
RightRearSceneCapture // Right mirror (180° rear)
```

### Render Targets
All render targets are fixed at 1920x1080:
- LeftRenderTarget
- RightRenderTarget
- LeftRearRenderTarget
- RightRearRenderTarget

### Slate Widget Structure
```cpp
SConstraintCanvas (full viewport)
├─ SBox [Anchors: 0.0-0.33] (Left third)
│  ├─ SImage (LeftBrush) - clipped to fit
│  └─ SOverlay → SBox (200x600 mirror)
│     └─ SImage (LeftRearBrush) - UV cropped
├─ SBox [Anchors: 0.33-0.66] (Center third)
│  └─ SImage (ForwardCamera native view)
└─ SBox [Anchors: 0.66-1.0] (Right third)
   ├─ SImage (RightBrush) - clipped to fit
   └─ SOverlay → SBox (200x600 mirror)
      └─ SImage (RightRearBrush) - UV cropped
```

## Thread Safety

### Critical Section Protection
```cpp
FCriticalSection DataLock;  // Protects LeftOffset, RightOffset
```

### Thread-Safe Data Flow
```
UDP Thread (FUdpMirrorReceiver::Run)
   ↓ Receives packet
   ↓ Parse "left:X,right:Y"
   ↓ FScopeLock(DataLock)
   ↓ Update LeftOffset, RightOffset
   ↓ Unlock
   
Game Thread (ACarlaSpectatorPawn::Tick)
   ↓ Call UpdateMirrorOffsetsFromUdp()
   ↓ Call UdpReceiver->GetMirrorOffsets()
   ↓ FScopeLock(DataLock)
   ↓ Read LeftOffset, RightOffset
   ↓ Unlock
   ↓ Update UI brushes
```

## Build Instructions

```bash
cd /home/smarteye/carla
make launch  # Builds and launches CARLA
```

## Usage

1. **Start CARLA**:
   ```bash
   cd /home/smarteye/carla
   make launch
   ```

2. **Control Mirrors** (in separate terminal):
   ```bash
   python3 test_udp_mirror_control.py
   ```

3. **Manual UDP Test** (using netcat):
   ```bash
   echo "left:0.3,right:0.7" | nc -u -w1 127.0.0.1 8888
   ```

## Configuration

### Mirror Crop Offsets (EditAnywhere)
You can manually adjust these in the Unreal Editor:
- **LeftMirrorCropOffset**: 0.0-1.0 (default: 0.5)
- **RightMirrorCropOffset**: 0.0-1.0 (default: 0.5)

### UDP Port
Default: 8888 (can be changed in CarlaSpectatorPawn.h)

## Performance Notes

### Why Async?
The original synchronous implementation caused "huge dip in frames per second" because:
- `Socket->Recv()` would block the game thread
- Even non-blocking sockets require polling overhead
- Every frame checked UDP socket (wasteful CPU)

### Async Benefits
- **Dedicated thread** handles UDP blocking
- Game thread only does lightweight memory read (50Hz)
- **Zero socket operations** in game loop
- Efficient blocking wait with timeout in UDP thread

### Expected Performance
- **No frame rate impact** from UDP
- Mirror updates at 50Hz (smooth enough for mirrors)
- UDP thread sleeps efficiently when no data

## Troubleshooting

### No Mirror Movement
1. Check UDP receiver started:
   ```
   LogTemp: UDP receiver thread started on port 8888
   ```

2. Verify UDP packets arrive:
   ```bash
   sudo tcpdump -i lo -n port 8888
   ```

3. Check for port conflicts:
   ```bash
   sudo netstat -tulpn | grep 8888
   ```

### Performance Issues
- **If still seeing FPS drops**: Check Tick() frequency
- **Thread not stopping**: Check StopUdpReceiver() in EndPlay
- **Memory leaks**: Verify UdpReceiverThread is deleted

### Compilation Errors
- **FSocket undefined**: Check `Sockets` module in Carla.Build.cs
- **FRunnable not found**: Verify includes `HAL/Runnable.h`
- **Linker errors**: Ensure `Networking` module added

## Future Improvements

1. **Retry Logic**: Auto-restart UDP thread if socket fails
2. **Multiple Clients**: Support multiple UDP senders (last wins)
3. **Protocol Extension**: Add JSON support for more parameters
4. **Dynamic Port**: Make UDP port configurable via command line
5. **Health Monitoring**: Log packet receive rate in UI

## Architecture Diagram

```
┌─────────────────────────────────────────────┐
│         Game Thread (Tick @ 60fps)          │
│  ┌──────────────────────────────────────┐   │
│  │ UpdateMirrorOffsetsFromUdp() @ 50Hz  │   │
│  │   ↓ GetMirrorOffsets() [thread-safe] │   │
│  │   ↓ Update Slate brushes UV regions  │   │
│  └──────────────────────────────────────┘   │
└──────────────────┬──────────────────────────┘
                   │ FScopeLock
                   ↓
┌─────────────────────────────────────────────┐
│           Thread-Safe Storage               │
│  ┌──────────────────────────────────────┐   │
│  │ FCriticalSection DataLock            │   │
│  │ float LeftOffset, RightOffset        │   │
│  └──────────────────────────────────────┘   │
└──────────────────┬──────────────────────────┘
                   │ FScopeLock
                   ↑
┌─────────────────────────────────────────────┐
│       UDP Thread (FUdpMirrorReceiver)       │
│  ┌──────────────────────────────────────┐   │
│  │ Run() - Blocking receive loop        │   │
│  │   ↓ Socket->Wait(100ms timeout)      │   │
│  │   ↓ Parse "left:X,right:Y"           │   │
│  │   ↓ Update offsets [thread-safe]     │   │
│  └──────────────────────────────────────┘   │
└─────────────────────────────────────────────┘
                   ↑
              UDP Port 8888
```

## Credits
Implementation: Custom CARLA spectator with Slate UI and async UDP
Based on: CARLA Simulator 0.9.x / Unreal Engine 4.26
