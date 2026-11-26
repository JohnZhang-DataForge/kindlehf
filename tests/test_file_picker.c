#include "file_picker.h"

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    gboolean saw_directory_first;
    gboolean saw_nes_file;
    gboolean rejected_non_nes;
    gboolean found_tree;
} PickerTest;

static GtkWidget *find_named(GtkWidget *widget, const char *name) {
    if (!widget) return NULL;
    if (g_strcmp0(gtk_widget_get_name(widget), name) == 0) return widget;
    if (GTK_IS_CONTAINER(widget)) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
        GList *it;
        GtkWidget *found = NULL;
        for (it = children; it; it = it->next) {
            found = find_named(GTK_WIDGET(it->data), name);
            if (found) break;
        }
        g_list_free(children);
        if (found) return found;
    }
    return NULL;
}

static gboolean inspect_and_cancel(gpointer data) {
    PickerTest *test = (PickerTest *)data;
    GList *windows = gtk_window_list_toplevels();
    GList *it;
    GtkWidget *picker_window = NULL;
    GtkWidget *tree_widget = NULL;
    GtkWidget *cancel = NULL;

    for (it = windows; it; it = it->next) {
        GtkWidget *window = GTK_WIDGET(it->data);
        const char *title = gtk_window_get_title(GTK_WINDOW(window));
        if (title && strstr(title, "application_ID:nes")) {
            picker_window = window;
            break;
        }
    }
    g_list_free(windows);

    if (picker_window) {
        tree_widget = find_named(picker_window, "nes-file-picker-tree");
        cancel = find_named(picker_window, "nes-file-picker-cancel");
    }
    if (tree_widget && GTK_IS_TREE_VIEW(tree_widget)) {
        GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree_widget));
        GtkTreeIter iter;
        gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
        gboolean first = TRUE;
        test->found_tree = TRUE;
        while (valid) {
            gchar *label = NULL;
            gboolean is_dir = FALSE;
            gtk_tree_model_get(model, &iter, 0, &label, 2, &is_dir, -1);
            if (first) {
                test->saw_directory_first = is_dir && label && g_str_has_suffix(label, "/");
                first = FALSE;
            }
            if (label && g_ascii_strcasecmp(label, "homebrew.NES") == 0)
                test->saw_nes_file = !is_dir;
            if (label && g_ascii_strcasecmp(label, "ignore.gbc") == 0)
                test->rejected_non_nes = TRUE;
            g_free(label);
            valid = gtk_tree_model_iter_next(model, &iter);
        }
    }

    if (cancel && GTK_IS_BUTTON(cancel)) gtk_button_clicked(GTK_BUTTON(cancel));
    else gtk_main_quit();
    return FALSE;
}

int main(int argc, char **argv) {
    GError *error = NULL;
    char *root;
    char *subdir;
    char *rom;
    char *other;
    char current[4096];
    char *selected;
    PickerTest test = {0};

    gtk_init(&argc, &argv);
    root = g_dir_make_tmp("nes-picker-test-XXXXXX", &error);
    if (!root) {
        fprintf(stderr, "temporary directory: %s\n", error->message);
        g_error_free(error);
        return 2;
    }
    subdir = g_build_filename(root, "Folder", NULL);
    rom = g_build_filename(root, "homebrew.NES", NULL);
    other = g_build_filename(root, "ignore.gbc", NULL);
    if (g_mkdir(subdir, 0700) != 0 ||
        !g_file_set_contents(rom, "test", 4, &error) ||
        !g_file_set_contents(other, "test", 4, &error)) {
        fprintf(stderr, "fixture creation failed\n");
        if (error) g_error_free(error);
        return 2;
    }
    g_strlcpy(current, root, sizeof(current));
    g_timeout_add(150, inspect_and_cancel, &test);
    selected = nes_file_picker_run(root, current, sizeof(current));

    if (selected) g_free(selected);
    if (!test.found_tree || !test.saw_directory_first || !test.saw_nes_file ||
        test.rejected_non_nes || strcmp(current, root) != 0) {
        fprintf(stderr, "picker smoke test failed: tree=%d dirs-first=%d nes=%d gbc-filter=%d\n",
                test.found_tree, test.saw_directory_first, test.saw_nes_file,
                !test.rejected_non_nes);
        return 1;
    }

    g_remove(rom);
    g_remove(other);
    g_rmdir(subdir);
    g_rmdir(root);
    g_free(subdir);
    g_free(rom);
    g_free(other);
    g_free(root);
    puts("GTK2 NES picker smoke test: PASS");
    return 0;
}
