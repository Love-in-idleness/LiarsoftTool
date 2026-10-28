#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "win_util.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <optional>

namespace {
constexpr int game_edit = 101, game_browse = 102, root_edit = 103, root_browse = 104;
constexpr int plugin_list = 105, add_plugin = 106, start_game = 107;
constexpr int check_game = 108, check_report = 109, check_progress = 110;
constexpr UINT check_update = WM_APP + 1, check_done = WM_APP + 2;
HWND game_control, root_control, plugins_control;
HWND report_control, progress_control;
std::vector<std::wstring> plugin_paths;
bool updating = false;
size_t generation = 0;
struct CheckTask {
    std::thread thread;
    std::atomic<bool> cancel{false};
    std::atomic<bool> progressPending{false};
    size_t generation;
    std::mutex mutex;
    size_t done = 0, total = 0;
    std::string source;
    liarsoft::runtime::CheckResult result;
};
std::unique_ptr<CheckTask> task;
std::optional<liarsoft::runtime::CheckResult> last_check;

bool check_failed() {
    if (last_check) for (const auto& issue : last_check->issues)
        if (issue.severity == liarsoft::runtime::CheckSeverity::Error) return true;
    return false;
}

void show_report(const std::string& text) {
    std::wstring value;
    for (auto ch : rscript::utf16(text.c_str())) {
        if (ch == L'\n') value += L'\r';
        value += ch;
    }
    SetWindowTextW(report_control, value.c_str());
}

void invalidate_check() {
    ++generation;
    last_check.reset();
    if (task) task->cancel = true;
    if (report_control) {
        show_report("Inputs changed; run Check compatibility again.");
        SendMessageW(progress_control, PBM_SETPOS, 0, 0);
        EnableWindow(GetDlgItem(GetParent(report_control), start_game), !task);
    }
}

rscript::LaunchOptions current_options();

void begin_check(HWND window) {
    if (task) { task->cancel = true; return; }
    const auto options = current_options();
    task = std::make_unique<CheckTask>();
    task->generation = generation;
    auto* active = task.get();
    SetWindowTextW(GetDlgItem(window, check_game), L"Cancel check");
    EnableWindow(GetDlgItem(window, start_game), FALSE);
    show_report("Checking compatibility... (read-only)");
    SendMessageW(progress_control, PBM_SETPOS, 0, 0);
    try {
        active->thread = std::thread([window, options, active] {
            try {
                active->result = rscript::check_launch(options, true,
                    [window, active](size_t done, size_t total, const std::string& source) {
                        if (active->cancel) return false;
                        {
                            std::lock_guard<std::mutex> lock(active->mutex);
                            active->done = done; active->total = total; active->source = source;
                        }
                        if (!active->progressPending.exchange(true))
                            PostMessageW(window, check_update, active->generation, 0);
                        return !active->cancel;
                    });
            } catch (const std::exception& error) {
                active->result.complete = false;
                active->result.issues.push_back({liarsoft::runtime::CheckSeverity::Error, "check", error.what()});
            }
            PostMessageW(window, check_done, active->generation, 0);
        });
    } catch (...) {
        task.reset();
        EnableWindow(GetDlgItem(window, start_game), !check_failed());
        SetWindowTextW(GetDlgItem(window, check_game), L"Check compatibility");
        throw;
    }
}

std::wstring control_text(HWND control) {
    int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, &value[0], length + 1);
    value.resize(length);
    return value;
}

rscript::LaunchOptions current_options() {
    rscript::LaunchOptions options;
    options.game = control_text(game_control);
    options.root = control_text(root_control);
    for (size_t i = 0; i < plugin_paths.size(); ++i)
        if (ListView_GetCheckState(plugins_control, static_cast<int>(i))) options.plugins.push_back(plugin_paths[i]);
    return options;
}

std::wstring select_file(HWND window, bool dll) {
    wchar_t buffer[32768]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window;
    dialog.lpstrFilter = dll ? L"Extension DLL\0*.dll\0All files\0*.*\0" : L"Game executable\0*.exe\0All files\0*.*\0";
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = 32768;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&dialog) ? buffer : L"";
}

void append_plugin(const std::wstring& path) {
    for (const auto& known : plugin_paths) if (_wcsicmp(known.c_str(), path.c_str()) == 0) return;
    std::wstring name = path.substr(path.find_last_of(L"\\/") + 1);
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = static_cast<int>(plugin_paths.size());
    item.pszText = &name[0];
    ListView_InsertItem(plugins_control, &item);
    ListView_SetItemText(plugins_control, item.iItem, 1, const_cast<wchar_t*>(path.c_str()));
    ListView_SetCheckState(plugins_control, item.iItem, FALSE);
    plugin_paths.push_back(path);
    invalidate_check();
}

void update_dependencies(int changed) {
    if (updating || changed < 0 || changed > 2) return;
    updating = true;
    if (changed > 0 && ListView_GetCheckState(plugins_control, changed))
        ListView_SetCheckState(plugins_control, 0, TRUE);
    if (changed == 0 && !ListView_GetCheckState(plugins_control, 0)) {
        ListView_SetCheckState(plugins_control, 1, FALSE);
        ListView_SetCheckState(plugins_control, 2, FALSE);
    }
    updating = false;
}

HWND create_control(HWND parent, const wchar_t* type, const wchar_t* text,
                    DWORD style, int x, int y, int width, int height, int id = 0) {
    HWND result = CreateWindowExW(wcscmp(type, WC_EDITW) == 0 ? WS_EX_CLIENTEDGE : 0,
        type, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(result, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return result;
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE: {
        create_control(window, WC_STATICW, L"Game executable", 0, 16, 12, 500, 20);
        game_control = create_control(window, WC_EDITW, L"", WS_TABSTOP | ES_AUTOHSCROLL, 16, 36, 540, 26, game_edit);
        create_control(window, WC_BUTTONW, L"Browse...", WS_TABSTOP | BS_PUSHBUTTON, 568, 36, 90, 26, game_browse);
        create_control(window, WC_STATICW, L"Override resources (empty = game directory)", 0, 16, 76, 600, 20);
        root_control = create_control(window, WC_EDITW, L"", WS_TABSTOP | ES_AUTOHSCROLL, 16, 100, 540, 26, root_edit);
        create_control(window, WC_BUTTONW, L"Browse...", WS_TABSTOP | BS_PUSHBUTTON, 568, 100, 90, 26, root_browse);
        create_control(window, WC_STATICW, L"Enable only trusted DLLs. All unchecked runs the original game.", 0, 16, 144, 640, 20);
        plugins_control = create_control(window, WC_LISTVIEWW, L"", WS_TABSTOP | WS_BORDER | LVS_REPORT |
            LVS_SINGLESEL, 16, 172, 642, 172, plugin_list);
        ListView_SetExtendedListViewStyle(plugins_control, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;
        column.cx = 190;
        column.pszText = const_cast<wchar_t*>(L"DLL");
        ListView_InsertColumn(plugins_control, 0, &column);
        column.cx = 430;
        column.pszText = const_cast<wchar_t*>(L"Path");
        ListView_InsertColumn(plugins_control, 1, &column);
        std::wstring directory = rscript::parent_path(rscript::module_path());
        for (const auto* name : {L"RScriptFiles.dll", L"RScriptResources.dll", L"RScriptUnicode.dll"})
            append_plugin(directory + L"\\" + name);
        create_control(window, WC_BUTTONW, L"Add DLL...", WS_TABSTOP | BS_PUSHBUTTON, 16, 360, 112, 30, add_plugin);
        create_control(window, WC_BUTTONW, L"Start game", WS_TABSTOP | BS_DEFPUSHBUTTON, 526, 360, 132, 30, start_game);
        create_control(window, WC_BUTTONW, L"Check compatibility", WS_TABSTOP | BS_PUSHBUTTON, 300, 360, 210, 30, check_game);
        progress_control = create_control(window, PROGRESS_CLASSW, L"", 0, 16, 404, 642, 18, check_progress);
        report_control = create_control(window, WC_EDITW, L"Run Check compatibility for a full patch check.",
            WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            16, 432, 642, 180, check_report);
        SendMessageW(report_control, EM_SETLIMITTEXT, 0x7ffffffe, 0);
        return 0;
    }
    case WM_NOTIFY: {
        auto* header = reinterpret_cast<NMHDR*>(lparam);
        if (header->idFrom == plugin_list && header->code == LVN_ITEMCHANGED) {
            auto* change = reinterpret_cast<NMLISTVIEW*>(lparam);
            if ((change->uOldState ^ change->uNewState) & LVIS_STATEIMAGEMASK) {
                update_dependencies(change->iItem);
                invalidate_check();
            }
        }
        break;
    }
    case WM_COMMAND:
        try {
            if ((LOWORD(wparam) == game_edit || LOWORD(wparam) == root_edit) && HIWORD(wparam) == EN_CHANGE) {
                invalidate_check();
            } else if (LOWORD(wparam) == check_game) {
                begin_check(window);
            } else if (LOWORD(wparam) == game_browse) {
                std::wstring path = select_file(window, false);
                if (!path.empty()) {
                    SetWindowTextW(game_control, path.c_str());
                    SetWindowTextW(root_control, rscript::parent_path(path).c_str());
                }
            } else if (LOWORD(wparam) == root_browse) {
                BROWSEINFOW dialog{};
                dialog.hwndOwner = window;
                dialog.lpszTitle = L"Choose the override resource directory";
                dialog.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&dialog);
                if (item) {
                    wchar_t path[MAX_PATH]{};
                    if (SHGetPathFromIDListW(item, path)) SetWindowTextW(root_control, path);
                    CoTaskMemFree(item);
                }
            } else if (LOWORD(wparam) == add_plugin) {
                std::wstring path = select_file(window, true);
                if (!path.empty()) append_plugin(path);
            } else if (LOWORD(wparam) == start_game) {
                if (task) return 0;
                if (check_failed()) { show_report(last_check->text()); return 0; }
                const auto options = current_options();
                const auto checked = rscript::check_launch(options, false);
                show_report(checked.text());
                if (!checked.ok()) return 0;
                EnableWindow(GetDlgItem(window, start_game), FALSE);
                try { rscript::launch(options); }
                catch (...) { EnableWindow(GetDlgItem(window, start_game), TRUE); throw; }
                EnableWindow(GetDlgItem(window, start_game), TRUE);
            }
        } catch (const std::exception& error) {
            MessageBoxW(window, rscript::utf16(error.what()).c_str(), L"RScript Launcher", MB_OK | MB_ICONERROR);
        }
        return 0;
    case check_update:
        if (task) task->progressPending = false;
        if (task && wparam == generation && wparam == task->generation) {
            std::lock_guard<std::mutex> lock(task->mutex);
            SendMessageW(progress_control, PBM_SETRANGE32, 0, task->total ? task->total : 1);
            SendMessageW(progress_control, PBM_SETPOS, task->done, 0);
            show_report("Checking: " + task->source + "\nScripts: " + std::to_string(task->done) + "/" + std::to_string(task->total));
        }
        return 0;
    case check_done:
        if (task && wparam == task->generation) {
            task->thread.join();
            if (wparam == generation) {
                if (task->cancel) { task->result.cancelled = true; task->result.complete = false; }
                show_report(task->result.text());
                // A cancelled recheck cannot clear already confirmed errors.
                if (!check_failed() || (task->result.complete && !task->result.cancelled))
                    last_check = std::move(task->result);
            }
            task.reset();
            EnableWindow(GetDlgItem(window, start_game), !check_failed());
            SetWindowTextW(GetDlgItem(window, check_game), L"Check compatibility");
        }
        return 0;
    case WM_DESTROY:
        if (task) {
            task->cancel = true;
            task->thread.join();
            task.reset();
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int gui(HINSTANCE instance) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&controls);
    WNDCLASSW type{};
    type.lpfnWndProc = window_proc;
    type.hInstance = instance;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    type.lpszClassName = L"RScriptLauncher";
    if (!RegisterClassW(&type)) throw std::runtime_error("Could not register launcher window");
    HWND window = CreateWindowExW(0, type.lpszClassName, L"RScript Launcher", WS_OVERLAPPED |
        WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 694, 666,
        nullptr, nullptr, instance, nullptr);
    if (!window) throw std::runtime_error("Could not create launcher window");
    ShowWindow(window, SW_SHOW);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    CoUninitialize();
    return static_cast<int>(message.wParam);
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, wchar_t*, int) {
    int count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    bool headless = count > 1;
    try {
        if (!arguments) throw std::runtime_error("Could not read launcher arguments");
        if (!headless) { LocalFree(arguments); arguments = nullptr; return gui(instance); }
        rscript::LaunchOptions options;
        bool check = false;
        for (int i = 1; i < count; ++i) {
            std::wstring option = arguments[i];
            if (option == L"--help") {
                std::puts("RScriptLauncher --game GAME.exe [--resources DIR] [--plugin DLL] [--kinsoku JSON] [--wait | --check]\n"
                          "--check  Read-only compatibility and complete patch check; does not launch the game.\n"
                          "Normal launch performs lightweight checks only. Errors return nonzero; pending verification does not block startup.");
                LocalFree(arguments);
                return 0;
            }
            if (option == L"--wait") options.wait = true;
            else if (option == L"--check") check = true;
            else if (option == L"--game" || option == L"--resources" || option == L"--plugin" || option == L"--kinsoku") {
                if (++i >= count) throw std::runtime_error("Missing launcher option argument");
                if (option == L"--game") options.game = arguments[i];
                else if (option == L"--resources") options.root = arguments[i];
                else if (option == L"--kinsoku") {
                    if (!arguments[i][0]) throw std::runtime_error("Kinsoku JSON path must not be empty");
                    options.kinsoku = arguments[i];
                }
                else options.plugins.emplace_back(arguments[i]);
            } else throw std::runtime_error("Usage: RScriptLauncher --game GAME.exe [--resources DIR] [--plugin DLL] [--kinsoku JSON] [--wait | --check]");
        }
        LocalFree(arguments);
        arguments = nullptr;
        if (check) {
            if (options.wait) throw std::runtime_error("--check cannot be combined with --wait");
            const auto result = rscript::check_launch(options, true);
            std::fputs(result.text().c_str(), stdout);
            return result.ok() ? 0 : 1;
        }
        return static_cast<int>(rscript::launch(options));
    } catch (const std::exception& error) {
        if (arguments) LocalFree(arguments);
        if (headless) std::fprintf(stderr, "RScript Launcher: %s\n", error.what());
        else MessageBoxW(nullptr, rscript::utf16(error.what()).c_str(), L"RScript Launcher", MB_OK | MB_ICONERROR);
        return 1;
    }
}
