#include <gtk/gtk.h>
#include <libnotify/notify.h>
#include "theme.h"
#include "panel.h"
#include "scratchpad.h"
#include "portwatch.h"
#include "procmon.h"
#include "gitdash.h"
#include "clipman.h"
#include "logwatch.h"
#include "envman.h"
#include "sshdash.h"
#include "shell.h"
#include "battery.h"
#include "sysstat.h"

static GtkWidget *s_stack;
static GtkWidget *s_window;
static GtkWidget *s_sidebar_list;
static GtkStatusIcon *s_tray_icon;
static int s_tick;

/* Panel registry */
static PanelDef panels[] = {
    { "gitdash",    "Git",       gitdash_create,    gitdash_refresh,    gitdash_cleanup    },
    { "procmon",    "Processes",  procmon_create,    procmon_refresh,    procmon_cleanup    },
    { "clipman",    "Clipboard",  clipman_create,    clipman_refresh,    clipman_cleanup    },
    { "portwatch",  "Ports",      portwatch_create,  portwatch_refresh,  portwatch_cleanup  },
    { "scratchpad", "Scratchpad", scratchpad_create, scratchpad_refresh, scratchpad_cleanup },
    { "logwatch",   "Logs",       logwatch_create,   logwatch_refresh,   logwatch_cleanup   },
    { "envman",     "Env Vars",   envman_create,     envman_refresh,     envman_cleanup     },
    { "sshdash",    "SSH",        sshdash_create,    sshdash_refresh,    sshdash_cleanup    },
    { "shell",      "Shell",      shell_create,      shell_refresh,      shell_cleanup      },
    { "battery",    "Battery",    battery_create,     battery_refresh,    battery_cleanup    },
    { "sysstat",    "System",     sysstat_create,     sysstat_refresh,    sysstat_cleanup    },
};
static const int N_PANELS = sizeof(panels) / sizeof(panels[0]);

/* Refresh intervals (in seconds) */
static int refresh_intervals[] = {
    10,  /* gitdash    */
    3,   /* procmon    */
    1,   /* clipman    */
    5,   /* portwatch  */
    0,   /* scratchpad */
    2,   /* logwatch   */
    0,   /* envman     */
    0,   /* sshdash    */
    0,   /* shell      */
    3,   /* battery    */
    1,   /* sysstat    */
};

static gboolean on_tick(gpointer data) {
    (void)data;
    s_tick++;

    const char *visible = gtk_stack_get_visible_child_name(GTK_STACK(s_stack));
    if (!visible) return TRUE;

    for (int i = 0; i < N_PANELS; i++) {
        if (strcmp(visible, panels[i].name) != 0) continue;
        if (refresh_intervals[i] <= 0) break;
        if (s_tick % refresh_intervals[i] == 0)
            panels[i].refresh();
        break;
    }
    return TRUE;
}

static void on_sidebar_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    (void)box; (void)data;
    int idx = gtk_list_box_row_get_index(row);
    if (idx >= 0 && idx < N_PANELS) {
        gtk_stack_set_visible_child_name(GTK_STACK(s_stack), panels[idx].name);
        /* Immediate refresh on panel switch */
        if (panels[idx].refresh)
            panels[idx].refresh();
    }
}

static void apply_css(void) {
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        /* Window */
        "window { background-color: " CAT_BASE "; }"

        /* Sidebar */
        "#sidebar {"
        "  background-color: " CAT_MANTLE ";"
        "}"
        "#sidebar row {"
        "  padding: 10px 16px;"
        "  border-left: 3px solid transparent;"
        "  color: " CAT_SUBTEXT1 ";"
        "}"
        "#sidebar row:selected {"
        "  background-color: " CAT_SURFACE0 ";"
        "  border-left: 3px solid " CAT_BLUE ";"
        "  color: " CAT_TEXT ";"
        "}"
        "#sidebar row:hover {"
        "  background-color: " CAT_SURFACE0 ";"
        "}"

        /* Labels */
        "label { color: " CAT_TEXT "; }"

        /* TreeViews */
        "#panel-tree {"
        "  background-color: " CAT_MANTLE ";"
        "  color: " CAT_TEXT ";"
        "}"
        "#panel-tree header button {"
        "  background-color: " CAT_SURFACE1 ";"
        "  color: " CAT_TEXT ";"
        "  border: none;"
        "  padding: 6px 8px;"
        "}"

        /* Search entries */
        "#panel-search {"
        "  background-color: " CAT_SURFACE0 ";"
        "  color: " CAT_TEXT ";"
        "  border: 1px solid " CAT_SURFACE1 ";"
        "  border-radius: 6px;"
        "  padding: 6px 10px;"
        "  caret-color: " CAT_BLUE ";"
        "}"

        /* Buttons — filled for WCAG AA contrast */
        "#action-btn, .action-btn {"
        "  background-image: none;"
        "  background-color: " CAT_BLUE ";"
        "  color: " CAT_CRUST ";"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 6px 14px;"
        "  font-weight: bold;"
        "  text-shadow: none;"
        "  box-shadow: none;"
        "}"
        "#action-btn:hover, .action-btn:hover {"
        "  background-image: none;"
        "  background-color: " CAT_SAPPHIRE ";"
        "}"
        "#danger-btn, .danger-btn {"
        "  background-image: none;"
        "  background-color: " CAT_RED ";"
        "  color: " CAT_CRUST ";"
        "  border: none;"
        "  border-radius: 6px;"
        "  padding: 6px 14px;"
        "  font-weight: bold;"
        "  text-shadow: none;"
        "  box-shadow: none;"
        "}"
        "#danger-btn:hover, .danger-btn:hover {"
        "  background-image: none;"
        "  background-color: " CAT_MAROON ";"
        "}"
        /* Override all GtkButton defaults in our app */
        "button {"
        "  background-image: none;"
        "  text-shadow: none;"
        "}"

        /* Project cards */
        "#project-card {"
        "  background-color: " CAT_MANTLE ";"
        "  border: 1px solid " CAT_SURFACE0 ";"
        "  border-radius: 8px;"
        "}"

        /* Clipboard list */
        "#clip-list {"
        "  background-color: " CAT_MANTLE ";"
        "}"
        "#clip-list row {"
        "  border-bottom: 1px solid " CAT_SURFACE0 ";"
        "}"
        "#clip-list row:hover {"
        "  background-color: " CAT_SURFACE0 ";"
        "}"

        /* Combo boxes */
        "combobox button {"
        "  background-color: " CAT_SURFACE0 ";"
        "  color: " CAT_TEXT ";"
        "  border: 1px solid " CAT_SURFACE1 ";"
        "}"

        /* Scrollbars */
        "scrollbar slider {"
        "  background-color: " CAT_SURFACE1 ";"
        "  border-radius: 4px;"
        "  min-width: 6px;"
        "  min-height: 6px;"
        "}"
        "scrollbar {"
        "  background-color: transparent;"
        "}"

        /* Text views — force dark theme on all inner text nodes */
        "textview {"
        "  background-color: " CAT_CRUST ";"
        "  color: " CAT_TEXT ";"
        "}"
        "textview text {"
        "  background-color: " CAT_CRUST ";"
        "  color: " CAT_TEXT ";"
        "}"

        /* Entries */
        "entry {"
        "  background-color: " CAT_SURFACE0 ";"
        "  color: " CAT_TEXT ";"
        "}"

        /* Separator */
        "separator {"
        "  background-color: " CAT_SURFACE0 ";"
        "  min-height: 1px;"
        "}"
    , -1, NULL);

    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
}

/* Switch to panel by index */
static void switch_to_panel(int idx) {
    if (idx < 0 || idx >= N_PANELS) return;
    gtk_stack_set_visible_child_name(GTK_STACK(s_stack), panels[idx].name);
    gtk_list_box_select_row(GTK_LIST_BOX(s_sidebar_list),
        gtk_list_box_get_row_at_index(GTK_LIST_BOX(s_sidebar_list), idx));
    if (panels[idx].refresh) panels[idx].refresh();
}

/* Keyboard shortcuts: Ctrl+1 through Ctrl+0 */
static gboolean on_key_press(GtkWidget *w, GdkEventKey *ev, gpointer data) {
    (void)w; (void)data;
    if (!(ev->state & GDK_CONTROL_MASK)) return FALSE;

    int idx = -1;
    if (ev->keyval >= GDK_KEY_1 && ev->keyval <= GDK_KEY_9)
        idx = ev->keyval - GDK_KEY_1;
    else if (ev->keyval == GDK_KEY_0)
        idx = 9;

    if (idx >= 0 && idx < N_PANELS) {
        switch_to_panel(idx);
        return TRUE;
    }
    return FALSE;
}

/* System tray */
static void on_tray_activate(GtkStatusIcon *icon, gpointer data) {
    (void)icon; (void)data;
    if (gtk_widget_get_visible(s_window)) {
        gtk_widget_hide(s_window);
    } else {
        gtk_widget_show(s_window);
        gtk_window_present(GTK_WINDOW(s_window));
    }
}

static void on_tray_quit(GtkMenuItem *item, gpointer data) {
    (void)item; (void)data;
    for (int i = 0; i < N_PANELS; i++)
        if (panels[i].cleanup) panels[i].cleanup();
    gtk_main_quit();
}

static void on_tray_popup(GtkStatusIcon *icon, guint button, guint time, gpointer data) {
    (void)icon; (void)data;
    GtkWidget *menu = gtk_menu_new();

    GtkWidget *item_show = gtk_menu_item_new_with_label("Show DevDash");
    g_signal_connect_swapped(item_show, "activate",
        G_CALLBACK(on_tray_activate), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_show);

    GtkWidget *sep = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);

    GtkWidget *item_quit = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(item_quit, "activate", G_CALLBACK(on_tray_quit), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item_quit);

    gtk_widget_show_all(menu);
    gtk_menu_popup(GTK_MENU(menu), NULL, NULL,
        gtk_status_icon_position_menu, icon, button, time);
}

static void setup_tray_icon(void) {
    s_tray_icon = gtk_status_icon_new_from_icon_name("devdash");
    gtk_status_icon_set_tooltip_text(s_tray_icon, "DevDash");
    gtk_status_icon_set_visible(s_tray_icon, TRUE);
    g_signal_connect(s_tray_icon, "activate", G_CALLBACK(on_tray_activate), NULL);
    g_signal_connect(s_tray_icon, "popup-menu", G_CALLBACK(on_tray_popup), NULL);
}

/* Minimize to tray instead of closing */
static gboolean on_delete_event(GtkWidget *w, GdkEvent *ev, gpointer data) {
    (void)ev; (void)data;
    gtk_widget_hide(w);
    return TRUE; /* prevent destroy */
}

static void on_destroy(GtkWidget *w, gpointer data) {
    (void)w; (void)data;
    for (int i = 0; i < N_PANELS; i++)
        if (panels[i].cleanup) panels[i].cleanup();
    gtk_main_quit();
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);
    notify_init("devdash");
    apply_css();

    /* Main window */
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    s_window = window;
    gtk_window_set_title(GTK_WINDOW(window), "DevDash");
    gtk_window_set_default_size(GTK_WINDOW(window), 1100, 750);
    gtk_window_set_icon_name(GTK_WINDOW(window), "devdash");
    g_signal_connect(window, "delete-event", G_CALLBACK(on_delete_event), NULL);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), NULL);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key_press), NULL);

    /* Main layout: sidebar | content */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(window), hbox);

    /* Sidebar */
    GtkWidget *sidebar_frame = gtk_frame_new(NULL);
    gtk_widget_set_size_request(sidebar_frame, 160, -1);

    GtkWidget *sidebar_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(sidebar_box, "sidebar");
    gtk_container_add(GTK_CONTAINER(sidebar_frame), sidebar_box);

    /* Sidebar title */
    GtkWidget *logo = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(logo),
        "<span font='16' weight='bold' foreground='" CAT_BLUE "'>devdash</span>");
    gtk_widget_set_margin_top(logo, 16);
    gtk_widget_set_margin_bottom(logo, 12);
    gtk_box_pack_start(GTK_BOX(sidebar_box), logo, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(sidebar_box),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    s_sidebar_list = gtk_list_box_new();
    GtkWidget *sidebar_list = s_sidebar_list;
    gtk_widget_set_name(sidebar_list, "sidebar");
    g_signal_connect(sidebar_list, "row-activated",
        G_CALLBACK(on_sidebar_row_activated), NULL);
    gtk_box_pack_start(GTK_BOX(sidebar_box), sidebar_list, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(hbox), sidebar_frame, FALSE, FALSE, 0);

    /* Content stack */
    s_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(s_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(s_stack), 150);
    gtk_box_pack_start(GTK_BOX(hbox), s_stack, TRUE, TRUE, 0);

    /* Register all panels */
    for (int i = 0; i < N_PANELS; i++) {
        /* Sidebar entry */
        GtkWidget *label = gtk_label_new(panels[i].label);
        gtk_label_set_xalign(GTK_LABEL(label), 0);
        gtk_list_box_insert(GTK_LIST_BOX(sidebar_list), label, -1);

        /* Stack page */
        GtkWidget *page = panels[i].create();
        gtk_stack_add_named(GTK_STACK(s_stack), page, panels[i].name);
    }

    /* Select first panel */
    gtk_list_box_select_row(GTK_LIST_BOX(sidebar_list),
        gtk_list_box_get_row_at_index(GTK_LIST_BOX(sidebar_list), 0));
    gtk_stack_set_visible_child_name(GTK_STACK(s_stack), panels[0].name);

    /* Master tick: 1 second */
    g_timeout_add(1000, on_tick, NULL);

    /* System tray icon */
    setup_tray_icon();

    gtk_widget_show_all(window);
    gtk_main();

    notify_uninit();
    return 0;
}
