/*
 * Native GTK2 ROM picker for Kindle NES.
 *
 * The layout and touch interaction follow the SimpleFilePicker interface in
 * Gambatte-K2 (crazy-electron/gambatte-k2, GPL-3.0): Cancel + current path,
 * a scrollable single-column file list, parent-directory row, directories
 * before ROMs, and drag-to-scroll without treating a drag as a tap.
 *
 * This is a C implementation for NES ROMs and does not use the Gambatte core
 * or libgee. The emulator core remains libxnes.
 */
#define _GNU_SOURCE
#include "file_picker.h"

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { COL_LABEL, COL_FULL_PATH, COL_IS_DIRECTORY, N_COLUMNS };

typedef struct {
    GtkWidget *window;
    GtkWidget *path_label;
    GtkWidget *tree;
    GtkListStore *store;
    GtkScrolledWindow *scrolled;
    char root[PATH_MAX];
    char current[PATH_MAX];
    char *selected_path;
    char *pressed_path;
    double press_y;
    double last_y;
    gboolean dragging;
    gboolean finished;
} NesPicker;

static gboolean path_is_inside(const char *path, const char *root) {
    size_t n;
    if (!path || !root) return FALSE;
    n = strlen(root);
    return strncmp(path, root, n) == 0 && (path[n] == '\0' || path[n] == '/');
}

static gboolean is_nes_file(const char *name) {
    size_t n;
    if (!name) return FALSE;
    n = strlen(name);
    return n >= 4 && g_ascii_strcasecmp(name + n - 4, ".nes") == 0;
}

static gint compare_names(gconstpointer a, gconstpointer b) {
    const char *left = *(char * const *)a;
    const char *right = *(char * const *)b;
    return g_ascii_strcasecmp(left, right);
}

static void picker_quit(NesPicker *picker) {
    if (!picker->finished) {
        picker->finished = TRUE;
        if (gtk_main_level() > 0) gtk_main_quit();
    }
}

static gboolean row_at_event(NesPicker *picker, GdkEventButton *event,
                             char **full_path, gboolean *is_directory) {
    GtkTreePath *tree_path = NULL;
    GtkTreeViewColumn *column = NULL;
    GtkTreeIter iter;
    gint cell_x = 0, cell_y = 0;
    gboolean is_dir = FALSE;
    gchar *full = NULL;
    gboolean found;

    found = gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(picker->tree),
                                          (gint)event->x, (gint)event->y,
                                          &tree_path, &column, &cell_x, &cell_y);
    if (!found) return FALSE;

    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(picker->store), &iter, tree_path)) {
        gtk_tree_model_get(GTK_TREE_MODEL(picker->store), &iter,
                           COL_FULL_PATH, &full,
                           COL_IS_DIRECTORY, &is_dir, -1);
    }
    gtk_tree_path_free(tree_path);
    if (!full) return FALSE;

    if (full_path) *full_path = full;
    else g_free(full);
    if (is_directory) *is_directory = is_dir;
    return TRUE;
}

static gboolean picker_change_directory(NesPicker *picker, const char *path) {
    char resolved[PATH_MAX];
    GPtrArray *directories;
    GPtrArray *files;
    GDir *dir;
    const gchar *name;
    GtkTreeIter iter;
    guint i;

    if (!realpath(path, resolved) || !path_is_inside(resolved, picker->root))
        return FALSE;
    dir = g_dir_open(resolved, 0, NULL);
    if (!dir) return FALSE;

    g_strlcpy(picker->current, resolved, sizeof(picker->current));
    gtk_list_store_clear(picker->store);
    directories = g_ptr_array_new_with_free_func(g_free);
    files = g_ptr_array_new_with_free_func(g_free);

    if (strcmp(resolved, picker->root) != 0) {
        char *parent = g_path_get_dirname(resolved);
        gtk_list_store_append(picker->store, &iter);
        gtk_list_store_set(picker->store, &iter,
                           COL_LABEL, "..",
                           COL_FULL_PATH, parent,
                           COL_IS_DIRECTORY, TRUE, -1);
        g_free(parent);
    }

    while ((name = g_dir_read_name(dir)) != NULL) {
        char *full = g_build_filename(resolved, name, NULL);
        struct stat st;
        if (g_stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
            g_ptr_array_add(directories, g_strdup(name));
        } else if (g_stat(full, &st) == 0 && S_ISREG(st.st_mode) && is_nes_file(name)) {
            g_ptr_array_add(files, g_strdup(name));
        }
        g_free(full);
    }
    g_dir_close(dir);
    g_ptr_array_sort(directories, compare_names);
    g_ptr_array_sort(files, compare_names);

    for (i = 0; i < directories->len; i++) {
        const char *entry = g_ptr_array_index(directories, i);
        char *full = g_build_filename(resolved, entry, NULL);
        char *label = g_strconcat(entry, "/", NULL);
        gtk_list_store_append(picker->store, &iter);
        gtk_list_store_set(picker->store, &iter,
                           COL_LABEL, label,
                           COL_FULL_PATH, full,
                           COL_IS_DIRECTORY, TRUE, -1);
        g_free(label);
        g_free(full);
    }
    for (i = 0; i < files->len; i++) {
        const char *entry = g_ptr_array_index(files, i);
        char *full = g_build_filename(resolved, entry, NULL);
        gtk_list_store_append(picker->store, &iter);
        gtk_list_store_set(picker->store, &iter,
                           COL_LABEL, entry,
                           COL_FULL_PATH, full,
                           COL_IS_DIRECTORY, FALSE, -1);
        g_free(full);
    }

    g_ptr_array_free(directories, TRUE);
    g_ptr_array_free(files, TRUE);
    gtk_label_set_text(GTK_LABEL(picker->path_label), picker->current);
    return TRUE;
}

static void on_cancel_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    picker_quit((NesPicker *)user_data);
}

static gboolean on_window_delete(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
    (void)widget;
    (void)event;
    picker_quit((NesPicker *)user_data);
    return TRUE;
}

static gboolean on_tree_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer user_data) {
    (void)widget;
    (void)event;
    (void)user_data;
    return FALSE;
}

static gboolean on_tree_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    NesPicker *picker = (NesPicker *)user_data;
    char *full = NULL;
    (void)widget;

    if (event->button != 1) return FALSE;
    g_free(picker->pressed_path);
    picker->pressed_path = NULL;
    picker->press_y = event->y;
    picker->last_y = event->y;
    picker->dragging = FALSE;
    if (row_at_event(picker, event, &full, NULL)) {
        picker->pressed_path = full;
    }
    return FALSE;
}

static gboolean on_tree_motion(GtkWidget *widget, GdkEventMotion *event, gpointer user_data) {
    NesPicker *picker = (NesPicker *)user_data;
    (void)widget;

    if (!(event->state & GDK_BUTTON1_MASK)) return FALSE;
    if (fabs(event->y - picker->press_y) > 5.0) picker->dragging = TRUE;
    if (!picker->dragging) return FALSE;

    {
        GtkAdjustment *adjustment = gtk_scrolled_window_get_vadjustment(picker->scrolled);
        gdouble delta = picker->last_y - event->y;
        gdouble max_value = MAX(adjustment->lower,
                                adjustment->upper - adjustment->page_size);
        gtk_adjustment_set_value(adjustment,
                                 CLAMP(adjustment->value + delta,
                                       adjustment->lower, max_value));
    }
    picker->last_y = event->y;
    return TRUE;
}

static gboolean on_tree_button_release(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
    NesPicker *picker = (NesPicker *)user_data;
    char *full = NULL;
    gboolean is_dir = FALSE;
    (void)widget;

    if (event->button != 1) return FALSE;
    if (picker->dragging || fabs(event->y - picker->press_y) > 5.0) {
        picker->dragging = FALSE;
        g_free(picker->pressed_path);
        picker->pressed_path = NULL;
        return TRUE;
    }
    if (!picker->pressed_path ||
        !row_at_event(picker, event, &full, &is_dir) ||
        g_strcmp0(full, picker->pressed_path) != 0) {
        g_free(full);
        g_free(picker->pressed_path);
        picker->pressed_path = NULL;
        return FALSE;
    }

    if (is_dir) {
        if (picker_change_directory(picker, full)) {
            GtkTreePath *first = gtk_tree_path_new_from_indices(0, -1);
            gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(picker->tree), first,
                                         NULL, TRUE, 0.0f, 0.0f);
            gtk_tree_path_free(first);
        }
    } else {
        picker->selected_path = g_strdup(full);
        picker_quit(picker);
    }

    g_free(full);
    g_free(picker->pressed_path);
        picker->pressed_path = NULL;
    return TRUE;
}

char *nes_file_picker_run(GtkWindow *parent, const char *root_dir, char *current_dir, size_t current_dir_size) {
    NesPicker picker;
    GtkWidget *main_vbox;
    GtkWidget *top_hbox;
    GtkWidget *cancel_button;
    GtkWidget *scrolled_window;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    char resolved_root[PATH_MAX];
    char resolved_start[PATH_MAX];
    char *result;

    if (!root_dir || !current_dir || current_dir_size == 0 ||
        !realpath(root_dir, resolved_root)) return NULL;

    memset(&picker, 0, sizeof(picker));
    g_strlcpy(picker.root, resolved_root, sizeof(picker.root));
    if (realpath(current_dir, resolved_start) &&
        path_is_inside(resolved_start, picker.root))
        g_strlcpy(picker.current, resolved_start, sizeof(picker.current));
    else
        g_strlcpy(picker.current, picker.root, sizeof(picker.current));

    picker.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(picker.window),
                         "L:A_N:application_ID:nes_PC:N_O:URL_DM:PZ_O:U");
    if (parent) gtk_window_set_transient_for(GTK_WINDOW(picker.window), parent);
    gtk_window_set_default_size(GTK_WINDOW(picker.window), 600, 760);
    gtk_window_set_position(GTK_WINDOW(picker.window), GTK_WIN_POS_CENTER);
    gtk_window_set_modal(GTK_WINDOW(picker.window), TRUE);

    main_vbox = gtk_vbox_new(FALSE, 5);
    gtk_container_set_border_width(GTK_CONTAINER(main_vbox), 10);
    gtk_container_add(GTK_CONTAINER(picker.window), main_vbox);

    top_hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_vbox), top_hbox, FALSE, FALSE, 0);

    cancel_button = gtk_button_new_with_label("cancel");
    gtk_widget_set_name(cancel_button, "nes-file-picker-cancel");
    gtk_box_pack_start(GTK_BOX(top_hbox), cancel_button, FALSE, FALSE, 0);
    g_signal_connect(cancel_button, "clicked", G_CALLBACK(on_cancel_clicked), &picker);

    picker.path_label = gtk_label_new(picker.current);
    gtk_misc_set_alignment(GTK_MISC(picker.path_label), 0.0f, 0.5f);
    gtk_label_set_ellipsize(GTK_LABEL(picker.path_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_pack_start(GTK_BOX(top_hbox), picker.path_label, TRUE, TRUE, 10);

    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    picker.scrolled = GTK_SCROLLED_WINDOW(scrolled_window);
    gtk_scrolled_window_set_policy(picker.scrolled, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(picker.scrolled, GTK_SHADOW_IN);
    gtk_box_pack_start(GTK_BOX(main_vbox), scrolled_window, TRUE, TRUE, 0);

    picker.store = gtk_list_store_new(N_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
    picker.tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(picker.store));
    gtk_widget_set_name(picker.tree, "nes-file-picker-tree");
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(picker.tree), FALSE);
    gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(picker.tree), FALSE);
    gtk_tree_selection_set_mode(
        gtk_tree_view_get_selection(GTK_TREE_VIEW(picker.tree)), GTK_SELECTION_SINGLE);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "font", "Sans 16",
                 "wrap-mode", PANGO_WRAP_WORD_CHAR, NULL);
    gtk_cell_renderer_set_padding(renderer, 6, 8);
    column = gtk_tree_view_column_new_with_attributes(NULL, renderer, "text", COL_LABEL, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(picker.tree), column);
    gtk_container_add(GTK_CONTAINER(scrolled_window), picker.tree);

    gtk_widget_add_events(picker.tree, GDK_BUTTON_PRESS_MASK |
                          GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK |
                          GDK_SCROLL_MASK);
    g_signal_connect(picker.tree, "scroll-event", G_CALLBACK(on_tree_scroll), &picker);
    g_signal_connect(picker.tree, "button-press-event", G_CALLBACK(on_tree_button_press), &picker);
    g_signal_connect(picker.tree, "motion-notify-event", G_CALLBACK(on_tree_motion), &picker);
    g_signal_connect(picker.tree, "button-release-event", G_CALLBACK(on_tree_button_release), &picker);
    g_signal_connect(picker.window, "delete-event", G_CALLBACK(on_window_delete), &picker);

    if (!picker_change_directory(&picker, picker.current)) {
        gtk_widget_destroy(picker.window);
        g_object_unref(picker.store);
        return NULL;
    }

    gtk_widget_show_all(picker.window);
    gtk_main();

    g_strlcpy(current_dir, picker.current, current_dir_size);
    result = picker.selected_path;
    picker.selected_path = NULL;

    gtk_widget_destroy(picker.window);
    g_object_unref(picker.store);
    g_free(picker.pressed_path);
    g_free(picker.selected_path);
    return result;
}
