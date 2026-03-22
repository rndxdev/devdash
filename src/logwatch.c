#include "logwatch.h"
#include "theme.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LOG_LINES 500
#define MAX_LOG_FILES 8
#define MAX_READ_BYTES 8192
#define MAX_LINES_PER_TICK 50

static GtkTextBuffer *s_buffer;
static GtkWidget *s_textview;
static GtkWidget *s_file_combo;
static GtkTextTag *s_tag_error;
static GtkTextTag *s_tag_warn;
static GtkTextTag *s_tag_info;
static GtkTextTag *s_tag_debug;
static GtkTextTag *s_tag_normal;

static char s_files[MAX_LOG_FILES][512];
static int s_nfiles;
static long s_offsets[MAX_LOG_FILES];
static int s_line_count;

static void load_config(void) {
    const char *home = g_get_home_dir();
    char path[512];
    snprintf(path, sizeof(path), "%s/.config/devdash/logwatch.conf", home);

    FILE *f = fopen(path, "r");
    if (!f) {
        char dir[256];
        snprintf(dir, sizeof(dir), "%s/.config/devdash", home);
        g_mkdir_with_parents(dir, 0755);
        f = fopen(path, "w");
        if (f) {
            fprintf(f, "/var/log/syslog\n");
            fprintf(f, "/var/log/auth.log\n");
            fclose(f);
        }
        f = fopen(path, "r");
        if (!f) return;
    }

    s_nfiles = 0;
    char line[512];
    while (fgets(line, sizeof(line), f) && s_nfiles < MAX_LOG_FILES) {
        trim_inplace(line);
        if (!line[0] || line[0] == '#') continue;
        strncpy(s_files[s_nfiles], line, sizeof(s_files[0]) - 1);
        s_nfiles++;
    }
    fclose(f);
}

static GtkTextTag *classify_line(const char *line) {
    if (strcasestr(line, "error") || strcasestr(line, "fatal") || strcasestr(line, "fail"))
        return s_tag_error;
    if (strcasestr(line, "warn"))
        return s_tag_warn;
    if (strcasestr(line, "info"))
        return s_tag_info;
    if (strcasestr(line, "debug"))
        return s_tag_debug;
    return s_tag_normal;
}

static void trim_buffer_top(void) {
    if (s_line_count <= MAX_LOG_LINES) return;
    int excess = s_line_count - MAX_LOG_LINES;
    GtkTextIter start, cutoff;
    gtk_text_buffer_get_start_iter(s_buffer, &start);
    cutoff = start;
    gtk_text_iter_forward_lines(&cutoff, excess);
    gtk_text_buffer_delete(s_buffer, &start, &cutoff);
    s_line_count = MAX_LOG_LINES;
}

static void read_new_lines(int idx) {
    if (idx < 0 || idx >= s_nfiles) return;

    FILE *f = fopen(s_files[idx], "r");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);

    if (s_offsets[idx] == 0) {
        /* First read: only grab last 2KB to keep it fast */
        long start = size - 2048;
        if (start < 0) start = 0;
        fseek(f, start, SEEK_SET);
        if (start > 0) {
            /* Skip partial line */
            int c;
            while ((c = fgetc(f)) != EOF && c != '\n');
        }
    } else if (size > s_offsets[idx]) {
        /* Only read new data, but cap how much */
        long new_data = size - s_offsets[idx];
        if (new_data > MAX_READ_BYTES) {
            /* Skip ahead if too much accumulated */
            fseek(f, size - MAX_READ_BYTES, SEEK_SET);
            int c;
            while ((c = fgetc(f)) != EOF && c != '\n');
        } else {
            fseek(f, s_offsets[idx], SEEK_SET);
        }
    } else {
        /* File was truncated/rotated */
        long start = size - 2048;
        if (start < 0) start = 0;
        fseek(f, start, SEEK_SET);
        if (start > 0) {
            int c;
            while ((c = fgetc(f)) != EOF && c != '\n');
        }
    }

    char line[1024];
    int lines_added = 0;

    while (fgets(line, sizeof(line), f) && lines_added < MAX_LINES_PER_TICK) {
        trim_inplace(line);
        if (!line[0]) continue;

        /* Ensure valid UTF-8 */
        const char *safe = line;
        char *valid = NULL;
        if (!g_utf8_validate(line, -1, NULL)) {
            valid = g_utf8_make_valid(line, -1);
            safe = valid;
        }

        GtkTextIter end;
        gtk_text_buffer_get_end_iter(s_buffer, &end);
        GtkTextTag *tag = classify_line(safe);
        gtk_text_buffer_insert_with_tags(s_buffer, &end, safe, -1, tag, NULL);
        gtk_text_buffer_get_end_iter(s_buffer, &end);
        gtk_text_buffer_insert(s_buffer, &end, "\n", 1);
        s_line_count++;
        lines_added++;

        if (valid) g_free(valid);
    }

    s_offsets[idx] = ftell(f);
    fclose(f);

    /* Trim excess lines in one batch */
    trim_buffer_top();

    /* Auto-scroll to bottom */
    if (lines_added > 0) {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(s_buffer, &end);
        GtkTextMark *mark = gtk_text_buffer_get_mark(s_buffer, "end-mark");
        if (!mark)
            mark = gtk_text_buffer_create_mark(s_buffer, "end-mark", &end, FALSE);
        else
            gtk_text_buffer_move_mark(s_buffer, mark, &end);
        gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(s_textview), mark, 0, FALSE, 0, 0);
    }
}

static void on_file_changed(GtkComboBox *combo, gpointer data) {
    (void)data;
    int idx = gtk_combo_box_get_active(combo);
    gtk_text_buffer_set_text(s_buffer, "", -1);
    s_line_count = 0;
    s_offsets[idx] = 0;
    read_new_lines(idx);
}

static void on_clear_clicked(GtkWidget *btn, gpointer data) {
    (void)btn; (void)data;
    gtk_text_buffer_set_text(s_buffer, "", -1);
    s_line_count = 0;
}

void logwatch_refresh(void) {
    int idx = gtk_combo_box_get_active(GTK_COMBO_BOX(s_file_combo));
    if (idx >= 0) read_new_lines(idx);
}

GtkWidget *logwatch_create(void) {
    load_config();

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);

    GtkWidget *title = make_label(
        "<span font='14' weight='bold' foreground='" CAT_BLUE "'>Log Watch</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    /* File selector */
    s_file_combo = gtk_combo_box_text_new();
    for (int i = 0; i < s_nfiles; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(s_file_combo), s_files[i]);
    if (s_nfiles > 0) gtk_combo_box_set_active(GTK_COMBO_BOX(s_file_combo), 0);
    g_signal_connect(s_file_combo, "changed", G_CALLBACK(on_file_changed), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), s_file_combo, FALSE, FALSE, 4);

    /* Text view */
    s_textview = gtk_text_view_new();
    s_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(s_textview));
    gtk_text_view_set_editable(GTK_TEXT_VIEW(s_textview), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(s_textview), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(s_textview), 8);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(s_textview), 8);
    gtk_widget_set_name(s_textview, "log-text");

    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "#log-text {"
        "  font-family: monospace;"
        "  font-size: 11px;"
        "  background-color: " CAT_CRUST ";"
        "  color: " CAT_TEXT ";"
        "}", -1, NULL);
    gtk_style_context_add_provider(
        gtk_widget_get_style_context(s_textview),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    /* Tags for severity */
    GtkTextTagTable *tt = gtk_text_buffer_get_tag_table(s_buffer);
    s_tag_error = gtk_text_tag_new("error");
    g_object_set(s_tag_error, "foreground", CAT_RED, NULL);
    gtk_text_tag_table_add(tt, s_tag_error);

    s_tag_warn = gtk_text_tag_new("warn");
    g_object_set(s_tag_warn, "foreground", CAT_YELLOW, NULL);
    gtk_text_tag_table_add(tt, s_tag_warn);

    s_tag_info = gtk_text_tag_new("info");
    g_object_set(s_tag_info, "foreground", CAT_BLUE, NULL);
    gtk_text_tag_table_add(tt, s_tag_info);

    s_tag_debug = gtk_text_tag_new("debug");
    g_object_set(s_tag_debug, "foreground", CAT_OVERLAY0, NULL);
    gtk_text_tag_table_add(tt, s_tag_debug);

    s_tag_normal = gtk_text_tag_new("normal");
    g_object_set(s_tag_normal, "foreground", CAT_SUBTEXT0, NULL);
    gtk_text_tag_table_add(tt, s_tag_normal);

    GtkWidget *sw = make_scrolled(s_textview);
    gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 4);

    /* Clear button */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(hbox, GTK_ALIGN_END);
    GtkWidget *btn = gtk_button_new_with_label("Clear");
    gtk_widget_set_name(btn, "danger-btn");
    g_signal_connect(btn, "clicked", G_CALLBACK(on_clear_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    /* Initial load */
    if (s_nfiles > 0) read_new_lines(0);

    return vbox;
}

void logwatch_cleanup(void) {}
