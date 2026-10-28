#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <commctrl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <chrono>
#include "pe_fixture.h"

namespace fs = std::filesystem;
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
BOOL CALLBACK findWindow(HWND candidate, LPARAM pointer) {
    auto* pair = reinterpret_cast<std::pair<DWORD,HWND*>*>(pointer);
    DWORD pid = 0; GetWindowThreadProcessId(candidate, &pid);
    wchar_t type[64]{}; GetClassNameW(candidate, type, 64);
    if (pid == pair->first && std::wstring(type) == L"RScriptLauncher") { *pair->second = candidate; return FALSE; }
    return TRUE;
}
std::wstring text(HWND window) {
    std::wstring value(size_t(SendMessageW(window, WM_GETTEXTLENGTH, 0, 0)) + 1, L'\0');
    SendMessageW(window, WM_GETTEXT, value.size(), reinterpret_cast<LPARAM>(value.data()));
    value.resize(wcslen(value.c_str()));
    return value;
}
template<class F> bool wait(F condition) {
    for (unsigned i = 0; i < 1000; ++i) {
        if (condition()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}
}
int wmain(int argc, wchar_t** argv) {
    PROCESS_INFORMATION process{};
    fs::path root;
    HWND window = nullptr;
    try {
        require(argc == 2, "Expected launcher path");
        root = fs::temp_directory_path() / (L"preflight UI 中文 Русский " + std::to_wstring(GetCurrentProcessId()));
        fs::create_directories(root / "scr");
        PeFixture game;
        for (const auto* name : {"CreateFileA", "ReadFile", "SetFilePointer", "GetFileSize", "CloseHandle"}) game.addImport(name);
        const auto exe = root / "game.exe";
        {
            std::ofstream output(exe, std::ios::binary);
            output.write(reinterpret_cast<const char*>(game.bytes.data()), game.bytes.size());
        }
        std::string script = ";@gsc-byte-format modern-36\n;@gsc-schema rscript18\n";
        for (unsigned i = 0; i < 200; ++i) script += "*font 1 0 0 0 0 \"native\"\n";
        script += "*end\n";
        for (unsigned i = 0; i < 500; ++i) {
            std::ofstream output(root / "scr" / (std::to_string(i) + ".tsc")); output << script;
        }
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        std::wstring command = L"\"" + std::wstring(argv[1]) + L"\"";
        require(CreateProcessW(argv[1], command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process), "Cannot run launcher fixture");
        std::pair<DWORD,HWND*> find{process.dwProcessId, &window};
        require(wait([&] { EnumWindows(findWindow, reinterpret_cast<LPARAM>(&find)); return window != nullptr; }), "Launcher window did not appear");
        require(wait([&] { return IsWindowVisible(window) && GetDlgItem(window, 109); }), "Launcher controls did not finish initialization");
        SendMessageW(GetDlgItem(window, 101), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(exe.c_str()));
        SendMessageW(GetDlgItem(window, 103), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(root.c_str()));
        const auto list = GetDlgItem(window, 105);
        LVITEMW checked{};
        checked.stateMask = LVIS_STATEIMAGEMASK;
        checked.state = INDEXTOSTATEIMAGEMASK(2);
        auto* remoteItem = VirtualAllocEx(process.hProcess, nullptr, sizeof(checked), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        require(remoteItem != nullptr, "Cannot allocate list-view test state");
        SIZE_T written = 0;
        require(WriteProcessMemory(process.hProcess, remoteItem, &checked, sizeof(checked), &written) && written == sizeof(checked), "Cannot set list-view test state");
        for (unsigned index = 0; index < 2; ++index)
            require(SendMessageW(list, LVM_SETITEMSTATE, index, reinterpret_cast<LPARAM>(remoteItem)) != FALSE, "Cannot enable the test DLLs");
        VirtualFreeEx(process.hProcess, remoteItem, 0, MEM_RELEASE);
        const auto button = GetDlgItem(window, 108), report = GetDlgItem(window, 109);
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(108, BN_CLICKED), reinterpret_cast<LPARAM>(button));
        if (!wait([&] { return text(button) == L"Cancel check"; })) {
            std::wcerr << L"Button: " << text(button) << L"\nReport: " << text(report) << L'\n';
            throw std::runtime_error("Background check did not start");
        }
        // Editing while the worker runs must invalidate both progress and result.
        const auto changed = root / "different.exe";
        SendMessageW(GetDlgItem(window, 101), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(changed.c_str()));
        require(wait([&] { return text(button) == L"Check compatibility"; }), "Changed-input check did not cancel");
        require(text(report).find(L"Inputs changed") != std::wstring::npos, "Stale check result replaced the input-change message");
        SendMessageW(GetDlgItem(window, 101), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(exe.c_str()));
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(108, BN_CLICKED), reinterpret_cast<LPARAM>(button));
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(108, BN_CLICKED), reinterpret_cast<LPARAM>(button));
        require(wait([&] { return text(button) == L"Check compatibility"; }), "Cancel button did not finish");
        require(text(report).find(L"CANCELLED") != std::wstring::npos, "Cancelled check reported success");
        {
            std::ofstream invalid(root / "scr/broken.tsc"); invalid << "broken script\n";
        }
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(108, BN_CLICKED), reinterpret_cast<LPARAM>(button));
        require(wait([&] { return text(button) == L"Check compatibility"; }), "Full check did not finish");
        require(text(report).find(L"ERROR") != std::wstring::npos && !IsWindowEnabled(GetDlgItem(window, 107)),
                "A known patch error did not block Start game");
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(108, BN_CLICKED), reinterpret_cast<LPARAM>(button));
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(108, BN_CLICKED), reinterpret_cast<LPARAM>(button));
        require(wait([&] { return text(button) == L"Check compatibility"; }), "Cancelled recheck did not finish");
        require(!IsWindowEnabled(GetDlgItem(window, 107)), "Cancelling a recheck bypassed a known error");
        SendMessageW(GetDlgItem(window, 101), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(changed.c_str()));
        require(IsWindowEnabled(GetDlgItem(window, 107)), "Input change did not clear the old error state");
        SendMessageW(window, WM_CLOSE, 0, 0);
        require(WaitForSingleObject(process.hProcess, 20000) == WAIT_OBJECT_0, "Launcher did not close");
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
        fs::remove_all(root);
        std::cout << "Launcher GUI: background cancellation and stale-result invalidation passed\n";
        return 0;
    } catch (const std::exception& error) {
        if (window) PostMessageW(window, WM_CLOSE, 0, 0);
        if (process.hProcess) {
            if (WaitForSingleObject(process.hProcess, 20000) != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 1);
            CloseHandle(process.hThread); CloseHandle(process.hProcess);
        }
        if (!root.empty()) fs::remove_all(root);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
