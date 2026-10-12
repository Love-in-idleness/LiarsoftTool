#include "fileio.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;
static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> static void reject(F operation) {
    bool rejected = false;
    try { operation(); } catch (const std::exception&) { rejected = true; }
    require(rejected, "Failed write was not rejected");
}
int main() {
    const auto root = fs::temp_directory_path() / ("liarsoft-fileio-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    const auto target = root / "output";
    const std::vector<uint8_t> old{'o', 'l', 'd'}, next{'n', 'e', 'w'};
    try {
        require(liarsoft::writeFileIfChanged(target.string(), old), "New file not written");
        const auto timestamp = fs::last_write_time(target);
        require(!liarsoft::writeFileIfChanged(target.string(), old) &&
                fs::last_write_time(target) == timestamp, "Identical file was rewritten");
        fs::permissions(target, fs::perms::owner_read);
        reject([&] { liarsoft::writeFileIfChanged(target.string(), next); });
        require(liarsoft::readFile(target) == old, "Read-only target was damaged");
        fs::permissions(target, fs::perms::owner_read | fs::perms::owner_write);
        const auto writablePermissions = fs::status(target).permissions();
#ifdef _WIN32
        const auto lock = CreateFileW(target.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        require(lock != INVALID_HANDLE_VALUE, "Could not lock replacement target");
        reject([&] { liarsoft::writeFileIfChanged(target.string(), next); });
        CloseHandle(lock);
#elif defined(__linux__)
        for (const char* fault : {"write", "close", "replace"}) {
            setenv("LIARSOFT_TEST_IO_FAILURE", fault, 1);
            reject([&] { liarsoft::writeFileIfChanged(target.string(), next); });
            unsetenv("LIARSOFT_TEST_IO_FAILURE");
            require(liarsoft::readFile(target) == old &&
                    fs::last_write_time(target) == timestamp, "Failed write damaged old target");
            for (const auto& entry : fs::directory_iterator(root))
                require(entry.path() == target, "Temporary file leaked after failure");
        }
        const auto link = root / "link";
        fs::create_symlink(target, link);
        require(liarsoft::writeFileIfChanged(link.string(), next) && fs::is_symlink(link) &&
                liarsoft::readFile(target) == next, "Existing symlink behavior changed");
        fs::remove(link);
#endif
        require(liarsoft::writeFileIfChanged(target.string(), old) ||
                liarsoft::readFile(target) == old, "Replacement failed");
        require(fs::status(target).permissions() == writablePermissions, "Permissions changed");
        liarsoft::writeTextFileIfChanged((root / "text").string(), "one\ntwo\n");
#ifdef _WIN32
        const std::string native = "one\r\ntwo\r\n";
#else
        const std::string native = "one\ntwo\n";
#endif
        require(liarsoft::readFile(root / "text") ==
                std::vector<uint8_t>(native.begin(), native.end()), "Text newline behavior changed");
        require(!liarsoft::writeTextFileIfChanged((root / "text").string(), "one\ntwo\n"),
                "Identical text rewritten");
        liarsoft::writeFileIfChanged(target.string(), {});
        require(fs::file_size(target) == 0, "Empty file could not be written");
    } catch (const std::exception& error) {
#if defined(__linux__)
        unsetenv("LIARSOFT_TEST_IO_FAILURE");
#endif
        fs::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
    fs::remove_all(root);
}
