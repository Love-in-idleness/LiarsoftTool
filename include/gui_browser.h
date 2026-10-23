#pragma once
#include <gtk/gtk.h>
#include <string>

namespace liarsoft::gui {
GtkWidget* createResourceBrowser(GtkWindow* parent, GtkDropDown* resourceEncoding,
                                void (*queueFile)(const std::string& utf8Path));
bool resourceBrowserWriting(GtkWidget* browser);
}
