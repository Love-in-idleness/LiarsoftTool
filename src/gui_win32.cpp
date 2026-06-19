#ifdef _WIN32

#include "gui_win32.h"
#define _WIN32_WINNT 0x0600
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <filesystem>
#include <algorithm>
#include <cstring>

#include "gscfile.h"
#include "transfile.h"
#include "xflarchive.h"
#include "wcg_decoder.h"
#include "lim_decoder.h"
#include "lwg_decoder.h"
#include "wav_ogg.h"
#include "exe_patch.h"
#include "stb_image.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

namespace fs = std::filesystem;

// ---- Globals ----
static HWND g_hWnd, g_hListView, g_hBtnAdd, g_hBtnRemove, g_hBtnClear, g_hBtnConvert;
static HWND g_hBtnRef, g_hBtnOutDir;
static HWND g_hCboEnc, g_hEditRef, g_hEditOutDir, g_hProgress, g_hStatus;
static HWND g_hLblEnc, g_hLblRef, g_hLblOutDir;
static std::vector<std::string> g_inputs;
static std::vector<std::string> g_outputs;
static std::vector<std::string> g_statuses;
static std::mutex g_mutex;
static bool g_running = false;

static std::string getExtension(const std::string& path) {
    auto pos = path.rfind('.');
    if (pos == std::string::npos) return "";
    std::string ext = path.substr(pos);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

static std::string replaceExtension(const std::string& path, const std::string& newExt) {
    auto pos = path.rfind('.');
    if (pos == std::string::npos) return path + newExt;
    return path.substr(0, pos) + newExt;
}

static std::string guessOutput(const std::string& in, const std::string& outDir) {
    std::string ext = getExtension(in);
    fs::path p(in);
    fs::path base = outDir.empty() ? p.parent_path() : fs::path(outDir);
    std::string stem = p.stem().string();
    if (ext == ".gsc") return (base / (stem + ".txt")).string();
    if (ext == ".txt") return (base / (stem + ".gsc")).string();
    if (ext == ".xfl" || ext == ".lwg") return (base / stem).string();
    if (ext == ".wcg" || ext == ".lim") return (base / (stem + ".png")).string();
    if (ext == ".exe") return (base / (stem + ".gbk.exe")).string();
    if (ext == ".wav") return (base / (stem + ".ogg")).string();
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp")
        return (base / (stem + ".wcg")).string();
    return (base / p.filename()).string();
}

static std::string guessType(const std::string& path) {
    std::string ext = getExtension(path);
    if (ext == ".gsc") return "GSC -> TXT";
    if (ext == ".txt") return "TXT -> GSC";
    if (ext == ".xfl") return "XFL -> DIR";
    if (ext == ".lwg") return "LWG -> DIR";
    if (ext == ".wcg") return "WCG -> PNG";
    if (ext == ".lim") return "LIM -> PNG";
    if (ext == ".wav") return "WAV -> OGG";
    if (ext == ".ogg") return "OGG -> WAV";
    if (ext == ".exe") return "EXE SJIS->GBK";
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") return "IMG -> WCG";
    if (fs::is_directory(path)) return "DIR -> XFL/LWG";
    return "?";
}

static bool isSupported(const std::string& path) {
    std::string ext = getExtension(path);
    return ext == ".gsc" || ext == ".txt" || ext == ".xfl" || ext == ".lwg" ||
           ext == ".wcg" || ext == ".lim" || ext == ".wav" || ext == ".ogg" || ext == ".exe" ||
           ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" ||
           fs::is_directory(path);
}

// ---- ListView helpers ----
static void lvInsert(int idx, const std::string& in, const std::string& out, const std::string& type, const std::string& status) {
    LVITEM item = {};
    item.mask = LVIF_TEXT;
    item.iItem = idx;
    
    // Use ANSI strings directly (ListViewA)
    char inBuf[1024], outBuf[1024], typeBuf[256], statusBuf[256];
    strncpy(inBuf, in.c_str(), sizeof(inBuf)-1); inBuf[sizeof(inBuf)-1] = 0;
    strncpy(outBuf, out.c_str(), sizeof(outBuf)-1); outBuf[sizeof(outBuf)-1] = 0;
    strncpy(typeBuf, type.c_str(), sizeof(typeBuf)-1); typeBuf[sizeof(typeBuf)-1] = 0;
    strncpy(statusBuf, status.c_str(), sizeof(statusBuf)-1); statusBuf[sizeof(statusBuf)-1] = 0;
    
    item.pszText = inBuf;        ListView_InsertItem(g_hListView, &item);
    item.iSubItem = 1; item.pszText = outBuf;    ListView_SetItem(g_hListView, &item);
    item.iSubItem = 2; item.pszText = typeBuf;   ListView_SetItem(g_hListView, &item);
    item.iSubItem = 3; item.pszText = statusBuf; ListView_SetItem(g_hListView, &item);
}

static void lvSetStatus(int idx, const std::string& s) {
    LVITEM item = {};
    item.iItem = idx;
    item.iSubItem = 3;
    char buf[1024];
    strncpy(buf, s.c_str(), sizeof(buf)-1); buf[sizeof(buf)-1] = 0;
    item.pszText = buf;
    ListView_SetItem(g_hListView, &item);
    g_statuses[idx] = s;
}

static void addFile(const std::string& path, const std::string& outDir) {
    if (!isSupported(path)) return;
    std::string out = guessOutput(path, outDir);
    int idx = g_inputs.size();
    g_inputs.push_back(path);
    g_outputs.push_back(out);
    g_statuses.push_back("Ready");
    lvInsert(idx, path, out, guessType(path), "Ready");
    EnableWindow(g_hBtnConvert, TRUE);
}

static void rebuildOutputs() {
    char buf[1024];
    GetWindowTextA(g_hEditOutDir, buf, sizeof(buf));
    std::string outDir(buf);
    for (size_t i = 0; i < g_inputs.size(); ++i) {
        g_outputs[i] = guessOutput(g_inputs[i], outDir);
        lvSetStatus(i, ""); // just refresh output col
        // Actually need to update the output column
        char buf[1024];
        strncpy(buf, g_outputs[i].c_str(), sizeof(buf)-1); buf[sizeof(buf)-1] = 0;
        LVITEM item = {};
        item.iItem = (int)i; item.iSubItem = 1; item.pszText = buf;
        ListView_SetItem(g_hListView, &item);
    }
}

// ---- Conversion worker ----
static void convertAll(const std::string& encoding, const std::string& refPath) {
    g_running = true;
    EnableWindow(g_hBtnConvert, FALSE);
    
    for (size_t i = 0; i < g_inputs.size(); ++i) {
        if (!g_running) break;
        std::string in = g_inputs[i];
        std::string out = g_outputs[i];
        std::string ext = getExtension(in);
        
        SendMessage(g_hProgress, PBM_SETPOS, (WPARAM)(i * 100 / g_inputs.size()), 0);
        
        try {
            if (fs::is_directory(in)) {
                bool hasMeta = fs::exists(in + "/.meta.xml");
                if (hasMeta) liarsoft::LwgPacker::packToFile(in, out, encoding);
                else { liarsoft::XflArchive arch; arch.encoding = encoding; arch.addDirectory(in); arch.save(out); }
            } else if (ext == ".gsc") {
                auto gsc = liarsoft::GscFile::fromFile(in, encoding);
                liarsoft::TransFile::fromGsc(gsc).save(out);
            } else if (ext == ".txt") {
                std::string ref = refPath.empty() ? replaceExtension(in, ".gsc") : refPath;
                liarsoft::TransFile::fromFile(in).toGsc(ref, encoding).save(out);
            } else if (ext == ".xfl") {
                liarsoft::XflArchive::fromFile(in, encoding).extractToDirectory(out);
            } else if (ext == ".lwg") {
                std::ifstream fs(in, std::ios::binary);
                fs.seekg(0, std::ios::end); size_t sz = fs.tellg(); fs.seekg(0, std::ios::beg);
                std::vector<uint8_t> raw(sz); fs.read((char*)raw.data(), sz);
                auto arch = liarsoft::LwgDecoder::decode(raw, encoding);
                liarsoft::LwgDecoder::extractToDirectory(arch, out, encoding);
            } else if (ext == ".wcg") {
                std::ifstream fs(in, std::ios::binary);
                fs.seekg(0, std::ios::end); size_t sz = fs.tellg(); fs.seekg(0, std::ios::beg);
                std::vector<uint8_t> raw(sz); fs.read((char*)raw.data(), sz);
                liarsoft::wcgSavePng(liarsoft::wcgDecode(raw), out);
            } else if (ext == ".lim") {
                std::ifstream fs(in, std::ios::binary);
                fs.seekg(0, std::ios::end); size_t sz = fs.tellg(); fs.seekg(0, std::ios::beg);
                std::vector<uint8_t> raw(sz); fs.read((char*)raw.data(), sz);
                liarsoft::limSavePng(liarsoft::limDecode(raw), out);
            } else if (ext == ".wav") {
                liarsoft::WavOggExtractor::extractToFile(in, out);
            } else if (ext == ".ogg") {
                std::string ref = refPath.empty() ? replaceExtension(in, ".wav") : refPath;
                liarsoft::WavOggExtractor::embedToFile(in, ref, out);
            } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") {
                int w, h, ch;
                unsigned char* px = stbi_load(in.c_str(), &w, &h, &ch, 4);
                if (!px) throw std::runtime_error("Failed to load");
                auto d = liarsoft::wcgEncode(px, (uint32_t)w, (uint32_t)h);
                stbi_image_free(px);
                std::ofstream fout(out, std::ios::binary);
                fout.write((char*)d.data(), d.size());
            } else if (ext == ".exe") {
                liarsoft::exeConvertFile(in, out, encoding);
            }
            lvSetStatus((int)i, "OK");
        } catch (const std::exception& e) {
            lvSetStatus((int)i, std::string("FAIL: ") + e.what());
        }
    }
    
    SendMessage(g_hProgress, PBM_SETPOS, 100, 0);
    SetWindowTextA(g_hStatus, "Done.");
    EnableWindow(g_hBtnConvert, TRUE);
    g_running = false;
}

// ---- Dialog callbacks ----
static void onAddFiles() {
    char buf[8192] = {};
    OPENFILENAMEA ofn = {sizeof(ofn)};
    ofn.hwndOwner = g_hWnd;
    ofn.lpstrFilter = "All Supported\0*.gsc;*.txt;*.xfl;*.lwg;*.wcg;*.lim;*.wav;*.png;*.jpg;*.jpeg;*.bmp\0All Files\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = sizeof(buf);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
    
    if (GetOpenFileNameA(&ofn)) {
        char outDir[1024]; GetWindowTextA(g_hEditOutDir, outDir, sizeof(outDir));
        // Multi-select parsing
        const char* p = buf + strlen(buf) + 1;
        if (*p == 0) {
            addFile(buf, outDir);
        } else {
            std::string dir = buf;
            while (*p) {
                addFile(dir + "\\" + p, outDir);
                p += strlen(p) + 1;
            }
        }
    }
}

static void onRemoveSelected() {
    int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
    if (sel >= 0) {
        ListView_DeleteItem(g_hListView, sel);
        g_inputs.erase(g_inputs.begin() + sel);
        g_outputs.erase(g_outputs.begin() + sel);
        g_statuses.erase(g_statuses.begin() + sel);
    }
}

static void onClear() {
    ListView_DeleteAllItems(g_hListView);
    g_inputs.clear(); g_outputs.clear(); g_statuses.clear();
    SendMessage(g_hProgress, PBM_SETPOS, 0, 0);
    SetWindowTextA(g_hStatus, "Ready.");
    EnableWindow(g_hBtnConvert, FALSE);
}

static void onChooseOutDir() {
    BROWSEINFOA bi = {};
    bi.hwndOwner = g_hWnd;
    bi.lpszTitle = "Select output directory";
    bi.ulFlags = BIF_RETURNONLYFSDIRS;
    LPITEMIDLIST pidl = SHBrowseForFolderA(&bi);
    if (pidl) {
        char path[MAX_PATH];
        SHGetPathFromIDListA(pidl, path);
        SetWindowTextA(g_hEditOutDir, path);
        rebuildOutputs();
        CoTaskMemFree(pidl);
    }
}

static void onChooseRef() {
    char buf[MAX_PATH] = {};
    OPENFILENAMEA ofn = {sizeof(ofn)};
    ofn.hwndOwner = g_hWnd;
    ofn.lpstrFilter = "GSC Files\0*.gsc\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = sizeof(buf);
    ofn.Flags = OFN_FILEMUSTEXIST;
    if (GetOpenFileNameA(&ofn)) SetWindowTextA(g_hEditRef, buf);
}

static void onConvert() {
    char enc[64], ref[1024];
    GetWindowTextA(g_hCboEnc, enc, sizeof(enc));
    GetWindowTextA(g_hEditRef, ref, sizeof(ref));
    std::string encoding = (strcmp(enc, "GBK") == 0) ? "GBK" : (strcmp(enc, "CP1251") == 0) ? "CP1251" : "SHIFT_JIS";
    std::string refPath = ref;
    std::thread t(convertAll, encoding, refPath);
    t.detach();
}

static void onDropFiles(HDROP hDrop) {
    char outDir[1024]; GetWindowTextA(g_hEditOutDir, outDir, sizeof(outDir));
    UINT count = DragQueryFileA(hDrop, 0xFFFFFFFF, NULL, 0);
    for (UINT i = 0; i < count; ++i) {
        char path[MAX_PATH];
        DragQueryFileA(hDrop, i, path, sizeof(path));
        addFile(path, outDir);
    }
    DragFinish(hDrop);
}

// ---- Window procedure ----
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS};
        InitCommonControlsEx(&icc);
        
        // --- Top controls ---
        g_hLblEnc = CreateWindowA("STATIC", "Encoding:", WS_VISIBLE | WS_CHILD, 10, 12, 85, 20, hWnd, (HMENU)201, NULL, NULL);
        g_hCboEnc = CreateWindowA("COMBOBOX", NULL, WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST,
            100, 10, 205, 22, hWnd, (HMENU)200, NULL, NULL);
        SendMessageA(g_hCboEnc, CB_ADDSTRING, 0, (LPARAM)"SHIFT_JIS");
        SendMessageA(g_hCboEnc, CB_ADDSTRING, 0, (LPARAM)"GBK");
        SendMessageA(g_hCboEnc, CB_ADDSTRING, 0, (LPARAM)"CP1251 (Cyrillic/English)");
        SendMessageA(g_hCboEnc, CB_SETCURSEL, 0, 0);
        SendMessageA(g_hCboEnc, CB_SETDROPPEDWIDTH, 260, 0);
        
        g_hLblRef = CreateWindowA("STATIC", "Reference:", WS_VISIBLE | WS_CHILD, 315, 12, 124, 20, hWnd, (HMENU)202, NULL, NULL);
        g_hEditRef = CreateWindowA("EDIT", NULL, WS_VISIBLE | WS_CHILD | WS_BORDER,
            444, 10, 316, 22, hWnd, (HMENU)203, NULL, NULL);
        g_hBtnRef = CreateWindowA("BUTTON", "...", WS_VISIBLE | WS_CHILD,
            765, 10, 25, 22, hWnd, (HMENU)101, NULL, NULL);
        
        // --- ListView ---
        g_hListView = CreateWindowA(WC_LISTVIEWA, NULL,
            WS_VISIBLE | WS_CHILD | LVS_REPORT | LVS_SINGLESEL | WS_BORDER,
            10, 40, 760, 320, hWnd, NULL, NULL, NULL);
        ListView_SetExtendedListViewStyle(g_hListView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        
        LVCOLUMN col = {LVCF_TEXT | LVCF_WIDTH | LVCF_FMT};
        col.cx = 260; col.pszText = (LPSTR)"Input";   col.fmt = LVCFMT_LEFT;  ListView_InsertColumn(g_hListView, 0, &col);
        col.cx = 260; col.pszText = (LPSTR)"Output";  col.fmt = LVCFMT_LEFT;  ListView_InsertColumn(g_hListView, 1, &col);
        col.cx = 140; col.pszText = (LPSTR)"Type";    col.fmt = LVCFMT_LEFT;  ListView_InsertColumn(g_hListView, 2, &col);
        col.cx =  96; col.pszText = (LPSTR)"Status";  col.fmt = LVCFMT_LEFT;  ListView_InsertColumn(g_hListView, 3, &col);
        
        // Drag & drop support
        DragAcceptFiles(hWnd, TRUE);
        
        // --- Bottom controls ---
        g_hBtnAdd = CreateWindowA("BUTTON", "Add Files", WS_VISIBLE | WS_CHILD,
            10, 370, 80, 26, hWnd, (HMENU)102, NULL, NULL);
        g_hBtnRemove = CreateWindowA("BUTTON", "Remove", WS_VISIBLE | WS_CHILD,
            100, 370, 78, 26, hWnd, (HMENU)103, NULL, NULL);
        g_hBtnClear = CreateWindowA("BUTTON", "Clear", WS_VISIBLE | WS_CHILD,
            188, 370, 50, 26, hWnd, (HMENU)104, NULL, NULL);
        
        g_hLblOutDir = CreateWindowA("STATIC", "Out Dir:", WS_VISIBLE | WS_CHILD, 248, 374, 110, 20, hWnd, (HMENU)205, NULL, NULL);
        g_hEditOutDir = CreateWindowA("EDIT", NULL, WS_VISIBLE | WS_CHILD | WS_BORDER,
            365, 372, 165, 22, hWnd, (HMENU)206, NULL, NULL);
        g_hBtnOutDir = CreateWindowA("BUTTON", "...", WS_VISIBLE | WS_CHILD,
            535, 370, 25, 22, hWnd, (HMENU)105, NULL, NULL);
        
        g_hBtnConvert = CreateWindowA("BUTTON", "Convert All", WS_VISIBLE | WS_CHILD,
            570, 370, 100, 30, hWnd, (HMENU)106, NULL, NULL);
        EnableWindow(g_hBtnConvert, FALSE);
        
        // --- Progress & Status ---
        g_hProgress = CreateWindowA(PROGRESS_CLASSA, NULL, 
            WS_VISIBLE | WS_CHILD | PBS_SMOOTH,
            10, 405, 760, 18, hWnd, NULL, NULL, NULL);
        SendMessage(g_hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        
        g_hStatus = CreateWindowA("STATIC", "Ready. Drag files or click Add Files.",
            WS_VISIBLE | WS_CHILD, 10, 428, 760, 20, hWnd, NULL, NULL, NULL);
        
        g_hWnd = hWnd;
        return 0;
    }
    
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case 101: onChooseRef(); break;
        case 102: onAddFiles(); break;
        case 103: onRemoveSelected(); break;
        case 104: onClear(); break;
        case 105: onChooseOutDir(); break;
        case 106: onConvert(); break;
        }
        return 0;
    
    case WM_NOTIFY: {
        LPNMHDR nm = (LPNMHDR)lParam;
        if (nm->hwndFrom == g_hListView && nm->code == NM_CUSTOMDRAW) {
            LPNMLVCUSTOMDRAW lvcd = (LPNMLVCUSTOMDRAW)lParam;
            switch (lvcd->nmcd.dwDrawStage) {
            case CDDS_PREPAINT:
                SetWindowLongPtr(hWnd, DWLP_MSGRESULT, CDRF_NOTIFYITEMDRAW);
                return TRUE;
            case CDDS_ITEMPREPAINT: {
                // Alternating row colors
                if (lvcd->nmcd.dwItemSpec & 1)
                    lvcd->clrTextBk = RGB(240, 246, 255);  // light blue
                else
                    lvcd->clrTextBk = RGB(255, 255, 255);  // white
                // Selected row highlight
                if (ListView_GetItemState(g_hListView, (int)lvcd->nmcd.dwItemSpec, LVIS_SELECTED) & LVIS_SELECTED) {
                    lvcd->clrTextBk = RGB(0, 120, 215);
                    lvcd->clrText  = RGB(255, 255, 255);
                }
                SetWindowLongPtr(hWnd, DWLP_MSGRESULT, CDRF_NEWFONT);
                return TRUE;
            }
            }
        }
        return 0;
    }
    
    case WM_DROPFILES:
        onDropFiles((HDROP)wParam);
        return 0;
    
    case WM_SIZE: {
        int w = LOWORD(lParam), h = HIWORD(lParam);
        if (w == 0 || h == 0) return 0;  // minimized
        int m = 10, by = (h < 200) ? 60 : h - 115;
        if (by < 60) by = 60;

        // --- Top row: fixed y, ref edit stretches to fill space ---
        // Encoding label + combo (fixed pos/size)
        SetWindowPos(g_hLblEnc,   NULL, m,      12, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        SetWindowPos(g_hCboEnc,   NULL, 100,    10, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        // Ref label (fixed), edit (stretches), button (right-anchored)
        SetWindowPos(g_hLblRef,   NULL, 315,    12, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        int rbx = w - 35;                               // ref button right edge
        int refEditW = rbx - 449;                        // edit fills from 444 to rbx-5
        if (refEditW < 60) refEditW = 60;
        SetWindowPos(g_hEditRef,  NULL, 0, 0, refEditW, 22, SWP_NOZORDER | SWP_NOMOVE);
        SetWindowPos(g_hBtnRef,   NULL, rbx,    10, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        // --- ListView fills between top row and bottom controls ---
        int lvH = by - 50;
        if (lvH < 40) lvH = 40;
        SetWindowPos(g_hListView, NULL, m, 40, w - 2*m, lvH, SWP_NOZORDER);

        // --- Resize ListView columns: Type+Status fixed, Input+Output split 50/50 ---
        int sbW = GetSystemMetrics(SM_CXVSCROLL) + 6;   // scrollbar + border
        int avail = (w - 2*m) - sbW;
        if (avail < 300) avail = 300;
        int fixedCols = 140 + 96;                        // Type + Status
        int half = (avail - fixedCols) / 2;
        if (half < 80) half = 80;
        ListView_SetColumnWidth(g_hListView, 0, half);
        ListView_SetColumnWidth(g_hListView, 1, half);
        ListView_SetColumnWidth(g_hListView, 2, 140);
        ListView_SetColumnWidth(g_hListView, 3,  96);

        // --- Bottom button row ---
        SetWindowPos(g_hBtnAdd,     NULL, m,      by, 80, 26, SWP_NOZORDER);
        SetWindowPos(g_hBtnRemove,  NULL, 100,    by, 78, 26, SWP_NOZORDER);
        SetWindowPos(g_hBtnClear,   NULL, 188,    by, 50, 26, SWP_NOZORDER);

        // OutDir label + edit + button (no overlap with Convert)
        SetWindowPos(g_hLblOutDir,  NULL, 248,    by+4, 110, 20, SWP_NOZORDER);
        int cnvX = w - 120;                               // Convert button left edge
        int btnX = cnvX - 30;                             // "..." button (25w + 5 gap)
        int edW  = btnX - 370;                            // edit fills from 365 to btnX-5
        if (edW < 40) edW = 40;
        SetWindowPos(g_hEditOutDir, NULL, 365,    by+2, edW, 22, SWP_NOZORDER);
        SetWindowPos(g_hBtnOutDir,  NULL, btnX,   by,   25,  22, SWP_NOZORDER);

        // Convert button (right-anchored)
        SetWindowPos(g_hBtnConvert, NULL, w-m-110, by-2, 100, 30, SWP_NOZORDER);

        // Progress bar + status
        SetWindowPos(g_hProgress,   NULL, m, by+32, w-2*m, 18, SWP_NOZORDER);
        SetWindowPos(g_hStatus,     NULL, m, by+54, w-2*m, 20, SWP_NOZORDER);

        // Force full repaint to eliminate black artefacts
        InvalidateRect(hWnd, NULL, TRUE);
        return 0;
    }
    
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

int runGuiWin32(const char* /*cmdLine*/) {
    WNDCLASSA wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "LiarsoftToolWin";
    RegisterClassA(&wc);
    
    HWND hWnd = CreateWindowExA(0, "LiarsoftToolWin", "LiarsoftTool",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 800, 520,
        NULL, NULL, wc.hInstance, NULL);
    
    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

#endif // _WIN32
