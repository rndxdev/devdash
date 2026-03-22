#include "portwatch.h"
#include "theme.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

enum {
    COL_PW_PROTO, COL_PW_PORT, COL_PW_PID, COL_PW_PROCESS, COL_PW_ADDR,
    COL_PW_PID_INT, NUM_PW_COLS
};

static GtkListStore *s_store;
static GtkWidget *s_tree;

static void parse_ss_output(const char *output) {
    gtk_list_store_clear(s_store);
    if (!output) return;

    const char *line = output;
    /* skip header */
    line = strchr(line, '\n');
    if (!line) return;
    line++;

    while (*line) {
        const char *eol = strchr(line, '\n');
        if (!eol) eol = line + strlen(line);

        char state[32] = "", local[256] = "", peer[256] = "", process[512] = "";
        int recvq = 0, sendq = 0;

        /* Parse: State Recv-Q Send-Q Local Peer Process */
        int n = sscanf(line, "%31s %d %d %255s %255s %511[^\n]",
                       state, &recvq, &sendq, local, peer, process);
        if (n >= 4) {
            /* Extract port from local address */
            char *colon = strrchr(local, ':');
            char port[16] = "?";
            if (colon) {
                snprintf(port, sizeof(port), "%s", colon + 1);
                *colon = '\0';
            }

            /* Extract PID from process field: users:(("name",pid=123,fd=4)) */
            char pname[128] = "-";
            int pid = 0;
            if (n >= 6) {
                char *pp = strstr(process, "pid=");
                if (pp) pid = atoi(pp + 4);
                char *pn = strstr(process, "((\"");
                if (pn) {
                    pn += 3;
                    char *pe = strchr(pn, '"');
                    if (pe) {
                        size_t len = pe - pn;
                        if (len >= sizeof(pname)) len = sizeof(pname) - 1;
                        memcpy(pname, pn, len);
                        pname[len] = '\0';
                    }
                }
            }

            char proto[8] = "tcp";
            char pid_str[16];
            if (pid > 0)
                snprintf(pid_str, sizeof(pid_str), "%d", pid);
            else
                snprintf(pid_str, sizeof(pid_str), "-");

            GtkTreeIter iter;
            gtk_list_store_append(s_store, &iter);
            gtk_list_store_set(s_store, &iter,
                COL_PW_PROTO, proto,
                COL_PW_PORT, port,
                COL_PW_PID, pid_str,
                COL_PW_PROCESS, pname,
                COL_PW_ADDR, local,
                COL_PW_PID_INT, pid,
                -1);
        }

        if (*eol == '\0') break;
        line = eol + 1;
    }
}

static void do_refresh(void) {
    char *out = run_cmd("ss -tlnp 2>/dev/null");
    parse_ss_output(out);
    free(out);
}

static void on_kill_clicked(GtkWidget *btn, gpointer data) {
    (void)btn; (void)data;
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(s_tree));
    GtkTreeIter iter;
    GtkTreeModel *model;
    if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return;

    int pid = 0;
    gtk_tree_model_get(model, &iter, COL_PW_PID_INT, &pid, -1);
    if (pid <= 0) return;

    char msg[128];
    snprintf(msg, sizeof(msg), "Kill process %d?", pid);
    GtkWidget *dialog = gtk_message_dialog_new(NULL,
        GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO, "%s", msg);
    int resp = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (resp == GTK_RESPONSE_YES) {
        kill(pid, SIGTERM);
        g_timeout_add(500, (GSourceFunc)do_refresh, NULL);
    }
}

GtkWidget *portwatch_create(void) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);

    GtkWidget *title = make_label(
        "<span font='14' weight='bold' foreground='" CAT_BLUE "'>Port Watch</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    GtkWidget *subtitle = make_label(
        "<span font='11' foreground='" CAT_SUBTEXT0 "'>Listening TCP ports — refreshes every 5s</span>");
    gtk_box_pack_start(GTK_BOX(vbox), subtitle, FALSE, FALSE, 0);

    /* TreeView */
    s_store = gtk_list_store_new(NUM_PW_COLS,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);

    s_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(s_store));
    g_object_unref(s_store);
    gtk_widget_set_name(s_tree, "panel-tree");

    const char *titles[] = {"Proto", "Port", "PID", "Process", "Address"};
    for (int i = 0; i < 5; i++) {
        GtkCellRenderer *r = gtk_cell_renderer_text_new();
        GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(
            titles[i], r, "text", i, NULL);
        gtk_tree_view_column_set_resizable(c, TRUE);
        gtk_tree_view_column_set_expand(c, i == 4);
        gtk_tree_view_append_column(GTK_TREE_VIEW(s_tree), c);
    }

    GtkWidget *sw = make_scrolled(s_tree);
    gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 4);

    /* Buttons */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(hbox, GTK_ALIGN_END);

    GtkWidget *btn_refresh = gtk_button_new_with_label("Refresh");
    gtk_widget_set_name(btn_refresh, "action-btn");
    g_signal_connect_swapped(btn_refresh, "clicked", G_CALLBACK(do_refresh), NULL);

    GtkWidget *btn_kill = gtk_button_new_with_label("Kill");
    gtk_widget_set_name(btn_kill, "danger-btn");
    g_signal_connect(btn_kill, "clicked", G_CALLBACK(on_kill_clicked), NULL);

    gtk_box_pack_start(GTK_BOX(hbox), btn_refresh, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hbox), btn_kill, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    do_refresh();
    return vbox;
}

void portwatch_refresh(void) { do_refresh(); }
void portwatch_cleanup(void) {}
