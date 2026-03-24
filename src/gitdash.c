#include "gitdash.h"
#include "theme.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PROJECTS 32
#define CONF_NAME "gitdash.conf"

static GtkWidget *s_container;
static GtkWidget *s_subtitle;
static char s_conf_path[512];

typedef struct {
    char path[512];
    char name[128];
} Project;

static Project s_projects[MAX_PROJECTS];
static int s_nprojects;

static void do_refresh(void);

static void load_config(void) {
    const char *home = g_get_home_dir();
    snprintf(s_conf_path, sizeof(s_conf_path), "%s/.config/devdash/%s", home, CONF_NAME);

    FILE *f = fopen(s_conf_path, "r");
    if (!f) {
        /* Create default with home and common dirs */
        char dir[256];
        snprintf(dir, sizeof(dir), "%s/.config/devdash", home);
        g_mkdir_with_parents(dir, 0755);
        f = fopen(s_conf_path, "w");
        if (f) {
            fprintf(f, "%s/projects/devdash\n", home);
            fprintf(f, "%s/projects/battmon\n", home);
            fclose(f);
        }
        f = fopen(s_conf_path, "r");
        if (!f) return;
    }

    s_nprojects = 0;
    char line[512];
    while (fgets(line, sizeof(line), f) && s_nprojects < MAX_PROJECTS) {
        trim_inplace(line);
        if (!line[0] || line[0] == '#') continue;
        strncpy(s_projects[s_nprojects].path, line, sizeof(s_projects[0].path) - 1);

        /* Extract project name */
        const char *slash = strrchr(line, '/');
        strncpy(s_projects[s_nprojects].name, slash ? slash + 1 : line,
                sizeof(s_projects[0].name) - 1);
        s_nprojects++;
    }
    fclose(f);
}

static void save_config(void) {
    FILE *f = fopen(s_conf_path, "w");
    if (!f) return;
    for (int i = 0; i < s_nprojects; i++)
        fprintf(f, "%s\n", s_projects[i].path);
    fclose(f);
}

static void update_subtitle(void) {
    if (!s_subtitle) return;
    char *sub = markup_fmt(
        "<span font='11' foreground='" CAT_SUBTEXT0 "'>Tracking %d project%s — edit ~/.config/devdash/%s</span>",
        s_nprojects, s_nprojects == 1 ? "" : "s", CONF_NAME);
    gtk_label_set_markup(GTK_LABEL(s_subtitle), sub);
    free(sub);
}

static void on_remove_clicked(GtkButton *btn, gpointer data) {
    (void)btn;
    int idx = GPOINTER_TO_INT(data);
    if (idx < 0 || idx >= s_nprojects) return;

    /* Shift remaining projects down */
    for (int i = idx; i < s_nprojects - 1; i++)
        s_projects[i] = s_projects[i + 1];
    s_nprojects--;

    save_config();
    update_subtitle();
    do_refresh();
}

static void on_add_clicked(GtkButton *btn, gpointer data) {
    (void)data;
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Select Git Project Folder",
        GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(btn))),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Add", GTK_RESPONSE_ACCEPT,
        NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (folder && s_nprojects < MAX_PROJECTS) {
            /* Check for duplicates */
            int dup = 0;
            for (int i = 0; i < s_nprojects; i++) {
                if (strcmp(s_projects[i].path, folder) == 0) { dup = 1; break; }
            }
            if (!dup) {
                strncpy(s_projects[s_nprojects].path, folder,
                        sizeof(s_projects[0].path) - 1);
                const char *slash = strrchr(folder, '/');
                strncpy(s_projects[s_nprojects].name, slash ? slash + 1 : folder,
                        sizeof(s_projects[0].name) - 1);
                s_nprojects++;
                save_config();
                update_subtitle();
                do_refresh();
            }
        }
        g_free(folder);
    }
    gtk_widget_destroy(dialog);
}

static GtkWidget *build_project_card(Project *p, int idx) {
    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_widget_set_name(frame, "project-card");

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    gtk_container_add(GTK_CONTAINER(frame), vbox);

    /* Header row with remove button */
    GtkWidget *hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(vbox), hdr, FALSE, FALSE, 0);

    /* Branch */
    char cmd[600];
    snprintf(cmd, sizeof(cmd), "git -C '%s' rev-parse --abbrev-ref HEAD 2>/dev/null", p->path);
    char *branch = run_cmd(cmd);
    trim_inplace(branch);
    if (!branch || !branch[0]) {
        /* Not a git repo */
        char *m = markup_fmt(
            "<span font='13' weight='bold' foreground='" CAT_TEXT "'>%s</span>"
            "  <span font='11' foreground='" CAT_OVERLAY2 "'>not a git repo</span>",
            p->name);
        gtk_box_pack_start(GTK_BOX(hdr), make_label(m), TRUE, TRUE, 0);
        free(m);
        free(branch);

        GtkWidget *rm_btn = gtk_button_new_with_label("Remove");
        gtk_widget_set_name(rm_btn, "remove-btn");
        g_signal_connect(rm_btn, "clicked", G_CALLBACK(on_remove_clicked),
                         GINT_TO_POINTER(idx));
        gtk_box_pack_end(GTK_BOX(hdr), rm_btn, FALSE, FALSE, 0);

        return frame;
    }

    /* Status (dirty/clean) */
    snprintf(cmd, sizeof(cmd), "git -C '%s' status --porcelain 2>/dev/null | wc -l", p->path);
    char *changes_str = run_cmd(cmd);
    trim_inplace(changes_str);
    int changes = changes_str ? atoi(changes_str) : 0;
    free(changes_str);

    const char *dot_color = changes > 0 ? CAT_YELLOW : CAT_GREEN;
    const char *status_text = changes > 0 ? "dirty" : "clean";

    char changes_info[32] = "";
    if (changes > 0) snprintf(changes_info, sizeof(changes_info), ", %d changed", changes);

    char *header = markup_fmt(
        "<span font='13' weight='bold' foreground='" CAT_TEXT "'>%s</span>"
        "  <span font='12' foreground='" CAT_BLUE "'>%s</span>"
        "  <span font='11' foreground='%s'>%s%s</span>",
        p->name, branch, dot_color, status_text, changes_info);
    gtk_box_pack_start(GTK_BOX(hdr), make_label(header), TRUE, TRUE, 0);
    free(header);
    free(branch);

    GtkWidget *rm_btn = gtk_button_new_with_label("Remove");
    gtk_widget_set_name(rm_btn, "remove-btn");
    g_signal_connect(rm_btn, "clicked", G_CALLBACK(on_remove_clicked),
                     GINT_TO_POINTER(idx));
    gtk_box_pack_end(GTK_BOX(hdr), rm_btn, FALSE, FALSE, 0);

    /* Recent commits */
    snprintf(cmd, sizeof(cmd),
        "git -C '%s' log --oneline -5 --format='%%h %%s (%%cr)' 2>/dev/null", p->path);
    char *log = run_cmd(cmd);
    if (log && log[0]) {
        char *line = strtok(log, "\n");
        while (line) {
            /* Escape markup characters */
            char *escaped = g_markup_escape_text(line, -1);
            char *m = markup_fmt(
                "<span font='10' foreground='" CAT_SUBTEXT0 "'>  %s</span>", escaped);
            gtk_box_pack_start(GTK_BOX(vbox), make_label(m), FALSE, FALSE, 0);
            free(m);
            g_free(escaped);
            line = strtok(NULL, "\n");
        }
    }
    free(log);

    return frame;
}

static void do_refresh(void) {
    /* Clear container */
    GList *children = gtk_container_get_children(GTK_CONTAINER(s_container));
    for (GList *l = children; l; l = l->next)
        gtk_widget_destroy(l->data);
    g_list_free(children);

    for (int i = 0; i < s_nprojects; i++) {
        GtkWidget *card = build_project_card(&s_projects[i], i);
        gtk_box_pack_start(GTK_BOX(s_container), card, FALSE, FALSE, 4);
    }
    gtk_widget_show_all(s_container);
}

GtkWidget *gitdash_create(void) {
    load_config();

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);

    GtkWidget *title = make_label(
        "<span font='14' weight='bold' foreground='" CAT_BLUE "'>Git Dashboard</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    char *sub = markup_fmt(
        "<span font='11' foreground='" CAT_SUBTEXT0 "'>Tracking %d project%s — edit ~/.config/devdash/%s</span>",
        s_nprojects, s_nprojects == 1 ? "" : "s", CONF_NAME);
    s_subtitle = make_label(sub);
    gtk_box_pack_start(GTK_BOX(vbox), s_subtitle, FALSE, FALSE, 0);
    free(sub);

    s_container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *sw = make_scrolled(s_container);
    gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 4);

    /* Action buttons */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(hbox, GTK_ALIGN_END);

    GtkWidget *add_btn = gtk_button_new_with_label("Add Project");
    gtk_widget_set_name(add_btn, "action-btn");
    gtk_style_context_add_class(gtk_widget_get_style_context(add_btn), "action-btn");
    g_signal_connect(add_btn, "clicked", G_CALLBACK(on_add_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), add_btn, FALSE, FALSE, 0);

    GtkWidget *btn = gtk_button_new_with_label("Refresh");
    gtk_widget_set_name(btn, "action-btn");
    gtk_style_context_add_class(gtk_widget_get_style_context(btn), "action-btn");
    g_signal_connect_swapped(btn, "clicked", G_CALLBACK(do_refresh), NULL);
    gtk_box_pack_start(GTK_BOX(hbox), btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    do_refresh();
    return vbox;
}

void gitdash_refresh(void) { do_refresh(); }
void gitdash_cleanup(void) {}
