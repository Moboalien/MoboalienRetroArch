#include "config_file.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>

static inline std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

ConfigFile::ConfigFile(const std::string& path)
    : m_path(path) {}

bool ConfigFile::Load() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return LoadUnlocked();
}

bool ConfigFile::LoadUnlocked() const {
    if (m_loaded) return true;
    m_cache.clear();
    std::ifstream in(m_path);
    if (!in.is_open()) {
        m_loaded = true; // treat missing file as empty
        return true;
    }

    std::string line;
    while (std::getline(in, line)) {
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string name = trim(line.substr(0, pos));
        std::string value = trim(line.substr(pos + 1));
        if (!name.empty()) {
            m_cache[name] = value;
        }
    }
    m_loaded = true;
    return true;
}

bool ConfigFile::Save() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return SaveUnlocked();
}

bool ConfigFile::SaveUnlocked() const {
    std::ofstream out(m_path, std::ofstream::trunc);
    if (!out.is_open()) return false;
    for (const auto& kv : m_cache) {
        out << kv.first << "=" << kv.second << "\n";
    }
    return true;
}

std::string ConfigFile::GetConfig(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!LoadUnlocked()) return "";
    auto it = m_cache.find(name);
    if (it == m_cache.end()) return "";
    return it->second;
}

bool ConfigFile::SetConfig(const std::string& name, const std::string& value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!LoadUnlocked()) return false;
    m_cache[name] = value;
    return SaveUnlocked();
}
