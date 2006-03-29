#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <libbonobo.h>
#include <gtk/gtk.h>

#include "activate-settings-daemon.h"


/*#include "GNOME_SettingsDaemon.h"*/

static void popup_error_message (void)
{
  GtkWidget *dialog;

  dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING,
				   GTK_BUTTONS_OK, _("Unable to start the settings manager 'gnome-settings-daemon'.\n"
				   "Without the GNOME settings manager running, some preferences may not take effect. This could "
				   "indicate a problem with Bonobo, or a non-GNOME (e.g. KDE) settings manager may already "
				   "be active and conflicting with the GNOME settings manager."));

  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);
}

/* Returns FALSE if activation failed, else TRUE */
gboolean 
activate_settings_daemon (void)
{
  CORBA_Environment ev;
  CORBA_Object object;

  /*GNOME_SettingsDaemon corba_foo;*/
  
  bonobo_init (NULL, NULL);
  
  CORBA_exception_init (&ev);

  object = bonobo_activation_activate_from_id  ("OAFIID:GNOME_SettingsDaemon",
						0, NULL, &ev);
  
  if (BONOBO_EX(&ev) || object == CORBA_OBJECT_NIL ) {
    popup_error_message ();
    CORBA_exception_free(&ev);
    return FALSE;
  }

  /*bool = GNOME_SettingsDaemon_awake (corba_foo, "MyService", &ev);
    printf ("bool is %d\n", bool);*/

  CORBA_exception_free(&ev);
  return TRUE;
}
