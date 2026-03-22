#include "sysstat.h"
#include "theme.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/statvfs.h>

#define HISTORY_LEN 120

static float s_cpu_hist[HISTORY_LEN];
static float s_ram_hist[HISTORY_LEN];
static int s_hist_count;

static GtkWidget *s_cpu_draw;
static GtkWidget *s_ram_draw;
static GtkWidget *s_disk_draw;
static GtkWidget *s_cpu_label;
static GtkWidget *s_ram_label;
static GtkWidget *s_disk_label;

/* Previous CPU reading for delta */
static long long s_prev_total, s_prev_idle;

static float read_cpu(void) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return 0;

    long long user, nice, system, idle, iowait, irq, softirq, steal;
    if (fscanf(f, "cpu %lld %lld %lld %lld %lld %lld %lld %lld",
               &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) != 8) {
        fclose(f);
        return 0;
    }
    fclose(f);

    long long total = user + nice + system + idle + iowait + irq + softirq + steal;
    long long total_idle = idle + iowait;

    float pct = 0;
    if (s_prev_total > 0) {
        long long dt = total - s_prev_total;
        long long di = total_idle - s_prev_idle;
        if (dt > 0) pct = (1.0f - (float)di / (float)dt) * 100.0f;
    }

    s_prev_total = total;
    s_prev_idle = total_idle;
    return pct;
}

static float s_ram_used_gb, s_ram_total_gb;

static float read_ram(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return 0;

    long total = 0, available = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "MemTotal:", 9) == 0)
            sscanf(line + 9, "%ld", &total);
        else if (strncmp(line, "MemAvailable:", 13) == 0)
            sscanf(line + 13, "%ld", &available);
    }
    fclose(f);

    if (total <= 0) return 0;
    s_ram_total_gb = total / 1048576.0f;
    s_ram_used_gb = (total - available) / 1048576.0f;
    return ((total - available) / (float)total) * 100.0f;
}

static float s_disk_used_gb, s_disk_total_gb;

static float read_disk(void) {
    struct statvfs st;
    if (statvfs("/", &st) != 0) return 0;

    double total = (double)st.f_blocks * st.f_frsize;
    double free_b = (double)st.f_bfree * st.f_frsize;
    double used = total - free_b;

    s_disk_total_gb = total / (1024.0 * 1024.0 * 1024.0);
    s_disk_used_gb = used / (1024.0 * 1024.0 * 1024.0);

    if (total <= 0) return 0;
    return (used / total) * 100.0f;
}

static void push_history(float *hist, float val) {
    memmove(hist, hist + 1, (HISTORY_LEN - 1) * sizeof(float));
    hist[HISTORY_LEN - 1] = val;
}

static void draw_graph(GtkWidget *widget, cairo_t *cr, float *hist,
                       double r, double g, double b) {
    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);

    /* Background */
    cairo_set_source_rgb(cr, 0.067, 0.067, 0.106); /* CAT_CRUST */
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_fill(cr);

    /* Grid lines */
    cairo_set_source_rgba(cr, 0.192, 0.196, 0.267, 0.5); /* CAT_SURFACE0 */
    cairo_set_line_width(cr, 0.5);
    for (int i = 1; i < 4; i++) {
        double y = h * i / 4.0;
        cairo_move_to(cr, 0, y);
        cairo_line_to(cr, w, y);
        cairo_stroke(cr);
    }
    for (int i = 1; i < 6; i++) {
        double x = w * i / 6.0;
        cairo_move_to(cr, x, 0);
        cairo_line_to(cr, x, h);
        cairo_stroke(cr);
    }

    int points = s_hist_count < HISTORY_LEN ? s_hist_count : HISTORY_LEN;
    if (points < 2) return;

    double step = (double)w / (HISTORY_LEN - 1);
    int start = HISTORY_LEN - points;

    /* Filled area */
    cairo_move_to(cr, (start) * step, h);
    for (int i = start; i < HISTORY_LEN; i++) {
        double x = (i) * step;
        double y = h - (hist[i] / 100.0) * h;
        if (y < 2) y = 2;
        cairo_line_to(cr, x, y);
    }
    cairo_line_to(cr, (HISTORY_LEN - 1) * step, h);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, r, g, b, 0.2);
    cairo_fill_preserve(cr);

    /* Line */
    cairo_set_source_rgba(cr, r, g, b, 0.9);
    cairo_set_line_width(cr, 2.0);
    cairo_stroke(cr);

    /* Redraw line on top */
    cairo_set_source_rgba(cr, r, g, b, 0.9);
    cairo_set_line_width(cr, 2.0);
    for (int i = start; i < HISTORY_LEN; i++) {
        double x = (i) * step;
        double y = h - (hist[i] / 100.0) * h;
        if (y < 2) y = 2;
        if (i == start) cairo_move_to(cr, x, y);
        else cairo_line_to(cr, x, y);
    }
    cairo_stroke(cr);
}

static gboolean draw_cpu(GtkWidget *w, cairo_t *cr, gpointer data) {
    (void)data;
    draw_graph(w, cr, s_cpu_hist, 0.537, 0.706, 0.980); /* CAT_BLUE */
    return FALSE;
}

static gboolean draw_ram(GtkWidget *w, cairo_t *cr, gpointer data) {
    (void)data;
    draw_graph(w, cr, s_ram_hist, 0.651, 0.890, 0.631); /* CAT_GREEN */
    return FALSE;
}

static gboolean draw_disk(GtkWidget *w, cairo_t *cr, gpointer data) {
    (void)data;
    int width = gtk_widget_get_allocated_width(w);
    int height = gtk_widget_get_allocated_height(w);

    float pct = read_disk();

    /* Background */
    cairo_set_source_rgb(cr, 0.067, 0.067, 0.106);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_fill(cr);

    /* Bar background */
    double pad = 4, bar_h = height - pad * 2;
    double radius = bar_h / 2;
    cairo_new_sub_path(cr);
    cairo_arc(cr, pad + radius, pad + radius, radius, G_PI/2, 3*G_PI/2);
    cairo_arc(cr, width - pad - radius, pad + radius, radius, -G_PI/2, G_PI/2);
    cairo_close_path(cr);
    cairo_set_source_rgb(cr, 0.192, 0.196, 0.267);
    cairo_fill(cr);

    /* Bar fill */
    double fill_w = (width - pad * 2) * (pct / 100.0);
    if (fill_w < bar_h) fill_w = bar_h;
    cairo_new_sub_path(cr);
    cairo_arc(cr, pad + radius, pad + radius, radius, G_PI/2, 3*G_PI/2);
    cairo_arc(cr, pad + fill_w - radius, pad + radius, radius, -G_PI/2, G_PI/2);
    cairo_close_path(cr);

    /* Color: green < 70, yellow < 90, red >= 90 */
    if (pct < 70)
        cairo_set_source_rgb(cr, 0.651, 0.890, 0.631);
    else if (pct < 90)
        cairo_set_source_rgb(cr, 0.976, 0.886, 0.435);
    else
        cairo_set_source_rgb(cr, 0.953, 0.545, 0.659);
    cairo_fill(cr);

    return FALSE;
}

void sysstat_refresh(void) {
    float cpu = read_cpu();
    float ram = read_ram();

    push_history(s_cpu_hist, cpu);
    push_history(s_ram_hist, ram);
    if (s_hist_count < HISTORY_LEN) s_hist_count++;

    char buf[256];

    snprintf(buf, sizeof(buf),
        "<span font='12' foreground='" CAT_BLUE "'>CPU</span>"
        "  <span font='12' foreground='" CAT_TEXT "'>%.1f%%</span>", cpu);
    gtk_label_set_markup(GTK_LABEL(s_cpu_label), buf);

    snprintf(buf, sizeof(buf),
        "<span font='12' foreground='" CAT_GREEN "'>RAM</span>"
        "  <span font='12' foreground='" CAT_TEXT "'>%.1f / %.1f GB (%.0f%%)</span>",
        s_ram_used_gb, s_ram_total_gb, ram);
    gtk_label_set_markup(GTK_LABEL(s_ram_label), buf);

    float disk_pct = read_disk();
    snprintf(buf, sizeof(buf),
        "<span font='12' foreground='" CAT_PEACH "'>Disk /</span>"
        "  <span font='12' foreground='" CAT_TEXT "'>%.1f / %.1f GB (%.0f%%)</span>",
        s_disk_used_gb, s_disk_total_gb, disk_pct);
    gtk_label_set_markup(GTK_LABEL(s_disk_label), buf);

    gtk_widget_queue_draw(s_cpu_draw);
    gtk_widget_queue_draw(s_ram_draw);
    gtk_widget_queue_draw(s_disk_draw);
}

GtkWidget *sysstat_create(void) {
    memset(s_cpu_hist, 0, sizeof(s_cpu_hist));
    memset(s_ram_hist, 0, sizeof(s_ram_hist));
    s_hist_count = 0;
    s_prev_total = 0;
    s_prev_idle = 0;

    /* Prime the CPU reader (first call returns 0) */
    read_cpu();

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 16);

    GtkWidget *title = make_label(
        "<span font='14' weight='bold' foreground='" CAT_BLUE "'>System Stats</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *subtitle = make_label(
        "<span font='11' foreground='" CAT_SUBTEXT0 "'>Live CPU, RAM, and disk usage</span>");
    gtk_box_pack_start(GTK_BOX(vbox), subtitle, FALSE, FALSE, 0);

    /* CPU */
    s_cpu_label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(s_cpu_label), 0);
    gtk_box_pack_start(GTK_BOX(vbox), s_cpu_label, FALSE, FALSE, 4);

    s_cpu_draw = gtk_drawing_area_new();
    gtk_widget_set_size_request(s_cpu_draw, -1, 120);
    g_signal_connect(s_cpu_draw, "draw", G_CALLBACK(draw_cpu), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), s_cpu_draw, FALSE, FALSE, 0);

    /* RAM */
    s_ram_label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(s_ram_label), 0);
    gtk_box_pack_start(GTK_BOX(vbox), s_ram_label, FALSE, FALSE, 4);

    s_ram_draw = gtk_drawing_area_new();
    gtk_widget_set_size_request(s_ram_draw, -1, 120);
    g_signal_connect(s_ram_draw, "draw", G_CALLBACK(draw_ram), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), s_ram_draw, FALSE, FALSE, 0);

    /* Disk */
    s_disk_label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(s_disk_label), 0);
    gtk_box_pack_start(GTK_BOX(vbox), s_disk_label, FALSE, FALSE, 4);

    s_disk_draw = gtk_drawing_area_new();
    gtk_widget_set_size_request(s_disk_draw, -1, 30);
    g_signal_connect(s_disk_draw, "draw", G_CALLBACK(draw_disk), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), s_disk_draw, FALSE, FALSE, 0);

    /* Initial refresh */
    sysstat_refresh();

    return vbox;
}

void sysstat_cleanup(void) {}
