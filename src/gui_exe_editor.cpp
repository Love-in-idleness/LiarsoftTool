#include "gui_exe_editor.h"
#include "exe_editor.h"
#include <adwaita.h>
#include <algorithm>
#include <atomic>
#include <libintl.h>
#include <memory>
#include <thread>
#include <utility>
#define E_(s) gettext(s)
#define N_(s) s

namespace liarsoft::gui {
namespace {
struct Editor {
    GtkWindow *parent;
    GtkWidget *root, *controls, *summary, *source, *target, *texts, *start, *end, *save, *cancel, *status;
    std::vector<GtkWidget *> translations;
    std::filesystem::path file;
    std::optional<ExeDocument> document;
    std::atomic<uint64_t> generation{0};
    std::atomic<bool> alive{true}, writing{false};
    bool dirty = false, updating = false, rulesEdited = false;
    AdwAlertDialog *leaveDialog = nullptr;
};
using State = std::shared_ptr<Editor>;
[[maybe_unused]] constexpr const char *coreMessages[] = {
    N_("Font charset does not establish the Windows ANSI code page; use a matching system locale"),
    N_("No verified CreateFontA charset operands"),
    N_("No supported native line-break classifier"),
    N_("Signed executables cannot be edited"),
    N_("Text source encoding is uncertain; select it explicitly"),
    N_("Select the text source encoding"),
    N_("Text does not decode in the selected source encoding"),
    N_("Unverified or interior string reference"),
    N_("No verified text reference"),
    N_("Some font charset arguments could not be verified"),
    N_("Embedded text"),
    N_("Window title (APINI)"),
    N_("Scene caption"),
    N_("Window text"),
    N_("Control text"),
    N_("External RsInit configuration can override the embedded window title")};

State state(GtkWidget *widget) {
    return *static_cast<State *>(g_object_get_data(G_OBJECT(widget), "exe-editor"));
}
void post(std::function<void()> callback) {
    g_idle_add_full(
        G_PRIORITY_DEFAULT,
        [](gpointer data) -> gboolean {
            (*static_cast<std::function<void()> *>(data))();
            return G_SOURCE_REMOVE;
        },
        new std::function<void()>(std::move(callback)),
        [](gpointer data) { delete static_cast<std::function<void()> *>(data); });
}
void message(const State &e, const std::string &text) {
    if (e->alive)
        gtk_label_set_text(GTK_LABEL(e->status), E_(text.c_str()));
}
GtkWidget *label(const char *text) {
    auto *w = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(w), 0);
    gtk_label_set_wrap(GTK_LABEL(w), TRUE);
    gtk_label_set_selectable(GTK_LABEL(w), TRUE);
    return w;
}
GtkWidget *textView(bool editable = true) {
    auto *w = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(w), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(w), editable);
    gtk_widget_set_size_request(w, -1, 64);
    return w;
}
std::string text(GtkWidget *view) {
    GtkTextIter a, b;
    auto *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_text_buffer_get_bounds(buffer, &a, &b);
    auto *value = gtk_text_buffer_get_text(buffer, &a, &b, FALSE);
    std::string out(value);
    g_free(value);
    return out;
}
void setText(GtkWidget *view, const std::string &value) {
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), value.data(), int(value.size()));
}
void dirty(const State &e) {
    if (!e->updating && e->document)
        e->dirty = true;
}
void changed(GtkTextBuffer *buffer, gpointer pointer) {
    const auto e = *static_cast<State *>(pointer);
    if (!e->updating && e->document &&
        (buffer == gtk_text_view_get_buffer(GTK_TEXT_VIEW(e->start)) ||
         buffer == gtk_text_view_get_buffer(GTK_TEXT_VIEW(e->end))))
        e->rulesEdited = true;
    dirty(e);
}
void watch(const State &e, GtkWidget *view) {
    g_signal_connect_data(
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), "changed", G_CALLBACK(changed), new State(e),
        [](gpointer p, GClosure *) { delete static_cast<State *>(p); }, G_CONNECT_DEFAULT);
}
std::string selected(GtkWidget *dropdown, bool automatic = false) {
    const auto index = gtk_drop_down_get_selected(GTK_DROP_DOWN(dropdown));
    return automatic && index == 0 ? "auto" : exeEncodings().at(index - (automatic ? 1 : 0));
}
void select(GtkWidget *dropdown, const std::string &encoding, bool automatic = false) {
    const auto &names = exeEncodings();
    const auto it = std::find(names.begin(), names.end(), encoding);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown),
                               it == names.end() ? 0 : guint(it - names.begin() + (automatic ? 1 : 0)));
}
GtkWidget *dropdown(bool automatic = false) {
    auto *names = gtk_string_list_new(nullptr);
    if (automatic)
        gtk_string_list_append(names, E_("Infer text source encoding"));
    for (const auto &name : exeEncodings())
        gtk_string_list_append(names, name.c_str());
    return gtk_drop_down_new(G_LIST_MODEL(names), nullptr);
}
void update(const State &e) {
    if (!e->document)
        return;
    e->updating = true;
    const auto &doc = *e->document;
    while (auto *child = gtk_widget_get_first_child(e->texts))
        gtk_box_remove(GTK_BOX(e->texts), child);
    e->translations.clear();
    std::string summary = E_("Detected font charsets:");
    for (auto c : doc.charsets)
        summary += " " + std::to_string(c);
    summary += "\n";
    summary += E_("Text source encoding:");
    summary += " " + (doc.sourceEncoding.empty() ? E_("Unconfirmed") : doc.sourceEncoding);
    for (const auto &note : doc.notices)
        summary += "\n" + std::string(E_(note.c_str()));
    gtk_label_set_text(GTK_LABEL(e->summary), summary.c_str());
    select(e->target, doc.sourceEncoding);
    for (const auto &item : doc.texts) {
        auto *name =
            label((item.id + " — " + E_(item.role.c_str()) +
                   (item.capacity ? " (" + std::to_string(item.capacity - 1) + " " + E_("bytes") + ")" : "") +
                   " — " + (item.editable ? E_("Editable") : E_(item.reason.c_str())))
                      .c_str());
        gtk_box_append(GTK_BOX(e->texts), name);
        auto *original = label(item.text.c_str());
        gtk_widget_add_css_class(original, "dim-label");
        gtk_box_append(GTK_BOX(e->texts), original);
        auto *translated = textView(item.editable);
        setText(translated, item.text);
        watch(e, translated);
        gtk_box_append(GTK_BOX(e->texts), translated);
        e->translations.push_back(translated);
    }
    if (doc.texts.empty())
        gtk_box_append(GTK_BOX(e->texts), label(E_("No supported embedded text was found.")));
    setText(e->start, doc.rules.noLineStart);
    setText(e->end, doc.rules.noLineEnd);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(e->start), !doc.classifiers.empty());
    gtk_text_view_set_editable(GTK_TEXT_VIEW(e->end), !doc.classifiers.empty());
    gtk_widget_set_sensitive(e->save, doc.writable && !doc.sourceEncoding.empty());
    e->dirty = false;
    e->rulesEdited = false;
    e->updating = false;
}
void analyse(const State &e, const std::filesystem::path &file, const std::string &source) {
    const auto ticket = ++e->generation;
    e->file = file;
    e->document.reset();
    e->dirty = false;
    gtk_widget_set_sensitive(e->controls, FALSE);
    gtk_widget_set_sensitive(e->save, FALSE);
    gtk_widget_set_sensitive(e->cancel, TRUE);
    message(e, E_("Inspecting EXE…"));
    std::thread([e, ticket, file, source] {
        auto cancelled = [e, ticket] { return !e->alive || e->generation != ticket; };
        std::optional<ExeDocument> doc;
        std::string error;
        try {
            doc = inspectExeFile(file, source, cancelled);
        } catch (const std::exception &ex) {
            error = ex.what();
        }
        post([e, ticket, doc = std::move(doc), error = std::move(error)]() mutable {
            if (!e->alive || e->generation != ticket)
                return;
            gtk_widget_set_sensitive(e->controls, TRUE);
            gtk_widget_set_sensitive(e->cancel, FALSE);
            if (!error.empty()) {
                message(e, error);
                return;
            }
            e->document = std::move(doc);
            update(e);
            message(e, E_("Inspection complete. Changes are written only by Save as."));
        });
    }).detach();
}
ExeChanges changes(const State &e) {
    ExeChanges out;
    const auto &doc = *e->document;
    const auto target = selected(e->target);
    if (target != doc.sourceEncoding ||
        ((!doc.fontOperands.empty() || !doc.fontBridges.empty()) && doc.fontsComplete))
        out.encoding = target;
    for (size_t i = 0; i < doc.texts.size(); ++i) {
        const auto value = text(e->translations[i]);
        if (value != doc.texts[i].text)
            out.texts[doc.texts[i].id] = value;
    }
    const auto start = text(e->start), end = text(e->end);
    if (start != doc.rules.noLineStart || end != doc.rules.noLineEnd)
        out.rules = ExeRules{start, end};
    return out;
}
void chooseSave(const State &e, std::function<void()> after = {});
void leave(const State &e, std::function<void()> continuation) {
    if (e->writing) {
        message(e, E_("Please wait for the EXE save to finish."));
        return;
    }
    if (!e->dirty) {
        ++e->generation;
        continuation();
        return;
    }
    if (e->leaveDialog)
        return;
    auto *dialog = ADW_ALERT_DIALOG(
        adw_alert_dialog_new(E_("Save EXE changes?"), E_("The source EXE has not been modified.")));
    e->leaveDialog = dialog;
    adw_alert_dialog_add_responses(dialog, "cancel", E_("Cancel"), "discard", E_("Discard"), "save",
                                   E_("Save as…"), nullptr);
    adw_alert_dialog_set_default_response(dialog, "cancel");
    adw_alert_dialog_set_close_response(dialog, "cancel");
    struct Pending {
        State e;
        std::function<void()> next;
    };
    adw_alert_dialog_choose(
        dialog, GTK_WIDGET(e->parent), nullptr,
        [](GObject *object, GAsyncResult *result, gpointer data) {
            std::unique_ptr<Pending> p(static_cast<Pending *>(data));
            const std::string response = adw_alert_dialog_choose_finish(ADW_ALERT_DIALOG(object), result);
            p->e->leaveDialog = nullptr;
            if (!p->e->alive)
                return;
            if (response == "discard") {
                p->e->dirty = false;
                ++p->e->generation;
                p->next();
            } else if (response == "save")
                chooseSave(p->e, std::move(p->next));
        },
        new Pending{e, std::move(continuation)});
}
void save(const State &e, const std::filesystem::path &output, std::function<void()> after = {}) {
    if (!e->document || e->writing)
        return;
    const auto ticket = ++e->generation;
    const auto doc = *e->document;
    const auto request = changes(e);
    const auto source = e->file;
    e->writing = true;
    gtk_widget_set_sensitive(e->controls, FALSE);
    gtk_widget_set_sensitive(e->save, FALSE);
    gtk_widget_set_sensitive(e->cancel, TRUE);
    message(e, E_("Saving edited EXE…"));
    std::thread([e, ticket, doc, request, source, output, after = std::move(after)] {
        std::string error;
        try {
            saveEditedExe(source, output, doc, request,
                          [e, ticket] { return !e->alive || e->generation != ticket; });
        } catch (const std::exception &ex) {
            error = ex.what();
        }
        e->writing = false;
        post([e, ticket, error, after] {
            if (!e->alive)
                return;
            gtk_widget_set_sensitive(e->controls, TRUE);
            gtk_widget_set_sensitive(e->save, e->document && e->document->writable);
            gtk_widget_set_sensitive(e->cancel, FALSE);
            if (e->generation != ticket) {
                message(e, error.empty() ? E_("Save completed before cancellation.")
                                         : E_("EXE operation cancelled."));
                return;
            }
            if (!error.empty()) {
                message(e, error);
                return;
            }
            e->dirty = false;
            message(e, E_("Edited EXE saved. The source file is unchanged."));
            if (after)
                after();
        });
    }).detach();
}
void chooseSave(const State &e, std::function<void()> after) {
    if (!e->document)
        return;
    auto *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, E_("Save edited EXE"));
    const auto name = e->file.stem().u8string() + ".edited.exe";
    gtk_file_dialog_set_initial_name(dialog, name.c_str());
    struct Pending {
        State e;
        std::function<void()> next;
    };
    gtk_file_dialog_save(
        dialog, e->parent, nullptr,
        [](GObject *object, GAsyncResult *result, gpointer data) {
            std::unique_ptr<Pending> p(static_cast<Pending *>(data));
            GError *error = nullptr;
            auto *file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(object), result, &error);
            if (file) {
                auto *name = g_file_get_path(file);
                if (name && p->e->alive)
                    save(p->e, std::filesystem::u8path(name), std::move(p->next));
                g_free(name);
                g_object_unref(file);
            }
            if (error)
                g_error_free(error);
        },
        new Pending{e, std::move(after)});
    g_object_unref(dialog);
}
void choose(const State &e, bool rules) {
    if (e->writing)
        return;
    auto *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, rules ? E_("Load line-break JSON") : E_("Open EXE"));
    struct Pending {
        State e;
        bool rules;
    };
    gtk_file_dialog_open(
        dialog, e->parent, nullptr,
        [](GObject *object, GAsyncResult *result, gpointer data) {
            std::unique_ptr<Pending> p(static_cast<Pending *>(data));
            GError *error = nullptr;
            auto *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(object), result, &error);
            if (file) {
                auto *name = g_file_get_path(file);
                if (name && p->e->alive) {
                    const auto path = std::filesystem::u8path(name);
                    if (p->rules) {
                        try {
                            auto value = readExeRules(path);
                            setText(p->e->start, value.noLineStart);
                            setText(p->e->end, value.noLineEnd);
                        } catch (const std::exception &ex) {
                            message(p->e, ex.what());
                        }
                    } else
                        leave(p->e, [e = p->e, path] { analyse(e, path, selected(e->source, true)); });
                }
                g_free(name);
                g_object_unref(file);
            }
            if (error)
                g_error_free(error);
        },
        new Pending{e, rules});
    g_object_unref(dialog);
}
GtkWidget *button(const State &e, const char *name, void (*callback)(const State &)) {
    auto *widget = gtk_button_new_with_label(name);
    struct Click {
        State e;
        void (*function)(const State &);
    };
    g_signal_connect_data(
        widget, "clicked", G_CALLBACK(+[](GtkButton *, gpointer data) {
            auto *p = static_cast<Click *>(data);
            p->function(p->e);
        }),
        new Click{e, callback}, [](gpointer p, GClosure *) { delete static_cast<Click *>(p); },
        G_CONNECT_DEFAULT);
    return widget;
}
void heading(GtkWidget *box, const char *value) {
    auto *w = label(value);
    gtk_widget_add_css_class(w, "title-3");
    gtk_box_append(GTK_BOX(box), w);
}
} // namespace
GtkWidget *createExeEditor(GtkWindow *parent) {
    auto e = std::make_shared<Editor>();
    e->parent = parent;
    e->root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    for (auto set : {gtk_widget_set_margin_start, gtk_widget_set_margin_end, gtk_widget_set_margin_top,
                     gtk_widget_set_margin_bottom})
        set(e->root, 18);
    g_object_set_data_full(G_OBJECT(e->root), "exe-editor", new State(e), [](gpointer p) {
        auto *ref = static_cast<State *>(p);
        (*ref)->alive = false;
        ++(*ref)->generation;
        delete ref;
    });
    auto *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(e->root), actions);
    gtk_box_append(GTK_BOX(actions), button(e, E_("Open EXE"), +[](const State &e) { choose(e, false); }));
    e->save = button(e, E_("Save as…"), +[](const State &e) { chooseSave(e); });
    gtk_box_append(GTK_BOX(actions), e->save);
    gtk_widget_set_sensitive(e->save, FALSE);
    e->cancel = button(
        e, E_("Cancel"), +[](const State &e) {
            ++e->generation;
            gtk_widget_set_sensitive(e->controls, TRUE);
            gtk_widget_set_sensitive(e->cancel, FALSE);
            message(e, E_("EXE operation cancelled."));
        });
    gtk_box_append(GTK_BOX(actions), e->cancel);
    gtk_widget_set_sensitive(e->cancel, FALSE);
    auto *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(e->root), scroll);
    e->controls = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), e->controls);
    heading(e->controls, E_("Encoding"));
    gtk_box_append(GTK_BOX(e->controls), label(E_("Font charset and text encoding are separate. Matching "
                                                  "Windows locale and fonts are still required.")));
    e->summary = label(E_("Open a supported PE32/x86 RScript EXE to inspect it."));
    gtk_box_append(GTK_BOX(e->controls), e->summary);
    gtk_box_append(GTK_BOX(e->controls), label(E_("Text source encoding")));
    e->source = dropdown(true);
    gtk_box_append(GTK_BOX(e->controls), e->source);
    gtk_box_append(GTK_BOX(e->controls),
                   button(
                       e, E_("Reinspect with source encoding"), +[](const State &e) {
                           if (!e->file.empty())
                               leave(e, [e] { analyse(e, e->file, selected(e->source, true)); });
                       }));
    gtk_box_append(GTK_BOX(e->controls), label(E_("Target encoding")));
    e->target = dropdown();
    gtk_box_append(GTK_BOX(e->controls), e->target);
    g_signal_connect_data(
        e->target, "notify::selected", G_CALLBACK(+[](GObject *, GParamSpec *, gpointer p) {
            const auto e = *static_cast<State *>(p);
            if (!e->updating && e->document && !e->rulesEdited) {
                const auto encoding = selected(e->target);
                const auto rules =
                    encoding == e->document->sourceEncoding ? e->document->rules : defaultExeRules(encoding);
                e->updating = true;
                setText(e->start, rules.noLineStart);
                setText(e->end, rules.noLineEnd);
                e->updating = false;
            }
            dirty(e);
        }),
        new State(e), [](gpointer p, GClosure *) { delete static_cast<State *>(p); }, G_CONNECT_DEFAULT);
    gtk_box_append(GTK_BOX(e->controls),
                   button(
                       e, E_("Use target encoding default configuration"), +[](const State &e) {
                           try {
                               if (!e->document)
                                   return;
                               const auto value = defaultExeConfiguration(*e->document, selected(e->target));
                               if (value.rules) {
                                   setText(e->start, value.rules->noLineStart);
                                   setText(e->end, value.rules->noLineEnd);
                               }
                               for (size_t n = 0; n < e->document->texts.size(); ++n) {
                                   const auto &item = e->document->texts[n];
                                   const auto translation = value.texts.find(item.id);
                                   if (translation != value.texts.end() &&
                                       text(e->translations[n]) == item.text)
                                       setText(e->translations[n], translation->second);
                               }
                           } catch (const std::exception &ex) {
                               message(e, ex.what());
                           }
                       }));
    heading(e->controls, E_("Embedded text"));
    e->texts = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_append(GTK_BOX(e->controls), e->texts);
    heading(e->controls, E_("Line-break rules"));
    gtk_box_append(GTK_BOX(e->controls), button(
                                             e, E_("Load line-break JSON"), +[](const State &e) {
                                                 if (e->document)
                                                     choose(e, true);
                                             }));
    gtk_box_append(GTK_BOX(e->controls), label(E_("Forbidden at line start")));
    e->start = textView();
    watch(e, e->start);
    gtk_box_append(GTK_BOX(e->controls), e->start);
    gtk_box_append(GTK_BOX(e->controls), label(E_("Forbidden at line end")));
    e->end = textView();
    watch(e, e->end);
    gtk_box_append(GTK_BOX(e->controls), e->end);
    e->status = label(E_("Experimental: writes a separate EXE; unsupported items remain read-only."));
    gtk_box_append(GTK_BOX(e->root), e->status);
    return e->root;
}
bool exeEditorWriting(GtkWidget *editor) { return editor && state(editor)->writing; }
bool exeEditorDirty(GtkWidget *editor) { return editor && state(editor)->dirty; }
void leaveExeEditor(GtkWidget *editor, std::function<void()> continuation) {
    if (editor)
        leave(state(editor), std::move(continuation));
    else
        continuation();
}
} // namespace liarsoft::gui
