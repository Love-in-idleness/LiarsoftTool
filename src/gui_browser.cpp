#include "gui_browser.h"
#include "gui_common.h"
#include "resource_preview.h"
#include "encoding.h"
#include "fileio.h"
#include <adwaita.h>
#include <libintl.h>
#include <algorithm>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace liarsoft::gui {
namespace {
namespace fs = std::filesystem;
#define B_(text) gettext(text)
constexpr uint64_t HexPageSize = 4096;
struct Node {
    std::string name;
    fs::path file;
    std::optional<ResourceSource> source;
    std::optional<ArchiveEntryInfo> metadata;
    bool directory = false, embedded = false;
};
struct Browser {
    GtkWidget *root, *list, *address, *info, *status, *stack, *text, *hex, *image, *zoom;
    GtkWidget *back, *previous, *next, *page, *exportButton, *queueButton, *encoding, *mediaControls, *volume;
    GtkDropDown* baseEncoding;
    GtkStringList* names;
    GtkSingleSelection* selection;
    gulong selectionSignal;
    GtkApplication* app;
    GWeakRef window;
    GCancellable* pending = nullptr;
    std::vector<uint32_t> pixels;
    cairo_surface_t* imageSurface = nullptr;
    GtkMediaStream* media = nullptr;
    std::vector<Node> nodes, history;
    std::optional<Node> selected;
    uint64_t generation = 0, hexGeneration = 0, hexOffset = 0;
    unsigned writes = 0;
    void (*queueFile)(const std::string&);
    ~Browser() {
        if (pending) g_cancellable_cancel(pending);
        g_clear_object(&pending); g_clear_object(&media);
        if (imageSurface) cairo_surface_destroy(imageSurface);
        g_object_unref(baseEncoding); g_weak_ref_clear(&window);
        g_clear_object(&app);
    }
};
Browser* browser(GtkWidget* root) { return static_cast<Browser*>(g_object_get_data(G_OBJECT(root), "resource-browser")); }
std::string message(const std::exception& error) {
    return g_utf8_validate(error.what(), -1, nullptr) ? error.what() : nativeTextToUtf8(error.what());
}
GtkWidget* browserLabel(const char* text) {
    auto* widget = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(widget), 0);
    gtk_label_set_wrap(GTK_LABEL(widget), TRUE);
    return widget;
}
GtkWidget* browserButton(const char* text, GCallback callback, Browser* b) {
    auto* widget = gtk_button_new_with_label(text);
    g_signal_connect(widget, "clicked", callback, b);
    return widget;
}
void setText(GtkWidget* widget, const std::string& text) {
    if (text.size() > G_MAXINT) throw std::runtime_error("Text exceeds the viewer size; use the hex view");
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget)), text.data(), static_cast<int>(text.size()));
}
std::string encoding(Browser* b) {
    const auto choice = gtk_drop_down_get_selected(GTK_DROP_DOWN(b->encoding));
    const auto index = choice ? choice - 1 : gtk_drop_down_get_selected(b->baseEncoding);
    return index ? supportedEncodings().at(index - 1) : "auto";
}
std::string hexPage(const ResourceSource& source, uint64_t offset) {
    const auto bytes = source.read(offset, static_cast<size_t>(std::min(HexPageSize, source.size() - offset)));
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (size_t row = 0; row < bytes.size(); row += 16) {
        output << std::setw(16) << offset + row << "  ";
        for (size_t col = 0; col < 16; ++col)
            if (row + col < bytes.size()) output << std::setw(2) << unsigned(bytes[row + col]) << ' ';
            else output << "   ";
        output << " ";
        for (size_t col = row; col < std::min(row + 16, bytes.size()); ++col)
            output << (bytes[col] >= 32 && bytes[col] < 127 ? char(bytes[col]) : '.');
        output << '\n';
    }
    return output.str();
}
void clearPreview(Browser* b) {
    if (b->media) gtk_media_stream_pause(b->media);
    gtk_media_controls_set_media_stream(GTK_MEDIA_CONTROLS(b->mediaControls), nullptr);
    g_clear_object(&b->media);
    if (b->imageSurface) { cairo_surface_destroy(b->imageSurface); b->imageSurface = nullptr; }
    b->pixels.clear();
    setText(b->text, ""); setText(b->hex, "");
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(b->image), 0);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(b->image), 0);
    gtk_widget_queue_draw(b->image);
}
void zoomImage(Browser* b) {
    if (!b->imageSurface) return;
    const auto zoom = gtk_range_get_value(GTK_RANGE(b->zoom));
    gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(b->image), cairo_image_surface_get_width(b->imageSurface) * zoom);
    gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(b->image), cairo_image_surface_get_height(b->imageSurface) * zoom);
    gtk_widget_queue_draw(b->image);
}
enum class RequestKind { Open, Preview, Hex, Export };
struct Request {
    RequestKind kind;
    Node node;
    std::string encoding;
    uint64_t generation, hexGeneration, offset = 0;
    size_t depth = 0;
    fs::path destination;
};
struct Result {
    RequestKind kind;
    Node node;
    std::vector<Node> nodes;
    ResourcePreview preview;
    std::string hex, error, details;
};
void request(Browser* b, RequestKind kind, Node node, size_t depth = 0, const fs::path& destination = {});
void showPageButtons(Browser* b) {
    const auto size = b->selected && b->selected->source ? b->selected->source->size() : 0;
    gtk_widget_set_sensitive(b->previous, size && b->hexOffset > 0);
    gtk_widget_set_sensitive(b->next, size > b->hexOffset && size - b->hexOffset > HexPageSize);
    gtk_label_set_text(GTK_LABEL(b->page), (std::to_string(b->hexOffset / HexPageSize + 1) + " / " +
        std::to_string(std::max<uint64_t>(1, size / HexPageSize + (size % HexPageSize != 0)))).c_str());
}
void applyResult(Browser* b, Result& result, const Request& input) {
    gtk_label_set_text(GTK_LABEL(b->status), result.error.c_str());
    if (result.kind == RequestKind::Open && result.error.empty()) {
        b->selected.reset(); b->history.resize(input.depth); b->history.push_back(result.node);
        b->nodes = std::move(result.nodes);
        std::vector<const char*> names;
        for (const auto& node : b->nodes) names.push_back(node.name.c_str());
        names.push_back(nullptr);
        g_signal_handler_block(b->selection, b->selectionSignal);
        gtk_single_selection_set_selected(b->selection, GTK_INVALID_LIST_POSITION);
        gtk_string_list_splice(b->names, 0, g_list_model_get_n_items(G_LIST_MODEL(b->names)), names.data());
        g_signal_handler_unblock(b->selection, b->selectionSignal);
        std::string address;
        for (const auto& node : b->history) address += (address.empty() ? "" : " / ") + node.name;
        gtk_label_set_text(GTK_LABEL(b->address), address.c_str());
        gtk_label_set_text(GTK_LABEL(b->info), result.details.c_str());
        gtk_widget_set_sensitive(b->back, b->history.size() > 1);
    } else if (result.kind != RequestKind::Hex) {
        b->selected = result.node;
        gtk_label_set_text(GTK_LABEL(b->info), result.details.c_str());
        auto& preview = result.preview;
        if (result.error.empty() && preview.kind == PreviewKind::Image) {
            b->pixels.resize(preview.image.pixels.size() / 4);
            for (size_t i = 0; i < b->pixels.size(); ++i) {
                const auto* rgba = preview.image.pixels.data() + 4 * i;
                const uint32_t alpha = rgba[3];
                b->pixels[i] = (alpha << 24) | (((rgba[0] * alpha + 127) / 255) << 16) |
                    (((rgba[1] * alpha + 127) / 255) << 8) | ((rgba[2] * alpha + 127) / 255);
            }
            b->imageSurface = cairo_image_surface_create_for_data(reinterpret_cast<unsigned char*>(b->pixels.data()),
                CAIRO_FORMAT_ARGB32, preview.image.width, preview.image.height, preview.image.width * 4);
            if (cairo_surface_status(b->imageSurface) != CAIRO_STATUS_SUCCESS)
                throw std::runtime_error(cairo_status_to_string(cairo_surface_status(b->imageSurface)));
            zoomImage(b); gtk_stack_set_visible_child_name(GTK_STACK(b->stack), "image");
        } else if (result.error.empty() && preview.kind == PreviewKind::Audio) {
            auto* bytes = new std::vector<uint8_t>(std::move(preview.audio));
            auto* storage = g_bytes_new_with_free_func(bytes->data(), bytes->size(),
                +[](gpointer data) { delete static_cast<std::vector<uint8_t>*>(data); }, bytes);
            auto* stream = g_memory_input_stream_new_from_bytes(storage);
            b->media = gtk_media_file_new_for_input_stream(stream);
            g_object_unref(stream); g_bytes_unref(storage);
            gtk_media_controls_set_media_stream(GTK_MEDIA_CONTROLS(b->mediaControls), b->media);
            gtk_media_stream_set_volume(b->media, gtk_range_get_value(GTK_RANGE(b->volume)));
            g_signal_connect_object(b->media, "notify::error", reinterpret_cast<GCallback>(+[](GtkMediaStream* media, GParamSpec*, GtkWidget* root) {
                if (const auto* error = gtk_media_stream_get_error(media))
                    gtk_label_set_text(GTK_LABEL(browser(root)->status), error->message);
            }), b->root, G_CONNECT_DEFAULT);
            gtk_stack_set_visible_child_name(GTK_STACK(b->stack), "audio");
        } else {
            setText(b->text, !result.error.empty() ? std::string(B_("Preview failed; raw bytes remain available.")) + "\n" + result.error :
                preview.kind == PreviewKind::Text ? preview.text : B_("No decoder for this entry. Use the hex view, or double-click to open an archive."));
            gtk_stack_set_visible_child_name(GTK_STACK(b->stack), "text");
        }
        if (!preview.warnings.empty()) {
            std::string warnings;
            for (const auto& warning : preview.warnings) warnings += warning + "\n";
            gtk_label_set_text(GTK_LABEL(b->status), warnings.c_str());
        }
    }
    if (input.hexGeneration == b->hexGeneration) setText(b->hex, result.hex);
    const bool selected = b->selected && b->selected->source;
    gtk_widget_set_sensitive(b->exportButton, selected && !b->writes);
    gtk_widget_set_sensitive(b->queueButton, selected && !b->selected->embedded);
    showPageButtons(b);
}
void request(Browser* b, RequestKind kind, Node node, size_t depth, const fs::path& destination) {
    if (kind != RequestKind::Hex && kind != RequestKind::Export) {
        if (b->pending) g_cancellable_cancel(b->pending);
        g_clear_object(&b->pending); b->pending = g_cancellable_new();
        ++b->generation; ++b->hexGeneration; b->hexOffset = 0;
        clearPreview(b); b->selected.reset();
        gtk_widget_set_sensitive(b->exportButton, FALSE); gtk_widget_set_sensitive(b->queueButton, FALSE);
        gtk_widget_set_sensitive(b->list, kind != RequestKind::Open);
    } else if (kind == RequestKind::Hex) ++b->hexGeneration;
    else { ++b->writes; gtk_widget_set_sensitive(b->exportButton, FALSE); }
    auto* input = new Request{kind, std::move(node), encoding(b), b->generation, b->hexGeneration, b->hexOffset, depth, destination};
    if (b->app) g_application_hold(G_APPLICATION(b->app));
    auto* task = g_task_new(b->root, kind == RequestKind::Export ? nullptr : b->pending,
        +[](GObject* source, GAsyncResult* async, gpointer) {
            auto* b = browser(GTK_WIDGET(source));
            auto* task = G_TASK(async);
            auto* input = static_cast<Request*>(g_task_get_task_data(task));
            GError* error = nullptr;
            std::unique_ptr<Result> result(static_cast<Result*>(g_task_propagate_pointer(task, &error)));
            if (input->kind == RequestKind::Export) --b->writes;
            const bool current = input->generation == b->generation &&
                (input->kind != RequestKind::Hex || input->hexGeneration == b->hexGeneration);
            if (gtk_widget_get_root(b->root) && (current || input->kind == RequestKind::Export)) {
                if (input->kind == RequestKind::Export) {
                    gtk_label_set_text(GTK_LABEL(b->status), error ? error->message : result->error.empty() ? B_("Export complete") : result->error.c_str());
                    gtk_widget_set_sensitive(b->exportButton, b->selected && b->selected->source && !b->writes);
                } else if (current && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                    gtk_widget_set_sensitive(b->list, TRUE);
                    if (error) gtk_label_set_text(GTK_LABEL(b->status), error->message);
                    else try { applyResult(b, *result, *input); }
                         catch (const std::exception& failure) { gtk_label_set_text(GTK_LABEL(b->status), message(failure).c_str()); }
                }
            }
            g_clear_error(&error);
            if (b->app) g_application_release(G_APPLICATION(b->app));
        }, nullptr);
    g_task_set_task_data(task, input, +[](gpointer data) { delete static_cast<Request*>(data); });
    g_task_run_in_thread(task, +[](GTask* task, gpointer, gpointer data, GCancellable* cancel) {
        auto* input = static_cast<Request*>(data);
        auto result = std::make_unique<Result>(); result->kind = input->kind; result->node = input->node;
        try {
            if (cancel && g_cancellable_is_cancelled(cancel)) { g_task_return_error_if_cancelled(task); return; }
            auto& node = result->node;
            if (node.directory) {
                for (const auto& item : fs::directory_iterator(node.file)) {
                    if (cancel && g_cancellable_is_cancelled(cancel)) { g_task_return_error_if_cancelled(task); return; }
                    if (item.is_directory() || item.is_regular_file())
                        result->nodes.push_back({item.path().filename().u8string(), item.path(), {}, {}, item.is_directory(), false});
                }
                std::sort(result->nodes.begin(), result->nodes.end(), [](const Node& a, const Node& b) {
                    return a.directory != b.directory ? a.directory > b.directory : a.name < b.name;
                });
                result->kind = RequestKind::Open;
                result->details = std::to_string(result->nodes.size()) + " " + B_("entries");
            } else {
                if (!node.source) node.source.emplace(node.file);
                auto& source = *node.source;
                if (input->kind == RequestKind::Export) {
                    std::error_code error;
                    if (fs::weakly_canonical(node.file) == fs::weakly_canonical(input->destination) || fs::equivalent(node.file, input->destination, error))
                        throw std::runtime_error(B_("Export cannot overwrite its source file."));
                    writeFileIfChanged(nativePathFromUtf8(input->destination.u8string()), source.readAll());
                    fs::last_write_time(input->destination, fs::last_write_time(node.file));
                } else {
                    result->details = node.name + " · " + std::to_string(source.size()) + " " + B_("bytes");
                    if (node.metadata) result->details += " · x=" + std::to_string(node.metadata->x) +
                        " y=" + std::to_string(node.metadata->y) + " flag=" + std::to_string(node.metadata->flag);
                    const auto header = source.read(0, static_cast<size_t>(std::min<uint64_t>(4, source.size())));
                    if (input->kind == RequestKind::Open && header.size() == 4 && header[0] == 'L' && (header[1] == 'B' || header[1] == 'G') && header[2] == 1 && header[3] == 0) {
                        const auto archiveEncoding = input->encoding == "auto" ? "CP932" : input->encoding;
                        const auto index = readArchiveIndex(source, archiveEncoding);
                        for (const auto& entry : index.entries)
                            result->nodes.push_back({entry.name, node.file, source.subrange(entry.offset, entry.size),
                                index.format == "LWG" ? std::optional<ArchiveEntryInfo>(entry) : std::nullopt, false, true});
                        result->details = index.format + " · " + std::to_string(index.entries.size()) + " " + B_("entries");
                        result->details += " · " + archiveEncoding;
                        if (index.format == "LWG") result->details += " · " + std::to_string(index.width) + " × " + std::to_string(index.height);
                    } else {
                        result->hex = hexPage(source, input->offset);
                        if (input->kind != RequestKind::Hex) {
                            result->kind = RequestKind::Preview;
                            result->preview = previewResource(source, node.name, input->encoding);
                            const auto& preview = result->preview;
                            result->details += " · " + (preview.format.empty() ? B_("Unknown type") : preview.format);
                            if (preview.kind == PreviewKind::Text) result->details += " · " +
                                preview.encoding + (preview.format == "GSC" ? " → UTF-8" : "");
                            if (preview.kind == PreviewKind::Image) result->details += " · " +
                                std::to_string(preview.image.width) + " × " + std::to_string(preview.image.height);
                        }
                    }
                }
            }
        } catch (const std::exception& error) {
            result->error = message(error);
            if (result->kind == RequestKind::Open) {
                result->kind = RequestKind::Preview;
                if (result->node.source) try { result->hex = hexPage(*result->node.source, 0); } catch (...) { }
            }
        }
        g_task_return_pointer(task, result.release(), +[](gpointer data) { delete static_cast<Result*>(data); });
    });
    g_object_unref(task);
}
void openPath(Browser* b, const fs::path& file) {
    try {
        const bool directory = fs::is_directory(file);
        g_signal_handler_block(b->selection, b->selectionSignal);
        gtk_single_selection_set_selected(b->selection, GTK_INVALID_LIST_POSITION);
        gtk_string_list_splice(b->names, 0, g_list_model_get_n_items(G_LIST_MODEL(b->names)), nullptr);
        g_signal_handler_unblock(b->selection, b->selectionSignal);
        b->nodes.clear(); b->history.clear();
        gtk_widget_set_sensitive(b->back, FALSE);
        gtk_label_set_text(GTK_LABEL(b->address), file.u8string().c_str());
        request(b, RequestKind::Open, {file.filename().u8string(), file, {}, {}, directory, false});
    }
    catch (const std::exception& error) { gtk_label_set_text(GTK_LABEL(b->status), message(error).c_str()); }
}
enum class DialogKind { File, Folder, Export };
void chooseResource(Browser* b, DialogKind kind) {
    if (kind == DialogKind::Export && (!b->selected || !b->selected->source || b->writes)) return;
    auto* dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, kind == DialogKind::Export ? B_("Export original bytes") : B_("Open resource"));
    struct Context { GtkWidget* root; DialogKind kind; std::optional<Node> node; };
    auto* context = new Context{GTK_WIDGET(g_object_ref(b->root)), kind, b->selected};
    if (kind == DialogKind::Export) gtk_file_dialog_set_initial_name(dialog, fs::u8path(b->selected->name).filename().u8string().c_str());
    auto callback = +[](GObject* object, GAsyncResult* result, gpointer data) {
        std::unique_ptr<Context> context(static_cast<Context*>(data));
        auto* b = browser(context->root);
        GError* error = nullptr;
        GFile* file = context->kind == DialogKind::Folder ? gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(object), result, &error) :
            context->kind == DialogKind::Export ? gtk_file_dialog_save_finish(GTK_FILE_DIALOG(object), result, &error) :
            gtk_file_dialog_open_finish(GTK_FILE_DIALOG(object), result, &error);
        if (gtk_widget_get_root(b->root)) {
            if (file) {
                auto* path = g_file_get_path(file);
                if (path) {
                    if (context->kind == DialogKind::Export) request(b, RequestKind::Export, *context->node, 0, fs::u8path(path));
                    else openPath(b, fs::u8path(path));
                    g_free(path);
                }
            } else if (!g_error_matches(error, GTK_DIALOG_ERROR, GTK_DIALOG_ERROR_DISMISSED) && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                gtk_label_set_text(GTK_LABEL(b->status), error ? error->message : B_("Operation failed"));
        }
        g_clear_object(&file); g_clear_error(&error); g_object_unref(context->root); g_object_unref(object);
    };
    auto* parent = GTK_WINDOW(g_weak_ref_get(&b->window));
    if (kind == DialogKind::File) gtk_file_dialog_open(dialog, parent, nullptr, callback, context);
    else if (kind == DialogKind::Folder) gtk_file_dialog_select_folder(dialog, parent, nullptr, callback, context);
    else gtk_file_dialog_save(dialog, parent, nullptr, callback, context);
    if (parent) g_object_unref(parent);
}
GtkWidget* textPage(GtkWidget** text) {
    *text = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(*text), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(*text), TRUE);
    auto* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), *text);
    return scroll;
}
}

GtkWidget* createResourceBrowser(GtkWindow* parent, GtkDropDown* resourceEncoding,
                                void (*queueFile)(const std::string&)) {
    auto* b = new Browser{};
    b->baseEncoding = GTK_DROP_DOWN(g_object_ref(resourceEncoding)); b->queueFile = queueFile;
    b->app = gtk_window_get_application(parent);
    if (b->app) g_object_ref(b->app);
    g_weak_ref_init(&b->window, parent);
    b->root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(b->root, 12); gtk_widget_set_margin_end(b->root, 12);
    gtk_widget_set_margin_top(b->root, 12); gtk_widget_set_margin_bottom(b->root, 12);
    g_object_set_data_full(G_OBJECT(b->root), "resource-browser", b, +[](gpointer data) { delete static_cast<Browser*>(data); });
    auto* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(actions), browserButton(B_("Open file"), reinterpret_cast<GCallback>(+[](GtkButton*, Browser* b) { chooseResource(b, DialogKind::File); }), b));
    gtk_box_append(GTK_BOX(actions), browserButton(B_("Open folder"), reinterpret_cast<GCallback>(+[](GtkButton*, Browser* b) { chooseResource(b, DialogKind::Folder); }), b));
    b->back = browserButton(B_("Back"), reinterpret_cast<GCallback>(+[](GtkButton*, Browser* b) {
        if (b->history.size() > 1) request(b, RequestKind::Open, b->history[b->history.size() - 2], b->history.size() - 2);
    }), b);
    gtk_widget_set_sensitive(b->back, FALSE); gtk_box_append(GTK_BOX(actions), b->back);
    auto* choices = gtk_string_list_new(nullptr);
    gtk_string_list_append(choices, B_("Resource setting"));
    gtk_string_list_append(choices, B_("Auto (GSC input)"));
    for (const auto& encoding : supportedEncodings()) gtk_string_list_append(choices, encoding.c_str());
    b->encoding = gtk_drop_down_new(G_LIST_MODEL(choices), nullptr);
    gtk_widget_set_tooltip_text(b->encoding, B_("Text and archive filename encoding"));
    gtk_box_append(GTK_BOX(actions), b->encoding); gtk_box_append(GTK_BOX(b->root), actions);
    b->address = browserLabel(B_("Open a folder or archive. Browsing never unpacks or changes the input."));
    gtk_box_append(GTK_BOX(b->root), b->address);
    auto* split = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL); gtk_widget_set_vexpand(split, TRUE);
    gtk_paned_set_position(GTK_PANED(split), 260);
    b->names = gtk_string_list_new(nullptr);
    b->selection = gtk_single_selection_new(G_LIST_MODEL(b->names));
    gtk_single_selection_set_autoselect(b->selection, FALSE);
    gtk_single_selection_set_can_unselect(b->selection, TRUE);
    auto* factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", reinterpret_cast<GCallback>(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
        auto* row = browserLabel("");
        gtk_label_set_ellipsize(GTK_LABEL(row), PANGO_ELLIPSIZE_MIDDLE); gtk_label_set_wrap(GTK_LABEL(row), FALSE);
        gtk_widget_set_margin_start(row, 10); gtk_widget_set_margin_end(row, 10);
        gtk_widget_set_margin_top(row, 8); gtk_widget_set_margin_bottom(row, 8);
        gtk_list_item_set_child(item, row);
    }), nullptr);
    g_signal_connect(factory, "bind", reinterpret_cast<GCallback>(+[](GtkSignalListItemFactory*, GtkListItem* item, gpointer) {
        auto* row = gtk_list_item_get_child(item);
        const auto* name = gtk_string_object_get_string(GTK_STRING_OBJECT(gtk_list_item_get_item(item)));
        gtk_label_set_text(GTK_LABEL(row), name); gtk_widget_set_tooltip_text(row, name);
    }), nullptr);
    b->list = gtk_list_view_new(GTK_SELECTION_MODEL(b->selection), GTK_LIST_ITEM_FACTORY(factory));
    gtk_list_view_set_single_click_activate(GTK_LIST_VIEW(b->list), FALSE);
    auto* scroll = gtk_scrolled_window_new(); gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), b->list);
    gtk_paned_set_start_child(GTK_PANED(split), scroll);
    auto* right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    b->info = browserLabel(""); gtk_box_append(GTK_BOX(right), b->info);
    b->stack = gtk_stack_new(); gtk_widget_set_vexpand(b->stack, TRUE);
    auto* tabs = gtk_stack_switcher_new(); gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(tabs), GTK_STACK(b->stack));
    gtk_box_append(GTK_BOX(right), tabs);
    gtk_stack_add_titled(GTK_STACK(b->stack), textPage(&b->text), "text", B_("Text"));
    auto* imageBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    b->zoom = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.25, 4, 0.25);
    gtk_range_set_value(GTK_RANGE(b->zoom), 1); gtk_widget_set_tooltip_text(b->zoom, B_("Image zoom"));
    g_signal_connect(b->zoom, "value-changed", reinterpret_cast<GCallback>(+[](GtkRange*, Browser* b) { zoomImage(b); }), b);
    gtk_box_append(GTK_BOX(imageBox), b->zoom);
    b->image = gtk_drawing_area_new();
    gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(b->image), +[](GtkDrawingArea*, cairo_t* cr, int width, int height, gpointer data) {
        auto* b = static_cast<Browser*>(data);
        auto* tile = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 24, 24);
        auto* painter = cairo_create(tile);
        cairo_set_source_rgb(painter, 0.8, 0.8, 0.8); cairo_paint(painter);
        cairo_set_source_rgb(painter, 0.6, 0.6, 0.6);
        cairo_rectangle(painter, 0, 0, 12, 12); cairo_rectangle(painter, 12, 12, 12, 12); cairo_fill(painter);
        cairo_destroy(painter);
        auto* pattern = cairo_pattern_create_for_surface(tile); cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
        cairo_set_source(cr, pattern); cairo_paint(cr);
        cairo_pattern_destroy(pattern); cairo_surface_destroy(tile);
        if (b->imageSurface) {
            const double zoom = gtk_range_get_value(GTK_RANGE(b->zoom)); cairo_scale(cr, zoom, zoom);
            cairo_set_source_surface(cr, b->imageSurface, 0, 0); cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST); cairo_paint(cr);
        }
    }, b, nullptr);
    scroll = gtk_scrolled_window_new(); gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), b->image); gtk_box_append(GTK_BOX(imageBox), scroll);
    gtk_stack_add_titled(GTK_STACK(b->stack), imageBox, "image", B_("Image"));
    auto* audioBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    b->mediaControls = gtk_media_controls_new(nullptr); gtk_box_append(GTK_BOX(audioBox), b->mediaControls);
    gtk_box_append(GTK_BOX(audioBox), browserLabel(B_("Volume")));
    b->volume = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1, 0.01); gtk_range_set_value(GTK_RANGE(b->volume), 1);
    gtk_widget_set_tooltip_text(b->volume, B_("Volume")); gtk_box_append(GTK_BOX(audioBox), b->volume);
    g_signal_connect(b->volume, "value-changed", reinterpret_cast<GCallback>(+[](GtkRange* range, Browser* b) {
        if (b->media) gtk_media_stream_set_volume(b->media, gtk_range_get_value(range));
    }), b);
    gtk_stack_add_titled(GTK_STACK(b->stack), audioBox, "audio", B_("Audio"));
    auto* hexBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    scroll = textPage(&b->hex); gtk_widget_set_vexpand(scroll, TRUE); gtk_box_append(GTK_BOX(hexBox), scroll);
    auto* pager = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    b->previous = browserButton(B_("Previous page"), reinterpret_cast<GCallback>(+[](GtkButton*, Browser* b) {
        if (b->selected && b->selected->source && b->hexOffset >= HexPageSize) { b->hexOffset -= HexPageSize; request(b, RequestKind::Hex, *b->selected); }
    }), b);
    b->next = browserButton(B_("Next page"), reinterpret_cast<GCallback>(+[](GtkButton*, Browser* b) {
        if (b->selected && b->selected->source && b->selected->source->size() - b->hexOffset > HexPageSize) {
            b->hexOffset += HexPageSize; request(b, RequestKind::Hex, *b->selected);
        }
    }), b);
    b->page = browserLabel("");
    for (auto* child : {b->previous, b->page, b->next}) gtk_box_append(GTK_BOX(pager), child);
    gtk_box_append(GTK_BOX(hexBox), pager); gtk_stack_add_titled(GTK_STACK(b->stack), hexBox, "hex", B_("Hex"));
    gtk_box_append(GTK_BOX(right), b->stack);
    auto* exports = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    b->exportButton = browserButton(B_("Export original bytes"), reinterpret_cast<GCallback>(+[](GtkButton*, Browser* b) { chooseResource(b, DialogKind::Export); }), b);
    b->queueButton = browserButton(B_("Add to conversion tasks"), reinterpret_cast<GCallback>(+[](GtkButton*, Browser* b) {
        if (b->selected && !b->selected->embedded) b->queueFile(b->selected->file.u8string());
    }), b);
    for (auto* child : {b->exportButton, b->queueButton}) { gtk_widget_set_sensitive(child, FALSE); gtk_box_append(GTK_BOX(exports), child); }
    gtk_box_append(GTK_BOX(right), exports); gtk_paned_set_end_child(GTK_PANED(split), right); gtk_box_append(GTK_BOX(b->root), split);
    b->status = browserLabel(""); gtk_box_append(GTK_BOX(b->root), b->status);
    b->selectionSignal = g_signal_connect(b->selection, "notify::selected", reinterpret_cast<GCallback>(+[](GtkSingleSelection* selection, GParamSpec*, Browser* b) {
        const auto index = gtk_single_selection_get_selected(selection);
        if (index == GTK_INVALID_LIST_POSITION) return;
        auto node = b->nodes.at(index);
        if (!node.directory) request(b, RequestKind::Preview, std::move(node));
        else {
            if (b->pending) g_cancellable_cancel(b->pending);
            ++b->generation; ++b->hexGeneration; b->hexOffset = 0; clearPreview(b); b->selected.reset();
            gtk_label_set_text(GTK_LABEL(b->info), node.name.c_str());
            gtk_widget_set_sensitive(b->exportButton, FALSE); gtk_widget_set_sensitive(b->queueButton, FALSE);
            showPageButtons(b);
        }
    }), b);
    g_signal_connect(b->list, "activate", reinterpret_cast<GCallback>(+[](GtkListView*, guint index, Browser* b) {
        request(b, RequestKind::Open, b->nodes.at(index), b->history.size());
    }), b);
    auto refresh = +[](GObject*, GParamSpec*, GtkWidget* root) {
        auto* b = browser(root);
        if (b->selected) request(b, RequestKind::Preview, *b->selected);
        else if (!b->history.empty()) request(b, RequestKind::Open, b->history.back(), b->history.size() - 1);
    };
    g_signal_connect_object(b->encoding, "notify::selected", reinterpret_cast<GCallback>(refresh), b->root, G_CONNECT_DEFAULT);
    g_signal_connect_object(b->baseEncoding, "notify::selected", reinterpret_cast<GCallback>(refresh), b->root, G_CONNECT_DEFAULT);
    g_signal_connect(b->root, "unmap", reinterpret_cast<GCallback>(+[](GtkWidget*, Browser* b) { if (b->media) gtk_media_stream_pause(b->media); }), b);
    g_signal_connect(b->root, "unrealize", reinterpret_cast<GCallback>(+[](GtkWidget*, Browser* b) { if (b->pending) g_cancellable_cancel(b->pending); }), b);
    showPageButtons(b);
    return b->root;
}
bool resourceBrowserWriting(GtkWidget* root) { return root && browser(root)->writes != 0; }
}
