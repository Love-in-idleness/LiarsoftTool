#pragma once

#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace rscript {

constexpr uint32_t max_plugins = 64;
constexpr uint32_t max_packet_size = 1024 * 1024;
constexpr uint32_t max_kinsoku_size = 64 * 1024;

struct HostInitPacket {
    uint32_t size;
    uint32_t abi_version;
    uint32_t total_size;
    uint32_t game_offset;
    uint32_t root_offset;
    uint32_t kinsoku_offset;
    uint32_t plugin_count;
    uint32_t plugin_offsets[max_plugins];
    uint32_t result;
    wchar_t error[1024];
};

std::wstring error_text(DWORD code);
std::wstring utf16(const char* text);
std::string utf8(const std::wstring& text);
std::wstring full_path(const std::wstring& path);
std::wstring module_path(HMODULE module = nullptr);
std::wstring parent_path(const std::wstring& path);
std::wstring quote_argument(const std::wstring& text);
WORD pe_machine(const std::wstring& path);
std::wstring read_kinsoku(const std::wstring& path);
const wchar_t* packet_string(const HostInitPacket* packet, uint32_t offset);

struct LaunchOptions {
    std::wstring game;
    std::wstring root;
    std::vector<std::wstring> plugins;
    bool wait = false;
    std::wstring kinsoku;
};

DWORD launch(const LaunchOptions& options);

} // namespace rscript
