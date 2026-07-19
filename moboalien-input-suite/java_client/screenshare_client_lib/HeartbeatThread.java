package java_client.screenshare_client_lib;

import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;

public class HeartbeatThread extends Thread {
    private final DatagramSocket socket;
    private final InetAddress address;
    private final int port;
    private volatile boolean running = true;

    public HeartbeatThread(DatagramSocket socket, InetAddress address, int port) {
        this.socket = socket;
        this.address = address;
        this.port = port;
        setDaemon(true);
    }

    @Override
    public void run() {
        while (running) {
            try {
                byte[] hello = new byte[]{1};
                DatagramPacket packet = new DatagramPacket(hello, hello.length, address, port);
                socket.send(packet);
                Thread.sleep(1000);
            } catch (Exception e) {
                break;
            }
        }
    }

    public void shutdown() {
        running = false;
    }
}
