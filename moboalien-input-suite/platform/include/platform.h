#pragma once

#include <cstdint>
#include <string>
#include <memory>

class Platform {
public:
    virtual ~Platform() = default;

    // Opaque handles for platform-specific types
    struct SharedMemoryHandle_t;
    struct ThreadHandle_t;
    struct MutexHandle_t;
    struct ProcessHandle_t;
    struct EventHandle_t;
    typedef SharedMemoryHandle_t* SharedMemoryHandle;
    typedef ThreadHandle_t* ThreadHandle;
    typedef MutexHandle_t* MutexHandle;
    typedef ProcessHandle_t* ProcessHandle;
    typedef EventHandle_t* EventHandle;

    static const uintptr_t INVALID_HANDLE = 0;
    static const uintptr_t INVALID_SOCKET_HANDLE = (~(uintptr_t)0);

    // Sockets
    virtual bool SocketsInitialize() = 0;
    virtual void SocketsCleanup() = 0;
    virtual void CloseSocket(uintptr_t socket) = 0;
    virtual uintptr_t CreateListenSocket(int port, int backlog) = 0;
    virtual uintptr_t CreateUDPSocket(int port) = 0;
    virtual int GetSocketPort(uintptr_t socket) = 0;
    virtual uintptr_t AcceptConnection(uintptr_t listen_socket, std::string& out_client_ip, int& out_client_port) = 0;
    virtual bool SetSocketSendBuffer(uintptr_t socket, int size) = 0;
    virtual int Send(uintptr_t socket, const char* buffer, int len, int flags) = 0;
    virtual int Recv(uintptr_t socket, char* buffer, int len, int flags) = 0;
    virtual bool SetSocketNonBlocking(uintptr_t socket, bool non_blocking) = 0;
    virtual int GetSocketSendBuffer(uintptr_t socket) = 0;
    virtual int RecvFrom(uintptr_t socket, char* buffer, int len, int flags, std::string& out_client_ip, int& out_client_port) = 0;
    virtual int SendTo(uintptr_t socket, const char* buffer, int len, int flags, const std::string& dest_ip, int dest_port) = 0;

    // Shared Memory
    virtual SharedMemoryHandle CreateSharedMemory(const char* name, size_t size, void** out_ptr) = 0;
    virtual void CloseSharedMemory(SharedMemoryHandle handle) = 0;

    // System Info
    virtual void GetScreenDimensions(int& width, int& height) = 0;
    virtual void GetCursorPosition(int& x, int& y) = 0;
    virtual int GetPhysicalCoreCount() = 0;

    // Scatter-Gather I/O
    struct Buffer {
        const void* data;
        size_t len;
    };

    static const int SEND_ERROR = -1;
    static const int SEND_WOULDBLOCK = -2;

    // Threads
    typedef void* (*ThreadRoutine)(void*);

    virtual bool CreateThread(ThreadHandle* thread, ThreadRoutine start_routine, void* arg) = 0;
    virtual void JoinThread(ThreadHandle thread) = 0;
    virtual void DetachThread(ThreadHandle thread) = 0;
    virtual void Sleep(int milliseconds) = 0;

    // Process Management
    virtual ProcessHandle CreateNewProcess(const std::string& commandLine, bool inheritHandles = false) = 0;
    virtual bool TerminateProcess(ProcessHandle process, int exitCode) = 0;
    virtual bool IsProcessRunning(ProcessHandle process) = 0;
    virtual void CloseProcessHandle(ProcessHandle process) = 0;

    // Timing
    virtual uint64_t GetTickCountMs() = 0;
    virtual void SetTimerResolution(int period) = 0;

    // Priority
    virtual void SetCurrentThreadHighPriority() = 0;
    virtual void SetProcessHighPriority() = 0;
    virtual bool EnableMMCSSForCurrentThread() = 0;
    virtual void SetCurrentThreadAffinity(int cpuCore) = 0;

    // Events
    virtual EventHandle CreateNewEvent() = 0;
    virtual void SetEvent(EventHandle event) = 0;
    virtual bool WaitForEvent(EventHandle event, int timeoutMs) = 0;
    virtual void CloseEvent(EventHandle event) = 0;
};

std::unique_ptr<Platform> CreatePlatform();