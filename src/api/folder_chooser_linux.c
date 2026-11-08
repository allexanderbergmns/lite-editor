#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

static void
on_response(GtkDialog *dialog, int response, gpointer user_data)
{
  const char *path = NULL;
  if (response == GTK_RESPONSE_ACCEPT) {
    GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
    path = gtk_file_chooser_get_filename(chooser);
  }
  if (path) {
    /* gtk_file_chooser_get_filename returns a strdup'd path. */
    g_print("%s", path);
    g_free(path);
  }
  gtk_main_quit();
}

int
main(int argc, char **argv)
{
  if (argc < 4 || strcmp(argv[1], "--lite-folder-chooser") != 0) {
    return 1;
  }

  const char *output_path = argv[2];
  const char *title = argc > 3 ? argv[3] : "Choose Folder";
  const char *initial = (argc > 4 && argv[4][0]) ? argv[4] : NULL;

  GtkWidget *dialog = gtk_file_chooser_native_new(
      title,
      NULL,
      GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
      "Cancel", GTK_RESPONSE_CANCEL,
      "Open",   GTK_RESPONSE_ACCEPT,
      NULL);

  if (initial) {
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), initial);
  }

  g_signal_connect(dialog, "response", G_CALLBACK(on_response), NULL);

  gtk_main();

  return 0;
}
