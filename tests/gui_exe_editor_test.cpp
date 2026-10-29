#include "../src/gui_exe_editor.cpp"
#include "exe_editor_fixture.h"
#include "fileio.h"
#include <chrono>
#include <iostream>
using namespace liarsoft;
using namespace liarsoft::gui;
namespace fs = std::filesystem;
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void wait(F condition, const char *phase) {
    for (unsigned n = 0; n < 2000; ++n) {
        while (g_main_context_iteration(nullptr, FALSE)) {
        }
        if (condition())
            return;
        g_usleep(10000);
    }
    throw std::runtime_error(std::string("EXE GUI timeout: ") + phase);
}
int main() {
    const auto root =
        fs::temp_directory_path() /
        ("exe-gui-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    GtkWindow *window = nullptr;
    try {
        fs::create_directories(root);
        g_setenv("GSETTINGS_BACKEND", "memory", TRUE);
        g_setenv("GTK_A11Y", "none", TRUE);
        adw_init();
        window = GTK_WINDOW(adw_window_new());
        gtk_window_set_default_size(window, 800, 650);
        auto *widget = createExeEditor(window);
        adw_window_set_content(ADW_WINDOW(window), widget);
        gtk_window_present(window);
        auto e = state(widget);
        const auto source = root / "game.exe", other = root / "other.exe", output = root / "saved.exe";
        const auto fixture = editorFixture();
        writePathIfChanged(source, fixture.bytes);
        auto modified = fixture.bytes;
        modified.back() ^= 1;
        writePathIfChanged(other, modified);
        analyse(e, source, "auto");
        analyse(e, other, "CP932");
        wait([&] { return e->document.has_value(); }, "analysis");
        require(e->document->bytes == modified && e->document->texts.size() == 2,
                "Stale EXE analysis replaced current data");
        require(!exeEditorDirty(widget), "Loaded EXE marked dirty");
        setText(e->translations[0],
                "A substantially longer GUI translation, including the original format placeholder %s");
        require(exeEditorDirty(widget), "Prompt changes did not mark editor dirty");
        select(e->target, "GBK");
        std::function<GtkWidget *(GtkWidget *)> findDefaults = [&](GtkWidget *w) -> GtkWidget * {
            if (GTK_IS_BUTTON(w) && gtk_button_get_label(GTK_BUTTON(w)) &&
                std::string(gtk_button_get_label(GTK_BUTTON(w))) ==
                    "Use target encoding default configuration")
                return w;
            for (auto *c = gtk_widget_get_first_child(w); c; c = gtk_widget_get_next_sibling(c))
                if (auto *found = findDefaults(c))
                    return found;
            return nullptr;
        };
        auto *defaults = findDefaults(widget);
        require(defaults, "Default configuration button missing");
        const auto custom = text(e->translations[0]);
        g_signal_emit_by_name(defaults, "clicked");
        require(text(e->translations[0]) == custom && text(e->translations[1]) == u8"确认",
                "Default configuration overwrote custom text or missed stock text");

        bool left = false;
        leaveExeEditor(widget, [&] { left = true; });
        require(e->leaveDialog && !left, "Unsaved changes did not prompt");
        wait([&] { return gtk_widget_get_mapped(GTK_WIDGET(e->leaveDialog)); }, "cancel dialog mapped");
        adw_dialog_force_close(ADW_DIALOG(e->leaveDialog));
        wait([&] { return !e->leaveDialog; }, "cancel response");
        require(!left && exeEditorDirty(widget), "Cancel discarded changes");
        leaveExeEditor(widget, [&] { left = true; });
        wait([&] { return gtk_widget_get_mapped(GTK_WIDGET(e->leaveDialog)); }, "discard dialog mapped");
        g_signal_emit_by_name(e->leaveDialog, "response", "discard");
        wait([&] { return left; }, "discard response");
        require(!exeEditorDirty(widget), "Discard did not clear unsaved state");
        setText(e->translations[0], "Saved by the GUI with format %s");
        setText(e->start, ";");
        setText(e->end, "(");
        require(changes(e).rules.has_value(), "Rule edits did not enter the shared request");
        save(e, output);
        require(exeEditorWriting(widget), "GUI save is not asynchronous");
        wait([&] { return !exeEditorWriting(widget) && !exeEditorDirty(widget); }, "save");
        const auto saved = inspectExeFile(output);
        require(saved.expandedRules && saved.rules.noLineStart == ";", "GUI did not save expanded rules");
        require(readFile(other) == modified && readFile(source) == fixture.bytes, "GUI changed input EXE");
        analyse(e, source, "auto");
        g_signal_emit_by_name(e->cancel, "clicked");
        for (unsigned n = 0; n < 50; ++n) {
            while (g_main_context_iteration(nullptr, FALSE)) {
            }
            g_usleep(1000);
        }
        require(!e->document && std::string(gtk_label_get_text(GTK_LABEL(e->status))).find("cancelled") !=
                                    std::string::npos,
                "Cancelled inspection showed a stale result");
        gtk_window_destroy(window);
        window = nullptr;
        while (g_main_context_iteration(nullptr, FALSE)) {
        }
        fs::remove_all(root);
        std::cout << "EXE GUI: background results, prompt/rule edits, unsaved cancel/discard and safe save "
                     "passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        if (window)
            gtk_window_destroy(window);
        std::error_code ignored;
        fs::remove_all(root, ignored);
        return 1;
    }
}
