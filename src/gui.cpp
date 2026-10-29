#include "gui.h"
#include "gui_common.h"
#include "gui_preferences.h"
#include "gui_browser.h"
#include "gui_exe_editor.h"
#include "conversion.h"
#include "encoding.h"
#include "xflarchive.h"
#include "update_check.h"
#include <adwaita.h>
#include <libintl.h>
#include <functional>
#include <iostream>
#include <thread>
#include <utility>

#define _(text) gettext(text)

namespace {
struct Job {
    std::string input, output;
    GtkWidget *row, *outputLabel, *typeLabel, *status;
};
struct Gui {
    GtkWindow* window = nullptr;
    GtkWidget *content, *list, *convert, *status, *progress, *browser = nullptr;
    GtkWidget *encoding, *reference, *output, *recursive, *packOnly, *unpackOnly, *txt, *vorbis, *workers;
    GtkWidget *preferencesButton, *updateButton;
    GtkWidget *exeEditor = nullptr, *exeToggle = nullptr;
    AdwViewStackPage* exePage = nullptr;
    bool changingExperimental = false;
    AdwDialog* preferencesDialog = nullptr;
    AdwAlertDialog* alert = nullptr;
    liarsoft::gui::Preferences preferences;
    std::vector<Job> jobs;
    std::thread worker;
    bool busy = false;
} ui;

static void post(std::function<void()> callback) {
    // Always dispatch on the GTK thread, even before its main loop starts.
    g_idle_add_full(G_PRIORITY_DEFAULT, [](gpointer data) -> gboolean {
        (*static_cast<std::function<void()>*>(data))();
        return G_SOURCE_REMOVE;
    }, new std::function<void()>(std::move(callback)), [](gpointer data) {
        delete static_cast<std::function<void()>*>(data);
    });
}
static std::string display(const std::string& text) {
    return liarsoft::gui::nativeTextToUtf8(text);
}
static std::string path(GtkWidget* entry) {
    return liarsoft::gui::nativePathFromUtf8(gtk_editable_get_text(GTK_EDITABLE(entry)));
}
static bool active(GtkWidget* widget) { return gtk_check_button_get_active(GTK_CHECK_BUTTON(widget)); }
static GtkWidget* label(const char* text, bool expand = false) {
    auto* widget = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(widget), 0);
    gtk_label_set_ellipsize(GTK_LABEL(widget), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_hexpand(widget, expand);
    return widget;
}
static void margins(GtkWidget* widget, int size) {
    gtk_widget_set_margin_start(widget, size); gtk_widget_set_margin_end(widget, size);
    gtk_widget_set_margin_top(widget, size); gtk_widget_set_margin_bottom(widget, size);
}
static GtkWidget* button(const char* text, GCallback callback, gpointer data = nullptr) {
    auto* widget = gtk_button_new_with_label(text);
    g_signal_connect(widget, "clicked", callback, data);
    return widget;
}
static void report(const char* heading, const std::string& details, bool releases = false) {
    auto* dialog = adw_alert_dialog_new(heading, nullptr);
    ui.alert = ADW_ALERT_DIALOG(dialog);
    auto* text = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD_CHAR);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(text)), details.c_str(), -1);
    auto* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scroll), 440);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 160);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), text);
    adw_alert_dialog_set_extra_child(ui.alert, scroll);
    adw_alert_dialog_add_response(ui.alert, "copy", _("Copy details"));
    adw_alert_dialog_add_response(ui.alert, "close", _("Close"));
    if (releases) adw_alert_dialog_add_response(ui.alert, "releases", _("Open releases"));
    adw_alert_dialog_set_close_response(ui.alert, "close");
    g_signal_connect(dialog, "response", reinterpret_cast<GCallback>(+[](AdwAlertDialog* dialog, const char* response, gpointer) {
        if (std::string(response) == "copy") {
            auto* scroll = adw_alert_dialog_get_extra_child(dialog);
            auto* view = gtk_scrolled_window_get_child(GTK_SCROLLED_WINDOW(scroll));
            auto* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
            GtkTextIter begin, end;
            gtk_text_buffer_get_bounds(buffer, &begin, &end);
            auto* text = gtk_text_buffer_get_text(buffer, &begin, &end, FALSE);
            gdk_clipboard_set_text(gtk_widget_get_clipboard(GTK_WIDGET(ui.window)), text);
            g_free(text);
        } else if (std::string(response) == "releases") {
            GError* error = nullptr;
            if (!g_app_info_launch_default_for_uri(liarsoft::LatestReleaseUrl, nullptr, &error)) {
                const std::string message = error->message;
                g_error_free(error);
                post([message] { report(_("Could not open the link"), message); });
            }
        }
        ui.alert = nullptr;
    }), nullptr);
    adw_dialog_present(dialog, GTK_WIDGET(ui.window));
}
static void inputError(const std::exception& error) { report(_("Operation failed"), display(error.what())); }
static void sensitive() {
    gtk_widget_set_sensitive(ui.content, !ui.busy);
    gtk_widget_set_sensitive(ui.preferencesButton, !ui.busy);
    gtk_widget_set_sensitive(ui.updateButton, !ui.busy);
    gtk_widget_set_sensitive(ui.convert, !ui.busy && !ui.jobs.empty());
    if (ui.preferencesDialog) gtk_widget_set_sensitive(GTK_WIDGET(ui.preferencesDialog), !ui.busy);
}
static void busy(bool value) { ui.busy = value; sensitive(); }
static gboolean closeWindow(GtkWindow*, gpointer) {
    const bool working = ui.busy || liarsoft::gui::resourceBrowserWriting(ui.browser) || liarsoft::gui::exeEditorWriting(ui.exeEditor);
    if (working) gtk_label_set_text(GTK_LABEL(ui.status), _("Please wait for the operation to finish."));
    if (!working && liarsoft::gui::exeEditorDirty(ui.exeEditor)) {
        liarsoft::gui::leaveExeEditor(ui.exeEditor, [] { gtk_window_destroy(ui.window); });
        return TRUE;
    }
    return working;
}
static liarsoft::conversion::ConversionOptions options() {
    const auto index = gtk_drop_down_get_selected(GTK_DROP_DOWN(ui.encoding));
    return {index ? liarsoft::supportedEncodings().at(index - 1) : "auto", path(ui.reference),
        active(ui.recursive), !active(ui.txt), active(ui.unpackOnly), active(ui.vorbis),
        static_cast<unsigned>(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(ui.workers)))};
}
static void updateOutputs() {
    if (ui.busy) return;
    try {
        const auto opt = options();
        const auto directory = path(ui.output);
        for (auto& job : ui.jobs) {
            job.output = liarsoft::conversion::guessOutput(job.input, directory, opt.gscToTsc, opt.encoding, opt.unpackOnly);
            gtk_label_set_text(GTK_LABEL(job.outputLabel), display(job.output).c_str());
            gtk_widget_set_tooltip_text(job.outputLabel, display(job.output).c_str());
            gtk_label_set_text(GTK_LABEL(job.typeLabel), liarsoft::conversion::guessType(
                job.input, opt.gscToTsc, opt.encoding, opt.unpackOnly).c_str());
        }
    } catch (const std::exception& error) { inputError(error); }
}
static void addFiles(const std::vector<std::string>& paths) {
    if (ui.busy) return;
    for (const auto& value : paths) {
        try {
            const auto input = liarsoft::gui::nativePathFromUtf8(value);
            if (!liarsoft::conversion::isSupported(input)) {
                report(_("Unsupported input"), value); continue;
            }
            auto* row = gtk_list_box_row_new();
            auto* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
            margins(box, 12);
            gtk_box_append(GTK_BOX(box), label(value.c_str()));
            auto* output = label("");
            gtk_widget_add_css_class(output, "dim-label");
            gtk_box_append(GTK_BOX(box), output);
            auto* footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
            auto* type = label("", true);
            auto* status = label(_("Ready"));
            gtk_box_append(GTK_BOX(footer), type); gtk_box_append(GTK_BOX(footer), status);
            gtk_box_append(GTK_BOX(box), footer);
            gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
            gtk_list_box_append(GTK_LIST_BOX(ui.list), row);
            ui.jobs.push_back({input, "", row, output, type, status});
        } catch (const std::exception& error) { inputError(error); }
    }
    updateOutputs(); sensitive();
}
static void clearFiles() {
    if (ui.busy) return;
    for (const auto& job : ui.jobs) gtk_list_box_remove(GTK_LIST_BOX(ui.list), job.row);
    ui.jobs.clear(); sensitive();
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui.progress), 0);
    gtk_label_set_text(GTK_LABEL(ui.status), _("Ready"));
}
static void convert() {
    if (ui.busy || ui.jobs.empty() || liarsoft::gui::resourceBrowserWriting(ui.browser)) return;
    try {
        const auto opt = options();
        const bool packOnly = active(ui.packOnly);
        const auto jobs = ui.jobs;
        busy(true);
        gtk_label_set_text(GTK_LABEL(ui.status), _("Converting…"));
        ui.worker = std::thread([opt, packOnly, jobs] {
            std::vector<liarsoft::gui::ConversionDiagnostic> diagnostics;
            bool errors = false;
            size_t done = 0;
            for (const auto& job : jobs) {
                std::string status;
                try {
                    if (!liarsoft::matchesOperationMode(job.input, packOnly, opt.unpackOnly, opt.recursive))
                        status = _("Skipped");
                    else {
                        const auto result = liarsoft::conversion::convert(job.input, job.output, opt);
                        status = result.warnings.empty() ? (result.retainedPcm ? _("OK (PCM retained)") : _("OK")) : _("Warning");
                        for (const auto& warning : result.warnings) {
                            diagnostics.push_back({false, job.input, job.output, warning});
                            std::cerr << "Warning: " << warning << '\n';
                        }
                    }
                } catch (const std::exception& error) {
                    errors = true; status = _("Failed");
                    diagnostics.push_back({true, job.input, job.output, error.what()});
                }
                const double fraction = static_cast<double>(++done) / jobs.size();
                post([job, status, fraction] {
                    gtk_label_set_text(GTK_LABEL(job.status), status.c_str());
                    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ui.progress), fraction);
                });
            }
            // Localize framing; preserve the complete core/system diagnostics.
            std::string details;
            for (const auto& item : diagnostics) details += std::string(item.error ? _("Error") : _("Warning")) +
                "\n" + _("Input") + ": " + display(item.inputPath) + "\n" + _("Output") + ": " +
                display(item.outputPath) + "\n" + display(item.message) + "\n\n";
            post([errors, details] {
                ui.worker.join(); busy(false);
                gtk_label_set_text(GTK_LABEL(ui.status), errors ? _("Completed with errors") :
                    details.empty() ? _("All done") : _("Completed with warnings"));
                if (!details.empty()) report(errors ? _("Completed with errors") : _("Completed with warnings"), details);
            });
        });
    } catch (const std::exception& error) { busy(false); inputError(error); }
}
static void checkUpdate() {
    if (ui.busy) return;
    busy(true);
    gtk_label_set_text(GTK_LABEL(ui.status), _("Checking GitHub for updates…"));
    try {
        ui.worker = std::thread([] {
            liarsoft::UpdateInfo info{};
            std::string error;
            try { info = liarsoft::checkForUpdate(); }
            catch (const std::exception& exception) { error = exception.what(); }
            post([info, error] {
                ui.worker.join(); busy(false);
                gtk_label_set_text(GTK_LABEL(ui.status), error.empty() ? _("Update check complete") : _("Update check failed"));
                if (!error.empty()) report(_("Update check failed"), display(error));
                else report(info.available ? _("A new version is available") : _("No newer release is available"),
                    std::string(_("Current")) + ": " + liarsoft::CurrentVersion + "\n" + _("Latest") + ": " + info.latestVersion, true);
            });
        });
    } catch (const std::exception& error) { busy(false); inputError(error); }
}
enum class Choose { Files, Folder, Output, Reference };
static void choose(Choose purpose) {
    auto* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, purpose == Choose::Output ? _("Output directory") :
        purpose == Choose::Reference ? _("Reference GSC") : _("Select input"));
    auto* context = new Choose(purpose);
    auto callback = +[](GObject* object, GAsyncResult* result, gpointer data) {
        const auto purpose = *static_cast<Choose*>(data);
        delete static_cast<Choose*>(data);
        auto* dialog = GTK_FILE_DIALOG(object);
        GError* error = nullptr;
        std::vector<std::string> paths;
        auto append = [&paths](GFile* file) {
            if (!file) return;
            auto* filename = g_file_get_path(file);
            if (filename) { paths.emplace_back(filename); g_free(filename); }
            g_object_unref(file);
        };
        if (purpose == Choose::Files) {
            auto* files = gtk_file_dialog_open_multiple_finish(dialog, result, &error);
            if (files) {
                for (guint i = 0; i < g_list_model_get_n_items(files); ++i)
                    append(G_FILE(g_list_model_get_item(files, i)));
                g_object_unref(files);
            }
        } else if (purpose == Choose::Reference)
            append(gtk_file_dialog_open_finish(dialog, result, &error));
        else append(gtk_file_dialog_select_folder_finish(dialog, result, &error));
        if (error) {
            if (!g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED) &&
                !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) report(_("Operation failed"), error->message);
            g_error_free(error);
        }
        if (!paths.empty()) {
            if (purpose == Choose::Output || purpose == Choose::Reference)
                gtk_editable_set_text(GTK_EDITABLE(purpose == Choose::Output ? ui.output : ui.reference), paths[0].c_str());
            else addFiles(paths);
        }
        g_object_unref(object);
    };
    if (purpose == Choose::Files) gtk_file_dialog_open_multiple(dialog, ui.window, nullptr, callback, context);
    else if (purpose == Choose::Reference) gtk_file_dialog_open(dialog, ui.window, nullptr, callback, context);
    else gtk_file_dialog_select_folder(dialog, ui.window, nullptr, callback, context);
}
static void theme() {
    const AdwColorScheme schemes[] = {ADW_COLOR_SCHEME_DEFAULT, ADW_COLOR_SCHEME_FORCE_LIGHT, ADW_COLOR_SCHEME_FORCE_DARK};
    adw_style_manager_set_color_scheme(adw_style_manager_get_default(), schemes[ui.preferences.theme]);
}
static void savePreferences() {
    try { liarsoft::gui::savePreferences(ui.preferences); }
    catch (const std::exception& error) { inputError(error); }
}
static void addRow(AdwPreferencesGroup* group, const char* title, GtkWidget* widget, GtkWidget* extra = nullptr) {
    auto* row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    gtk_widget_set_valign(widget, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(ADW_ACTION_ROW(row), widget);
    if (extra) adw_action_row_add_suffix(ADW_ACTION_ROW(row), extra);
    adw_action_row_set_activatable_widget(ADW_ACTION_ROW(row), widget);
    adw_preferences_group_add(group, row);
}
static GtkWidget* check(AdwPreferencesGroup* group, const char* title) {
    auto* widget = gtk_check_button_new();
    gtk_accessible_update_property(GTK_ACCESSIBLE(widget), GTK_ACCESSIBLE_PROPERTY_LABEL, title, -1);
    addRow(group, title, widget);
    return widget;
}
static void buildPreferences() {
    ui.preferencesDialog = ADW_DIALOG(adw_preferences_dialog_new());
    g_object_ref_sink(ui.preferencesDialog);
    adw_dialog_set_title(ui.preferencesDialog, _("Preferences"));
    auto* page = adw_preferences_page_new();
    adw_preferences_dialog_add(ADW_PREFERENCES_DIALOG(ui.preferencesDialog), ADW_PREFERENCES_PAGE(page));
    auto* group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(group, _("Appearance and language"));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), group);
    const char* themes[] = {_("Follow system"), _("Light"), _("Dark"), nullptr};
    auto* appearance = gtk_drop_down_new_from_strings(themes);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(appearance), ui.preferences.theme);
    addRow(group, _("Theme"), appearance);
    g_signal_connect(appearance, "notify::selected", reinterpret_cast<GCallback>(+[](GObject* object, GParamSpec*, gpointer) {
        ui.preferences.theme = gtk_drop_down_get_selected(GTK_DROP_DOWN(object)); theme(); savePreferences();
    }), nullptr);
    const char* languages[] = {_("System language"), "English", "Русский", "简体中文", "日本語", nullptr};
    auto* language = gtk_drop_down_new_from_strings(languages);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(language), ui.preferences.language == "en" ? 1 :
        ui.preferences.language == "ru" ? 2 : ui.preferences.language == "zh_CN" ? 3 :
        ui.preferences.language == "ja" ? 4 : 0);
    addRow(group, _("Language (after restart)"), language);
    g_signal_connect(language, "notify::selected", reinterpret_cast<GCallback>(+[](GObject* object, GParamSpec*, gpointer) {
        const char* values[] = {"", "en", "ru", "zh_CN", "ja"};
        ui.preferences.language = values[gtk_drop_down_get_selected(GTK_DROP_DOWN(object))]; savePreferences();
    }), nullptr);
    group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(group, _("Conversion"));
    adw_preferences_page_add(ADW_PREFERENCES_PAGE(page), group);
    auto* encodings = gtk_string_list_new(nullptr);
    gtk_string_list_append(encodings, _("Auto (GSC input)"));
    for (const auto& encoding : liarsoft::supportedEncodings()) gtk_string_list_append(encodings, encoding.c_str());
    ui.encoding = gtk_drop_down_new(G_LIST_MODEL(encodings), nullptr);
    addRow(group, _("Resource encoding"), ui.encoding);
    gtk_widget_set_tooltip_text(ui.encoding, _("Auto detects GSC input. Choose an explicit encoding for packing or GSC output. Archive names and other legacy text use CP932 in auto mode."));
    ui.exeToggle = check(group, _("EXE editor (experimental)"));
    gtk_check_button_set_active(GTK_CHECK_BUTTON(ui.exeToggle), ui.preferences.experimentalExe);
    g_signal_connect(ui.exeToggle,"toggled",reinterpret_cast<GCallback>(+[](GtkCheckButton* toggle,gpointer) {
        if (ui.changingExperimental) return;
        const bool enabled = gtk_check_button_get_active(toggle);
        auto apply = [enabled] {
            ui.preferences.experimentalExe = enabled;
            if (ui.exePage) adw_view_stack_page_set_visible(ui.exePage, enabled);
            if (!enabled && ui.exeEditor && adw_view_stack_get_visible_child(ADW_VIEW_STACK(ui.content)) == ui.exeEditor)
                adw_view_stack_set_visible_child_name(ADW_VIEW_STACK(ui.content),"tasks");
            ui.changingExperimental = true; gtk_check_button_set_active(GTK_CHECK_BUTTON(ui.exeToggle),enabled); ui.changingExperimental = false;
            savePreferences();
        };
        if (!enabled) {
            ui.changingExperimental = true; gtk_check_button_set_active(toggle, TRUE); ui.changingExperimental = false;
            liarsoft::gui::leaveExeEditor(ui.exeEditor, apply);
        } else apply();
    }),nullptr);
    ui.recursive = check(group, _("Recursive"));
    ui.packOnly = check(group, _("Pack only"));
    ui.unpackOnly = check(group, _("Unpack only"));
    ui.txt = check(group, _("GSC → TXT (instead of TSC)"));
    ui.vorbis = check(group, _("Vorbis-in-WAV"));
    ui.workers = gtk_spin_button_new_with_range(0, liarsoft::MaxConversionWorkers, 1);
    addRow(group, _("Threads (0 = automatic)"), ui.workers);
    ui.output = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ui.output), _("Same as input"));
    addRow(group, _("Output directory"), ui.output, button(_("Choose…"), reinterpret_cast<GCallback>(+[](GtkButton*, gpointer) { choose(Choose::Output); })));
    ui.reference = gtk_entry_new();
    addRow(group, _("Reference GSC for TXT"), ui.reference, button(_("Choose…"), reinterpret_cast<GCallback>(+[](GtkButton*, gpointer) { choose(Choose::Reference); })));
    for (auto* widget : {ui.output, ui.reference})
        g_signal_connect(widget, "changed", reinterpret_cast<GCallback>(+[](GtkEditable*, gpointer) { updateOutputs(); }), nullptr);
    for (auto* widget : {ui.unpackOnly, ui.txt})
        g_signal_connect(widget, "toggled", reinterpret_cast<GCallback>(+[](GtkCheckButton*, gpointer) { updateOutputs(); }), nullptr);
    g_signal_connect(ui.encoding, "notify::selected", reinterpret_cast<GCallback>(+[](GObject*, GParamSpec*, gpointer) { updateOutputs(); }), nullptr);
}
static void activate(GtkApplication* app, gpointer) {
    if (ui.window) { gtk_window_present(ui.window); return; }
    ui.window = GTK_WINDOW(adw_application_window_new(app));
    gtk_window_set_title(ui.window, (std::string("LiarsoftTool ") + liarsoft::CurrentVersion).c_str());
    gtk_window_set_default_size(ui.window, 960, 680);
    g_signal_connect(ui.window, "close-request", G_CALLBACK(closeWindow), nullptr);
    auto* toolbar = ADW_TOOLBAR_VIEW(adw_toolbar_view_new());
    auto* header = ADW_HEADER_BAR(adw_header_bar_new());
    ui.preferencesButton = button(_("Preferences"), reinterpret_cast<GCallback>(+[](GtkButton*, gpointer) {
        if (!ui.busy) adw_dialog_present(ui.preferencesDialog, GTK_WIDGET(ui.window));
    }));
    ui.updateButton = button(_("Check for updates"), reinterpret_cast<GCallback>(+[](GtkButton*, gpointer) { checkUpdate(); }));
    adw_header_bar_pack_start(header, ui.updateButton);
    adw_header_bar_pack_end(header, ui.preferencesButton);
    adw_toolbar_view_add_top_bar(toolbar, GTK_WIDGET(header));
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(ui.window), GTK_WIDGET(toolbar));
    ui.content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    margins(ui.content, 18);
    adw_toolbar_view_set_content(toolbar, ui.content);
    auto* heading = label(_("Conversion tasks"));
    gtk_widget_add_css_class(heading, "title-2");
    gtk_box_append(GTK_BOX(ui.content), heading);
    auto* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(actions), button(_("Add files"), reinterpret_cast<GCallback>(+[](GtkButton*, gpointer) { choose(Choose::Files); })));
    gtk_box_append(GTK_BOX(actions), button(_("Add folder"), reinterpret_cast<GCallback>(+[](GtkButton*, gpointer) { choose(Choose::Folder); })));
    gtk_box_append(GTK_BOX(actions), button(_("Remove"), reinterpret_cast<GCallback>(+[](GtkButton*, gpointer) {
        if (ui.busy) return;
        auto* selected = gtk_list_box_get_selected_row(GTK_LIST_BOX(ui.list));
        for (auto it = ui.jobs.begin(); it != ui.jobs.end(); ++it) if (it->row == GTK_WIDGET(selected)) {
            gtk_list_box_remove(GTK_LIST_BOX(ui.list), it->row); ui.jobs.erase(it); break;
        }
        sensitive();
    })));
    gtk_box_append(GTK_BOX(actions), button(_("Clear"), reinterpret_cast<GCallback>(+[](GtkButton*, gpointer) { clearFiles(); })));
    auto* spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0); gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(actions), spacer);
    ui.convert = button(_("Convert all"), reinterpret_cast<GCallback>(+[](GtkButton*, gpointer) { convert(); }));
    gtk_widget_add_css_class(ui.convert, "suggested-action");
    gtk_box_append(GTK_BOX(actions), ui.convert);
    gtk_box_append(GTK_BOX(ui.content), actions);
    auto* scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    ui.list = gtk_list_box_new();
    gtk_widget_add_css_class(ui.list, "boxed-list");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), ui.list);
    gtk_box_append(GTK_BOX(ui.content), scroll);
    ui.progress = gtk_progress_bar_new();
    gtk_box_append(GTK_BOX(ui.content), ui.progress);
    ui.status = label(_("Ready"));
    gtk_box_append(GTK_BOX(ui.content), ui.status);
    auto* drop = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY);
    g_signal_connect(drop, "drop", reinterpret_cast<GCallback>(+[](GtkDropTarget*, const GValue* value, double, double, gpointer) -> gboolean {
        if (ui.busy) return FALSE;
        std::vector<std::string> paths;
        for (auto* item = gdk_file_list_get_files(static_cast<GdkFileList*>(g_value_get_boxed(value))); item; item = item->next) {
            auto* filename = g_file_get_path(G_FILE(item->data));
            if (filename) { paths.emplace_back(filename); g_free(filename); }
        }
        addFiles(paths); return TRUE;
    }), nullptr);
    gtk_widget_add_controller(ui.content, GTK_EVENT_CONTROLLER(drop));
    buildPreferences();
    auto* tasks = ui.content;
    g_object_ref(tasks);
    adw_toolbar_view_set_content(toolbar, nullptr);
    ui.content = adw_view_stack_new();
    adw_view_stack_add_titled(ADW_VIEW_STACK(ui.content), tasks, "tasks", _("Conversion tasks"));
    g_object_unref(tasks);
    ui.browser = liarsoft::gui::createResourceBrowser(ui.window, GTK_DROP_DOWN(ui.encoding), +[](const std::string& file) {
        addFiles({file});
        adw_view_stack_set_visible_child_name(ADW_VIEW_STACK(ui.content), "tasks");
    });
    adw_view_stack_add_titled(ADW_VIEW_STACK(ui.content), ui.browser, "resources", _("Resources"));
    ui.exeEditor = liarsoft::gui::createExeEditor(ui.window);
    ui.exePage = adw_view_stack_add_titled(ADW_VIEW_STACK(ui.content), ui.exeEditor, "exe", _("EXE editor"));
    adw_view_stack_page_set_visible(ui.exePage, ui.preferences.experimentalExe);
    auto* switcher = adw_view_switcher_new();
    adw_view_switcher_set_stack(ADW_VIEW_SWITCHER(switcher), ADW_VIEW_STACK(ui.content));
    adw_header_bar_set_title_widget(header, switcher);
    adw_toolbar_view_set_content(toolbar, ui.content);
    theme(); sensitive();
    gtk_window_present(ui.window);
}
}

int runGui(int argc, char* argv[]) {
#ifdef _WIN32
    g_setenv("GDK_BACKEND", "win32", TRUE);
#endif
    std::string settingsError;
    try { ui.preferences = liarsoft::gui::loadPreferences(); }
    catch (const std::exception& error) { settingsError = error.what(); }
    liarsoft::gui::initializeTranslations(liarsoft::gui::effectiveLanguage(ui.preferences.language), argv[0]);
    auto* app = adw_application_new("io.github.LiarsoftTool", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), nullptr);
    if (!settingsError.empty()) post([settingsError] { report(_("Could not read preferences"), settingsError); });
    const int result = g_application_run(G_APPLICATION(app), argc, argv);
    if (ui.worker.joinable()) ui.worker.join();
    if (ui.preferencesDialog) { g_object_unref(ui.preferencesDialog); ui.preferencesDialog = nullptr; }
    g_object_unref(app);
    ui.window = nullptr;
    return result;
}
