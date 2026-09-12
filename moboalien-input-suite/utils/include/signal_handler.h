#pragma once

#include <atomic>
#include <functional>
#include <unordered_map>
#include <csignal>
#include <mutex>
#include <cstdint>

/**
 * Generic signal handler for graceful shutdown across all modules
 * Provides thread-safe shutdown notification and cleanup callbacks
 */
class SignalHandler {
public:
    using ShutdownCallback = std::function<void()>;
    using CallbackId = uint64_t;

    // Get singleton instance
    static SignalHandler& getInstance();

    // Initialize signal handling (call once at program start)
    void initialize();

    // Register callback to be called on shutdown; returns an id that can be used to unregister
    CallbackId registerCallback(ShutdownCallback callback);

    // Unregister a previously registered callback
    void unregisterCallback(CallbackId id);

    // Check if shutdown has been requested
    static bool isShutdownRequested();

    // Request shutdown (can be called from signal handler or code)
    static void requestShutdown();

    // Reset shutdown flag so the server can be restarted in the same process
    static void resetShutdown();

    // Get shutdown flag for direct checking
    static const std::atomic<bool>& getShutdownFlag();

private:
    SignalHandler() = default;
    ~SignalHandler() = default;

    // Internal signal handler function
    static void handleSignal(int signal);

    // Signal handler for the previous handler installed on a signal
    // (SIGINT/SIGTERM/SIGBREAK), so it can be chained after our own
    // shutdown handling. Nullptr when there was none.
    static void (*s_prevSigHandlers[3])(int);

    // Static shutdown flag
    static std::atomic<bool> s_shutdownRequested;

    // Map of cleanup callbacks keyed by id
    std::unordered_map<CallbackId, ShutdownCallback> m_callbacks;

    // Mutex for thread-safe callback registration
    static std::mutex s_callbackMutex;
};
