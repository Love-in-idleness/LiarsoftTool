#pragma once
#include <functional>
#include <gtk/gtk.h>
namespace liarsoft::gui {
GtkWidget *createExeEditor(GtkWindow *parent);
bool exeEditorWriting(GtkWidget *editor);
bool exeEditorDirty(GtkWidget *editor);
void leaveExeEditor(GtkWidget *editor, std::function<void()> continuation);
} // namespace liarsoft::gui
