#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <string>
#include <memory>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <thread>
#include <cstdio>
#include <map>
#include <cassert>

#include "controller/i_controller.h"
#include "platform.h"
#include "config_file.h"
#include "utils.h"
#include "i_crypto_helper.h"
#include "packet_types.h"
#include "packet_codec.h"
#include "packet_encryptor.h"

#include "auth/i_auth_service.h"
#include "auth/auth_service.h"
#include "controller/handshake.h"
#include "i_input_injector.h"
#include "i_image_encoder.h"
#include "screen_capture.h"
#include "signal_handler.h"
#include "logger.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#endif

static const char* TAG = "ControllerServer";

static bool IsRunAsAdmin() {
#ifdef _WIN32
    BOOL fIsRunAsAdmin = FALSE;
    PSID pAdministratorsGroup = NULL;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &pAdministratorsGroup)) {
        CheckTokenMembership(NULL, pAdministratorsGroup, &fIsRunAsAdmin);
        FreeSid(pAdministratorsGroup);
    }
    return fIsRunAsAdmin == TRUE;
#else
    return true;
#endif
}

static void SetWorkDirToExePath() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    if (GetModuleFileNameA(NULL, buffer, MAX_PATH)) {
        std::string path(buffer);
        size_t lastSlash = path.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            SetCurrentDirectoryA(path.substr(0, lastSlash).c_str());
        }
    }
#endif
}

struct ServerArgs {
    bool verbose = false;
    int minPressMs = 20;
    int handshakePort = 16235;
    ServerType serverType = ServerType::STANDARD;
};

static ServerArgs ParseArgs(int argc, char* argv[]) {
    ServerArgs args;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--verbose") == 0) {
            args.verbose = true;
        } else if (strcmp(argv[i], "--min-press-ms") == 0 && i + 1 < argc) {
            args.minPressMs = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "--handshake-port") == 0 && i + 1 < argc) {
            args.handshakePort = static_cast<int>(std::atoi(argv[++i]));
        } else if (strcmp(argv[i], "--server-type") == 0 && i + 1 < argc) {
            std::string type = argv[++i];
            if (type == "retroarch") {
                args.serverType = ServerType::RETROARCH;
            }
        }
    }
    return args;
}

struct SessionSecurity {
    uint64_t serverId;
    std::string serverSalt;
    std::vector<uint8_t> sessionKey;
};

static const char* CONFIG_PASSWORD_HASH = "password_hash";
static const char* CONFIG_SERVER_ID = "server_id";

static SessionSecurity InitializeSecurity(ConfigFile& config, ICryptoHelper* crypto, Platform* platform) {
    // Password hash logic
    if (config.GetConfig(CONFIG_PASSWORD_HASH).empty()) {
        LOGE(TAG, "Error: Password not set. Please set a password using the Control Panel. Setting 12345678 as default for now.");
        config.SetConfig(CONFIG_PASSWORD_HASH, crypto->HashText("12345678")); // Default password
    }
    std::string passwordHash = config.GetConfig(CONFIG_PASSWORD_HASH);
    assert(!passwordHash.empty());

    // Server ID logic
    if (config.GetConfig(CONFIG_SERVER_ID).empty()) {
        uint64_t guid = static_cast<uint64_t>(platform->GetTickCountMs()) << 32 | static_cast<uint64_t>(rand());
        config.SetConfig(CONFIG_SERVER_ID, std::to_string(guid));
    }
    uint64_t serverId = std::stoull(config.GetConfig(CONFIG_SERVER_ID));

    // Session key generation
    std::string serverSalt = GenerateRandomSalt(16);
    std::string keyHash = crypto->HashText(passwordHash + serverSalt);
    std::vector<uint8_t> sessionKey = *HexToBytes(keyHash);
    sessionKey.resize(16); // 128-bit key

    return { serverId, serverSalt, sessionKey };
}

//=============================================================================
// Main Function
//=============================================================================

int main(int argc, char* argv[]) {
    SetWorkDirToExePath();

    // Setup Logger
    Logger::GetInstance().SetOutputToFile(true, "server.log");
    Logger::GetInstance().SetOutputToConsole(true);

    ServerArgs args = ParseArgs(argc, argv);
    if (args.verbose) {
        Logger::GetInstance().SetLogLevel(LogLevel::VERBOSE);
        LOGI(TAG, "Verbose logging enabled");
    }
    LOGI(TAG, "Setting minimum press duration to " + std::to_string(args.minPressMs) + "ms");
    LOGI(TAG, "Handshake port set to " + std::to_string(args.handshakePort));

    auto platform = CreatePlatform();
    if (!platform->SocketsInitialize()) {
        LOGE(TAG, "Socket initialization failed");
        return 1;
    }
    
    ConfigFile mConfig("controller.cfg");
    ICryptoHelperPtr cryptoHelper = CreateCryptoHelper();
    SessionSecurity security = InitializeSecurity(mConfig, cryptoHelper.get(), platform.get());

    LOGI(TAG, "Server salt: " + security.serverSalt);
    LOGI(TAG, "Session Key: " + BytesToHex(security.sessionKey));

    uint64_t currentServerTicks = platform->GetTickCountMs();
    auto packetEncryptor = std::make_shared<PacketEncryptor>(
        cryptoHelper,  
        platform.get(), 
        currentServerTicks, 
        currentServerTicks, // Server time is same as client time for server-side
        security.sessionKey
    );
    auto inputInjector = CreateInputInjector();

    // Dependency-inject in-process screen capture and image encoder so the controller
    // can use them when starting an in-process screen server.
    auto imageEncoder = CreateImageEncoder();
    auto capture = CreateScreenCapture(platform.get());

    auto server = CreateController(platform.get(), inputInjector.get(), args.verbose, args.serverType, packetEncryptor.get(), imageEncoder.get(), capture.get());
    server->SetMinPressDuration(args.minPressMs);

    // Start handshake manager
    auto authService = CreateAuthService(cryptoHelper.get());
    HandshakeManager handshakeManager(
        platform.get(),
        authService.get(),
        security.sessionKey,
        security.serverSalt,
        static_cast<uint16_t>(args.handshakePort),
        server->GetControllerPort(),
        server->GetScreenPort(),
        security.serverId,
        0,
        true
    );
    handshakeManager.Start();

    server->Run();
    // Gracefully stop handshake manager when server exits
    handshakeManager.Stop();

    // Cleanup sockets
    platform->SocketsCleanup();

    return 0;
}