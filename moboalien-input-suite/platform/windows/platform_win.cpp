#include "platform.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <memory>
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#include <winsock2.h>
#include <process.h>
#include <ws2tcpip.h>
#include <vector>
#include <cstdio>
#include <string>
#include <avrt.h>
#pragma comment(lib, "Avrt.lib")
#include "utils.h"
static const char* TAG = "PlatformWin";

class PlatformWin final : public Platform {
public:
    // Sockets
    bool SocketsInitialize() override;
    void SocketsCleanup() override;
    void CloseSocket(uintptr_t socket) override;
    uintptr_t CreateListenSocket(int port, int backlog) override;
    uintptr_t CreateUDPSocket(int port) override;
    uintptr_t AcceptConnection(uintptr_t listen_socket, std::string& out_client_ip, int& out_client_port) override;
    bool SetSocketSendBuffer(uintptr_t socket, int size) override;
    int Send(uintptr_t socket, const char* buffer, int len, int flags) override;
    int Recv(uintptr_t socket, char* buffer, int len, int flags) override;
    bool SetSocketNonBlocking(uintptr_t socket, bool non_blocking) override;
    int GetSocketSendBuffer(uintptr_t socket) override;
    int GetSocketPort(uintptr_t socket) override;
    int RecvFrom(uintptr_t socket, char* buffer, int len, int flags, std::string& out_client_ip, int& out_client_port) override;
    int SendTo(uintptr_t socket, const char* buffer, int len, int flags, const std::string& dest_ip, int dest_port) override;

    // Shared Memory
    SharedMemoryHandle CreateSharedMemory(const char* name, size_t size, void** out_ptr) override;
    void CloseSharedMemory(SharedMemoryHandle handle) override;

    // System Info
    void GetScreenDimensions(int& width, int& height) override;
    void GetCursorPosition(int& x, int& y) override;
    int GetPhysicalCoreCount() override;

    // Threads
    bool CreateThread(ThreadHandle* thread, ThreadRoutine start_routine, void* arg) override;
    void JoinThread(ThreadHandle thread) override;
    void DetachThread(ThreadHandle thread) override;
    void Sleep(int milliseconds) override;

    // Process Management
    ProcessHandle CreateNewProcess(const std::string& commandLine, bool inheritHandles = false) override;
    bool TerminateProcess(ProcessHandle process, int exitCode) override;
    bool IsProcessRunning(ProcessHandle process) override;
    void CloseProcessHandle(ProcessHandle process) override;

    // Timing
    uint64_t GetTickCountMs() override;
    void SetTimerResolution(int period) override;

    // Priority
    void SetCurrentThreadHighPriority() override;
    void SetProcessHighPriority() override;
    bool EnableMMCSSForCurrentThread() override;
    void SetCurrentThreadAffinity(int cpuCore) override;

    // Events
    EventHandle CreateNewEvent() override;
    void SetEvent(EventHandle event) override;
    bool WaitForEvent(EventHandle event, int timeoutMs) override;
    void CloseEvent(EventHandle event) override;
};

struct Platform::SharedMemoryHandle_t final {
    HANDLE hMapFile;
    void* ptr;
};

struct Platform::ThreadHandle_t final {
    HANDLE handle;
};

struct Platform::MutexHandle_t final {
    CRITICAL_SECTION cs;
};

struct Platform::ProcessHandle_t final {
    HANDLE handle;
};

struct Platform::EventHandle_t final {
    HANDLE handle;
};

bool PlatformWin::SocketsInitialize() {
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
}

void PlatformWin::SocketsCleanup() {
    WSACleanup();
}

void PlatformWin::CloseSocket(uintptr_t socket) {
    ::closesocket(socket);
}

uintptr_t PlatformWin::CreateListenSocket(int port, int backlog) {
    uintptr_t listen_socket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_socket == INVALID_SOCKET) {
        return INVALID_SOCKET_HANDLE;
    }

    sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;

    // Allow immediate address reuse where possible so that restarting the
    // in-process server can rebind to the same port even if previous
    // connections are in TIME_WAIT.
    int reuse = 1;
    if (setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse)) == SOCKET_ERROR) {
        int error = WSAGetLastError();
        LOGW(TAG, "setsockopt(SO_REUSEADDR) failed for listen socket, error: " + std::to_string(error));
        // We continue even if setsockopt fails; bind may still succeed.
    }

    bool bound = false;
    addr.sin_port = htons(port);
    if (::bind(listen_socket, (sockaddr*)&addr, sizeof(addr)) != 0) {
        CloseSocket(listen_socket);
        return INVALID_SOCKET_HANDLE;
    }

    if (::listen(listen_socket, backlog) < 0) {
        CloseSocket(listen_socket);
        return INVALID_SOCKET_HANDLE;
    }

    return listen_socket;
}

uintptr_t PlatformWin::CreateUDPSocket(int port) {
    uintptr_t udpSock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSock == INVALID_SOCKET) {
        LOGE(TAG, "Socket creation failed, error: " + std::to_string(WSAGetLastError()));
        return INVALID_SOCKET_HANDLE;
    }
    
    // Set SO_REUSEADDR to allow immediate reuse of the port
    int reuse = 1;
    if (setsockopt(udpSock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse)) == SOCKET_ERROR) {
        int error = WSAGetLastError();
        LOGW(TAG, "setsockopt(SO_REUSEADDR) failed, error: " + std::to_string(error));
        // Don't fail the whole socket creation if SO_REUSEADDR fails
        // Just continue without it
    }
    
    // Set IP_TOS for DSCP (Expedited Forwarding / High Priority)
    // DSCP 46 (EF) shifted by 2 bits = 0xB8. Recommended for low latency real-time media.
    int tos = 0xB8;
    if (setsockopt(udpSock, IPPROTO_IP, IP_TOS, (char*)&tos, sizeof(tos)) == SOCKET_ERROR) {
        LOGW(TAG, "setsockopt(IP_TOS) failed, error: " + std::to_string(WSAGetLastError()));
    }
    
    sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (::bind(udpSock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        int error = WSAGetLastError();
        LOGE(TAG, "Bind failed on port " + std::to_string(port) + ", error: " + std::to_string(error));
        CloseSocket(udpSock);
        return INVALID_SOCKET_HANDLE;
    }
    return udpSock;
}

uintptr_t PlatformWin::AcceptConnection(uintptr_t listen_socket, std::string& out_client_ip, int& out_client_port) {
    sockaddr_in clientAddr;
    int addrLen = sizeof(clientAddr);
    uintptr_t client_socket = ::accept(listen_socket, (sockaddr*)&clientAddr, &addrLen);

    if (client_socket != INVALID_SOCKET) {
        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
        out_client_ip = clientIP;
        out_client_port = ntohs(clientAddr.sin_port);

        // Disable Nagle algorithm to reduce latency for small packets
        int nodelay = 1;
        if (::setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay)) == SOCKET_ERROR) {
            int err = WSAGetLastError();
            LOGW(TAG, "setsockopt(TCP_NODELAY) failed on client socket, error: " + std::to_string(err));
        }
    }
    return client_socket;
}

bool PlatformWin::SetSocketSendBuffer(uintptr_t socket, int size) {
    return ::setsockopt(socket, SOL_SOCKET, SO_SNDBUF, (const char*)&size, sizeof(size)) == 0;
}

int PlatformWin::Send(uintptr_t socket, const char* buffer, int len, int flags) {
    int result = ::send(socket, buffer, len, flags);
    if (result == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) {
            return Platform::SEND_WOULDBLOCK;
        }
        LOGE(TAG, "Send failed with error: " + std::to_string(err));
    }
    return result;
}

int PlatformWin::Recv(uintptr_t socket, char* buffer, int len, int flags) {
    return ::recv(socket, buffer, len, flags);
}

bool PlatformWin::SetSocketNonBlocking(uintptr_t socket, bool non_blocking) {
    u_long mode = non_blocking ? 1 : 0;
    return ::ioctlsocket(socket, FIONBIO, &mode) == 0;
}

int PlatformWin::GetSocketSendBuffer(uintptr_t socket) {
    int size = 0;
    int len = sizeof(size);
    if (::getsockopt(socket, SOL_SOCKET, SO_SNDBUF, (char*)&size, &len) == 0) { // NOLINT(win32-narrowing)
        return size;
    }
    return -1; // Error
}

int PlatformWin::RecvFrom(uintptr_t socket, char* buffer, int len, int flags, std::string& out_client_ip, int& out_client_port) {
    sockaddr_in clientAddr;
    int clientAddrLen = sizeof(clientAddr);
    int bytesReceived = ::recvfrom(socket, buffer, len, flags, (sockaddr*)&clientAddr, &clientAddrLen);

    if (bytesReceived > 0) {
        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
        out_client_ip = clientIP;
        out_client_port = ntohs(clientAddr.sin_port);
    }

    return bytesReceived;
}

int PlatformWin::GetSocketPort(uintptr_t socket) {
    if (socket == INVALID_SOCKET) return -1;
    sockaddr_in addr;
    int addrLen = sizeof(addr);
    if (::getsockname(socket, (sockaddr*)&addr, &addrLen) != 0) {
        return -1;
    }
    return ntohs(addr.sin_port);
}

int PlatformWin::SendTo(uintptr_t socket, const char* buffer, int len, int flags, const std::string& dest_ip, int dest_port) {
    sockaddr_in destAddr = {0};
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(dest_port);
    if (inet_pton(AF_INET, dest_ip.c_str(), &destAddr.sin_addr) != 1) {
        return -1;
    }
    return ::sendto(socket, buffer, len, flags, (sockaddr*)&destAddr, sizeof(destAddr));
}

void PlatformWin::GetScreenDimensions(int& width, int& height) {
    width = ::GetSystemMetrics(SM_CXSCREEN);
    height = ::GetSystemMetrics(SM_CYSCREEN);
}

void PlatformWin::GetCursorPosition(int& x, int& y) {
    POINT p;
    if (::GetCursorPos(&p)) {
        x = p.x;
        y = p.y;
    } else {
        x = 0;
        y = 0;
    }
}

int PlatformWin::GetPhysicalCoreCount() {
    DWORD returnLength = 0;
    // Call once to get the required buffer size
    ::GetLogicalProcessorInformation(NULL, &returnLength);
    if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
        return 1; // Fallback to 1 if we can't query the system
    }

    std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buffer(returnLength / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
    if (!::GetLogicalProcessorInformation(buffer.data(), &returnLength)) {
        return 1;
    }

    int physicalCores = 0;
    for (const auto& info : buffer) {
        if (info.Relationship == RelationProcessorCore) {
            physicalCores++;
        }
    }

    return (physicalCores > 0) ? physicalCores : 1;
}

Platform::SharedMemoryHandle PlatformWin::CreateSharedMemory(const char* name, size_t size, void** out_ptr) {
    HANDLE hMapFile = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, (DWORD)size, name);
    if (!hMapFile) {
        return nullptr;
    }

    void* ptr = MapViewOfFile(hMapFile, FILE_MAP_WRITE, 0, 0, size);
    if (!ptr) {
        CloseHandle(hMapFile);
        return nullptr;
    }

    SharedMemoryHandle handle = new SharedMemoryHandle_t;
    handle->hMapFile = hMapFile;
    handle->ptr = ptr;
    *out_ptr = ptr;
    return handle;
}

void PlatformWin::CloseSharedMemory(Platform::SharedMemoryHandle handle) {
    if (handle) {
        UnmapViewOfFile(handle->ptr);
        CloseHandle(handle->hMapFile);
        delete handle;
    }
}

bool PlatformWin::CreateThread(Platform::ThreadHandle* thread, ThreadRoutine start_routine, void* arg) {
    *thread = new ThreadHandle_t;
    (*thread)->handle = (HANDLE)_beginthreadex(NULL, 0, (unsigned int(__stdcall*)(void*))start_routine, arg, 0, NULL);
    return (*thread)->handle != NULL;
}

void PlatformWin::JoinThread(Platform::ThreadHandle thread) {
    if (thread && thread->handle) {
        WaitForSingleObject(thread->handle, INFINITE);
        CloseHandle(thread->handle);
        delete thread;
    }
}

void PlatformWin::DetachThread(Platform::ThreadHandle thread) {
    if (thread && thread->handle) {
        CloseHandle(thread->handle);
        delete thread;
    }
}

Platform::ProcessHandle PlatformWin::CreateNewProcess(const std::string& commandLine, bool inheritHandles) {
    // Convert std::string to std::wstring
    std::wstring wCommandLine(commandLine.begin(), commandLine.end());

    // CreateProcessW requires a mutable buffer for the command line.
    // Make a copy that can be modified.
    std::vector<wchar_t> cmdLineBuffer(wCommandLine.begin(), wCommandLine.end());
    cmdLineBuffer.push_back(L'\0'); // Ensure null-termination

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;

    if (::CreateProcessW(NULL, cmdLineBuffer.data(), NULL, NULL, inheritHandles, 0, NULL, NULL, &si, &pi)) {
        ProcessHandle handle = new ProcessHandle_t;
        handle->handle = pi.hProcess;
        ::CloseHandle(pi.hThread); // We don't need the thread handle.
        return handle;
    }
    return nullptr;
}

bool PlatformWin::TerminateProcess(Platform::ProcessHandle process, int exitCode) {
    if (process && process->handle) {
        return ::TerminateProcess(process->handle, exitCode);
    }
    return false;
}

bool PlatformWin::IsProcessRunning(Platform::ProcessHandle process) {
    if (process && process->handle) {
        DWORD exitCode;
        return ::GetExitCodeProcess(process->handle, &exitCode) && exitCode == STILL_ACTIVE;
    }
    return false;
}

void PlatformWin::CloseProcessHandle(Platform::ProcessHandle process) {
    if (process && process->handle) {
        ::CloseHandle(process->handle);
        delete process;
    }
}

void PlatformWin::Sleep(int milliseconds) {
    ::Sleep(milliseconds);
}

uint64_t PlatformWin::GetTickCountMs() {
    return ::GetTickCount64();
}

void PlatformWin::SetTimerResolution(int period) {
    ::timeBeginPeriod(period);
}

void PlatformWin::SetCurrentThreadHighPriority() {
    ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
}

bool PlatformWin::EnableMMCSSForCurrentThread() {
    // Use Multimedia Class Scheduler Service (MMCSS) for better real-time performance
    // without starving other system processes. This is the modern, preferred way for
    // latency-sensitive multimedia applications.
    DWORD taskIndex = 0;
    // "Games" is for video/display work. "Pro Audio" is for audio threads only.
    HANDLE hTask = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
    if (hTask) {
        LOGI(TAG, "Successfully registered thread with MMCSS task 'Pro Audio'.");
        // Set the relative priority within the MMCSS class to critical.
        if (!AvSetMmThreadPriority(hTask, AVRT_PRIORITY_CRITICAL)) {
            LOGW(TAG, "Failed to set MMCSS thread priority.");
        }
        return true;
    }
    LOGW(TAG, "Failed to register thread with MMCSS (is service running?).");
    return false;
}

void PlatformWin::SetProcessHighPriority() {
    if (!::SetPriorityClass(::GetCurrentProcess(), HIGH_PRIORITY_CLASS)) {
        LOGW(TAG, "Failed to set process priority class to HIGH, error: " + std::to_string(::GetLastError()));
    } else {
        LOGI(TAG, "Process priority class set to HIGH.");
    }
}

void PlatformWin::SetCurrentThreadAffinity(int cpuCore) {
    DWORD_PTR affinityMask = 1ULL << cpuCore;
    ::SetThreadAffinityMask(::GetCurrentThread(), affinityMask);
}

Platform::EventHandle PlatformWin::CreateNewEvent() {
    HANDLE hEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!hEvent) return nullptr;
    EventHandle handle = new EventHandle_t;
    handle->handle = hEvent;
    return handle;
}

void PlatformWin::SetEvent(Platform::EventHandle event) {
    if (event && event->handle) {
        ::SetEvent(event->handle);
    }
}

bool PlatformWin::WaitForEvent(Platform::EventHandle event, int timeoutMs) {
    if (!event || !event->handle) return false;
    DWORD result = ::WaitForSingleObject(event->handle, timeoutMs == -1 ? INFINITE : timeoutMs);
    return result == WAIT_OBJECT_0;
}

void PlatformWin::CloseEvent(Platform::EventHandle event) {
    if (event && event->handle) {
        ::CloseHandle(event->handle);
        delete event;
    }
}

std::unique_ptr<Platform> CreatePlatform() {
    return std::make_unique<PlatformWin>();
}