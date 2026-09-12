#include "signal_handler.h"
#include <iostream>
#include <mutex>
#include <vector>
#include <cstdio>

// Static member definitions
std::atomic<bool> SignalHandler::s_shutdownRequested(false);
std::mutex SignalHandler::s_callbackMutex;
void (*SignalHandler::s_prevSigHandlers[3])(int) = {nullptr, nullptr, nullptr};

// Internal monotonically-increasing callback ID generator
static std::atomic<uint64_t> s_nextCallbackId(1);

SignalHandler& SignalHandler::getInstance() {
    static SignalHandler instance;
    return instance;
}

void SignalHandler::initialize() {
    // Save the previously-installed handlers (if any) so they can be
    // chained later, then install ours. std::signal() returns the previous
    // handler (or SIG_ERR on failure) — we must not leak it, otherwise the
    // app's own SIGINT/SIGTERM handling (e.g. RetroArch's frontend_unix
    // quit-on-twice handler) would silently stop working.
    void (*prev)(int) = std::signal(SIGINT, handleSignal);   // Ctrl+C
    s_prevSigHandlers[0] = (prev != SIG_ERR && prev != handleSignal)
        ? prev : nullptr;

    prev = std::signal(SIGTERM, handleSignal);  // Termination signal
    s_prevSigHandlers[1] = (prev != SIG_ERR && prev != handleSignal)
        ? prev : nullptr;

#ifdef SIGBREAK
    prev = std::signal(SIGBREAK, handleSignal); // Windows Ctrl+Break
    s_prevSigHandlers[2] = (prev != SIG_ERR && prev != handleSignal)
        ? prev : nullptr;
#else
    s_prevSigHandlers[2] = nullptr;
#endif
}

SignalHandler::CallbackId SignalHandler::registerCallback(ShutdownCallback callback) {
    std::lock_guard<std::mutex> lock(s_callbackMutex);
    auto id = s_nextCallbackId.fetch_add(1);
    getInstance().m_callbacks.emplace(id, std::move(callback));
    return id;
}

void SignalHandler::unregisterCallback(CallbackId id) {
    std::lock_guard<std::mutex> lock(s_callbackMutex);
    getInstance().m_callbacks.erase(id);
}

bool SignalHandler::isShutdownRequested() {
    return s_shutdownRequested.load();
}

void SignalHandler::requestShutdown() {
    s_shutdownRequested.store(true);

    // Execute all registered callbacks. Copy them out while holding the lock to avoid
    // invoking callbacks while holding the mutex, which could cause deadlocks.
    std::vector<ShutdownCallback> callbacks;
    {
        std::lock_guard<std::mutex> lock(s_callbackMutex);
        for (const auto& kv : getInstance().m_callbacks) callbacks.push_back(kv.second);
    }

    for (const auto& callback : callbacks) {
        try {
            callback();
        } catch (const std::exception& e) {
            std::cerr << "Error in shutdown callback: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown error in shutdown callback" << std::endl;
        }
    }

    // Flush standard streams to ensure all logs are written immediately
    std::cout.flush();
    std::cerr.flush();
    fflush(stdout);
    fflush(stderr);
}

void SignalHandler::resetShutdown() {
    s_shutdownRequested.store(false);
}

const std::atomic<bool>& SignalHandler::getShutdownFlag() {
    return s_shutdownRequested;
}

void SignalHandler::handleSignal(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down gracefully..." << std::endl;
    requestShutdown();

    // Chain to the previously-installed handler so the host application keeps
    // its own signal semantics (e.g. RetroArch's second-signal force-exit).
    void (*prev)(int) = nullptr;
    switch (signal) {
        case SIGINT:   prev = s_prevSigHandlers[0]; break;
        case SIGTERM:  prev = s_prevSigHandlers[1]; break;
#ifdef SIGBREAK
        case SIGBREAK: prev = s_prevSigHandlers[2]; break;
#endif
        default: break;
    }
    if (prev && prev != SIG_DFL && prev != SIG_IGN && prev != handleSignal) {
        prev(signal);
    }
}
