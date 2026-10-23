#include <adwaita.h>

static gboolean closeWindow(gpointer window) {
    gtk_window_destroy(GTK_WINDOW(window));
    return G_SOURCE_REMOVE;
}

static void activate(GtkApplication* app, gpointer) {
    auto* window = adw_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "LiarsoftTool GTK4 check");
    gtk_window_set_default_size(GTK_WINDOW(window), 640, 400);
    auto* view = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(view), adw_header_bar_new());
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(view),
                               gtk_label_new("Resources / Conversion tasks"));
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(window), view);
    auto* style = adw_style_manager_get_default();
    const AdwColorScheme schemes[] = {ADW_COLOR_SCHEME_FORCE_DARK,
        ADW_COLOR_SCHEME_FORCE_LIGHT, ADW_COLOR_SCHEME_PREFER_LIGHT};
    for (auto scheme : schemes) {
        adw_style_manager_set_color_scheme(style, scheme);
        g_assert(adw_style_manager_get_color_scheme(style) == scheme);
    }
    gtk_window_present(GTK_WINDOW(window));
    g_timeout_add(1500, closeWindow, window);
}

int main(int argc, char** argv) {
#ifdef _WIN32
    // Wine may inherit an X11-only backend preference from the Linux host.
    g_setenv("GDK_BACKEND", "win32", TRUE);
#endif
    auto* app = adw_application_new("io.github.LiarsoftTool.MigrationCheck",
                                    G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), nullptr);
    const int result = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return result;
}
