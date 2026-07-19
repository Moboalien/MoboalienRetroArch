#include "platform.h"
#include "logger.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/resource.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <string>
#include <memory>

static const char* TAG = "PlatformAndroid";

// ---------------------------------------------------------------------------
// Opaque handle definitions
// ---------------------------------------------------------------------------

struct Platform::SharedMemoryHandle_t {
    void*  ptr;
    size_t size;
};

struct Platform::ThreadHandle_t {
    pthread_t thread;
};

struct Platform::MutexHandle_t {
    pthread_mutex_t mutex;
};

struct Platform::ProcessHandle_t {};

struct Platform::EventHandle_t {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    bool            signaled;
};

// ---------------------------------------------------------------------------
// PlatformAndroid
// ---------------------------------------------------------------------------

class PlatformAndroid final : public Platform {
public:
    // Sockets ----------------------------------------------------------------
    bool SocketsInitialize() override { return true; }
    void SocketsCleanup()    override {}

    void CloseSocket(uintptr_t socket) override {
        ::close(static_cast<int>(socket));
    }

    uintptr_t CreateListenSocket(int port, int backlog) override {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return INVALID_SOCKET_HANDLE;
        int reuse = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port        = htons(static_cast<uint16_t>(port));
        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 ||
            ::listen(fd, backlog) < 0) {
            ::close(fd);
            return INVALID_SOCKET_HANDLE;
        }
        return static_cast<uintptr_t>(fd);
    }

    uintptr_t CreateUDPSocket(int port) override {
        int fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (fd < 0) {
            LOGE(TAG, "socket() failed: " + std::string(strerror(errno)));
            return INVALID_SOCKET_HANDLE;
        }
        int reuse = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port        = htons(static_cast<uint16_t>(port));
        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            LOGE(TAG, "bind() failed on port " + std::to_string(port) + ": " + strerror(errno));
            ::close(fd);
            return INVALID_SOCKET_HANDLE;
        }
        return static_cast<uintptr_t>(fd);
    }

    int GetSocketPort(uintptr_t socket) override {
        sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        if (::getsockname(static_cast<int>(socket), reinterpret_cast<sockaddr*>(&addr), &len) < 0)
            return -1;
        return ntohs(addr.sin_port);
    }

    uintptr_t AcceptConnection(uintptr_t listen_socket, std::string& out_ip, int& out_port) override {
        sockaddr_in clientAddr{};
        socklen_t addrLen = sizeof(clientAddr);
        int fd = ::accept(static_cast<int>(listen_socket),
                          reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
        if (fd < 0) return INVALID_SOCKET_HANDLE;
        char buf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, buf, sizeof(buf));
        out_ip   = buf;
        out_port = ntohs(clientAddr.sin_port);
        return static_cast<uintptr_t>(fd);
    }

    bool SetSocketSendBuffer(uintptr_t socket, int size) override {
        return ::setsockopt(static_cast<int>(socket), SOL_SOCKET, SO_SNDBUF,
                            &size, sizeof(size)) == 0;
    }

    int GetSocketSendBuffer(uintptr_t socket) override {
        int size = 0; socklen_t len = sizeof(size);
        ::getsockopt(static_cast<int>(socket), SOL_SOCKET, SO_SNDBUF, &size, &len);
        return size;
    }

    int Send(uintptr_t socket, const char* buffer, int len, int flags) override {
        int r = static_cast<int>(::send(static_cast<int>(socket), buffer, len, flags));
        if (r < 0 && errno == EAGAIN) return SEND_WOULDBLOCK;
        return r;
    }

    int Recv(uintptr_t socket, char* buffer, int len, int flags) override {
        return static_cast<int>(::recv(static_cast<int>(socket), buffer, len, flags));
    }

    bool SetSocketNonBlocking(uintptr_t socket, bool non_blocking) override {
        int flags = ::fcntl(static_cast<int>(socket), F_GETFL, 0);
        if (flags < 0) return false;
        flags = non_blocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
        return ::fcntl(static_cast<int>(socket), F_SETFL, flags) == 0;
    }

    int RecvFrom(uintptr_t socket, char* buffer, int len, int flags,
                 std::string& out_ip, int& out_port) override {
        sockaddr_in clientAddr{};
        socklen_t addrLen = sizeof(clientAddr);
        int r = static_cast<int>(::recvfrom(static_cast<int>(socket), buffer, len, flags,
                                             reinterpret_cast<sockaddr*>(&clientAddr), &addrLen));
        if (r > 0) {
            char buf[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clientAddr.sin_addr, buf, sizeof(buf));
            out_ip   = buf;
            out_port = ntohs(clientAddr.sin_port);
        }
        return r;
    }

    int SendTo(uintptr_t socket, const char* buffer, int len, int flags,
               const std::string& dest_ip, int dest_port) override {
        sockaddr_in destAddr{};
        destAddr.sin_family = AF_INET;
        destAddr.sin_port   = htons(static_cast<uint16_t>(dest_port));
        if (::inet_pton(AF_INET, dest_ip.c_str(), &destAddr.sin_addr) != 1) return -1;
        return static_cast<int>(::sendto(static_cast<int>(socket), buffer, len, flags,
                                          reinterpret_cast<sockaddr*>(&destAddr), sizeof(destAddr)));
    }

    // Shared memory -- backed by heap (injector goes direct via JNI) ---------
    SharedMemoryHandle CreateSharedMemory(const char*, size_t size, void** out_ptr) override {
        void* mem = ::malloc(size);
        if (!mem) return nullptr;
        auto* h = new SharedMemoryHandle_t{mem, size};
        *out_ptr = mem;
        return h;
    }

    void CloseSharedMemory(SharedMemoryHandle handle) override {
        if (handle) { ::free(handle->ptr); delete handle; }
    }

    // System info -- stubs (screen streaming out of scope) -------------------
    void GetScreenDimensions(int& width, int& height) override { width = 0; height = 0; }
    void GetCursorPosition(int& x, int& y)             override { x = 0; y = 0; }
    int  GetPhysicalCoreCount()                         override { return 4; }

    // Threads ----------------------------------------------------------------
    bool CreateThread(ThreadHandle* thread, ThreadRoutine start_routine, void* arg) override {
        auto* h = new ThreadHandle_t{};
        if (pthread_create(&h->thread, nullptr,
                           reinterpret_cast<void*(*)(void*)>(start_routine), arg) != 0) {
            delete h;
            return false;
        }
        *thread = h;
        return true;
    }

    void JoinThread(ThreadHandle thread) override {
        if (thread) { pthread_join(thread->thread, nullptr); delete thread; }
    }

    void DetachThread(ThreadHandle thread) override {
        if (thread) { pthread_detach(thread->thread); delete thread; }
    }

    void Sleep(int milliseconds) override {
        ::usleep(static_cast<useconds_t>(milliseconds) * 1000u);
    }

    // Process management -- not supported on Android -------------------------
    ProcessHandle CreateNewProcess(const std::string&, bool) override { return nullptr; }
    bool          TerminateProcess(ProcessHandle, int)        override { return false; }
    bool          IsProcessRunning(ProcessHandle)             override { return false; }
    void          CloseProcessHandle(ProcessHandle)           override {}

    // Timing -----------------------------------------------------------------
    uint64_t GetTickCountMs() override {
        struct timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1000ULL +
               static_cast<uint64_t>(ts.tv_nsec) / 1000000ULL;
    }

    void SetTimerResolution(int) override {}

    // Priority ---------------------------------------------------------------
    void SetCurrentThreadHighPriority() override {
        setpriority(PRIO_PROCESS, 0, -16); // THREAD_PRIORITY_AUDIO
    }

    void SetProcessHighPriority()      override { SetCurrentThreadHighPriority(); }
    bool EnableMMCSSForCurrentThread() override { return false; }
    void SetCurrentThreadAffinity(int) override {}

    // Events (auto-reset condvar) --------------------------------------------
    EventHandle CreateNewEvent() override {
        auto* h = new EventHandle_t{};
        pthread_mutex_init(&h->mutex, nullptr);
        pthread_cond_init(&h->cond, nullptr);
        h->signaled = false;
        return h;
    }

    void SetEvent(EventHandle event) override {
        if (!event) return;
        pthread_mutex_lock(&event->mutex);
        event->signaled = true;
        pthread_cond_signal(&event->cond);
        pthread_mutex_unlock(&event->mutex);
    }

    bool WaitForEvent(EventHandle event, int timeoutMs) override {
        if (!event) return false;
        pthread_mutex_lock(&event->mutex);
        bool result = false;
        if (!event->signaled) {
            if (timeoutMs < 0) {
                pthread_cond_wait(&event->cond, &event->mutex);
                result = true;
            } else {
                struct timespec ts{};
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_sec  += timeoutMs / 1000;
                ts.tv_nsec += (timeoutMs % 1000) * 1000000L;
                if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
                result = (pthread_cond_timedwait(&event->cond, &event->mutex, &ts) == 0);
            }
        } else {
            result = true;
        }
        event->signaled = false; // auto-reset
        pthread_mutex_unlock(&event->mutex);
        return result;
    }

    void CloseEvent(EventHandle event) override {
        if (!event) return;
        pthread_mutex_destroy(&event->mutex);
        pthread_cond_destroy(&event->cond);
        delete event;
    }
};

std::unique_ptr<Platform> CreatePlatform() {
    return std::make_unique<PlatformAndroid>();
}
