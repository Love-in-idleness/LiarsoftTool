// Exercise the actual GUI callbacks without a second test-only frontend.
#include "../src/gui.cpp"
#include "fileio.h"
#include <chrono>
#include <filesystem>
#include <stdexcept>

int main(int argc, char* argv[]) {
    Glib::init();
#ifdef _WIN32
    // Wine can inherit the host's X11-only backend preference.
    Glib::setenv("GDK_BACKEND", "win32");
#endif
    namespace fs = std::filesystem;
    const auto directory = fs::temp_directory_path() / ("liarsoft-gtk-smoke-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(directory);
    const auto input = directory / "scene.tsc";
    const auto output = directory / "scene.gsc";
    liarsoft::writeTextFileIfChanged(input.string(),
        ";@gsc-byte-format modern-36\n;@gsc-schema modern\n*end\n");
    bool started = false;
    std::string failure;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    Glib::signal_timeout().connect([&]() {
        try {
            if (!started) {
                addFiles({liarsoft::gui::nativeTextToUtf8(input.string())}, "");
                g_convertBtn->clicked();
                if (!g_busy || g_mainBox->get_sensitive() || !onClose(nullptr))
                    throw std::runtime_error("GUI does not lock edits/close during conversion");
                onClear();
                addFiles({liarsoft::gui::nativeTextToUtf8(input.string())}, "");
                if (g_store->children().size() != 1)
                    throw std::runtime_error("GUI queue changed during conversion");
                started = true;
            } else if (!g_busy) {
                const auto row = *g_store->children().begin();
                if (!fs::exists(output) || row[g_columns.status] != "OK" ||
                    !g_mainBox->get_sensitive() || g_worker.joinable() || onClose(nullptr))
                    throw std::runtime_error("GUI did not finish/join the conversion worker");
                g_packOnlyCheck->set_active(true);
                g_unpackOnlyCheck->set_active(true);
                g_convertBtn->clicked();
                // Verify skipped jobs use the same safe completion path.
                Glib::signal_timeout().connect([&]() {
                    if (g_busy) return true;
                    const auto finishedRow = *g_store->children().begin();
                    if (finishedRow[g_columns.status] != "SKIPPED")
                        failure = "GUI operation filters did not skip the input";
                    g_window->hide();
                    return false;
                }, 10);
                return false;
            }
            if (std::chrono::steady_clock::now() > deadline)
                throw std::runtime_error("GUI conversion timed out");
        } catch (const std::exception& error) {
            failure = error.what();
            g_window->hide();
            return false;
        }
        return true;
    }, 10);
    const int result = runGui(argc, argv);
    fs::remove_all(directory);
    if (!failure.empty()) { std::cerr << failure << '\n'; return 1; }
    return result;
}
