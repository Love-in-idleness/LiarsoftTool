// Exercise production callbacks, with isolated configuration and no player data.
#include "../src/gui.cpp"
#include "fileio.h"
#include "gsc_decompiler.h"
#include "gscfile.h"
#include <chrono>
#include <filesystem>
#include <stdexcept>

namespace {
std::filesystem::path directory;
std::string failure;
int stage = 0;
auto started = std::chrono::steady_clock::now();
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
gboolean tick(gpointer) {
    try {
        require(std::chrono::steady_clock::now() - started < std::chrono::seconds(20), "GUI timeout");
        if (!ui.window) return G_SOURCE_CONTINUE;
        if (const auto* language=g_getenv("LIARSOFT_GUI_TEST_LANGUAGE")) {
            const std::string expected=std::string(language)=="zh_CN"?"设置":std::string(language)=="ru"?"Настройки":std::string(language)=="ja"?"設定":"Settings";
            require(_("Settings")==expected,"GTK initialization reset the selected interface language");
        }

        const auto input = directory / "scene.tsc";
        if (stage == 0) {
            if (const char* destination = g_getenv("LIARSOFT_GUI_CAPTURE")) {
                auto* paintable = gtk_widget_paintable_new(GTK_WIDGET(ui.window));
                auto* snapshot = gtk_snapshot_new();
                gdk_paintable_snapshot(paintable, snapshot, gtk_widget_get_width(GTK_WIDGET(ui.window)),
                                      gtk_widget_get_height(GTK_WIDGET(ui.window)));
                auto* node = gtk_snapshot_free_to_node(snapshot);
                if (!node) { g_object_unref(paintable); return G_SOURCE_CONTINUE; }
                auto* texture = gsk_renderer_render_texture(gtk_native_get_renderer(GTK_NATIVE(ui.window)), node, nullptr);
                require(gdk_texture_save_to_png(texture, destination), "window capture failed");
                g_object_unref(texture); gsk_render_node_unref(node); g_object_unref(paintable);
            }
            require(GTK_IS_STACK_SWITCHER(ui.navigation), "Navigation should use text-only tabs");
            std::function<void(GtkWidget*)> noIcons=[&](GtkWidget* w) {
                require(!GTK_IS_IMAGE(w) || !gtk_widget_get_visible(w), "Navigation has a placeholder icon");
                for (auto* c=gtk_widget_get_first_child(w);c;c=gtk_widget_get_next_sibling(c)) noIcons(c);
            };
            noIcons(ui.navigation);
            require(std::string(gtk_button_get_label(GTK_BUTTON(ui.preferencesButton)))==_("Settings"),"Settings button name differs");
            require(gtk_widget_get_ancestor(ui.updateButton,ADW_TYPE_PREFERENCES_GROUP) &&
                !gtk_widget_is_ancestor(ui.updateButton,GTK_WIDGET(ui.window)),"Update action is outside Settings");
            require(std::string(FeedbackUrl)=="https://github.com/Love-in-idleness/LiarsoftTool/issues/new/choose","Feedback link is incorrect");
            require(gtk_widget_is_ancestor(ui.encoding,ui.conversionOptions) &&
                gtk_widget_is_ancestor(ui.conversionOptions,GTK_WIDGET(ui.window)) &&
                !gtk_widget_is_ancestor(ui.encoding,GTK_WIDGET(ui.preferencesDialog)),
                "Conversion options are not on the main page");
            auto* experimental=gtk_widget_get_ancestor(ui.exeToggle,ADW_TYPE_PREFERENCES_GROUP);
            require(experimental && std::string(adw_preferences_group_get_title(ADW_PREFERENCES_GROUP(experimental)))==_("Experimental features") &&
                !gtk_widget_is_ancestor(ui.exeToggle,ui.conversionOptions),
                "EXE switch belongs in its own preferences group");
            require(options().referencePath.empty(), "GUI must use same-folder TXT references");
            require(!ui.preferences.experimentalExe && !gtk_stack_page_get_visible(ui.exePage), "EXE editor enabled by default");
            gtk_check_button_set_active(GTK_CHECK_BUTTON(ui.exeToggle), TRUE);
            noIcons(ui.navigation);
            require(gtk_stack_page_get_visible(ui.exePage) && liarsoft::gui::loadPreferences().experimentalExe, "EXE editor preference not saved");
            gtk_check_button_set_active(GTK_CHECK_BUTTON(ui.exeToggle), FALSE);
            require(!gtk_stack_page_get_visible(ui.exePage) && !liarsoft::gui::loadPreferences().experimentalExe, "EXE editor preference not cleared");
            require(options().encoding == "auto" && options().gscToTsc && !options().recursive &&
                !options().unpackOnly && !active(ui.packOnly) && !options().vorbisInWav &&
                options().workers == 0, "conversion defaults changed");
            for (guint index = 0; index < liarsoft::supportedEncodings().size(); ++index) {
                gtk_drop_down_set_selected(GTK_DROP_DOWN(ui.encoding), index + 1);
                require(options().encoding == liarsoft::supportedEncodings()[index],
                        "Conversion encoding index is incorrect");
            }
            gtk_drop_down_set_selected(GTK_DROP_DOWN(ui.encoding), 1);
            for (unsigned value = 0; value < 3; ++value) {
                ui.preferences.theme = value; theme(); savePreferences();
                require(liarsoft::gui::loadPreferences().theme == value, "theme not saved");
                const AdwColorScheme expected[] = {ADW_COLOR_SCHEME_DEFAULT, ADW_COLOR_SCHEME_FORCE_LIGHT, ADW_COLOR_SCHEME_FORCE_DARK};
                require(adw_style_manager_get_color_scheme(adw_style_manager_get_default()) == expected[value], "theme not applied");
            }
            for (auto language : {"en", "ru", "zh_CN", "ja", ""}) {
                ui.preferences.language = language; savePreferences();
                if (std::string(language)!="") {
                    const auto code=std::string(language)=="zh_CN"?"zh":language;
                    require(userGuideUrl()==std::string("https://github.com/Love-in-idleness/LiarsoftTool/blob/master/README.")+code+".md","Wrong user guide language");
                }
                require(liarsoft::gui::loadPreferences().language == language, "language not saved");
            }
            adw_dialog_present(ui.preferencesDialog, GTK_WIDGET(ui.window));
            adw_dialog_close(ui.preferencesDialog);
            adw_dialog_present(ui.preferencesDialog, GTK_WIDGET(ui.window));
            adw_dialog_close(ui.preferencesDialog);
            addFiles({display((directory / "image.bmp").string())});
            gtk_check_button_set_active(GTK_CHECK_BUTTON(ui.unpackOnly), TRUE);
            require(ui.jobs[0].output == (directory / "image.bmp").string(), "BMP normalization output");
            gtk_check_button_set_active(GTK_CHECK_BUTTON(ui.unpackOnly), FALSE);
            require(ui.jobs[0].output == (directory / "image.wcg").string(), "BMP packing output");
            clearFiles();
            addFiles({display(input.string()),display((directory/"one/scene.txt").string()),
                display((directory/"two/scene.txt").string())});
            g_signal_emit_by_name(ui.convert, "clicked");
            require(ui.busy && !gtk_widget_get_sensitive(ui.content) && closeWindow(nullptr, nullptr), "worker does not lock edits/close");
            clearFiles(); addFiles({display(input.string())});
            require(ui.jobs.size() == 3, "queue changed during conversion");
            stage = 1;
        } else if (!ui.busy && stage == 1) {
            require(std::filesystem::exists(directory / "scene.gsc"), "conversion produced no GSC");
            for (auto name:{"one","two"}) {
                const auto result=liarsoft::GscFile::fromFile((directory/name/"scene.gsc").string(),"CP932");
                const std::string expected=std::string("translated-")+name;
                require(std::find(result.strings.begin(),result.strings.end(),expected)!=result.strings.end(),
                    "TXT import used the wrong reference or failed");
            }

            require(std::string(gtk_label_get_text(GTK_LABEL(ui.jobs[0].status))) == _("OK"), "conversion status");
            require(gtk_widget_get_sensitive(ui.content) && !ui.worker.joinable() && !closeWindow(nullptr, nullptr), "worker not joined/unlocked");

            gtk_check_button_set_active(GTK_CHECK_BUTTON(ui.packOnly), TRUE);
            gtk_check_button_set_active(GTK_CHECK_BUTTON(ui.unpackOnly), TRUE);
            convert(); stage = 2;
        } else if (!ui.busy && stage == 2) {
            require(std::string(gtk_label_get_text(GTK_LABEL(ui.jobs[0].status))) == _("Skipped"), "both operation filters should skip");
            g_setenv("HTTPS_PROXY", "http://127.0.0.1:1", TRUE);
            g_setenv("ALL_PROXY", "http://127.0.0.1:1", TRUE);
            g_setenv("NO_PROXY", "", TRUE);
            checkUpdate(); stage = 3;
            require(ui.busy && closeWindow(nullptr, nullptr), "update not asynchronous");
        } else if (!ui.busy && stage == 3 && ui.alert) {
            require(std::string(gtk_label_get_text(GTK_LABEL(ui.status))) == _("Update check failed"), "offline error not reported");
            adw_dialog_force_close(ADW_DIALOG(ui.alert));
            gtk_window_destroy(ui.window); return G_SOURCE_REMOVE;
        }
    } catch (const std::exception& error) {
        failure = error.what();
        if (ui.window && !ui.busy) gtk_window_destroy(ui.window);
        else if (ui.window) g_application_quit(G_APPLICATION(gtk_window_get_application(ui.window)));
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}
}

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    directory = fs::temp_directory_path() / ("liarsoft-gtk4-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(directory);
    g_setenv("XDG_CONFIG_HOME", directory.string().c_str(), TRUE);
    g_setenv("XDG_CACHE_HOME", directory.string().c_str(), TRUE);
    g_setenv("LANGUAGE", argc == 2 ? argv[1] : "en", TRUE);
    if (argc == 2) {
        const std::string language = liarsoft::gui::effectiveLanguage("");
        const bool standalone = g_getenv("LIARSOFT_TEST_EMBEDDED") != nullptr;
        const std::string executable = standalone ? (directory / "standalone/gui").string() : argv[0];
        liarsoft::gui::initializeTranslations(language, executable.c_str());
        if (standalone) {
            const auto catalog = directory / "liarsofttool/locale" / language / "LC_MESSAGES/liarsofttool.mo";
            require(fs::is_regular_file(catalog), "standalone catalog not installed");
            const auto modified = fs::last_write_time(catalog);
            liarsoft::gui::initializeTranslations(language, executable.c_str());
            require(fs::last_write_time(catalog) == modified, "unchanged catalog was rewritten");
        }
        const std::string expected = language == "ru" ? "Настройки" : language == "zh_CN" ? "设置" :
            language == "ja" ? "設定" : "Settings";
        require(language == (std::string(argv[1]).compare(0, 2, "ja") == 0 ? "ja" :
            std::string(argv[1]) == "fr_FR" ? "en" : argv[1]), "system language detection failed");
        const std::string exeExpected = language == "ru" ? "Редактор EXE" : language == "zh_CN" ? "EXE 编辑" :
            language == "ja" ? "EXE 編集" : "EXE editor";
        require(_("EXE editor") == exeExpected, "EXE editor translation not loaded");
        if (_("Settings") != expected) {
            std::cerr << "language=" << language << " locale=" << std::setlocale(LC_MESSAGES, nullptr)
                      << " catalogs=" << bindtextdomain("liarsofttool", nullptr)
                      << " actual=" << _("Settings") << " expected=" << expected << '\n';
            require(false, "gettext language selection failed");
        }
        fs::remove_all(directory); return 0;
    }
    if (const auto* language=g_getenv("LIARSOFT_GUI_TEST_LANGUAGE")) {
        liarsoft::gui::Preferences preferences;preferences.language=language;
        liarsoft::gui::savePreferences(preferences);
    }
    liarsoft::writeTextFileIfChanged((directory / "scene.tsc").string(),
        ";@gsc-byte-format modern-36\n;@gsc-schema modern\n*end\n");
    for (auto name:{"one","two"}) {
        fs::create_directories(directory/name);
        const std::string source=";@gsc-byte-format modern-36\n;@gsc-schema modern\n*TXT 0 0 0 0 \"\" \"original\" 0\n*end\n";
        liarsoft::writeFileIfChanged((directory/name/"scene.gsc").string(),liarsoft::restoreGscFromTsc(source,"CP932"));
        liarsoft::writeTextFileIfChanged((directory/name/"scene.txt").string(),std::string("#original\n>translated-")+name+"\n");
    }
    g_timeout_add(10, tick, nullptr);
    const int result = runGui(argc, argv);
    fs::remove_all(directory);
    if (!failure.empty()) { std::cerr << failure << '\n'; return 1; }
    std::cout << "OK: GTK4 conversion, filters, preferences, theme, language, offline update\n";
    return result;
}
