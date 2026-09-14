package com.a2a.streaming.retro;

import android.app.Activity;
import android.app.Application;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.util.Log;
import android.view.Surface;

import com.a2a.streaming.retro.RetroHandoffNative;

/**
 * Portable AIDL service for the retro-handoff module. The encoder owner
 * (A2AStreaming) binds to it and hands over the MediaCodec input Surface;
 * this service forwards to the module's JNI bridge which injects the window
 * and, on start/stop, drives the host-integrated emulation via the host
 * control callbacks (see handoff_host_android.c).
 *
 * <p>While a hand-off surface is attached the host renders into the encoder
 * Surface instead of its own window, so this device's {@code RetroActivityFuture}
 * (an ANativeActivity whose window surface is the GL target) stays black. A
 * translucent {@link StreamingNoticeActivity} with its own window is shown over
 * it so the user knows the screen is being streamed, and is dismissed when the
 * hand-off ends.</p>
 *
 * <p>The notice cannot be started while this process is in the background
 * (Android background-activity-start restriction), so the service watches
 * {@link ActivityLifecycleCallbacks} and launches it as soon as a RetroArch
 * activity is resumed (i.e. actually visible).</p>
 *
 * This file is host-agnostic: it only knows the module contract and the name
 * of the monolithic library that hosts the JNI symbols.
 */
public final class RetroHandoffService extends Service {

    private static final String TAG = "RetroHandoffService";

    /** Matches the lib name baked into the monolithic build (phoenix). */
    private static final String HOST_LIBRARY = "retroarch-activity";

    private static final String CHANNEL_ID = "retro-handoff";
    private static volatile Service sInstance;
    private boolean mEmulationStarted = false;
    private boolean mSurfaceAttached = false;
    private boolean mAudioAttached = false;
    private boolean mStreaming = false;

    /** Set when the user intentionally backgrounds the app during streaming
     *  (notice's Minimize/Back); suppresses the re-launch retry until the app
     *  comes back to the foreground. */
    private volatile boolean mMinimized = false;
    /** Most recently resumed non-notice activity; used to background the
     *  RetroArch task on minimize. */
    private Activity mAppActivity;
    private final Handler mMainHandler = new Handler(Looper.getMainLooper());
    private final Runnable mNoticeRetry = new Runnable() {
        @Override
        public void run() {
            if (!mStreaming || mMinimized) {
                return;
            }
            if (!StreamingNoticeActivity.sRunning) {
                launchNotice();
            }
            mMainHandler.postDelayed(this, 500);
        }
    };

    private final Application.ActivityLifecycleCallbacks mLifecycleCallbacks =
            new Application.ActivityLifecycleCallbacks() {
                @Override
                public void onActivityResumed(Activity activity) {
                    if (activity instanceof StreamingNoticeActivity) {
                        return;
                    }
                    mMinimized = false;
                    mAppActivity = activity;
                    if (mStreaming && !StreamingNoticeActivity.sRunning) {
                        launchNotice();
                    }
                }

                @Override
                public void onActivityCreated(Activity activity, Bundle savedInstanceState) {
                }

                @Override
                public void onActivityStarted(Activity activity) {
                }

                @Override
                public void onActivityPaused(Activity activity) {
                }

                @Override
                public void onActivityStopped(Activity activity) {
                }

                @Override
                public void onActivitySaveInstanceState(Activity activity, Bundle outState) {
                }

                @Override
                public void onActivityDestroyed(Activity activity) {
                }
            };

    private final IRetroStreamService.Stub mServiceStub =
            new IRetroStreamService.Stub() {
                @Override
                public void attachEncoderSurface(Surface surface) {
                    if (RetroHandoffNative.attachEncoderSurface(surface)) {
                        mSurfaceAttached = true;
                        mStreaming = true;
                        mMainHandler.post(() -> {
                            launchNotice();
                            mMainHandler.postDelayed(mNoticeRetry, 500);
                            startEmulation();
                        });
                    }
                }

                @Override
                public void detachEncoderSurface() {
                    mSurfaceAttached = false;
                    mStreaming = false;
                    mMainHandler.removeCallbacks(mNoticeRetry);
                    mMainHandler.post(() -> {
                        dismissNotice();
                        stopEmulation();
                    });
                    RetroHandoffNative.detachEncoderSurface();
                }

                @Override
                public void startEmulation() {
                    // Emulation can run with either a video surface or an audio
                    // ring attached (audio-only streaming is a supported mode).
                    if ((mSurfaceAttached || mAudioAttached) && !mEmulationStarted) {
                        mEmulationStarted = RetroHandoffNative.startEmulation();
                    }
                }

                @Override
                public void stopEmulation() {
                    if (mEmulationStarted) {
                        RetroHandoffNative.stopEmulation();
                        mEmulationStarted = false;
                    }
                }

                @Override
                public boolean isProducerAlive() {
                    return RetroHandoffNative.isProducerAlive();
                }

                @Override
                public boolean isStreaming() {
                    return mSurfaceAttached && mEmulationStarted;
                }

                @Override
                public void attachAudioSink(ParcelFileDescriptor fd, int capacityBytes) {
                    int dup = -1;
                    try {
                        if (fd != null && capacityBytes > 0) {
                            dup = fd.dup().detachFd();
                        }
                    } catch (Exception e) {
                        Log.w(TAG, "audio sink fd dup failed", e);
                        dup = -1;
                    }
                    if (dup >= 0 && RetroHandoffNative.attachAudioSink(dup, capacityBytes)) {
                        mAudioAttached = true;
                        mStreaming = true;
                        Log.i(TAG, "audio sink attached: capacity=" + capacityBytes);
                        // Start emulation even without a video surface so audio-only
                        // streaming works standalone; no-op if already started by the
                        // surface path (emulation never runs twice).
                        mMainHandler.post(this::startEmulation);
                    } else {
                        Log.e(TAG, "audio sink attach failed");
                    }
                }

                @Override
                public void detachAudioSink() {
                    mAudioAttached = false;
                    mStreaming = mSurfaceAttached && mEmulationStarted;
                    RetroHandoffNative.detachAudioSink();
                    Log.i(TAG, "audio sink detached");
                }

                @Override
                public boolean isAudioStreaming() {
                    return mAudioAttached && mEmulationStarted;
                }
            };

    @Override
    public void onCreate() {
        super.onCreate();
        sInstance = this;
        RetroHandoffNative.init(HOST_LIBRARY);
        getApplication().registerActivityLifecycleCallbacks(mLifecycleCallbacks);
        createNotificationChannel();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        startForeground(1, buildNotification());
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        if (sInstance == this) {
            sInstance = null;
        }
        getApplication().unregisterActivityLifecycleCallbacks(mLifecycleCallbacks);
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return mServiceStub;
    }

    /**
     * Shows the "screen being streamed" notice. Only effective while this app has a visible
     * activity (otherwise the platform blocks the launch). Main-thread only.
     */
    private void launchNotice() {
        if (StreamingNoticeActivity.sRunning) {
            return;
        }
        StreamingNoticeActivity.sDismiss = false;
        try {
            startActivity(new Intent(this, StreamingNoticeActivity.class)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        } catch (Exception e) {
            Log.w(TAG, "could not launch streaming notice", e);
        }
    }

    /**
     * Requested by the notice's Exit button: closes the app outright. The
     * notice is dismissed, RetroArch's activity is finished, and the process
     * is killed — the binder dies, the client notices via the poll, and tears
     * down its own codec/transport. Main-thread only.
     */
    static void requestExit() {
        Service instance = sInstance;
        if (!(instance instanceof RetroHandoffService)) {
            return;
        }
        RetroHandoffService svc = (RetroHandoffService) instance;
        svc.mMainHandler.post(() -> {
            try {
                StreamingNoticeActivity.sDismiss = true;
                StreamingNoticeActivity notice = StreamingNoticeActivity.sCurrent;
                if (notice != null) {
                    notice.finish();
                }
                Activity app = svc.mAppActivity;
                if (app != null) {
                    app.finish();
                }
                svc.stopForeground(true);
                svc.stopSelf();
                android.os.Process.killProcess(android.os.Process.myPid());
            } catch (Exception e) {
                Log.w(TAG, "exit failed", e);
            }
        });
    }

    /**
     * Backgrounds the task that holds RetroArch's activity, so the system
     * reveals whatever activity/task was underneath (the previous app), then
     * finishes the notice overlay. Main-thread only.
     */
    static void minimizeToPrevious() {
        Service instance = sInstance;
        if (!(instance instanceof RetroHandoffService)) {
            return;
        }
        RetroHandoffService svc = (RetroHandoffService) instance;
        svc.mMinimized = true;
        Activity app = svc.mAppActivity;
        if (app != null) {
            try {
                /* Background the RA task first; the notice finishes next, so
                 * whatever task was below RA becomes visible. */
                app.moveTaskToBack(true);
            } catch (Exception e) {
                Log.w(TAG, "could not move RA task to back", e);
            }
        }
        StreamingNoticeActivity notice = StreamingNoticeActivity.sCurrent;
        if (notice != null) {
            notice.finish();
        }
    }

    private void dismissNotice() {
        StreamingNoticeActivity.sDismiss = true;
        StreamingNoticeActivity notice = StreamingNoticeActivity.sCurrent;
        if (notice != null) {
            notice.finish();
        }
    }

    private void createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            NotificationChannel channel = new NotificationChannel(
                    CHANNEL_ID, "Retro streaming", NotificationManager.IMPORTANCE_LOW);
            NotificationManager manager =
                    (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
            if (manager != null) {
                manager.createNotificationChannel(channel);
            }
        }
    }

    private Notification buildNotification() {
        Notification.Builder builder;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            builder = new Notification.Builder(this, CHANNEL_ID);
        } else {
            builder = new Notification.Builder(this);
        }
        builder.setContentTitle("Retro hand-off")
                .setContentText("Streaming emulation to A2A")
                .setSmallIcon(android.R.drawable.stat_sys_upload);
        return builder.build();
    }
}