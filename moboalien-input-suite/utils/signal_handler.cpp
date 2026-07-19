#include "signal_handler.h"
#include <iostream>
#include <mutex>
#include <vector>
#include <cstdio>

// Static member definitions
std::atomic<bool> SignalHandler::s_shutdownRequested(false);
std::mutex SignalHandler::s_callbackMutex;

// Internal monotonically-increasing callback ID generator
static std::atomic<uint64_t> s_nextCallbackId(1);

SignalHandler& SignalHandler::getInstance() {
    static SignalHandler instance;
    return instance;
}

void SignalHandler::initialize() {
    // Register signal handlers for common termination signals
    std::signal(SIGINT, handleSignal);   // Ctrl+C
    std::signal(SIGTERM, handleSignal);  // Termination signal
#ifdef SIGBREAK
    std::signal(SIGBREAK, handleSignal); // Windows Ctrl+Break
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
}
