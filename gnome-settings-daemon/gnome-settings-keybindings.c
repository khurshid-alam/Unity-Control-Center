#include <config.h>

#include <string.h>
#include <X11/keysym.h>
#include <glib/gi18n.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include "gnome-settings-module.h"
#include "utils.h"
#include "eggaccelerators.h"

/* we exclude shift, GDK_CONTROL_MASK and GDK_MOD1_MASK since we know what
   these modifiers mean
   these are the mods whose combinations are bound by the keygrabbing code */
#define IGNORED_MODS (0x2000 /*Xkb modifier*/ | GDK_LOCK_MASK  | \
        GDK_MOD2_MASK | GDK_MOD3_MASK | GDK_MOD4_MASK | GDK_MOD5_MASK)
/* these are the ones we actually use for global keys, we always only check
 * for these set */
#define USED_MODS (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK)

#define GCONF_BINDING_DIR "/desktop/gnome/keybindings"

typedef struct {
	GnomeSettingsModule parent;
} GnomeSettingsModuleKeybindings;

typedef struct {
	GnomeSettingsModuleClass parent_class;
} GnomeSettingsModuleKeybindingsClass;

typedef struct {
	guint keysym;
	guint state;
	guint keycode;
} Key;

typedef struct {
	char *binding_str;
	char *action;
	char *gconf_key;
	Key   key;
	Key   previous_key;
} Binding;

static GnomeSettingsModuleRunlevel gnome_settings_module_keybindings_get_runlevel (GnomeSettingsModule *module);
static gboolean gnome_settings_module_keybindings_initialize (GnomeSettingsModule *module, GConfClient *client);
static gboolean gnome_settings_module_keybindings_start (GnomeSettingsModule *module);
static gboolean gnome_settings_module_keybindings_stop (GnomeSettingsModule *module);

static GSList *binding_list = NULL;
static GSList *screens = NULL;

static void
gnome_settings_module_keybindings_class_init (GnomeSettingsModuleKeybindingsClass *klass)
{
	GnomeSettingsModuleClass *module_class;

	module_class = GNOME_SETTINGS_MODULE_CLASS (klass);
	module_class->get_runlevel = gnome_settings_module_keybindings_get_runlevel;
	module_class->initialize = gnome_settings_module_keybindings_initialize;
	module_class->start = gnome_settings_module_keybindings_start;
	module_class->stop = gnome_settings_module_keybindings_stop;
}

static void
gnome_settings_module_keybindings_init (GnomeSettingsModuleKeybindings *module)
{
}

GType
gnome_settings_module_keybindings_get_type (void)
{
	static GType module_type = 0;

	if (!module_type) {
		static const GTypeInfo module_info = {
			sizeof (GnomeSettingsModuleKeybindingsClass),
			NULL,		/* base_init */
			NULL,		/* base_finalize */
			(GClassInitFunc) gnome_settings_module_keybindings_class_init,
			NULL,		/* class_finalize */
			NULL,		/* class_data */
			sizeof (GnomeSettingsModuleKeybindings),
			0,		/* n_preallocs */
			(GInstanceInitFunc) gnome_settings_module_keybindings_init,
		};

		module_type = g_type_register_static (GNOME_SETTINGS_TYPE_MODULE,
						      "GnomeSettingsModuleKeybindings",
						      &module_info, 0);
	}

	return module_type;
}

static GnomeSettingsModuleRunlevel
gnome_settings_module_keybindings_get_runlevel (GnomeSettingsModule *module)
{
	return GNOME_SETTINGS_MODULE_RUNLEVEL_GNOME_SETTINGS;
}

static GSList *
get_screens_list (void)
{
	GdkDisplay *display = gdk_display_get_default();
	int n_screens;
	GSList *list = NULL;
	int i;

	n_screens = gdk_display_get_n_screens (display);

	if (n_screens == 1) {
		list = g_slist_append (list, gdk_screen_get_default ());
	} else {
		for (i = 0; i < n_screens; i++) {
			GdkScreen *screen;

			screen = gdk_display_get_screen (display, i);
			if (screen != NULL) {
				list = g_slist_prepend (list, screen);
			}
		}
		list = g_slist_reverse (list);
	}

	return list;
}

extern char **environ;

static char *
screen_exec_display_string (GdkScreen *screen)
{
	GString    *str;
	const char *old_display;
	char       *p;

	g_return_val_if_fail (GDK_IS_SCREEN (screen), NULL);

	old_display = gdk_display_get_name (gdk_screen_get_display (screen));

	str = g_string_new ("DISPLAY=");
	g_string_append (str, old_display);

	p = strrchr (str->str, '.');
	if (p && p >  strchr (str->str, ':'))
		g_string_truncate (str, p - str->str);

	g_string_append_printf (str, ".%d", gdk_screen_get_number (screen));

	return g_string_free (str, FALSE);
}

/**
 * get_exec_environment:
 *
 * Description: Modifies the current program environment to
 * ensure that $DISPLAY is set such that a launched application
 * inheriting this environment would appear on screen.
 *
 * Returns: a newly-allocated %NULL-terminated array of strings or
 * %NULL on error. Use g_strfreev() to free it.
 *
 * mainly ripped from egg_screen_exec_display_string in
 * gnome-panel/egg-screen-exec.c
 **/
static char **
get_exec_environment (XEvent *xevent)
{
	char **retval = NULL;
	int    i;
	int    display_index = -1;

	GdkScreen  *screen = NULL;

	GdkWindow *window = gdk_xid_table_lookup (xevent->xkey.root);

	if (window)
		screen = gdk_drawable_get_screen (GDK_DRAWABLE (window));

	g_return_val_if_fail (GDK_IS_SCREEN (screen), NULL);

	for (i = 0; environ [i]; i++)
		if (!strncmp (environ [i], "DISPLAY", 7))
			display_index = i;

	if (display_index == -1)
		display_index = i++;

	retval = g_new (char *, i + 1);

	for (i = 0; environ [i]; i++)
		if (i == display_index)
			retval [i] = screen_exec_display_string (screen);
		else
			retval [i] = g_strdup (environ [i]);

	retval [i] = NULL;

	return retval;
}

static gint
compare_bindings (gconstpointer a, gconstpointer b)
{
	Binding *key_a = (Binding*) a;
	char *key_b = (char*) b;

	return strcmp (key_b, key_a->gconf_key);
}

static gboolean
parse_binding (Binding *binding)
{
	g_return_val_if_fail (binding != NULL, FALSE);

	binding->key.keysym = 0;
	binding->key.state = 0;

	if (binding->binding_str == NULL ||
	    binding->binding_str[0] == '\0' ||
	    strcmp (binding->binding_str, "Disabled") == 0)
		return FALSE;

	if (egg_accelerator_parse_virtual (binding->binding_str, &binding->key.keysym, &binding->key.keycode, &binding->key.state) == FALSE)
		return FALSE;

	return TRUE;
}

static char *
entry_get_string (GConfEntry *entry)
{
	GConfValue *value = gconf_entry_get_value (entry);

	if (value == NULL || value->type != GCONF_VALUE_STRING)
		return NULL;

	return g_strdup (gconf_value_get_string (value));
}

static gboolean
bindings_get_entry (const char *subdir)
{
	Binding *new_binding;
	GSList *tmp_elem, *list, *li;
	char *gconf_key;
	char *action = NULL;
	char *key = NULL;
	gboolean ret = FALSE;
	GConfClient *client = gnome_settings_get_config_client ();

	g_return_val_if_fail (subdir != NULL, FALSE);

	/* value = gconf_entry_get_value (entry); */
	gconf_key = g_path_get_basename (subdir);

	if (!gconf_key)
		return FALSE;

	/* Get entries for this binding */
	list = gconf_client_all_entries (client, subdir, NULL);

	for (li = list; li != NULL; li = li->next) {
		GConfEntry *entry = li->data;
		char *key_name = g_path_get_basename (gconf_entry_get_key (entry));

		if (key_name == NULL)
			goto out;
		else if (strcmp (key_name, "action") == 0) {
			if (!action)
				action = entry_get_string (entry);
			else
				g_warning (_("Key Binding (%s) has its action defined multiple times\n"),
					   gconf_key);
		} else if (strcmp (key_name, "binding") == 0) {
			if (!key)
				key = entry_get_string (entry);
			else
				g_warning (_("Key Binding (%s) has its binding defined multiple times\n"),
					   gconf_key);
		}

		gconf_entry_free (entry);
	}

	if (!action || !key) {
		g_warning (_("Key Binding (%s) is incomplete\n"), gconf_key);
		goto out;
	}

	tmp_elem = g_slist_find_custom (binding_list, gconf_key,
					compare_bindings);

	if (!tmp_elem)
		new_binding = g_new0 (Binding, 1);
	else {
		new_binding = (Binding*) tmp_elem->data;
		g_free (new_binding->binding_str);
		g_free (new_binding->action);
	}

	new_binding->binding_str = key;
	new_binding->action = action;
	new_binding->gconf_key = gconf_key;

	new_binding->previous_key.keysym = new_binding->key.keysym;
	new_binding->previous_key.state = new_binding->key.state;
	new_binding->previous_key.keycode = new_binding->key.keycode;

	if (parse_binding (new_binding)) {
		binding_list = g_slist_append (binding_list, new_binding);
		ret = TRUE;
	} else {
		g_warning (_("Key Binding (%s) is invalid\n"), gconf_key);
		g_free (new_binding->binding_str);
		g_free (new_binding->action);
	}

 out:
	g_slist_free (list);
	return ret;
}

static gboolean
key_already_used (Binding *binding)
{
	GSList *li;

	for (li = binding_list; li != NULL; li = li->next) {
		Binding *tmp_binding =  (Binding*) li->data;

		if (tmp_binding != binding &&  tmp_binding->key.keycode == binding->key.keycode &&
		    tmp_binding->key.state == binding->key.state)
			return TRUE;
	}
	return FALSE;
}

static void
grab_key (GdkWindow *root, Key *key, int result, gboolean grab)
{
	gdk_error_trap_push ();
	if (grab)
		XGrabKey (GDK_DISPLAY(), key->keycode, (result | key->state),
			  GDK_WINDOW_XID (root), True, GrabModeAsync, GrabModeAsync);
	else
		XUngrabKey(GDK_DISPLAY(), key->keycode, (result | key->state),
			   GDK_WINDOW_XID (root));
	gdk_flush ();
	if (gdk_error_trap_pop ()) {
		g_warning (_("It seems that another application already has"
			     " access to key '%u'."), key->keycode);
	}
}

/* inspired from all_combinations from gnome-panel/gnome-panel/global-keys.c */
#define N_BITS 32
static void
do_grab (gboolean grab,
	 Key *key)
{
	int indexes[N_BITS];/*indexes of bits we need to flip*/
	int i, bit, bits_set_cnt;
	int uppervalue;
	guint mask_to_traverse = IGNORED_MODS & ~ key->state;

	bit = 0;
	for (i = 0; i < N_BITS; i++) {
		if (mask_to_traverse & (1<<i))
			indexes[bit++]=i;
	}

	bits_set_cnt = bit;

	uppervalue = 1<<bits_set_cnt;
	for (i = 0; i < uppervalue; i++) {
		GSList *l;
		int j, result = 0;

		for (j = 0; j < bits_set_cnt; j++) {
			if (i & (1<<j))
				result |= (1<<indexes[j]);
		}

		for (l = screens; l ; l = l->next) {
                  GdkScreen *screen = l->data;
                  grab_key (gdk_screen_get_root_window (screen), key, result,
			    grab);
		}
	}
}

static void
binding_register_keys (void)
{
	GSList *li;

	gdk_error_trap_push();

	/* Now check for changes and grab new key if not already used */
	for (li = binding_list ; li != NULL; li = li->next) {
		Binding *binding = (Binding *) li->data;

		if (binding->previous_key.keycode != binding->key.keycode ||
		    binding->previous_key.state != binding->key.state) {
			/* Ungrab key if it changed and not clashing with previously set binding */
			if (!key_already_used (binding)) {
				if (binding->previous_key.keycode)
					do_grab (FALSE, &binding->previous_key);
				do_grab (TRUE, &binding->key);

				binding->previous_key.keysym = binding->key.keysym;
				binding->previous_key.state = binding->key.state;
				binding->previous_key.keycode = binding->key.keycode;
			} else
				g_warning (_("Key Binding (%s) is already in use\n"), binding->binding_str);
		}
	}
	gdk_flush ();
	gdk_error_trap_pop();
}

static void
bindings_callback (GConfEntry *entry)
{
	/* ensure we get binding dir not a sub component */
	gchar** key_elems = g_strsplit (gconf_entry_get_key (entry), "/", 15);
	gchar* binding_entry  = g_strdup_printf ("/%s/%s/%s/%s", key_elems[1],
						 key_elems[2], key_elems[3],
						 key_elems[4]);
	g_strfreev (key_elems);

	bindings_get_entry (binding_entry);
	g_free (binding_entry);

	binding_register_keys ();
}

static GdkFilterReturn
keybindings_filter (GdkXEvent *gdk_xevent,
		    GdkEvent *event,
		    gpointer data)
{
	XEvent *xevent = (XEvent *)gdk_xevent;
	guint keycode, state;
	GSList *li;

	if(xevent->type != KeyPress)
		return GDK_FILTER_CONTINUE;

	keycode = xevent->xkey.keycode;
	state = xevent->xkey.state;

	for (li = binding_list; li != NULL; li = li->next) {
		Binding *binding = (Binding*) li->data;

		if (keycode == binding->key.keycode &&
		    (state & USED_MODS) == binding->key.state) {
			GError* error = NULL;
			gboolean retval;
			gchar **argv = NULL;
			gchar **envp = NULL;

			g_return_val_if_fail (binding->action != NULL, GDK_FILTER_CONTINUE);

			if (!g_shell_parse_argv (binding->action,
						 NULL, &argv,
						 &error))
				return GDK_FILTER_CONTINUE;

			envp = get_exec_environment (xevent);

			retval = g_spawn_async (NULL,
						argv,
						envp,
						G_SPAWN_SEARCH_PATH,
						NULL,
						NULL,
						NULL,
						&error);
			g_strfreev (argv);
			g_strfreev (envp);

			if (!retval) {
				GtkWidget *dialog = gtk_message_dialog_new (NULL, 0, GTK_MESSAGE_WARNING,
									    GTK_BUTTONS_CLOSE,
									    _("Error while trying to run (%s)\n"\
									      "which is linked to the key (%s)"),
									    binding->action,
									    binding->binding_str);
				g_signal_connect (dialog, "response",
						  G_CALLBACK (gtk_widget_destroy),
						  NULL);
				gtk_widget_show (dialog);
			}
			return GDK_FILTER_REMOVE;
		}
	}
	return GDK_FILTER_CONTINUE;
}

static gboolean
gnome_settings_module_keybindings_initialize (GnomeSettingsModule *module, GConfClient *client)
{
	GdkDisplay *dpy = gdk_display_get_default ();
	GdkScreen *screen;
	int screen_num = gdk_display_get_n_screens (dpy);
	int i;

	gnome_settings_register_config_callback (GCONF_BINDING_DIR, bindings_callback);

	for (i = 0; i < screen_num; i++) {
		screen = gdk_display_get_screen (dpy, i);
		gdk_window_add_filter (gdk_screen_get_root_window (screen),
				       keybindings_filter, NULL);
	}

	return TRUE;
}

static gboolean
gnome_settings_module_keybindings_start (GnomeSettingsModule *module)
{
	GSList *list, *li;

	list = gconf_client_all_dirs (gnome_settings_module_get_config_client (module), GCONF_BINDING_DIR, NULL);
	screens = get_screens_list ();

	for (li = list; li != NULL; li = li->next) {
		bindings_get_entry (li->data);
		g_free (li->data);
	}

	g_slist_free (list);

	binding_register_keys ();

	return TRUE;
}

static gboolean
gnome_settings_module_keybindings_stop (GnomeSettingsModule *module)
{
	return TRUE;
}
