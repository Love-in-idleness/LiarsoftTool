#include "win_util.h"

#include <tlhelp32.h>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct OwnedProcess {
    HANDLE handle = nullptr;
    ~OwnedProcess() {
        if (handle) { TerminateProcess(handle, 0); WaitForSingleObject(handle, 5000); CloseHandle(handle); }
    }
};
HANDLE findChild(DWORD parent, const std::wstring& expected, DWORD& pid) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return nullptr;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    HANDLE result = nullptr;
    if (Process32FirstW(snapshot, &entry)) do {
        if (entry.th32ParentProcessID != parent) continue;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE | PROCESS_TERMINATE,
                                      FALSE, entry.th32ProcessID);
        if (!process) continue;
        wchar_t path[32768];
        DWORD size = 32768;
        if (QueryFullProcessImageNameW(process, 0, path, &size) && !_wcsicmp(path, expected.c_str())) {
            result = process;
            pid = entry.th32ProcessID;
            break;
        }
        CloseHandle(process);
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return result;
}
struct WindowSearch { DWORD pid; HWND window = nullptr; };
BOOL CALLBACK findWindow(HWND window, LPARAM parameter) {
    auto* search = reinterpret_cast<WindowSearch*>(parameter);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == search->pid && IsWindowVisible(window) && !GetWindow(window, GW_OWNER)) {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}
void capture(HWND window, const wchar_t* path) {
    RECT rectangle{};
    require(GetWindowRect(window, &rectangle), "Cannot get game window rectangle");
    const LONG width = rectangle.right - rectangle.left, height = rectangle.bottom - rectangle.top;
    require(width > 0 && height > 0 && width <= 4096 && height <= 4096, "Invalid game window size");
    HDC screen = GetDC(nullptr), buffer = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    require(screen && buffer && bitmap, "Cannot create screenshot surface");
    HGDIOBJ previous = SelectObject(buffer, bitmap);
    require(BitBlt(buffer, 0, 0, width, height, screen, rectangle.left, rectangle.top, SRCCOPY), "Cannot capture game window");
    GdiFlush();
    const DWORD bytes = DWORD(width * height * 4);
    BITMAPFILEHEADER file{};
    file.bfType = 0x4d42;
    file.bfOffBits = sizeof(file) + sizeof(info.bmiHeader);
    file.bfSize = file.bfOffBits + bytes;
    HANDLE output = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(output != INVALID_HANDLE_VALUE, "Cannot create screenshot output (must not exist)");
    DWORD written;
    const bool ok = WriteFile(output, &file, sizeof(file), &written, nullptr) && written == sizeof(file) &&
        WriteFile(output, &info.bmiHeader, sizeof(info.bmiHeader), &written, nullptr) && written == sizeof(info.bmiHeader) &&
        WriteFile(output, pixels, bytes, &written, nullptr) && written == bytes;
    const bool closed = CloseHandle(output) != FALSE;
    SelectObject(buffer, previous);
    DeleteObject(bitmap);
    DeleteDC(buffer);
    ReleaseDC(nullptr, screen);
    require(ok && closed, "Screenshot write failed");
}
}

// Manual, asset-dependent smoke only: GAME.exe OVERRIDES OUTPUT.bmp [--no-escape].
// Runs the sibling launcher and terminates only its verified child game process.
int wmain(int argc, wchar_t** argv) {
    if (argc < 4 || argc > 5) return 2;
    OwnedProcess launcher, game;
    try {
        const auto directory = rscript::parent_path(rscript::module_path());
        const auto executable = rscript::full_path(argv[1]);
        std::wstring command = rscript::quote_argument(directory + L"\\RScriptLauncher.exe") +
            L" --game " + rscript::quote_argument(executable) + L" --resources " +
            rscript::quote_argument(rscript::full_path(argv[2]));
        for (const auto* plugin : {L"RScriptFiles.dll", L"RScriptResources.dll", L"RScriptUnicode.dll"})
            command += L" --plugin " + rscript::quote_argument(directory + L"\\" + plugin);
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                               directory.c_str(), &startup, &process), "Cannot start the test launcher");
        launcher.handle = process.hProcess;
        CloseHandle(process.hThread);
        DWORD gamePid = 0;
        for (unsigned i = 0; i < 300 && !game.handle; ++i) {
            game.handle = findChild(process.dwProcessId, executable, gamePid);
            if (!game.handle) Sleep(100);
        }
        require(game.handle, "The launcher did not create the requested game process");
        require(WaitForSingleObject(launcher.handle, 30000) == WAIT_OBJECT_0, "Launcher initialization timed out");
        DWORD exitCode = 1;
        require(GetExitCodeProcess(launcher.handle, &exitCode) && !exitCode, "Launcher or DLL initialization failed");
        std::printf("Game Windows PID: %lu\n", static_cast<unsigned long>(gamePid));
        WindowSearch search{gamePid};
        for (unsigned i = 0; i < 150 && !search.window; ++i) {
            EnumWindows(findWindow, reinterpret_cast<LPARAM>(&search));
            if (!search.window) Sleep(100);
        }
        require(search.window, "Game did not show a visible top-level window");
        if (argc == 4) for (unsigned i = 0; i < 3; ++i) {
            Sleep(1000);
            SetForegroundWindow(search.window);
            RECT client{};
            GetClientRect(search.window, &client);
            POINT center{client.right / 2, client.bottom / 2};
            ClientToScreen(search.window, &center);
            SetCursorPos(center.x, center.y);
            INPUT click[2]{};
            click[0].type = click[1].type = INPUT_MOUSE;
            click[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            click[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
            require(SendInput(2, click, sizeof(INPUT)) == 2, "Cannot send startup skip click");
        }
        Sleep(5000);
        require(WaitForSingleObject(game.handle, 0) == WAIT_TIMEOUT, "Game exited during startup smoke");
        wchar_t title[256];
        GetWindowTextW(search.window, title, 256);
        std::printf("Visible window: %s\n", rscript::utf8(title).c_str());
        capture(search.window, argv[3]);
        std::puts("Actual game startup smoke passed (not a playthrough)");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
