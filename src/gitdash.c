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
static char s_editor[128];

typedef struct {
    char path[512];
    char name[128];
} Project;

static Project s_projects[MAX_PROJECTS];
static int s_nprojects;

static void do_refresh(void);

/* ── editor / terminal detection ────────────────────────────────── */

static void detect_editor(void) {
    static const char *candidates[] = {
        "code", "codium", "zed", "subl", "atom", "kate", "gedit", "nvim", "vim", NULL
    };
    for (int i = 0; candidates[i]; i++) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "which %s 2>/dev/null", candidates[i]);
        char *out = run_cmd(cmd);
        if (out && out[0]) {
            trim_inplace(out);
            strncpy(s_editor, candidates[i], sizeof(s_editor) - 1);
            free(out);
            return;
        }
        free(out);
    }
    s_editor[0] = '\0';
}

static void launch_terminal(const char *path) {
    static const char *terms[] = {
        "x-terminal-emulator", "gnome-terminal", "konsole",
        "xfce4-terminal", "alacritty", "kitty", "xterm", NULL
    };
    for (int i = 0; terms[i]; i++) {
        char check[128];
        snprintf(check, sizeof(check), "which %s 2>/dev/null", terms[i]);
        char *out = run_cmd(check);
        if (out && out[0]) {
            free(out);
            char cmd[700];
            if (strcmp(terms[i], "gnome-terminal") == 0)
                snprintf(cmd, sizeof(cmd), "gnome-terminal --working-directory='%s' &", path);
            else if (strcmp(terms[i], "konsole") == 0)
                snprintf(cmd, sizeof(cmd), "konsole --workdir '%s' &", path);
            else
                snprintf(cmd, sizeof(cmd), "cd '%s' && %s &", path, terms[i]);
            int ret = system(cmd);
            (void)ret;
            return;
        }
        free(out);
    }
}

/* ── config ─────────────────────────────────────────────────────── */

static void load_config(void) {
    const char *home = g_get_home_dir();
    snprintf(s_conf_path, sizeof(s_conf_path), "%s/.config/devdash/%s", home, CONF_NAME);

    FILE *f = fopen(s_conf_path, "r");
    if (!f) {
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

/* ── add / remove callbacks ─────────────────────────────────────── */

static void on_remove_clicked(GtkButton *btn, gpointer data) {
    (void)btn;
    int idx = GPOINTER_TO_INT(data);
    if (idx < 0 || idx >= s_nprojects) return;
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
            int dup = 0;
            for (int i = 0; i < s_nprojects; i++)
                if (strcmp(s_projects[i].path, folder) == 0) { dup = 1; break; }
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

/* ── Git Workbench Dialog ───────────────────────────────────────── */

typedef struct {
    char path[512];
    GtkWidget *dialog;
    GtkWidget *output;          /* GtkTextView */
    GtkWidget *status_label;
    GtkWidget *branch_combo;
    GtkWidget *commit_entry;    /* commit message entry */
} Workbench;

/* Append text to the workbench output view */
static void wb_append(Workbench *wb, const char *prefix, const char *text) {
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(wb->output));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buf, &end);

    if (prefix) {
        gtk_text_buffer_insert_with_tags_by_name(buf, &end, prefix, -1, "cmd-tag", NULL);
        gtk_text_buffer_get_end_iter(buf, &end);
        gtk_text_buffer_insert(buf, &end, "\n", -1);
        gtk_text_buffer_get_end_iter(buf, &end);
    }
    if (text && text[0]) {
        gtk_text_buffer_insert(buf, &end, text, -1);
        gtk_text_buffer_get_end_iter(buf, &end);
        if (text[strlen(text) - 1] != '\n')
            gtk_text_buffer_insert(buf, &end, "\n", -1);
    }
    /* Scroll to bottom */
    gtk_text_buffer_get_end_iter(buf, &end);
    GtkTextMark *mark = gtk_text_buffer_get_mark(buf, "end-mark");
    if (!mark)
        mark = gtk_text_buffer_create_mark(buf, "end-mark", &end, FALSE);
    else
        gtk_text_buffer_move_mark(buf, mark, &end);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(wb->output), mark);
}

/* Run a git command, display output, return success */
static int wb_run(Workbench *wb, const char *git_args) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "git -C '%s' %s 2>&1", wb->path, git_args);
    wb_append(wb, cmd, NULL);
    char *out = run_cmd(cmd);
    wb_append(wb, NULL, out && out[0] ? out : "(no output)");
    int ok = (out != NULL);
    free(out);
    wb_append(wb, NULL, "");
    return ok;
}

/* Refresh the status label and branch combo inside the workbench */
static void wb_refresh_status(Workbench *wb) {
    char cmd[700];

    /* Current branch */
    snprintf(cmd, sizeof(cmd),
        "git -C '%s' rev-parse --abbrev-ref HEAD 2>/dev/null", wb->path);
    char *branch = run_cmd(cmd);
    trim_inplace(branch);

    /* Status summary */
    snprintf(cmd, sizeof(cmd),
        "git -C '%s' status --porcelain 2>/dev/null | wc -l", wb->path);
    char *cnt = run_cmd(cmd);
    trim_inplace(cnt);
    int changes = cnt ? atoi(cnt) : 0;
    free(cnt);

    /* Ahead/behind */
    snprintf(cmd, sizeof(cmd),
        "git -C '%s' rev-list --left-right --count HEAD...@{upstream} 2>/dev/null", wb->path);
    char *ab = run_cmd(cmd);
    trim_inplace(ab);
    int ahead = 0, behind = 0;
    if (ab && ab[0]) sscanf(ab, "%d\t%d", &ahead, &behind);
    free(ab);

    /* Stash count */
    snprintf(cmd, sizeof(cmd),
        "git -C '%s' stash list 2>/dev/null | wc -l", wb->path);
    char *sc = run_cmd(cmd);
    trim_inplace(sc);
    int stashes = sc ? atoi(sc) : 0;
    free(sc);

    GString *s = g_string_new("");
    g_string_append_printf(s,
        "<span font='12' weight='bold' foreground='" CAT_BLUE "'>%s</span>",
        branch && branch[0] ? branch : "???");
    g_string_append_printf(s,
        "  <span font='11' foreground='%s'>%s</span>",
        changes > 0 ? CAT_YELLOW : CAT_GREEN,
        changes > 0 ? "dirty" : "clean");
    if (changes > 0)
        g_string_append_printf(s,
            " <span font='11' foreground='" CAT_YELLOW "'>(%d changed)</span>", changes);
    if (ahead > 0)
        g_string_append_printf(s,
            "  <span font='11' foreground='" CAT_GREEN "'>↑%d</span>", ahead);
    if (behind > 0)
        g_string_append_printf(s,
            "  <span font='11' foreground='" CAT_RED "'>↓%d</span>", behind);
    if (stashes > 0)
        g_string_append_printf(s,
            "  <span font='11' foreground='" CAT_OVERLAY2 "'>%d stash%s</span>",
            stashes, stashes == 1 ? "" : "es");

    gtk_label_set_markup(GTK_LABEL(wb->status_label), s->str);
    g_string_free(s, TRUE);
    free(branch);

    /* Refresh branch combo */
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(wb->branch_combo));
    snprintf(cmd, sizeof(cmd),
        "git -C '%s' branch --format='%%(refname:short)' 2>/dev/null", wb->path);
    char *branches = run_cmd(cmd);
    if (branches && branches[0]) {
        char *b = strtok(branches, "\n");
        while (b) {
            trim_inplace(b);
            if (b[0]) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb->branch_combo), b);
            b = strtok(NULL, "\n");
        }
    }
    free(branches);
    gtk_combo_box_set_active(GTK_COMBO_BOX(wb->branch_combo), 0);
}

/* Workbench action button callbacks */
static void wb_on_fetch(GtkButton *b, gpointer data) {
    (void)b;
    Workbench *wb = data;
    wb_run(wb, "fetch --all --prune");
    wb_refresh_status(wb);
}

static void wb_on_pull_merge(GtkButton *b, gpointer data) {
    (void)b;
    Workbench *wb = data;
    wb_run(wb, "pull --ff-only");
    wb_refresh_status(wb);
}

static void wb_on_pull_rebase(GtkButton *b, gpointer data) {
    (void)b;
    Workbench *wb = data;
    wb_run(wb, "pull --rebase");
    wb_refresh_status(wb);
}

static void wb_on_push(GtkButton *b, gpointer data) {
    (void)b;
    Workbench *wb = data;
    wb_run(wb, "push");
    wb_refresh_status(wb);
}

static void wb_on_push_force(GtkButton *b, gpointer data) {
    (void)b;
    Workbench *wb = data;
    wb_run(wb, "push --force-with-lease");
    wb_refresh_status(wb);
}

static void wb_on_status(GtkButton *b, gpointer data) {
    (void)b;
    Workbench *wb = data;
    wb_run(wb, "status");
}

static void wb_on_log(GtkButton *b, gpointer data) {
    (void)b;
    Workbench *wb = data;
    wb_run(wb, "log --oneline -15 --format='%h %s (%cr)'");
}

static void wb_on_diff(GtkButton *b, gpointer data) {
    (void)b;
    Workbench *wb = data;
    wb_run(wb, "diff --stat");
}

static void wb_on_stash(GtkButton *b, gpointer data) {
    (void)b;
    Workbench *wb = data;
    wb_run(wb, "stash");
    wb_refresh_status(wb);
}

static void wb_on_stash_pop(GtkButton *b, gpointer data) {
    (void)b;
    Workbench *wb = data;
    wb_run(wb, "stash pop");
    wb_refresh_status(wb);
}

static void wb_on_stash_list(GtkButton *b, gpointer data) {
    (void)b;
    Workbench *wb = data;
    wb_run(wb, "stash list");
}

static void wb_on_stage_all(GtkButton *b, gpointer data) {
    (void)b;
    Workbench *wb = data;
    wb_run(wb, "add -A");
    wb_refresh_status(wb);
}

static void wb_on_unstage_all(GtkButton *b, gpointer data) {
    (void)b;
    Workbench *wb = data;
    wb_run(wb, "reset HEAD");
    wb_refresh_status(wb);
}

static void wb_on_commit(GtkButton *b, gpointer data) {
    (void)b;
    Workbench *wb = data;
    const char *msg = gtk_entry_get_text(GTK_ENTRY(wb->commit_entry));
    if (!msg || !msg[0]) {
        wb_append(wb, NULL, "Error: commit message cannot be empty");
        return;
    }
    /* Escape single quotes in message */
    GString *escaped = g_string_new("");
    for (const char *c = msg; *c; c++) {
        if (*c == '\'')
            g_string_append(escaped, "'\\''");
        else
            g_string_append_c(escaped, *c);
    }
    char args[1024];
    snprintf(args, sizeof(args), "commit -m '%s'", escaped->str);
    g_string_free(escaped, TRUE);
    wb_run(wb, args);
    gtk_entry_set_text(GTK_ENTRY(wb->commit_entry), "");
    wb_refresh_status(wb);
}

static void wb_on_checkout(GtkButton *b, gpointer data) {
    (void)b;
    Workbench *wb = data;
    char *branch = gtk_combo_box_text_get_active_text(
        GTK_COMBO_BOX_TEXT(wb->branch_combo));
    if (!branch || !branch[0]) {
        g_free(branch);
        return;
    }
    /* Escape single quotes */
    GString *escaped = g_string_new("");
    for (const char *c = branch; *c; c++) {
        if (*c == '\'')
            g_string_append(escaped, "'\\''");
        else
            g_string_append_c(escaped, *c);
    }
    char args[256];
    snprintf(args, sizeof(args), "checkout '%s'", escaped->str);
    g_string_free(escaped, TRUE);
    g_free(branch);
    wb_run(wb, args);
    wb_refresh_status(wb);
}

static void wb_on_editor(GtkButton *b, gpointer data) {
    (void)b;
    Workbench *wb = data;
    if (!s_editor[0]) return;
    char cmd[700];
    snprintf(cmd, sizeof(cmd), "%s '%s' &", s_editor, wb->path);
    int ret = system(cmd);
    (void)ret;
}

static void wb_on_terminal(GtkButton *b, gpointer data) {
    (void)b;
    Workbench *wb = data;
    launch_terminal(wb->path);
}

static void wb_on_clear(GtkButton *b, gpointer data) {
    (void)b;
    Workbench *wb = data;
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(wb->output));
    gtk_text_buffer_set_text(buf, "", 0);
}

static GtkWidget *wb_make_btn(const char *label, const char *css_name,
                               Workbench *wb, GCallback cb) {
    GtkWidget *btn = gtk_button_new_with_label(label);
    gtk_widget_set_name(btn, css_name);
    g_signal_connect(btn, "clicked", cb, wb);
    return btn;
}

static void open_workbench(const char *path, const char *name, GtkWidget *parent) {
    Workbench *wb = g_new0(Workbench, 1);
    strncpy(wb->path, path, sizeof(wb->path) - 1);

    /* Dialog window */
    char title[256];
    snprintf(title, sizeof(title), "Git Workbench — %s", name);
    wb->dialog = gtk_dialog_new_with_buttons(
        title,
        GTK_WINDOW(gtk_widget_get_toplevel(parent)),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Close", GTK_RESPONSE_CLOSE,
        NULL);
    gtk_window_set_default_size(GTK_WINDOW(wb->dialog), 820, 600);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(wb->dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 10);

    /* ── Status bar ── */
    wb->status_label = make_label("");
    gtk_box_pack_start(GTK_BOX(content), wb->status_label, FALSE, FALSE, 4);

    gtk_box_pack_start(GTK_BOX(content),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);

    /* ── Main area: buttons | output ── */
    GtkWidget *hpane = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(content), hpane, TRUE, TRUE, 0);

    /* Left: action buttons organized in groups */
    GtkWidget *btn_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(btn_scroll),
        GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(btn_scroll, 180, -1);

    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(btn_box), 4);
    gtk_container_add(GTK_CONTAINER(btn_scroll), btn_box);
    gtk_paned_pack1(GTK_PANED(hpane), btn_scroll, FALSE, FALSE);

    /* Group: Remote */
    gtk_box_pack_start(GTK_BOX(btn_box), make_label(
        "<span font='10' weight='bold' foreground='" CAT_BLUE "'>Remote</span>"),
        FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(btn_box),
        wb_make_btn("Fetch", "wb-btn", wb, G_CALLBACK(wb_on_fetch)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btn_box),
        wb_make_btn("Pull (ff-only)", "wb-btn", wb, G_CALLBACK(wb_on_pull_merge)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btn_box),
        wb_make_btn("Pull (rebase)", "wb-btn", wb, G_CALLBACK(wb_on_pull_rebase)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btn_box),
        wb_make_btn("Push", "wb-btn", wb, G_CALLBACK(wb_on_push)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btn_box),
        wb_make_btn("Push (force-lease)", "wb-btn-warn", wb, G_CALLBACK(wb_on_push_force)), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(btn_box),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);

    /* Group: Inspect */
    gtk_box_pack_start(GTK_BOX(btn_box), make_label(
        "<span font='10' weight='bold' foreground='" CAT_GREEN "'>Inspect</span>"),
        FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(btn_box),
        wb_make_btn("Status", "wb-btn", wb, G_CALLBACK(wb_on_status)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btn_box),
        wb_make_btn("Log (15)", "wb-btn", wb, G_CALLBACK(wb_on_log)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btn_box),
        wb_make_btn("Diff (stat)", "wb-btn", wb, G_CALLBACK(wb_on_diff)), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(btn_box),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);

    /* Group: Stash */
    gtk_box_pack_start(GTK_BOX(btn_box), make_label(
        "<span font='10' weight='bold' foreground='" CAT_MAUVE "'>Stash</span>"),
        FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(btn_box),
        wb_make_btn("Stash", "wb-btn", wb, G_CALLBACK(wb_on_stash)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btn_box),
        wb_make_btn("Stash Pop", "wb-btn", wb, G_CALLBACK(wb_on_stash_pop)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btn_box),
        wb_make_btn("Stash List", "wb-btn", wb, G_CALLBACK(wb_on_stash_list)), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(btn_box),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);

    /* Group: Staging & Commit */
    gtk_box_pack_start(GTK_BOX(btn_box), make_label(
        "<span font='10' weight='bold' foreground='" CAT_PEACH "'>Staging</span>"),
        FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(btn_box),
        wb_make_btn("Stage All", "wb-btn", wb, G_CALLBACK(wb_on_stage_all)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btn_box),
        wb_make_btn("Unstage All", "wb-btn", wb, G_CALLBACK(wb_on_unstage_all)), FALSE, FALSE, 0);

    /* Commit message entry + button */
    wb->commit_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(wb->commit_entry), "Commit message...");
    gtk_widget_set_name(wb->commit_entry, "panel-search");
    gtk_box_pack_start(GTK_BOX(btn_box), wb->commit_entry, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(btn_box),
        wb_make_btn("Commit", "wb-btn", wb, G_CALLBACK(wb_on_commit)), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(btn_box),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);

    /* Group: Branches */
    gtk_box_pack_start(GTK_BOX(btn_box), make_label(
        "<span font='10' weight='bold' foreground='" CAT_TEAL "'>Branches</span>"),
        FALSE, FALSE, 2);
    wb->branch_combo = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(btn_box), wb->branch_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btn_box),
        wb_make_btn("Checkout", "wb-btn", wb, G_CALLBACK(wb_on_checkout)), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(btn_box),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);

    /* Group: Tools */
    gtk_box_pack_start(GTK_BOX(btn_box), make_label(
        "<span font='10' weight='bold' foreground='" CAT_FLAMINGO "'>Tools</span>"),
        FALSE, FALSE, 2);
    if (s_editor[0]) {
        char lbl[192];
        snprintf(lbl, sizeof(lbl), "Open in %s", s_editor);
        gtk_box_pack_start(GTK_BOX(btn_box),
            wb_make_btn(lbl, "wb-btn", wb, G_CALLBACK(wb_on_editor)), FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(btn_box),
        wb_make_btn("Terminal", "wb-btn", wb, G_CALLBACK(wb_on_terminal)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(btn_box),
        wb_make_btn("Clear Output", "wb-btn", wb, G_CALLBACK(wb_on_clear)), FALSE, FALSE, 0);

    /* Right: output text view */
    wb->output = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(wb->output), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(wb->output), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(wb->output), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(wb->output), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(wb->output), 8);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(wb->output), 8);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(wb->output), 8);

    /* Tag for command lines */
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(wb->output));
    gtk_text_buffer_create_tag(buf, "cmd-tag",
        "foreground", CAT_BLUE,
        "weight", PANGO_WEIGHT_BOLD,
        NULL);

    GtkWidget *out_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(out_scroll),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(out_scroll), wb->output);
    gtk_paned_pack2(GTK_PANED(hpane), out_scroll, TRUE, TRUE);
    gtk_paned_set_position(GTK_PANED(hpane), 180);

    /* Populate initial state */
    wb_refresh_status(wb);
    wb_run(wb, "status");

    gtk_widget_show_all(wb->dialog);
    gtk_dialog_run(GTK_DIALOG(wb->dialog));
    gtk_widget_destroy(wb->dialog);
    g_free(wb);

    /* Refresh main dashboard after workbench closes */
    do_refresh();
}

static void on_workbench_clicked(GtkButton *btn, gpointer data) {
    (void)data;
    const char *path = g_object_get_data(G_OBJECT(btn), "project-path");
    const char *name = g_object_get_data(G_OBJECT(btn), "project-name");
    if (!path) return;
    open_workbench(path, name ? name : "project", GTK_WIDGET(btn));
}

/* ── simple card-level callbacks kept for editor / terminal ─────── */

static void on_editor_clicked(GtkButton *btn, gpointer data) {
    (void)data;
    const char *path = g_object_get_data(G_OBJECT(btn), "project-path");
    if (!path || !s_editor[0]) return;
    char cmd[700];
    snprintf(cmd, sizeof(cmd), "%s '%s' &", s_editor, path);
    int ret = system(cmd);
    (void)ret;
}

static void on_terminal_clicked(GtkButton *btn, gpointer data) {
    (void)data;
    const char *path = g_object_get_data(G_OBJECT(btn), "project-path");
    if (!path) return;
    launch_terminal(path);
}

static GtkWidget *make_card_btn(const char *label, const char *name,
                                const char *proj_path, const char *proj_name,
                                GCallback cb) {
    GtkWidget *b = gtk_button_new_with_label(label);
    gtk_widget_set_name(b, name);
    g_object_set_data_full(G_OBJECT(b), "project-path", g_strdup(proj_path), g_free);
    if (proj_name)
        g_object_set_data_full(G_OBJECT(b), "project-name", g_strdup(proj_name), g_free);
    g_signal_connect(b, "clicked", cb, NULL);
    return b;
}

/* ── project card ───────────────────────────────────────────────── */

static GtkWidget *build_project_card(Project *p, int idx) {
    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_widget_set_name(frame, "project-card");

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    gtk_container_add(GTK_CONTAINER(frame), vbox);

    /* Header row */
    GtkWidget *hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(vbox), hdr, FALSE, FALSE, 0);

    char cmd[700];
    snprintf(cmd, sizeof(cmd), "git -C '%s' rev-parse --abbrev-ref HEAD 2>/dev/null", p->path);
    char *branch = run_cmd(cmd);
    trim_inplace(branch);
    if (!branch || !branch[0]) {
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

    /* Status */
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

    GtkWidget *rm_btn = gtk_button_new_with_label("Remove");
    gtk_widget_set_name(rm_btn, "remove-btn");
    g_signal_connect(rm_btn, "clicked", G_CALLBACK(on_remove_clicked),
                     GINT_TO_POINTER(idx));
    gtk_box_pack_end(GTK_BOX(hdr), rm_btn, FALSE, FALSE, 0);

    /* Info row: ahead/behind, stash, tag */
    GString *info = g_string_new("<span font='10' foreground='" CAT_OVERLAY2 "'>");

    snprintf(cmd, sizeof(cmd),
        "git -C '%s' rev-list --left-right --count HEAD...@{upstream} 2>/dev/null", p->path);
    char *ab = run_cmd(cmd);
    trim_inplace(ab);
    if (ab && ab[0]) {
        int ahead = 0, behind = 0;
        if (sscanf(ab, "%d\t%d", &ahead, &behind) == 2 && (ahead || behind)) {
            if (ahead > 0)
                g_string_append_printf(info,
                    "<span foreground='" CAT_GREEN "'>↑%d</span>", ahead);
            if (ahead > 0 && behind > 0) g_string_append(info, " ");
            if (behind > 0)
                g_string_append_printf(info,
                    "<span foreground='" CAT_RED "'>↓%d</span>", behind);
            g_string_append(info, "  ");
        }
    }
    free(ab);

    snprintf(cmd, sizeof(cmd), "git -C '%s' stash list 2>/dev/null | wc -l", p->path);
    char *stash_str = run_cmd(cmd);
    trim_inplace(stash_str);
    int stashes = stash_str ? atoi(stash_str) : 0;
    free(stash_str);
    if (stashes > 0)
        g_string_append_printf(info, "%d stash%s  ", stashes, stashes == 1 ? "" : "es");

    snprintf(cmd, sizeof(cmd),
        "git -C '%s' describe --tags --abbrev=0 2>/dev/null", p->path);
    char *tag = run_cmd(cmd);
    trim_inplace(tag);
    if (tag && tag[0]) {
        char *esc = g_markup_escape_text(tag, -1);
        g_string_append_printf(info,
            "<span foreground='" CAT_MAUVE "'>%s</span>  ", esc);
        g_free(esc);
    }
    free(tag);

    g_string_append(info, "</span>");
    if (info->len > 80)
        gtk_box_pack_start(GTK_BOX(vbox), make_label(info->str), FALSE, FALSE, 0);
    g_string_free(info, TRUE);

    /* Branch list */
    snprintf(cmd, sizeof(cmd),
        "git -C '%s' branch --format='%%(refname:short)' 2>/dev/null", p->path);
    char *branches = run_cmd(cmd);
    if (branches && branches[0]) {
        GString *bstr = g_string_new(
            "<span font='10' foreground='" CAT_OVERLAY1 "'>  branches: </span>"
            "<span font='10' foreground='" CAT_SUBTEXT0 "'>");
        char *b = strtok(branches, "\n");
        int count = 0;
        while (b && count < 8) {
            trim_inplace(b);
            if (!b[0]) { b = strtok(NULL, "\n"); continue; }
            if (count > 0) g_string_append(bstr, ", ");
            char *esc = g_markup_escape_text(b, -1);
            if (strcmp(b, branch) == 0)
                g_string_append_printf(bstr,
                    "<span foreground='" CAT_BLUE "' weight='bold'>%s</span>", esc);
            else
                g_string_append(bstr, esc);
            g_free(esc);
            count++;
            b = strtok(NULL, "\n");
        }
        if (b) g_string_append(bstr, ", ...");
        g_string_append(bstr, "</span>");
        gtk_box_pack_start(GTK_BOX(vbox), make_label(bstr->str), FALSE, FALSE, 0);
        g_string_free(bstr, TRUE);
    }
    free(branches);
    free(branch);

    /* Recent commits */
    snprintf(cmd, sizeof(cmd),
        "git -C '%s' log --oneline -5 --format='%%h %%s (%%cr)' 2>/dev/null", p->path);
    char *log = run_cmd(cmd);
    if (log && log[0]) {
        char *line = strtok(log, "\n");
        while (line) {
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

    /* Action buttons row */
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_top(actions, 4);
    gtk_box_pack_start(GTK_BOX(vbox), actions, FALSE, FALSE, 0);

    /* Primary: Git Workbench */
    gtk_box_pack_start(GTK_BOX(actions),
        make_card_btn("Git Workbench", "action-btn", p->path, p->name,
                      G_CALLBACK(on_workbench_clicked)),
        FALSE, FALSE, 0);

    if (s_editor[0]) {
        char editor_label[192];
        snprintf(editor_label, sizeof(editor_label), "Open in %s", s_editor);
        gtk_box_pack_start(GTK_BOX(actions),
            make_card_btn(editor_label, "card-btn", p->path, NULL,
                          G_CALLBACK(on_editor_clicked)),
            FALSE, FALSE, 0);
    }

    gtk_box_pack_start(GTK_BOX(actions),
        make_card_btn("Terminal", "card-btn", p->path, NULL,
                      G_CALLBACK(on_terminal_clicked)),
        FALSE, FALSE, 0);

    return frame;
}

/* ── panel lifecycle ────────────────────────────────────────────── */

static void do_refresh(void) {
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
    detect_editor();

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
