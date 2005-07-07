/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* file-transfer-dialog.c
 * Copyright (C) 2002 Ximian, Inc.
 *
 * Written by Rachel Hestilow <hestilow@ximian.com> 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libgnomevfs/gnome-vfs-async-ops.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <limits.h>

#include "file-transfer-dialog.h"

enum
{
	PROP_0,
	PROP_FROM_URI,
	PROP_TO_URI,
	PROP_FRACTION_COMPLETE,
	PROP_NTH_URI,
	PROP_TOTAL_URIS
};

enum
{
	CANCEL,
	DONE,
	LAST_SIGNAL
};

guint file_transfer_dialog_signals[LAST_SIGNAL] = {0, };

struct _FileTransferDialogPrivate
{
	GtkWidget *progress; 
	GtkWidget *status;
	GtkWidget *current;
	GtkWidget *from;
	GtkWidget *to;
	guint nth;
	guint total;
	GnomeVFSAsyncHandle *handle;
};

static GObjectClass *parent_class;

static void
file_transfer_dialog_cancel (FileTransferDialog *dlg)
{
	if (dlg->priv->handle)
	{
		gnome_vfs_async_cancel (dlg->priv->handle);
		dlg->priv->handle = NULL;
	}
}

static void
file_transfer_dialog_finalize (GObject *obj)
{
	FileTransferDialog *dlg = FILE_TRANSFER_DIALOG (obj);
		
	g_free (dlg->priv);

	if (parent_class->finalize)
		parent_class->finalize (G_OBJECT (dlg));
}

static void
file_transfer_dialog_update_num_files (FileTransferDialog *dlg)
{
	gchar *str = g_strdup_printf (_("Copying file: %i of %i"),
				      dlg->priv->nth, dlg->priv->total);
	gtk_progress_bar_set_text (GTK_PROGRESS_BAR (dlg->priv->progress), str);
	g_free (str);
}

static void
file_transfer_dialog_response (GtkDialog *dlg, gint response_id)
{
	g_signal_emit (G_OBJECT (dlg),
		       file_transfer_dialog_signals[CANCEL], 0, NULL); 
}

static void
file_transfer_dialog_set_prop (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	FileTransferDialog *dlg = FILE_TRANSFER_DIALOG (object);
	gchar *str;
	gchar *str2;
	gchar *base;
	gchar *escaped;

	switch (prop_id)
	{
	case PROP_FROM_URI:
		base = g_path_get_basename (g_value_get_string (value));
		escaped = gnome_vfs_unescape_string_for_display (base);

		str = g_strdup_printf (_("Copying '%s'"), escaped);
		str2 = g_strdup_printf ("<i>%s</i>", str);
		gtk_label_set_markup (GTK_LABEL (dlg->priv->current),
				      str2);
		g_free (base);
		g_free (escaped);
		g_free (str);
		g_free (str2);

		base = g_path_get_dirname (g_value_get_string (value));
		escaped = gnome_vfs_format_uri_for_display (base);

		gtk_label_set_text (GTK_LABEL (dlg->priv->from),
				    escaped);
		g_free (base);
		g_free (escaped);
		break;
	case PROP_TO_URI:
		base = g_path_get_dirname (g_value_get_string (value));
		escaped = gnome_vfs_format_uri_for_display (base);

		gtk_label_set_text (GTK_LABEL (dlg->priv->to),
				    escaped);
		g_free (base);
		g_free (escaped);
		break;
	case PROP_FRACTION_COMPLETE:
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (dlg->priv->progress), g_value_get_double (value));
		break;
	case PROP_NTH_URI:
		dlg->priv->nth = g_value_get_uint (value);
		file_transfer_dialog_update_num_files (dlg);
		break;
	case PROP_TOTAL_URIS:
		dlg->priv->total = g_value_get_uint (value);
		file_transfer_dialog_update_num_files (dlg);
		break;
	}
}
	
static void
file_transfer_dialog_get_prop (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	FileTransferDialog *dlg = FILE_TRANSFER_DIALOG (object);
	
	switch (prop_id)
	{
	case PROP_NTH_URI:
		g_value_set_uint (value, dlg->priv->nth);
		break;
	case PROP_TOTAL_URIS:
		g_value_set_uint (value, dlg->priv->total);
		break;
	}
}

static void
file_transfer_dialog_class_init (FileTransferDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	klass->cancel = file_transfer_dialog_cancel;
	object_class->finalize = file_transfer_dialog_finalize;
	object_class->get_property = file_transfer_dialog_get_prop;
	object_class->set_property = file_transfer_dialog_set_prop;

	GTK_DIALOG_CLASS (klass)->response = file_transfer_dialog_response;

	g_object_class_install_property
		(object_class, PROP_FROM_URI,
		 g_param_spec_string ("from_uri",
				      _("From URI"),
				      _("URI currently transferring from"),
				      NULL,
				      G_PARAM_READWRITE));
	
	g_object_class_install_property
		(object_class, PROP_TO_URI,
		 g_param_spec_string ("to_uri",
				      _("To URI"),
				      _("URI currently transferring to"),
				      NULL,
				      G_PARAM_WRITABLE));

	g_object_class_install_property
		(object_class, PROP_FRACTION_COMPLETE,
		 g_param_spec_double ("fraction_complete",
				      _("Fraction completed"),
				      _("Fraction of transfer currently completed"),
				      0, 1, 0,
				      G_PARAM_WRITABLE));

	g_object_class_install_property
		(object_class, PROP_NTH_URI,
		 g_param_spec_uint ("nth_uri",
				    _("Current URI index"),
				    _("Current URI index - starts from 1"),
				    1, INT_MAX, 1,
				    G_PARAM_READWRITE));

	g_object_class_install_property
		(object_class, PROP_TOTAL_URIS,
		 g_param_spec_uint ("total_uris",
				    _("Total URIs"),
				    _("Total number of URIs"),
				    1, INT_MAX, 1,
				    G_PARAM_READWRITE));

	file_transfer_dialog_signals[CANCEL] =
		g_signal_new ("cancel",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (FileTransferDialogClass, cancel),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	file_transfer_dialog_signals[DONE] =
		g_signal_new ("done",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (FileTransferDialogClass, done),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	parent_class = 
		G_OBJECT_CLASS (g_type_class_ref (GTK_TYPE_DIALOG));
}

/**
 * eel_gtk_label_make_bold.
 *
 * Switches the font of label to a bold equivalent.
 * @label: The label.
 **/
static void
eel_gtk_label_make_bold (GtkLabel *label)
{
	PangoFontDescription *font_desc;

	font_desc = pango_font_description_new ();

	pango_font_description_set_weight (font_desc,
					   PANGO_WEIGHT_BOLD);

	/* This will only affect the weight of the font, the rest is
	 * from the current state of the widget, which comes from the
	 * theme or user prefs, since the font desc only has the
	 * weight flag turned on.
	 */
	gtk_widget_modify_font (GTK_WIDGET (label), font_desc);

	pango_font_description_free (font_desc);
}

/* from nautilus */
static void
create_titled_label (GtkTable   *table,
		     int         row,
		     GtkWidget **title_widget,
		     GtkWidget **label_text_widget)
{
	*title_widget = gtk_label_new ("");
	eel_gtk_label_make_bold (GTK_LABEL (*title_widget));
	gtk_misc_set_alignment (GTK_MISC (*title_widget), 1, 0);
	gtk_table_attach (table, *title_widget,
			  0, 1,
			  row, row + 1,
			  GTK_FILL, 0,
			  0, 0);
	gtk_widget_show (*title_widget);

	*label_text_widget = gtk_label_new ("");
	gtk_label_set_ellipsize (GTK_LABEL (label_text_widget), PANGO_ELLIPSIZE_END);
	gtk_table_attach (table, *label_text_widget,
			  1, 2,
			  row, row + 1,
			  GTK_FILL | GTK_EXPAND, 0,
			  0, 0);
	gtk_widget_show (*label_text_widget);
	gtk_misc_set_alignment (GTK_MISC (*label_text_widget), 0, 0);
}

static void
file_transfer_dialog_init (FileTransferDialog *dlg)
{
	GtkWidget *vbox;
	GtkWidget *hbox;
	GtkWidget *progress_vbox;
	GtkWidget *table;
	GtkWidget *label;
	char      *markup;

	dlg->priv = g_new0 (FileTransferDialogPrivate, 1);
	
	gtk_container_set_border_width (GTK_CONTAINER (GTK_DIALOG (dlg)->vbox),
					4);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dlg)->vbox), 4);

	vbox = gtk_vbox_new (FALSE, 6);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 6);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), vbox, TRUE, TRUE, 0);

	dlg->priv->status = gtk_label_new ("");
	markup = g_strdup_printf ("<big><b>%s</b></big>", _("Copying files"));
	gtk_label_set_markup (GTK_LABEL (dlg->priv->status), markup);
	g_free (markup);

	gtk_misc_set_alignment (GTK_MISC (dlg->priv->status), 0.0, 0.0);
	
	gtk_box_pack_start (GTK_BOX (vbox), dlg->priv->status, FALSE, FALSE, 0);

	hbox = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 0);

	table = gtk_table_new (2, 2, FALSE);
	gtk_table_set_row_spacings (GTK_TABLE (table), 4);
	gtk_table_set_col_spacings (GTK_TABLE (table), 4);

	create_titled_label (GTK_TABLE (table), 0,
			     &label, 
			     &dlg->priv->from);
	gtk_label_set_text (GTK_LABEL (label), _("From:"));
	create_titled_label (GTK_TABLE (table), 1,
			     &label, 
			     &dlg->priv->to);
	gtk_label_set_text (GTK_LABEL (label), _("To:"));

	gtk_box_pack_start (GTK_BOX (vbox), GTK_WIDGET (table), FALSE, FALSE, 0);

	progress_vbox = gtk_vbox_new (TRUE, 0);
	gtk_box_pack_start (GTK_BOX (vbox), progress_vbox, FALSE, FALSE, 0);

	dlg->priv->progress = gtk_progress_bar_new ();
	gtk_box_pack_start (GTK_BOX (progress_vbox),
			    dlg->priv->progress, FALSE, FALSE, 0);

	dlg->priv->current = gtk_label_new ("");
	gtk_box_pack_start (GTK_BOX (progress_vbox),
			    dlg->priv->current, FALSE, FALSE, 0);
	gtk_misc_set_alignment (GTK_MISC (dlg->priv->current), 0.0, 0.5);

	gtk_dialog_add_button (GTK_DIALOG (dlg),
			       GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

	gtk_window_set_title (GTK_WINDOW (dlg),
			      _("Copying files"));
	gtk_dialog_set_has_separator (GTK_DIALOG (dlg), FALSE);
	gtk_container_set_border_width (GTK_CONTAINER (dlg), 6);

	gtk_widget_show_all (GTK_DIALOG (dlg)->vbox);
}
	
GType
file_transfer_dialog_get_type (void)
{
	static GType file_transfer_dialog_type = 0;

	if (!file_transfer_dialog_type)
	{
		static GTypeInfo file_transfer_dialog_info =
		{
			sizeof (FileTransferDialogClass),
			NULL, /* GBaseInitFunc */
			NULL, /* GBaseFinalizeFunc */
			(GClassInitFunc) file_transfer_dialog_class_init,
			NULL, /* GClassFinalizeFunc */
			NULL, /* data */
			sizeof (FileTransferDialog),
			0, /* n_preallocs */
			(GInstanceInitFunc) file_transfer_dialog_init,
			NULL
		};

		file_transfer_dialog_type =
			g_type_register_static (GTK_TYPE_DIALOG,
					 	"FileTransferDialog",
						&file_transfer_dialog_info,
						0);
	}

	return file_transfer_dialog_type;
}

GtkWidget*
file_transfer_dialog_new (void)
{
	return GTK_WIDGET (g_object_new (file_transfer_dialog_get_type (),
					 NULL));
}

static int
file_transfer_dialog_update_cb (GnomeVFSAsyncHandle *handle,
				GnomeVFSXferProgressInfo *info,
				gpointer data)
{
	FileTransferDialog *dlg = FILE_TRANSFER_DIALOG (data);

	if (info->status == GNOME_VFS_XFER_PROGRESS_STATUS_VFSERROR)
		return GNOME_VFS_XFER_ERROR_ACTION_ABORT;
		
	if (info->source_name)
		g_object_set (G_OBJECT (dlg),
			      "from_uri", info->source_name,
			      NULL);
	if (info->target_name)
		g_object_set (G_OBJECT (dlg),
			      "to_uri", info->target_name,
			      NULL);

	if (info->bytes_total)
		g_object_set (G_OBJECT (dlg),
			      "fraction_complete", (double) info->total_bytes_copied / (double) info->bytes_total,
			      NULL);

	if (info->file_index && info->files_total)
		g_object_set (G_OBJECT (dlg),
			      "nth_uri", info->file_index,
			      "total_uris", info->files_total,
			      NULL);
	
	switch (info->phase)
	{
	case GNOME_VFS_XFER_PHASE_INITIAL:
		{
			char *str = g_strdup_printf ("<i>%s</i>", _("Connecting..."));
			gtk_label_set_markup (GTK_LABEL (dlg->priv->current),
					      str);
			g_free (str);
		}
		break;
	case GNOME_VFS_XFER_PHASE_READYTOGO:
	case GNOME_VFS_XFER_PHASE_OPENSOURCE:
		break;
	case GNOME_VFS_XFER_PHASE_COMPLETED:
		g_signal_emit (G_OBJECT (dlg),
			       file_transfer_dialog_signals[DONE],
			       0, NULL);
		return 0;
	default:
		break;
	}

	return 1;
}

GnomeVFSResult
file_transfer_dialog_wrap_async_xfer (FileTransferDialog *dlg,
				      GList *source_uri_list,
				      GList *target_uri_list,
				      GnomeVFSXferOptions xfer_options,
				      GnomeVFSXferErrorMode error_mode,
				      GnomeVFSXferOverwriteMode overwrite_mode,
				      int priority)
{
	g_return_val_if_fail (IS_FILE_TRANSFER_DIALOG (dlg),
			      GNOME_VFS_ERROR_BAD_PARAMETERS);

	return gnome_vfs_async_xfer (&dlg->priv->handle,
				     source_uri_list,
				     target_uri_list,
				     xfer_options,
				     error_mode,
				     overwrite_mode,
				     priority,
				     file_transfer_dialog_update_cb,
				     dlg,
				     NULL,
				     NULL
				   );
}
