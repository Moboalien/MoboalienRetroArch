package java_client.screenshare_client_lib;

public class FrameAssembly {
    private byte[][] fragments;
    private int receivedCount;
    private final int totalFragments;
    private int totalSize;
    private final boolean isKeyFrame;
    private final boolean isConfig;
    private final long pts;
    private byte[] assembledFrame;

    public FrameAssembly(int total, boolean isKey, boolean isConfig, long pts) {
        this.totalFragments = total;
        this.fragments = new byte[total][];
        this.receivedCount = 0;
        this.totalSize = 0;
        this.isKeyFrame = isKey;
        this.isConfig = isConfig;
        this.pts = pts;
        this.assembledFrame = null;
    }

    public boolean addFragment(int fragId, byte[] data) {
        if (fragments[fragId] == null) {
            fragments[fragId] = data;
            totalSize += data.length;
            receivedCount++;

            if (receivedCount == totalFragments) {
                assembledFrame = assembleFrame();
                fragments = null;
                return true;
            }
        }
        return false;
    }

    private byte[] assembleFrame() {
        byte[] result = new byte[totalSize];
        int offset = 0;
        for (byte[] frag : fragments) {
            System.arraycopy(frag, 0, result, offset, frag.length);
            offset += frag.length;
        }
        return result;
    }

    public byte[] getAssembledFrame() {
        return assembledFrame;
    }

    public boolean isKeyFrame() {
        return isKeyFrame;
    }

    public boolean isConfig() {
        return isConfig;
    }

    public long getPts() {
        return pts;
    }

    public int getReceivedCount() {
        return receivedCount;
    }

    public int getTotalFragments() {
        return totalFragments;
    }
}
