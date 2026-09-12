package com.a2a.streaming.retro;

import android.app.Activity;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Bundle;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.TextView;

/**
 * Interactive notice shown above the native RetroArch activity while a
 * hand-off stream is active.  The native activity's own window is black
 * during streaming (its GL renders into the encoder surface), so this
 * separate window tells the user the screen is being mirrored to a client.
 *
 * <p>Unlike a passive overlay this window is focusable and touchable:
 * Back or Minimize sends the RetroArch task to the background (streaming
 * continues thanks to the hand-off un-pause path), while Exit closes the
 * app outright; the client notices the binder death and tears down.</p>
 */
public final class StreamingNoticeActivity extends Activity {

    /** Set by {@link RetroHandoffService} when streaming stops; the notice dismisses itself. */
    static volatile boolean sDismiss;

    /** Currently showing notice, so the service can finish it directly. */
    static volatile StreamingNoticeActivity sCurrent;

    /** True while the notice is resumed (used to avoid duplicate launches). */
    static volatile boolean sRunning;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        sDismiss = false;

        /* Fully interactive: the user needs Back / Exit to control the
         * stream.  With the window focusable the native activity receives
         * APP_CMD_LOST_FOCUS, but the handler is gated by handoff_active()
         * so the run-loop stays alive and keeps feeding frames. */
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_TURN_SCREEN_ON);

        float dp = getResources().getDisplayMetrics().density;

        /* Banner text */
        TextView banner = new TextView(this);
        banner.setText("\u25CF Streaming \u2014 screen mirrored to client");
        banner.setTextColor(Color.WHITE);
        banner.setBackgroundColor(0xCC000000);
        banner.setGravity(Gravity.CENTER);
        banner.setTypeface(Typeface.DEFAULT_BOLD);
        banner.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        int pad = Math.round(14 * dp);
        banner.setPadding(pad, pad, pad, pad);

        /* Buttons */
        Button exitBtn = new Button(this);
        exitBtn.setText("Exit");
        exitBtn.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        exitBtn.setTextColor(Color.WHITE);
        exitBtn.setBackgroundColor(0xCCAA2222);
        exitBtn.setOnClickListener(this::onExitClicked);
        FrameLayout.LayoutParams exitLp = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT,
                Gravity.CENTER_HORIZONTAL | Gravity.BOTTOM);
        exitLp.bottomMargin = Math.round(80 * dp);

        Button minBtn = new Button(this);
        minBtn.setText("Minimize");
        minBtn.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13);
        minBtn.setTextColor(Color.WHITE);
        minBtn.setBackgroundColor(0xCC444444);
        minBtn.setOnClickListener(v -> minimize());
        FrameLayout.LayoutParams minLp = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT,
                FrameLayout.LayoutParams.WRAP_CONTENT,
                Gravity.CENTER_HORIZONTAL | Gravity.BOTTOM);
        minLp.bottomMargin = Math.round(24 * dp);

        FrameLayout root = new FrameLayout(this);
        root.addView(banner, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.WRAP_CONTENT,
                Gravity.TOP));
        root.addView(exitBtn, exitLp);
        root.addView(minBtn, minLp);
        setContentView(root);
    }

    @Override
    public void onBackPressed() {
        minimize();
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_BACK) {
            minimize();
            return true;
        }
        return super.onKeyDown(keyCode, event);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (sDismiss) {
            finish();
            return;
        }
        sCurrent = this;
        sRunning = true;
    }

    @Override
    protected void onPause() {
        super.onPause();
        sRunning = false;
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        sRunning = false;
        if (sCurrent == this) {
            sCurrent = null;
        }
    }

    /** Requests the producer-side teardown; the client polls isStreaming() and
     *  tears down its own codec/transport, then this notice self-dismisses. */
    private void onExitClicked(View v) {
        RetroHandoffService.requestExit();
    }

    /** Backgrounds the RA task so the previous app is revealed; the stream keeps running. */
    private void minimize() {
        RetroHandoffService.minimizeToPrevious();
    }
}