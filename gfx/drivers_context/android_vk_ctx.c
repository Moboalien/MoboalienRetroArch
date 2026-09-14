/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Hans-Kristian Arntzen
 *  Copyright (C) 2011-2017 - Daniel De Matteis
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdint.h>

#include <sys/system_properties.h>

#include <formats/image.h>
#include <string/stdstring.h>
#include <compat/strl.h>
#include <retro_timers.h>

#ifdef HAVE_CONFIG_H
#include "../../config.h"
#endif

#include "../common/vulkan_common.h"

#include "../../retro-handoff/c/handoff_surface.h"

#include "../../frontend/frontend_driver.h"
#include "../../frontend/drivers/platform_unix.h"
#include "../../verbosity.h"
#include "../../configuration.h"

typedef struct
{
   gfx_ctx_vulkan_data_t vk;
   unsigned width;
   unsigned height;
   int swap_interval;
} android_ctx_data_vk_t;

/* Native window the VkSurface was last created on, plus the ANativeWindow
 * reference we hold on it so the surface stays valid across hand-off swaps.
 * (ANativeWindow*)-1 is the "no window yet" sentinel. */
static ANativeWindow *g_stream_window = (ANativeWindow*)-1;

static void android_gfx_ctx_vk_teardown_surface(android_ctx_data_vk_t *and)
{
   if (and->vk.swapchain != VK_NULL_HANDLE)
      vulkan_destroy_swapchain(&and->vk);

   if (and->vk.vk_surface != VK_NULL_HANDLE)
   {
      PFN_vkDestroySurfaceKHR destroy_surface = NULL;
      if (VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_SYMBOL(
            and->vk.context.instance, "vkDestroySurfaceKHR", destroy_surface))
         destroy_surface(and->vk.context.instance, and->vk.vk_surface, NULL);
      else
         RARCH_WARN("[Vulkan] Failed to load vkDestroySurfaceKHR.\n");
      and->vk.vk_surface = VK_NULL_HANDLE;
   }
}

/* FORWARD DECLARATION */
bool android_display_get_metrics(void *data,
	enum display_metric_types type, float *value);
bool android_display_has_focus(void *data);

static void android_gfx_ctx_vk_destroy(void *data)
{
   android_ctx_data_vk_t *and = (android_ctx_data_vk_t*)data;

   if (!and)
      return;

   /* Always destroy the VkSurface here: its native window connection must
    * be torn down so a later vulkan_surface_create() (e.g. the reinit that
    * follows a hand-off surface switch) can reconnect the same window.
    * The upstream code passed android_app->window as the destroy_surface
    * flag, which silently keeps the surface alive when the activity window
    * is NULL (RA backgrounded during streaming) - leaving the encoder
    * window connected and making the next connect fail with EINVAL. */
   vulkan_context_destroy(&and->vk, true);

   /* Keep g_stream_window pointing at the last presentation window across
    * driver teardown/reinit.  A boot-time reinit (APK bundle extraction
    * completing ~1s into startup, fired through the runloop's task queue)
    * races the MainMenu -> RetroActivityFuture window swap: it runs
    * synchronously on the runloop thread, the very thread that would consume
    * the pending APP_CMD_INIT_WINDOW and restock android_app->window, so the
    * new context can come up with no activity window at all.  g_stream_window
    * (the window the previous context was presenting into) is then the only
    * valid surface target, so init/set_video_mode fall back to it.
    *
    * check_window() adopts a *new* target any time it differs from this one,
    * so preserving the ref here does not queue a second deferred switch (the
    * endless handoff_request_reset() REINIT loop that used to force the
    * handoff-active -> -1 reset is specific to the hand-off path; keeping the
    * reference in all cases stays safe for both).  The ref is reclaimed on
    * the next check_window() window swap or at process exit. */

   if (and->vk.context.queue_lock)
      slock_free(and->vk.context.queue_lock);

   free(data);
}

static void *android_gfx_ctx_vk_init(void *video_driver)
{
   struct android_app *android_app = (struct android_app*)g_android;
   android_ctx_data_vk_t *and  = (android_ctx_data_vk_t*)calloc(1, sizeof(*and));

   if (!android_app || !and)
      return NULL;

   if (!vulkan_context_init(&and->vk, VULKAN_WSI_ANDROID))
   {
      android_gfx_ctx_vk_destroy(and);
      return NULL;
   }

   /* A hand-off surface may be attached while the activity window is gone
    * (RA backgrounded during streaming).  When that happens the VkSurface
    * is created later from handoff_window() in check_window, so no window
    * is required at init time.
    *
    * A boot-time reinit (e.g. the APK bundle extraction task completing
    * ~1s into startup) can also race the MainMenu -> RetroActivityFuture
    * window swap.  The reinit runs synchronously on the runloop thread -
    * the same thread that would consume the pending APP_CMD_INIT_WINDOW and
    * restock android_app->window - so window can stay NULL for the whole
    * reinit.  Waiting cannot help (self-deadlock, returns NULL after 2s in
    * practice): failing the init makes vulkan_init fall through to the stub
    * context and leaves video_st->data == NULL, which crashes the menu/font
    * init that follows.
    *
    * Instead fall back to g_stream_window - the window the previous context
    * was presenting into, preserved across teardown by destroy() - which is
    * converted back into a live VkSurface in set_video_mode below. */
   if (!handoff_active()
         && android_app->window == NULL
         && g_stream_window == (ANativeWindow*)-1)
   {
      android_gfx_ctx_vk_destroy(and);
      return NULL;
   }

   return and;
}

static void android_gfx_ctx_vk_get_video_size(void *data,
      unsigned *width, unsigned *height)
{
   android_ctx_data_vk_t *and  = (android_ctx_data_vk_t*)data;

   *width  = and->width;
   *height = and->height;
}

static void android_gfx_ctx_vk_check_window(void *data, bool *quit,
      bool *resize, unsigned *width, unsigned *height)
{
   struct android_app *android_app      = (struct android_app*)g_android;
   unsigned new_width                   = 0;
   unsigned new_height                  = 0;
   android_ctx_data_vk_t *and           = (android_ctx_data_vk_t*)data;
   ANativeWindow *target                = (ANativeWindow*)-1;
   bool swap_surface                    = false;

   *quit                                = false;

   /* Unified surface hand-off (mirrors the GLES path in android_ctx.c):
      while an encoder surface is attached, RA renders into the handed-over
      native window instead of its activity window.  Rebuild the VkSurface
      (swapchain is recreated via set_resize) whenever the target changes or
      a resync is requested, polled once per frame on the graphics thread. */
   if (handoff_active())
      target = handoff_window();                      /* acquired ref */
   else if (android_app->window)
   {
      target = android_app->window;
      ANativeWindow_acquire(target);                  /* balanced below */
   }

   if (g_stream_window == (ANativeWindow*)-1)
   {
      /* First call: adopt the activity window, unless an encoder surface is
       * already attached (cold-boot attach must land on the pacer window). */
      if (!handoff_active())
      {
         g_stream_window = target;                    /* adopt ref */
         target          = (ANativeWindow*)-1;
      }
   }

   if (target != (ANativeWindow*)-1)
   {
      if (handoff_consume_resync_request() || (target != g_stream_window))
         swap_surface = true;
      else
         ANativeWindow_release(target);               /* transient ref */
   }

   if (swap_surface)
   {
      if (handoff_active())
      {
         /* Hardware Vulkan cores (e.g. swanstation) render directly into the
          * swapchain images and cache framebuffers/descriptor sets around
          * them.  Rebuilding the VkSurface/swapchain in place frees those
          * images while the core still references them, which segfaults the
          * driver on the next vkCmdBindDescriptorSets().  Instead defer a
          * full video driver reinit to the runloop: driver_uninit +
          * drivers_init bring the context back up on the handed-over window
          * AND re-run the core's context_reset, so its framebuffers are
          * re-created fresh against the new swapchain - the same path used
          * for config-triggered reinits.  The old surface stays valid until
          * then, so this frame keeps rendering without touching the core's
          * bindings. */
         RARCH_LOG("[Handoff] Vulkan warm hand-off: deferring surface switch to full video reinit.\n");
         if (g_stream_window != (ANativeWindow*)-1 && g_stream_window != target)
            ANativeWindow_release(g_stream_window);
         g_stream_window = target;                    /* adopt handoff target */
         handoff_request_reset();
      }
      else
      {
         RARCH_LOG("[Handoff] Vulkan switching surface (active=no).\n");

         android_gfx_ctx_vk_teardown_surface(and);

         if (g_stream_window != (ANativeWindow*)-1)
            ANativeWindow_release(g_stream_window);
         g_stream_window = target;                    /* adopt ref */

         if (g_stream_window != (ANativeWindow*)-1)
         {
            if (!vulkan_surface_create(&and->vk, VULKAN_WSI_ANDROID,
                     NULL, g_stream_window,
                     and->width, and->height, and->swap_interval))
               RARCH_WARN("[Handoff] Vulkan surface recreate failed.\n");
         }

         and->vk.flags |= VK_DATA_FLAG_NEED_NEW_SWAPCHAIN;
      }
   }

   if (android_app->content_rect.changed)
   {
      and->vk.flags                    |= VK_DATA_FLAG_NEED_NEW_SWAPCHAIN;
      android_app->content_rect.changed = false;
   }

   /* While presenting into the handed-over encoder surface the activity's
    * content rect is stale, so size the swapchain from the encoder window. */
   if (handoff_active())
   {
      ANativeWindow *w = handoff_window();
      if (w)
      {
         new_width  = (unsigned)ANativeWindow_getWidth(w);
         new_height = (unsigned)ANativeWindow_getHeight(w);
         if (new_width == 0 || new_height == 0)
         {
            new_width  = and->width;
            new_height = and->height;
         }
         ANativeWindow_release(w);
      }
      else
      {
         new_width  = android_app->content_rect.width;
         new_height = android_app->content_rect.height;
      }
   }
   else
   {
      new_width  = android_app->content_rect.width;
      new_height = android_app->content_rect.height;
   }

   /* Swapchains are recreated in set_resize as a
    * central place, so use that to trigger swapchain reinit. */
   *resize    = (and->vk.flags & VK_DATA_FLAG_NEED_NEW_SWAPCHAIN) ? true : false;

   if (new_width != *width || new_height != *height)
   {
      RARCH_LOG("[Vulkan] Resizing (%ux%u) -> (%ux%u).\n",
              *width, *height, new_width, new_height);

      *width  = new_width;
      *height = new_height;
      *resize = true;
   }
}

static bool android_gfx_ctx_vk_set_resize(void *data,
      unsigned width, unsigned height)
{
   android_ctx_data_vk_t        *and  = (android_ctx_data_vk_t*)data;
   struct android_app *android_app    = (struct android_app*)g_android;

   if (handoff_active())
   {
      ANativeWindow *w = handoff_window();
      if (w)
      {
         and->width  = (unsigned)ANativeWindow_getWidth(w);
         and->height = (unsigned)ANativeWindow_getHeight(w);
         if (and->width == 0 || and->height == 0)
         {
            and->width  = 1280;
            and->height = 720;
         }
         ANativeWindow_release(w);
      }
   }
   else
   {
      and->width  = android_app->content_rect.width;
      and->height = android_app->content_rect.height;
   }
   RARCH_LOG("[Vulkan] Native window size: %ux%u.\n", and->width, and->height);
   if (!vulkan_create_swapchain(&and->vk, and->width, and->height, and->swap_interval))
   {
      RARCH_ERR("[Vulkan] Failed to update swapchain.\n");
      return false;
   }

   if (and->vk.flags & VK_DATA_FLAG_CREATED_NEW_SWAPCHAIN)
      vulkan_acquire_next_image(&and->vk);
   and->vk.context.flags             |=  VK_CTX_FLAG_INVALID_SWAPCHAIN;
   and->vk.flags                     &= ~VK_DATA_FLAG_NEED_NEW_SWAPCHAIN;

   return true;
}

static bool android_gfx_ctx_vk_set_video_mode(void *data,
      unsigned width, unsigned height,
      bool fullscreen)
{
   struct android_app *android_app = (struct android_app*)g_android;
   android_ctx_data_vk_t *and      = (android_ctx_data_vk_t*)data;
   ANativeWindow *target           = android_app->window;
   bool transient                  = false;

   /* A hand-off surface switch re-inits the video driver from the runloop.
    * Target the handed-over encoder window in that case - the activity window
    * is the wrong surface (and NULL when RA is backgrounded during streaming). */
   if (handoff_active())
   {
      target    = handoff_window();                    /* acquired ref */
      transient = true;
   }
   /* Boot-time reinit raced the window swap: android_app->window is NULL but
    * g_stream_window still points at the window the previous context was
    * presenting into (destroy() keeps it alive across teardown/reinit, see
    * android_gfx_ctx_vk_destroy).  Create the new VkSurface on it instead of
    * failing, which would fall through to the stub context and crash the
    * subsequent menu/font init on video_st->data == NULL.  The ref stays
    * owned by the module so target is borrowed - do not release it here. */
   else if (!target && g_stream_window != (ANativeWindow*)-1)
   {
      RARCH_LOG("[Vulkan] set_video_mode: activity window NULL, "
            "reusing g_stream_window.\n");
      target = g_stream_window;
   }

   if (!target)
   {
      RARCH_ERR("[Vulkan] set_video_mode: no window available.\n");
      return false;
   }

   and->width                      = ANativeWindow_getWidth(target);
   and->height                     = ANativeWindow_getHeight(target);
   if (!vulkan_surface_create(&and->vk, VULKAN_WSI_ANDROID,
            NULL, target,
            and->width, and->height, and->swap_interval))
   {
      if (transient)
         ANativeWindow_release(target);
      RARCH_ERR("[Vulkan] Failed to create surface.\n");
      return false;
   }
   if (transient)
      ANativeWindow_release(target);

   RARCH_LOG("[Vulkan] Native window size: %ux%u.\n",
         and->width, and->height);
   return true;
}

static void android_gfx_ctx_vk_input_driver(void *data,
      const char *joypad_name,
      input_driver_t **input, void **input_data)
{
   void *androidinput   = input_driver_init_wrap(&input_android, joypad_name);

   *input               = androidinput ? &input_android : NULL;
   *input_data          = androidinput;
}

static enum gfx_ctx_api android_gfx_ctx_vk_get_api(void *data)
{
   return GFX_CTX_VULKAN_API;
}

static bool android_gfx_ctx_vk_bind_api(void *data,
      enum gfx_ctx_api api, unsigned major, unsigned minor)
{
   return (api == GFX_CTX_VULKAN_API);
}


static bool android_gfx_ctx_vk_suppress_screensaver(void *data, bool enable) { return false; }

static void android_gfx_ctx_vk_swap_buffers(void *data)
{
   android_ctx_data_vk_t *and  = (android_ctx_data_vk_t*)data;

   if (and->vk.context.flags & VK_CTX_FLAG_HAS_ACQUIRED_SWAPCHAIN)
   {
      and->vk.context.flags &= ~VK_CTX_FLAG_HAS_ACQUIRED_SWAPCHAIN;
      if (and->vk.swapchain == VK_NULL_HANDLE)
      {
         retro_sleep(10);
      }
      else
         vulkan_present(&and->vk, and->vk.context.current_swapchain_index);
   }
   vulkan_acquire_next_image(&and->vk);
}

static void android_gfx_ctx_vk_set_swap_interval(void *data, int swap_interval)
{
   android_ctx_data_vk_t *and  = (android_ctx_data_vk_t*)data;

   if (and->swap_interval != swap_interval)
   {
      RARCH_LOG("[Vulkan] Setting swap interval: %u.\n", swap_interval);
      and->swap_interval       = swap_interval;
      if (and->vk.swapchain)
         and->vk.flags        |= VK_DATA_FLAG_NEED_NEW_SWAPCHAIN;
   }
}

static gfx_ctx_proc_t android_gfx_ctx_vk_get_proc_address(const char *symbol) { return NULL; }
static void android_gfx_ctx_vk_bind_hw_render(void *data, bool enable) { }

static void *android_gfx_ctx_vk_get_context_data(void *data)
{
   android_ctx_data_vk_t *and = (android_ctx_data_vk_t*)data;
   return &and->vk.context;
}

static uint32_t android_gfx_ctx_vk_get_flags(void *data)
{
   uint32_t flags = 0;

#if defined(HAVE_SLANG) && defined(HAVE_SPIRV_CROSS)
   BIT32_SET(flags, GFX_CTX_FLAGS_SHADERS_SLANG);
#endif

   return flags;
}

static void android_gfx_ctx_vk_set_flags(void *data, uint32_t flags) { }

const gfx_ctx_driver_t gfx_ctx_vk_android = {
   android_gfx_ctx_vk_init,
   android_gfx_ctx_vk_destroy,
   android_gfx_ctx_vk_get_api,
   android_gfx_ctx_vk_bind_api,
   android_gfx_ctx_vk_set_swap_interval,
   android_gfx_ctx_vk_set_video_mode,
   android_gfx_ctx_vk_get_video_size,
   NULL,                                     /* get_refresh_rate */
   NULL,                                     /* get_video_output_size */
   NULL,                                     /* get_video_output_prev */
   NULL,                                     /* get_video_output_next */
   NULL, /* get_metrics - handled by display server */
   NULL,
   NULL,                                     /* update_title */
   android_gfx_ctx_vk_check_window,
   android_gfx_ctx_vk_set_resize,
   android_display_has_focus,
   android_gfx_ctx_vk_suppress_screensaver,
   false,                                    /* has_windowed */
   android_gfx_ctx_vk_swap_buffers,
   android_gfx_ctx_vk_input_driver,
   android_gfx_ctx_vk_get_proc_address,
   NULL,
   NULL,
   NULL,
   "vk_android",
   android_gfx_ctx_vk_get_flags,
   android_gfx_ctx_vk_set_flags,
   android_gfx_ctx_vk_bind_hw_render,
   android_gfx_ctx_vk_get_context_data,
   NULL,                                     /* make_current */
   NULL,                                     /* create_surface */
   NULL                                      /* destroy_surface */
};
