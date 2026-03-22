#include "scratchpad.h"
#include "theme.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static GtkTextBuffer *s_buffer;
static char s_path[512];

static void ensure_config_dir(void) {
    const char *home = g_get_home_dir();
    char dir[256];
    snprintf(dir, sizeof(dir), "%s/.config/devdash", home);
    g_mkdir_with_parents(dir, 0755);
    snprintf(s_path, sizeof(s_path), "%s/scratchpad.txt", dir);
}

static void load_file(void) {
    FILE *f = fopen(s_path, "r");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return; }
    char *buf = malloc(sz + 1);
    if (!buf) { fclose(f); return; }
    size_t rd = fread(buf, 1, sz, f);
    buf[rd] = '\0';
    fclose(f);
    gtk_text_buffer_set_text(s_buffer, buf, -1);
    free(buf);
}

static void save_file(void) {
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(s_buffer, &start, &end);
    char *text = gtk_text_buffer_get_text(s_buffer, &start, &end, FALSE);
    FILE *f = fopen(s_path, "w");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
    g_free(text);
}

static void on_save_clicked(GtkWidget *btn, gpointer data) {
    (void)btn; (void)data;
    save_file();
}

static void on_clear_clicked(GtkWidget *btn, gpointer data) {
    (void)btn; (void)data;
    gtk_text_buffer_set_text(s_buffer, "", -1);
    save_file();
}

static gboolean on_focus_out(GtkWidget *w, GdkEvent *e, gpointer data) {
    (void)w; (void)e; (void)data;
    save_file();
    return FALSE;
}

GtkWidget *scratchpad_create(void) {
    ensure_config_dir();

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);

    /* Title */
    GtkWidget *title = make_label(
        "<span font='14' weight='bold' foreground='" CAT_BLUE "'>Scratchpad</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *subtitle = make_label(
        "<span font='11' foreground='" CAT_SUBTEXT0 "'>Quick notes &amp; snippets — auto-saves on focus loss</span>");
    gtk_box_pack_start(GTK_BOX(vbox), subtitle, FALSE, FALSE, 0);

    /* Text view */
    GtkWidget *tv = gtk_text_view_new();
    s_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(tv));
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(tv), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(tv), 8);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(tv), 8);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(tv), 8);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(tv), 8);
    gtk_widget_set_name(tv, "scratchpad-text");
    g_signal_connect(tv, "focus-out-event", G_CALLBACK(on_focus_out), NULL);

    /* Monospace font via CSS */
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "#scratchpad-text {"
        "  font-family: monospace;"
        "  font-size: 13px;"
        "  background-color: " CAT_SURFACE0 ";"
        "  color: " CAT_TEXT ";"
        "  caret-color: " CAT_BLUE ";"
        "}", -1, NULL);
    gtk_style_context_add_provider(
        gtk_widget_get_style_context(tv),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    GtkWidget *sw = make_scrolled(tv);
    gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 4);

    /* Buttons */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(hbox, GTK_ALIGN_END);

    GtkWidget *btn_save = gtk_button_new_with_label("Save");
    gtk_widget_set_name(btn_save, "action-btn");
    g_signal_connect(btn_save, "clicked", G_CALLBACK(on_save_clicked), NULL);

    GtkWidget *btn_clear = gtk_button_new_with_label("Clear");
    gtk_widget_set_name(btn_clear, "danger-btn");
    g_signal_connect(btn_clear, "clicked", G_CALLBACK(on_clear_clicked), NULL);

    gtk_box_pack_start(GTK_BOX(hbox), btn_save, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), btn_clear, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    load_file();
    return vbox;
}

void scratchpad_refresh(void) {
    /* No periodic refresh needed */
}

void scratchpad_cleanup(void) {
    save_file();
}
