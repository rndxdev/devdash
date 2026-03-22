#pragma once
#include <gtk/gtk.h>

typedef struct {
    const char *name;
    const char *label;
    GtkWidget* (*create)(void);
    void       (*refresh)(void);
    void       (*cleanup)(void);
} PanelDef;
