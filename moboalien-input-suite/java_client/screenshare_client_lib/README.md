# Screenshare Client Library

Generic helper classes for H.264 video streaming over UDP, shared between Java desktop and Android clients.

## Components

### FrameAssembly.java
Handles reassembly of fragmented H.264 frames received over UDP.

**Features:**
- Manages frame fragments with metadata (keyframe, config, PTS)
- Assembles complete frames from multiple UDP packets
- Thread-safe fragment collection

**Usage:**
```java
FrameAssembly fa = new FrameAssembly(totalFragments, isKeyFrame, isConfig, pts);
boolean complete = fa.addFragment(fragmentId, data);
if (complete) {
    byte[] frame = fa.getAssembledFrame();
}
```

### H264StreamReceiver.java
UDP receiver that manages frame assembly and provides completed frames to consumers.

**Features:**
- Non-blocking frame retrieval via `takeFrame()`
- Automatic cleanup of incomplete/old frames
- Concurrent frame assembly with ordering
- Integrated heartbeat mechanism

**Usage:**
```java
H264StreamReceiver receiver = new H264StreamReceiver();
receiver.connect(host, port);

// In consumer thread
FrameAssembly fa = receiver.takeFrame();
byte[] frameData = fa.getAssembledFrame();
```

### HeartbeatThread.java
Daemon thread that sends periodic heartbeat packets to keep the connection alive.

**Features:**
- Automatic 1-second interval heartbeats
- Daemon thread (won't block JVM shutdown)
- Graceful shutdown support

**Usage:**
```java
HeartbeatThread heartbeat = new HeartbeatThread(socket, address, port);
heartbeat.start();
// Later...
heartbeat.shutdown();
```

## Protocol

UDP packet format (Big Endian):
```
Byte 0:      Type (0 = video frame)
Byte 1:      Flags (bit 0 = keyframe, bit 1 = config)
Bytes 2-5:   Frame ID (int)
Bytes 6-13:  PTS (long)
Bytes 14-15: Fragment ID (short)
Bytes 16-17: Total Fragments (short)
Bytes 18-19: Payload Size (short)
Bytes 20-21: Reserved (short)
Bytes 22+:   Payload data
```

## Integration

### Java Desktop (DirectH264Client)
```java
import java_client.screenshare_client_lib.FrameAssembly;
import java_client.screenshare_client_lib.H264StreamReceiver;
```

### Android (H264StreamView)
```java
import com.moboalien.satyam.controller.screenshare_client_lib.FrameAssembly;
import com.moboalien.satyam.controller.screenshare_client_lib.H264StreamReceiver;
```

## Notes

- All classes are platform-independent (pure Java)
- No external dependencies beyond standard Java libraries
- Optimized for low-latency video streaming
- Handles packet loss gracefully by dropping incomplete frames
