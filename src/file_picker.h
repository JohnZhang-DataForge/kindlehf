#ifndef NES_FILE_PICKER_H
#define NES_FILE_PICKER_H

#include <gtk/gtk.h>
#include <stddef.h>

/*
 * Opens the Gambatte-K2-style native GTK2 ROM picker.
 * root_dir confines navigation; current_dir is updated on selection or cancel.
 * Returns a malloc-allocated ROM path, or NULL when the user cancels.
 */
char *nes_file_picker_run(GtkWindow *parent, const char *root_dir, char *current_dir, size_t current_dir_size);

#endif
