/*
 ** Tiny GTK4 helper process for the Linux native folder picker.
 **
 ** lite itself does not link or initialise GTK. Instead it spawns this helper,
 ** which shows the desktop's native folder chooser and writes the chosen path
 ** into the file named on the command line.
 **
 ** Usage: lite_folder_chooser_helper --lite-folder-chooser <out-file> <title> [start-dir]
 **
 ** Exit status 0 means the path was written; anything else means the user
 ** cancelled or the dialog could not be shown.
 */

#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  const char *out_path;
  int status;
  GMainLoop *loop;
} Chooser;

static void on_folder_selected(GObject *source, GAsyncResult *res, gpointer user_data) {
  Chooser *c = user_data;
  GError *error = NULL;

  GFile *file = gtk_file_dialog_select_folder_finish(
      GTK_FILE_DIALOG(source), res, &error);

  if (file) {
    char *path = g_file_get_path(file);
    if (path) {
      FILE *f = fopen(c->out_path, "w");
      if (f) {
        if (fputs(path, f) >= 0) { c->status = 0; }
        fclose(f);
      }
      g_free(path);
    }
    g_object_unref(file);
  }
  if (error) { g_error_free(error); }

  g_main_loop_quit(c->loop);
}

int main(int argc, char **argv) {
  if (argc < 4 || strcmp(argv[1], "--lite-folder-chooser") != 0) {
    return 1;
  }

  const char *out_path = argv[2];
  const char *title = argv[3];
  const char *initial = (argc > 4 && argv[4][0]) ? argv[4] : NULL;

  /* No display / no Wayland or X11 session: let lite fall back. */
  if (!gtk_init_check()) { return 1; }

  GtkFileDialog *dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, title);

  if (initial) {
    GFile *start = g_file_new_for_path(initial);
    gtk_file_dialog_set_initial_folder(dialog, start);
    g_object_unref(start);
  }

  Chooser chooser = { out_path, 1, g_main_loop_new(NULL, FALSE) };

  gtk_file_dialog_select_folder(dialog, NULL, NULL, on_folder_selected, &chooser);
  g_main_loop_run(chooser.loop);

  g_main_loop_unref(chooser.loop);
  g_object_unref(dialog);

  return chooser.status;
}
