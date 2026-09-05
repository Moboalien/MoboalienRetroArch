package com.retroarch.browser.retroactivity;

import android.util.Log;

public final class MoboAlienServer {

    private static final String TAG = "MoboAlienServer";

    static {
        try {
            System.loadLibrary("retroarch-activity");
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "Failed to load retroarch-activity: " + e.getMessage());
        }
    }

    public static void start() {
        try {
            //nativeStart();
            Log.i(TAG, "MoboAlien server started");
        } catch (Exception e) {
            Log.e(TAG, "Failed to start MoboAlien server: " + e.getMessage());
        }
    }

    public static void stop() {
        try {
            //nativeStop();
            Log.i(TAG, "MoboAlien server stopped");
        } catch (Exception e) {
            Log.e(TAG, "Failed to stop MoboAlien server: " + e.getMessage());
        }
    }

    private static native void nativeStart();
    private static native void nativeStop();
}
