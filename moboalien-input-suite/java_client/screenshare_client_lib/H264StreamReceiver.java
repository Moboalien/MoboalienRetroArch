package java_client.screenshare_client_lib;

import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.SocketTimeoutException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.concurrent.ConcurrentSkipListMap;
import java.util.concurrent.atomic.AtomicInteger;

public class H264StreamReceiver {
    private volatile boolean running = true;
    private final ConcurrentSkipListMap<Integer, FrameAssembly> assemblyMap = new ConcurrentSkipListMap<>();
    private final ConcurrentSkipListMap<Integer, FrameAssembly> completedFrames = new ConcurrentSkipListMap<>();
    private final AtomicInteger lastWrittenFrameId = new AtomicInteger(-1);

    // Drop logging metrics
    private long lastLogTime = System.currentTimeMillis();
    private int dropOldFragmentCount = 0;
    private int dropIncompleteFrameCount = 0;
    private int dropCompletedFrameCount = 0;
    private int totalMissingFragments = 0;

    public FrameAssembly takeFrame() throws InterruptedException {
        synchronized (completedFrames) {
            while (running && completedFrames.isEmpty()) {
                completedFrames.wait(100);
            }
            if (!completedFrames.isEmpty()) {
                int nextFrameId = completedFrames.firstKey();
                lastWrittenFrameId.set(nextFrameId);
                return completedFrames.pollFirstEntry().getValue();
            }
        }
        return null;
    }

    public void connect(String host, int port) {
        new Thread(() -> {
            DatagramSocket socket = null;
            try {
                socket = new DatagramSocket();
                // Increase buffer to 2MB to prevent large I-Frames from dropping UDP packets
                socket.setReceiveBufferSize(2 * 1024 * 1024);
                socket.setSoTimeout(1000);

                final InetAddress address = InetAddress.getByName(host);
                final DatagramSocket finalSocket = socket;

                HeartbeatThread heartbeat = new HeartbeatThread(finalSocket, address, port);
                heartbeat.start();

                byte[] buffer = new byte[2048];

                while (running) {
                    DatagramPacket packet = new DatagramPacket(buffer, buffer.length);
                    try {
                        socket.receive(packet);
                    } catch (SocketTimeoutException e) {
                        continue;
                    }

                    ByteBuffer bb = ByteBuffer.wrap(packet.getData(), 0, packet.getLength());
                    bb.order(ByteOrder.BIG_ENDIAN);

                    byte type = bb.get();
                    if (type != 0) continue;

                    byte flags = bb.get();
                    int frameId = bb.getInt();
                    long pts = bb.getLong();
                    int fragId = bb.getShort() & 0xFFFF;
                    int totalFrags = bb.getShort() & 0xFFFF;
                    int payloadSize = bb.getShort() & 0xFFFF;
                    int reserved = bb.getShort() & 0xFFFF;

                    if (System.currentTimeMillis() - lastLogTime > 1000) {
                        if (dropOldFragmentCount > 0 || dropIncompleteFrameCount > 0 || dropCompletedFrameCount > 0) {
                            String incompleteStats = dropIncompleteFrameCount > 0 
                                ? dropIncompleteFrameCount + " (avg missing: " + (totalMissingFragments / dropIncompleteFrameCount) + ")" 
                                : "0";
                            System.out.println("Frame Drops in last 1s: Old Fragments=" + dropOldFragmentCount + 
                                               ", Incomplete Frames=" + incompleteStats + 
                                               ", Completed Frames (consumer slow)=" + dropCompletedFrameCount);
                        }
                        dropOldFragmentCount = 0;
                        dropIncompleteFrameCount = 0;
                        dropCompletedFrameCount = 0;
                        totalMissingFragments = 0;
                        lastLogTime = System.currentTimeMillis();
                    }

                    if (frameId <= lastWrittenFrameId.get()) {
                        dropOldFragmentCount++;
                        continue;
                    }

                    boolean isKeyFrame = (flags & 0x01) != 0;
                    boolean isConfig = (flags & 0x02) != 0;

                    FrameAssembly fa = assemblyMap.computeIfAbsent(frameId, k -> new FrameAssembly(totalFrags, isKeyFrame, isConfig, pts));

                    while (assemblyMap.size() > 4) {
                        FrameAssembly dropped = assemblyMap.pollFirstEntry().getValue();
                        dropIncompleteFrameCount++;
                        int missing = dropped.getTotalFragments() - dropped.getReceivedCount();
                        totalMissingFragments += missing;
                    }

                    byte[] data = new byte[payloadSize];
                    if (payloadSize > 0) {
                        bb.get(data);
                    }
                    boolean frameComplete = fa.addFragment(fragId, data);

                    if (frameComplete) {
                        assemblyMap.remove(frameId);
                        synchronized (completedFrames) {
                            while (completedFrames.size() > 5) {
                                completedFrames.pollFirstEntry();
                                dropCompletedFrameCount++;
                            }
                            completedFrames.put(frameId, fa);
                            completedFrames.notifyAll();
                        }
                    }
                }
            } catch (Exception e) {
                // Silently stop
            } finally {
                if (socket != null) {
                    socket.close();
                }
            }
        }).start();
    }

    public void stop() {
        running = false;
        synchronized (completedFrames) {
            completedFrames.notifyAll();
        }
    }
}
