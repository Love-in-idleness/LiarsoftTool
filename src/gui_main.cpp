#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "gui_win32.h"
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR cmdLine, int) {
    return runGuiWin32(cmdLine);
}
#else
#include "gui.h"
int main(int argc, char* argv[]) { return runGui(argc, argv); }
#endif
