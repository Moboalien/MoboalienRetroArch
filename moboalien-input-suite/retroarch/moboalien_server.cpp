#ifdef ANDROID
#include <jni.h>
#endif
#include <memory>
#include <atomic>
#include <thread>

#include "controller/i_controller.h"
#include "controller/handshake.h"
#include "controller/server_context.h"
#include "platform.h"
#include "i_input_injector.h"
#include "signal_handler.h"
#include "logger.h"

static const char* TAG = "MoboAlienServer";

// ---------------------------------------------------------------------------
// Global server state (one instance per process)
// ---------------------------------------------------------------------------

static std::unique_ptr<Platform>         g_platform;
static std::unique_ptr<IInputInjector>   g_injector;
static std::unique_ptr<IController>      g_controller;
static std::unique_ptr<HandshakeManager> g_handshake;
static std::thread                       g_serverThread;

#ifdef ANDROID
static JavaVM* g_jvm = nullptr;

// ---------------------------------------------------------------------------
// JNI_OnLoad
// ---------------------------------------------------------------------------

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    g_jvm = vm;
    return JNI_VERSION_1_6;
}
#endif

// ---------------------------------------------------------------------------
// moboalien_server_start / moboalien_server_stop (C API)
// ---------------------------------------------------------------------------

extern "C" void moboalien_server_start(void) {
    if (g_controller) {
        LOGW(TAG, "moboalien_server_start called while already running");
        return;
    }

    SignalHandler::resetShutdown();
    LOGI(TAG, "moboalien_server_start: initializing");

    g_platform   = CreatePlatform();
    g_injector   = CreateInputInjector();
    g_controller = CreateController(
        g_platform.get(),
        g_injector.get(),
        /*verbose=*/false,
        ServerType::RETROARCH,
        /*packetEncryptor=*/nullptr,
        /*imageEncoder=*/nullptr,
        /*screenCapture=*/nullptr
    );

    g_controller->SetMinPressDuration(20);

    g_handshake = std::make_unique<HandshakeManager>(
        g_platform.get(),
        /*authService=*/nullptr,
        /*sessionKey=*/std::vector<uint8_t>{},
        /*salt=*/"",
        /*handshakePort=*/16235,
        g_controller->GetControllerPort(),
        /*screenPort=*/0,
        /*serverId=*/0,
        /*serverType=*/0,
        /*passwordRequired=*/false
    );

    g_handshake->Start();
    LOGI(TAG, "moboalien_server_start: listening — controller port " +
         std::to_string(g_controller->GetControllerPort()));

    // Run the blocking controller loop on a background thread
    g_serverThread = std::thread([]() {
        g_controller->Run();
        g_handshake->Stop();
        LOGI(TAG, "server thread exited");
    });
}

extern "C" void moboalien_server_stop(void) {
    if (!g_controller) {
        LOGW(TAG, "moboalien_server_stop called while not running");
        return;
    }

    LOGI(TAG, "moboalien_server_stop: requesting shutdown");
    SignalHandler::requestShutdown();

    if (g_serverThread.joinable())
        g_serverThread.join();

    g_handshake.reset();
    g_controller.reset();
    g_injector.reset();
    g_platform.reset();

    SignalHandler::resetShutdown();
    LOGI(TAG, "moboalien_server_stop complete");
}

#ifdef ANDROID
// ---------------------------------------------------------------------------
// JNI wrappers for MoboAlienServer (backward compatibility)
// ---------------------------------------------------------------------------

extern "C" JNIEXPORT void JNICALL
Java_com_retroarch_browser_retroactivity_MoboAlienServer_nativeStart(
        JNIEnv*, jclass) {
    moboalien_server_start();
}

extern "C" JNIEXPORT void JNICALL
Java_com_retroarch_browser_retroactivity_MoboAlienServer_nativeStop(
        JNIEnv*, jclass) {
    moboalien_server_stop();
}
#endif
