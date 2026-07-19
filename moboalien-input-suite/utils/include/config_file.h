#pragma once

#include <string>
#include <unordered_map>
#include <mutex>

class ConfigFile {
public:
    explicit ConfigFile(const std::string& path = "controller.cfg");

    // Returns the value for the given name if present
    std::string GetConfig(const std::string& name) const;

    // Sets the value (adding or updating) and writes the file. Returns true on success.
    bool SetConfig(const std::string& name, const std::string& value);

private:
    // Thread-safe entry points
    bool Load() const;
    bool Save() const;

    // Helpers that assume the mutex is already locked (used internally)
    bool LoadUnlocked() const;
    bool SaveUnlocked() const;

    std::string m_path;
    // mutable so const GetConfig can lazily load
    mutable std::unordered_map<std::string, std::string> m_cache;
    mutable bool m_loaded = false;
    mutable std::mutex m_mutex;
};
