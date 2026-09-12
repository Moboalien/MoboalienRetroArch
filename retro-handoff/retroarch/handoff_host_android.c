/*
 * RetroArch host adapter for the portable retro-handoff module.
 *
 * This is the ONLY RetroArch-specific code, and in normal operation it is
 * passive: the user drives RetroArch entirely through its own UI (core
 * downloader, "Load Content", menus).  When the A2AStreaming app attaches an
 * encoder input Surface, the host simply makes sure RetroArch's GL context
 * re-creates its EGL surface on the handed-over native window, and switches
 * back to the activity window when the stream stops.
 *
 * The swap itself runs on RetroArch's graphics thread: attach/detach (which
 * can arrive on a binder thread) only latch a resync request via the module,
 * and android_gfx_ctx_check_window() in android_ctx.c consumes it once per
 * frame and re-creates the EGL surface there — never racing the render loop.
 *
 * A __attribute__((constructor)) wires the module's surface-changed and
 * start/stop control hooks the moment the library loads, so the embedding
 * service only ever drives the generic module API.
 */
#include "../../verbosity.h"
#include "../c/handoff_surface.h"

/* Minimal forward declarations for the android_app command pipe.
 * Including platform_unix.h directly drags in DEFAULT_MAX_PADS/MAX_AXIS
 * from input_driver.h; instead we declare just what we need. */
struct android_app;
extern struct android_app *g_android;
extern void android_app_write_cmd(struct android_app *android_app, int8_t cmd);

enum
{
   HANDOFF_CMD_GAINED_FOCUS = 6,   /* APP_CMD_GAINED_FOCUS */
   HANDOFF_CMD_LOST_FOCUS   = 7    /* APP_CMD_LOST_FOCUS   */
};

/* start/stop are called from the A2A binder.  The surface attach/detach itself
 * is what toggles the video target (it latches a resync request), so these
 * only keep the AIDL contract satisfied and make double-checks idempotent. */
static void handoff_host_start(void)
{
   RARCH_LOG("[Handoff] start requested\n");
   if (handoff_active())
      handoff_request_resync();
}

static void handoff_host_stop(void)
{
   RARCH_LOG("[Handoff] stop requested\n");
}

/* True only when a RetroArch native activity has actually started, i.e. its
 * GL context / android_app exists.  Used by the client to decide whether it
 * needs to cold-boot RetroArch; a backgrounded-but-loaded RetroArch reports
 * alive so nothing is launched over it. */
static bool handoff_android_alive(void)
{
   return (g_android != NULL);
}

static void handoff_surface_changed(void)
{
   struct android_app *android_app = (struct android_app*)g_android;
   bool active                     = handoff_active();

   RARCH_LOG("[Handoff] surface changed (active=%s)\n",
         active ? "yes" : "no");

   /* The user can attach the encoder surface while the RetroArch activity is
    * already backgrounded and paused (leave RA, then start the stream).  In
    * that case the LOST_FOCUS guard in the input driver never fired because
    * handoff was inactive at focus-loss time, so the run-loop is stuck paused.
    * Replay the platform focus-gain path: the command is consumed by
    * android_input_poll_main_cmd() on the run-loop thread, which clears
    * RUNLOOP_FLAG_PAUSED/IDLE (runloop_state.c handlers under frontend
    * app glue) just as a real focus gain would.  The inverse re-pauses on
    * detach so battery-draining rendering stops once streaming ends. */
   if (android_app)
   {
      if (active)
         android_app_write_cmd(android_app, HANDOFF_CMD_GAINED_FOCUS);
      else
         android_app_write_cmd(android_app, HANDOFF_CMD_LOST_FOCUS);
   }
}

/* ---- library-load wiring (portable module stays host-agnostic) ---- */

static void handoff_host_register(void) __attribute__((constructor));
static void handoff_host_register(void)
{
   handoff_set_changed_cb(handoff_surface_changed);
   handoff_set_host_control(handoff_host_start, handoff_host_stop);
   handoff_set_host_alive(handoff_android_alive);
   RARCH_LOG("[Handoff] host adapter registered\n");
}