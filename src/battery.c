#include "battery.h"
#include "theme.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define BAT_PATH "/sys/class/power_supply/BAT0/"

static GtkWidget *s_draw;
static GtkWidget *s_pct_label;
static GtkWidget *s_status_label;
static GtkWidget *s_rate_label;
static GtkWidget *s_health_label;
static GtkWidget *s_time_label;
static GtkWidget *s_temp_label;
static int s_pct;

static int read_int(const char *file) {
    char path[256];
    snprintf(path, sizeof(path), BAT_PATH "%s", file);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int val;
    if (fscanf(f, "%d", &val) != 1) val = -1;
    fclose(f);
    return val;
}

static void read_str(const char *file, char *buf, size_t len) {
    char path[256];
    snprintf(path, sizeof(path), BAT_PATH "%s", file);
    FILE *f = fopen(path, "r");
    if (!f) { snprintf(buf, len, "unknown"); return; }
    if (!fgets(buf, len, f)) snprintf(buf, len, "unknown");
    fclose(f);
    buf[strcspn(buf, "\n")] = '\0';
}

static void get_color(int pct, double *r, double *g, double *b) {
    if (pct >= 80)      { *r = 0.65; *g = 0.89; *b = 0.63; }
    else if (pct >= 50) { *r = 0.54; *g = 0.71; *b = 0.98; }
    else if (pct >= 20) { *r = 0.98; *g = 0.89; *b = 0.43; }
    else                { *r = 0.95; *g = 0.55; *b = 0.66; }
}

static gboolean draw_battery(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)data;
    int pct = s_pct;
    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);

    double bx = w * 0.15, by = h * 0.08;
    double bw = w * 0.65, bh = h * 0.84;
    double radius = 14.0;

    /* Battery outline */
    cairo_new_sub_path(cr);
    cairo_arc(cr, bx + bw - radius, by + radius, radius, -G_PI/2, 0);
    cairo_arc(cr, bx + bw - radius, by + bh - radius, radius, 0, G_PI/2);
    cairo_arc(cr, bx + radius, by + bh - radius, radius, G_PI/2, G_PI);
    cairo_arc(cr, bx + radius, by + radius, radius, G_PI, 3*G_PI/2);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, 0.85, 0.85, 0.85, 0.6);
    cairo_set_line_width(cr, 3.0);
    cairo_stroke(cr);

    /* Battery tip */
    double tx = bx + bw, ty = by + bh * 0.3;
    double tw = w * 0.06, th = bh * 0.4;
    cairo_new_sub_path(cr);
    cairo_arc(cr, tx + tw - 5, ty + 5, 5, -G_PI/2, 0);
    cairo_arc(cr, tx + tw - 5, ty + th - 5, 5, 0, G_PI/2);
    cairo_line_to(cr, tx, ty + th);
    cairo_line_to(cr, tx, ty);
    cairo_close_path(cr);
    cairo_set_source_rgba(cr, 0.85, 0.85, 0.85, 0.6);
    cairo_fill(cr);

    /* Fill */
    double pad = 6.0;
    double fill_max_w = bw - pad * 2;
    double fill_w = fill_max_w * (pct / 100.0);
    if (fill_w < 16) fill_w = 16;
    double fill_h = bh - pad * 2;

    double r, g, b;
    get_color(pct, &r, &g, &b);

    cairo_pattern_t *pat = cairo_pattern_create_linear(bx + pad, 0, bx + pad + fill_w, 0);
    cairo_pattern_add_color_stop_rgba(pat, 0.0, r, g, b, 0.6);
    cairo_pattern_add_color_stop_rgba(pat, 1.0, r, g, b, 1.0);

    double fx = bx + pad, fy = by + pad, fr = 10.0;
    cairo_new_sub_path(cr);
    cairo_arc(cr, fx + fill_w - fr, fy + fr, fr, -G_PI/2, 0);
    cairo_arc(cr, fx + fill_w - fr, fy + fill_h - fr, fr, 0, G_PI/2);
    cairo_arc(cr, fx + fr, fy + fill_h - fr, fr, G_PI/2, G_PI);
    cairo_arc(cr, fx + fr, fy + fr, fr, G_PI, 3*G_PI/2);
    cairo_close_path(cr);
    cairo_set_source(cr, pat);
    cairo_fill(cr);
    cairo_pattern_destroy(pat);

    /* Percentage text on battery */
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 38);
    char txt[8];
    snprintf(txt, sizeof(txt), "%d%%", pct);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, txt, &ext);
    cairo_move_to(cr, bx + (bw - ext.width) / 2 - ext.x_bearing,
                      by + (bh - ext.height) / 2 - ext.y_bearing);
    cairo_show_text(cr, txt);

    return FALSE;
}

void battery_refresh(void) {
    int pct = read_int("capacity");
    int current = read_int("current_now");
    int voltage = read_int("voltage_now");
    int charge_now = read_int("charge_now");
    int charge_full = read_int("charge_full");
    int charge_full_design = read_int("charge_full_design");

    char status[32];
    read_str("status", status, sizeof(status));

    if (pct < 0) return;
    s_pct = pct;

    double watts = (current / 1e6) * (voltage / 1e6);
    double health = charge_full_design > 0
        ? (charge_full / (double)charge_full_design) * 100.0 : 0;

    /* Time estimate */
    char time_str[64];
    if (strcmp(status, "Discharging") == 0 && current > 0) {
        double hours = charge_now / (double)current;
        int h = (int)hours;
        int m = (int)((hours - h) * 60);
        snprintf(time_str, sizeof(time_str), "%dh %dm remaining", h, m);
    } else if (strcmp(status, "Charging") == 0 && current > 0) {
        double hours = (charge_full - charge_now) / (double)current;
        int h = (int)hours;
        int m = (int)((hours - h) * 60);
        snprintf(time_str, sizeof(time_str), "%dh %dm to full", h, m);
    } else {
        snprintf(time_str, sizeof(time_str), "--");
    }

    /* Temperature */
    char temp_str[32] = "--";
    char *temp_out = run_cmd(
        "cat /sys/class/thermal/thermal_zone*/temp 2>/dev/null | head -1");
    if (temp_out && temp_out[0]) {
        double temp = atoi(temp_out) / 1000.0;
        snprintf(temp_str, sizeof(temp_str), "%.1f C", temp);
    }
    free(temp_out);

    /* Color for status */
    double r, g, b;
    get_color(pct, &r, &g, &b);
    char color_hex[8];
    snprintf(color_hex, sizeof(color_hex), "#%02x%02x%02x",
             (int)(r*255), (int)(g*255), (int)(b*255));

    char buf[256];

    snprintf(buf, sizeof(buf),
        "<span font='42' weight='bold' foreground='%s'>%d%%</span>", color_hex, pct);
    gtk_label_set_markup(GTK_LABEL(s_pct_label), buf);

    const char *status_icon = strcmp(status, "Charging") == 0 ? "+++" :
                              strcmp(status, "Discharging") == 0 ? "---" : " = ";
    snprintf(buf, sizeof(buf),
        "<span font='13' foreground='%s'>%s  %s</span>", color_hex, status_icon, status);
    gtk_label_set_markup(GTK_LABEL(s_status_label), buf);

    snprintf(buf, sizeof(buf),
        "<span font='13' foreground='" CAT_TEXT "'>%.1f W</span>", watts);
    gtk_label_set_markup(GTK_LABEL(s_rate_label), buf);

    snprintf(buf, sizeof(buf),
        "<span font='13' foreground='" CAT_TEXT "'>%.1f%%</span>", health);
    gtk_label_set_markup(GTK_LABEL(s_health_label), buf);

    snprintf(buf, sizeof(buf),
        "<span font='13' foreground='" CAT_TEXT "'>%s</span>", time_str);
    gtk_label_set_markup(GTK_LABEL(s_time_label), buf);

    snprintf(buf, sizeof(buf),
        "<span font='13' foreground='" CAT_TEXT "'>%s</span>", temp_str);
    gtk_label_set_markup(GTK_LABEL(s_temp_label), buf);

    gtk_widget_queue_draw(s_draw);
}

GtkWidget *battery_create(void) {
    s_pct = read_int("capacity");
    if (s_pct < 0) s_pct = 0;

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 16);

    GtkWidget *title = make_label(
        "<span font='14' weight='bold' foreground='" CAT_BLUE "'>Battery Monitor</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *subtitle = make_label(
        "<span font='11' foreground='" CAT_SUBTEXT0 "'>Live battery stats — updates every 3s</span>");
    gtk_box_pack_start(GTK_BOX(vbox), subtitle, FALSE, FALSE, 0);

    /* Battery drawing */
    s_draw = gtk_drawing_area_new();
    gtk_widget_set_size_request(s_draw, 360, 130);
    g_signal_connect(s_draw, "draw", G_CALLBACK(draw_battery), NULL);
    gtk_box_pack_start(GTK_BOX(vbox), s_draw, FALSE, FALSE, 8);

    /* Big percentage */
    s_pct_label = gtk_label_new(NULL);
    gtk_box_pack_start(GTK_BOX(vbox), s_pct_label, FALSE, FALSE, 0);

    /* Status */
    s_status_label = gtk_label_new(NULL);
    gtk_box_pack_start(GTK_BOX(vbox), s_status_label, FALSE, FALSE, 2);

    gtk_box_pack_start(GTK_BOX(vbox),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 8);

    /* Stats grid */
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 24);
    gtk_widget_set_halign(grid, GTK_ALIGN_CENTER);

    const char *labels[] = {"Power", "Health", "Time", "Temp"};
    const char *label_colors[] = {CAT_YELLOW, CAT_GREEN, CAT_BLUE, CAT_PEACH};
    GtkWidget **value_labels[] = {&s_rate_label, &s_health_label, &s_time_label, &s_temp_label};

    for (int i = 0; i < 4; i++) {
        char m[128];
        snprintf(m, sizeof(m),
            "<span font='13' foreground='%s'>%s</span>", label_colors[i], labels[i]);
        GtkWidget *lbl = make_label(m);
        *value_labels[i] = gtk_label_new(NULL);
        gtk_label_set_xalign(GTK_LABEL(*value_labels[i]), 0);
        gtk_grid_attach(GTK_GRID(grid), lbl, 0, i, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), *value_labels[i], 1, i, 1, 1);
    }

    gtk_box_pack_start(GTK_BOX(vbox), grid, FALSE, FALSE, 0);

    battery_refresh();
    return vbox;
}

void battery_cleanup(void) {}
