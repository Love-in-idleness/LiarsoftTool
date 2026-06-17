#ifdef _WIN32
#include "gui_win32.h"
int main(int argc, char* argv[]) { return runGuiWin32(argv[0]); }
#else
#include "gui.h"
int main(int argc, char* argv[]) { return runGui(argc, argv); }
#endif
