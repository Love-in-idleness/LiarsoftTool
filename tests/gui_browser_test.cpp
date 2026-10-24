// The production browser, with generated fixtures and no game/player files.
#include "../src/gui_browser.cpp"
#include "gui_preferences.h"
#include "encoding.h"
#include "wcg_decoder.h"
#include "wav_ogg.h"
#include "gsc_decompiler.h"
#include "xflarchive.h"
#include "vorbis_fixture.h"
#include <chrono>
#include <fstream>
#include <iostream>

using namespace liarsoft;
using namespace liarsoft::gui;
namespace {
fs::path directory;
GtkWidget* root;
GtkWindow* window;
Browser* b;
std::string failure, queued;
int stage = 0;
int staleTicks = 0;
auto started = std::chrono::steady_clock::now();
std::vector<uint8_t> archive, picture;
fs::file_time_type originalTime;
void require(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }
std::string getText(GtkWidget* view) {
    auto* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    GtkTextIter first, last; gtk_text_buffer_get_bounds(buffer, &first, &last);
    auto* value = gtk_text_buffer_get_text(buffer, &first, &last, FALSE);
    const std::string text(value); g_free(value); return text;
}
void select(size_t index) { gtk_single_selection_set_selected(b->selection, index); }
void enter(size_t index) { g_signal_emit_by_name(b->list, "activate", static_cast<guint>(index)); }
void capture() {
    const auto* destination = g_getenv("LIARSOFT_BROWSER_CAPTURE");
    if (!destination) return;
    auto* paintable = gtk_widget_paintable_new(GTK_WIDGET(window));
    auto* snapshot = gtk_snapshot_new();
    gdk_paintable_snapshot(paintable, snapshot, gtk_widget_get_width(GTK_WIDGET(window)), gtk_widget_get_height(GTK_WIDGET(window)));
    auto* node = gtk_snapshot_free_to_node(snapshot);
    require(node != nullptr, "browser capture has no render node");
    auto* texture = gsk_renderer_render_texture(gtk_native_get_renderer(GTK_NATIVE(window)), node, nullptr);
    require(gdk_texture_save_to_png(texture, destination), "browser capture failed");
    g_object_unref(texture); gsk_render_node_unref(node); g_object_unref(paintable);
}
gboolean step(gpointer) {
    try {
        require(std::chrono::steady_clock::now() - started < std::chrono::seconds(25), "browser timed out");
        if (!root) return G_SOURCE_CONTINUE;
        if (stage == 0) { openPath(b, directory / fs::u8path("资源 archive.xfl")); stage = 1; }
        else if (stage == 1 && b->nodes.size() == 2) {
            require(b->history.size() == 1 && !b->selected, "archive should only read its index");
            enter(0); stage = 2;
        } else if (stage == 2 && b->history.size() == 2) {
            require(b->nodes.size() == 3, "nested archive entry count");
            select(1); stage = 3;
        } else if (stage == 3 && b->imageSurface) {
            require(cairo_image_surface_get_width(b->imageSurface) == 2, "WCG image size");
            require(b->pixels[0] == 0xff010203 && b->pixels[1] == 0x80191e23, "image channels or alpha changed");
            require(!gtk_widget_get_sensitive(b->queueButton), "embedded entry cannot use the file converter directly");
            gtk_range_set_value(GTK_RANGE(b->zoom), 2);
            require(gtk_drawing_area_get_content_width(GTK_DRAWING_AREA(b->image)) == 4, "image zoom did not resize");
            require(getText(b->hex).find("57 47") != std::string::npos, "raw WCG bytes unavailable");
            request(b, RequestKind::Export, *b->selected, 0, directory / "export.wcg");
            require(resourceBrowserWriting(root), "export not tracked"); stage = 4;
        } else if (stage == 4 && !resourceBrowserWriting(root)) {
            require(readFile(directory / "export.wcg") == picture, "export bytes changed");
            request(b, RequestKind::Export, *b->selected, 0, directory / fs::u8path("资源 archive.xfl")); stage = 5;
        } else if (stage == 5 && !resourceBrowserWriting(root)) {
            require(std::string(gtk_label_get_text(GTK_LABEL(b->status))).find("overwrite") != std::string::npos, "source overwrite not refused");
            require(readFile(directory / fs::u8path("资源 archive.xfl")) == archive && fs::last_write_time(directory / fs::u8path("资源 archive.xfl")) == originalTime, "browsing/export modified source");
            select(0); stage = 6;
        } else if (stage == 6 && b->selected && b->selected->source && b->selected->source->size() == 0) {
            require(getText(b->hex).empty() && !gtk_widget_get_sensitive(b->next), "empty entry hex");
            select(2); stage = 7;
        } else if (stage == 7 && b->selected && b->selected->name == "text.tsc") {
            require(getText(b->text).find("*end") != std::string::npos, "TSC text unavailable");
            g_signal_emit_by_name(b->back, "clicked"); stage = 8;
        } else if (stage == 8 && b->history.size() == 1 && b->nodes.size() == 2) {
            select(1); stage = 9;
        } else if (stage == 9 && b->selected && b->selected->name == "zbroken.wcg") {
            require(!std::string(gtk_label_get_text(GTK_LABEL(b->status))).empty(), "damaged image had no error");
            require(getText(b->hex).find("57 47") != std::string::npos, "damaged image lost raw bytes");
            openPath(b, directory / "large.bin"); stage = 10;
        } else if (stage == 10 && b->selected && b->selected->name.find("large.bin") != std::string::npos) {
            require(b->nodes.empty() && b->history.empty() && !gtk_widget_get_sensitive(b->back), "standalone file retained old archive navigation");
            require(gtk_widget_get_sensitive(b->next), "raw byte pages unavailable");
            g_signal_emit_by_name(b->next, "clicked"); stage = 11;
        } else if (stage == 11 && getText(b->hex).find("0000000000001000") == 0) {
            openPath(b, directory / "slow.tsc");
            openPath(b, directory / "latest.tsc"); stage = 12;
        } else if (stage == 12 && getText(b->text) == "LATEST") {
            stage = 120;
        } else if (stage == 120) {
            require(getText(b->text) == "LATEST", "a stale preview replaced the new selection");
            if (++staleTicks < 30) return G_SOURCE_CONTINUE;
            capture();
            openPath(b, directory / "encoded.ini"); stage = 13;
        } else if (stage == 13 && b->selected && b->selected->name.find("encoded.ini") != std::string::npos) {
            require(getText(b->text) == "日本語", "CP932 text not decoded");
            openPath(b, directory / "voice.wav"); stage = 14;
        } else if (stage == 14 && b->media && gtk_media_stream_get_error(b->media)) {
            throw std::runtime_error(gtk_media_stream_get_error(b->media)->message);
        } else if (stage == 14 && b->media && gtk_media_stream_is_prepared(b->media)) {
            gtk_range_set_value(GTK_RANGE(b->volume), 0.3);
            require(gtk_media_stream_get_volume(b->media) == 0.3, "audio volume not connected");
            require(G_IS_SEEKABLE(gtk_media_file_get_input_stream(GTK_MEDIA_FILE(b->media))), "audio stream not seekable");
            require(gtk_media_stream_has_audio(b->media) && gtk_media_stream_get_duration(b->media) > 0, "audio not decoded");
            require(gtk_media_stream_is_seekable(b->media), "audio player cannot seek");
            gtk_media_stream_set_muted(b->media, TRUE);
            gtk_media_stream_play(b->media); stage = 140;
        } else if (stage == 140 && gtk_media_stream_get_timestamp(b->media) > 0) {
            gtk_media_stream_pause(b->media);
            require(!gtk_media_stream_get_playing(b->media), "audio did not pause");
            gtk_media_stream_seek(b->media, 1000000); stage = 141;
        } else if (stage == 141 && !gtk_media_stream_is_seeking(b->media)) {
            require(gtk_media_stream_get_timestamp(b->media) >= 900000, "audio seek did not reach target");
            openPath(b, directory / "scene.gsc"); stage = 15;
        } else if (stage == 15 && b->selected && b->selected->name.find("scene.gsc") != std::string::npos) {
            require(!b->media && getText(b->text).find("*end") != std::string::npos, "GSC preview or audio cleanup");
            g_signal_emit_by_name(b->queueButton, "clicked");
            require(queued == (directory / "scene.gsc").u8string(), "queue action input differs");
            openPath(b, directory); stage = 16;
        } else if (stage == 16 && b->history.size() == 1 && b->history[0].directory) {
            require(!b->nodes.empty(), "directory not browsed");
            require(readFile(directory / fs::u8path("资源 archive.xfl")) == archive, "input changed");
            openPath(b, directory / "slow.tsc");
            gtk_window_destroy(window); stage = 17;
            return G_SOURCE_REMOVE;
        }
    } catch (const std::exception& error) { failure = error.what(); gtk_window_destroy(window); return G_SOURCE_REMOVE; }
    return G_SOURCE_CONTINUE;
}
void activateBrowser(GtkApplication* app, gpointer) {
    window = GTK_WINDOW(gtk_application_window_new(app));
    auto* choices = gtk_string_list_new(nullptr);
    for (const auto& name : supportedEncodings()) gtk_string_list_append(choices, name.c_str());
    auto* encodingChoice = gtk_drop_down_new(G_LIST_MODEL(choices), nullptr);
    root = createResourceBrowser(window, GTK_DROP_DOWN(encodingChoice), +[](const std::string& file) { queued = file; });
    g_object_ref_sink(encodingChoice); g_object_unref(encodingChoice);
    b = browser(root);
    require(g_list_model_get_n_items(gtk_drop_down_get_model(GTK_DROP_DOWN(b->encoding))) ==
            supportedEncodings().size() + 1, "Preview encoding choices differ from conversion choices");
    for (guint index = 0; index < supportedEncodings().size(); ++index) {
        gtk_drop_down_set_selected(b->baseEncoding, index);
        require(encoding(b) == supportedEncodings()[index], "Preview resource encoding index is incorrect");
        gtk_drop_down_set_selected(GTK_DROP_DOWN(b->encoding), index + 1);
        require(encoding(b) == supportedEncodings()[index], "Preview explicit encoding index is incorrect");
        gtk_drop_down_set_selected(GTK_DROP_DOWN(b->encoding), 0);
    }
    gtk_drop_down_set_selected(b->baseEncoding, 0);
    gtk_window_set_default_size(window, 1000, 700);
    gtk_window_set_child(window, root); gtk_window_present(window);
}
}
int main(int argc, char** argv) {
#ifdef _WIN32
    g_setenv("GDK_BACKEND", "win32", TRUE);
#endif
    initializeTranslations("en", argv[0]);
    directory = fs::temp_directory_path() / ("liarsoft-browser-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(directory);
    picture = wcgEncode(std::vector<uint8_t>{1, 2, 3, 255, 50, 60, 70, 128}, 2, 1);
    const std::string tsc = ";@gsc-byte-format modern-36\n;@gsc-schema modern\n*end\n";
    XflArchive inner; inner.encoding = "CP932";
    inner.entries = {{"empty", {}}, {"image", picture}, {"text.tsc", {tsc.begin(), tsc.end()}}};
    XflArchive outer; outer.encoding = "CP932";
    outer.entries = {{"nested.xfl", inner.toBytes()}, {"zbroken.wcg", {'W', 'G'}}};
    archive = outer.toBytes();
    {
        std::ofstream output(directory / fs::u8path("资源 archive.xfl"), std::ios::binary);
        output.write(reinterpret_cast<const char*>(archive.data()), archive.size());
        output.close(); require(bool(output), "Unicode fixture could not be written");
    }
    originalTime = fs::last_write_time(directory / fs::u8path("资源 archive.xfl"));
    writeFileIfChanged((directory / "large.bin").string(), std::vector<uint8_t>(10000, 0));
    writeTextFileIfChanged((directory / "slow.tsc").string(), std::string(1024 * 1024, 'S'));
    writeTextFileIfChanged((directory / "latest.tsc").string(), "LATEST");
    const auto text = convertEncoding("日本語", "UTF-8", "CP932", true);
    writeFileIfChanged((directory / "encoded.ini").string(), {text.begin(), text.end()});
    writeFileIfChanged((directory / "scene.gsc").string(), restoreGscFromTsc(tsc));
    writeFileIfChanged((directory / "voice.wav").string(), WavOggExtractor::embed(testVorbis(1, 44100, 88200)));
    auto* app = adw_application_new("io.github.LiarsoftTool.BrowserTest", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activateBrowser), nullptr);
    g_timeout_add(10, step, nullptr);
    const int result = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app); fs::remove_all(directory);
    if (!failure.empty()) { std::cerr << failure << '\n'; return 1; }
    require(stage == 17, "browser test did not finish");
    std::cout << "OK: browser navigation, previews, paging, stale results, safe export and closing\n";
    return result;
}
