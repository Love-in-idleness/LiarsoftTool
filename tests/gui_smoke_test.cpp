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
    bool checkingUpdate = false;
    std::string failure;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    Glib::signal_timeout().connect([&]() {
        try {
            if (!started) {
                const auto bmp = directory / "image.bmp";
                addFiles({liarsoft::gui::nativeTextToUtf8(bmp.string())}, "");
                g_unpackOnlyCheck->set_active(true);
                auto bmpRow = *g_store->children().begin();
                if (bmpRow[g_columns.outputPath] != displayText(bmp.string()) ||
                    bmpRow[g_columns.fileType] != "CodeX BMP -> BMP")
                    throw std::runtime_error("GUI did not select BMP normalization");
                g_unpackOnlyCheck->set_active(false);
                if (bmpRow[g_columns.outputPath] != displayText((directory / "image.wcg").string()))
                    throw std::runtime_error("GUI did not restore BMP packing");
                onClear();
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
                    if (checkingUpdate) {
                        for (auto* window : Gtk::Window::list_toplevels()) {
                            auto* dialog = dynamic_cast<Gtk::MessageDialog*>(window);
                            if (!dialog || !dialog->get_visible()) continue;
                            if (g_statusLabel->get_text() != "Update check failed." ||
                                !g_mainBox->get_sensitive() || g_worker.joinable())
                                failure = "GUI did not recover from an offline update check";
                            dialog->response(Gtk::RESPONSE_CLOSE);
                            g_window->hide();
                            return false;
                        }
                        if (std::chrono::steady_clock::now() > deadline) {
                            failure = "GUI update dialog timed out";
                            g_window->hide();
                            return false;
                        }
                        return true;
                    }
                    const auto finishedRow = *g_store->children().begin();
                    if (finishedRow[g_columns.status] != "SKIPPED") {
                        failure = "GUI operation filters did not skip the input";
                        g_window->hide();
                        return false;
                    }
                    Glib::setenv("HTTPS_PROXY", "http://127.0.0.1:1");
                    Glib::setenv("ALL_PROXY", "http://127.0.0.1:1");
                    Glib::setenv("NO_PROXY", "");
                    checkingUpdate = true;
                    g_updateBtn->clicked();
                    if (!g_busy || g_mainBox->get_sensitive() || !onClose(nullptr))
                        failure = "GUI did not start the update check in the background";
                    return true;
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
