/* -*- mode: c; style: linux -*- */

/* gnome-keyboard-properties-xkb.c
 * Copyright (C) 2003 Sergey V. Oudaltsov
 *
 * Written by: Sergey V. Oudaltsov <svu@users.sourceforge.net>
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
#  include <config.h>
#endif

#include <gnome.h>
#include <glade/glade.h>

#include "capplet-util.h"

#include "gnome-keyboard-properties-xkb.h"
#include "libkbdraw/keyboard-drawing.h"

static GtkWidget * previewWindow = NULL;
static gint        preview_origin_x = -1;
static gint        preview_origin_y = -1;

static KeyboardDrawingGroupLevel groupsLevels[] = {{0,0},{0,1},{0,2},{0,3}};
static KeyboardDrawingGroupLevel * pGroupsLevels[] = {
groupsLevels, groupsLevels+1, groupsLevels+2, groupsLevels+3 };

static gboolean 
click_on_X (GtkWidget *widget, GdkEvent *event, GladeXML *dialog)
{
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (WID ("enable_preview")), FALSE);
  /* stop processing! */
  return TRUE;
}

GtkWidget*
create_preview_widget (GladeXML * dialog)
{
  GtkWidget *kbdraw = keyboard_drawing_new ();
  //keyboard_drawing_set_track_group (KEYBOARD_DRAWING (kbdraw), TRUE);
  //keyboard_drawing_set_track_config (KEYBOARD_DRAWING (kbdraw), TRUE);

  /* show levels 1 and 2 - hoping that no more levels are used */
  
  //keyboard_drawing_set_levels (KEYBOARD_DRAWING (kbdraw), 0, 1);
  keyboard_drawing_set_groups_levels (KEYBOARD_DRAWING (kbdraw), pGroupsLevels);
  return kbdraw;
}

static void
init_preview (GladeXML * dialog)
{
  GtkWidget *kbdraw = create_preview_widget (dialog);
  GtkWidget *capplet_dialog = glade_xml_get_widget(dialog, "keyboard_dialog");

  previewWindow = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_destroy_with_parent (GTK_WINDOW (previewWindow), TRUE);
  gtk_window_set_type_hint (GTK_WINDOW (previewWindow), GDK_WINDOW_TYPE_HINT_UTILITY);
  gtk_window_set_transient_for (GTK_WINDOW(previewWindow), GTK_WINDOW(capplet_dialog));

  gtk_window_set_skip_pager_hint(GTK_WINDOW (previewWindow), TRUE);
  gtk_window_set_skip_taskbar_hint(GTK_WINDOW (previewWindow), TRUE);

  gtk_window_set_resizable(GTK_WINDOW (previewWindow), TRUE);
  gtk_window_set_default_size(GTK_WINDOW (previewWindow), 500, 300);

  gtk_window_set_title(GTK_WINDOW (previewWindow), _("Keyboard layout preview"));

  gtk_container_add (GTK_CONTAINER (previewWindow), kbdraw);

  g_signal_connect (G_OBJECT (previewWindow), "delete-event",
                    G_CALLBACK (click_on_X), dialog);
}

void
preview_toggled (GladeXML * dialog, GtkWidget * button)
{
  gboolean doShow = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (button));
  
  if (doShow && previewWindow == NULL)
    init_preview (dialog);

  if (doShow)
    {
      gtk_widget_show_all (previewWindow);
      if (preview_origin_x >= 0)
         gtk_window_move (GTK_WINDOW (previewWindow), 
			  preview_origin_x, preview_origin_y);
    }
  else
    if (previewWindow != NULL)
      {
        gtk_window_get_position (GTK_WINDOW (previewWindow), 
				 &preview_origin_x, &preview_origin_y);
        gtk_widget_hide_all (previewWindow);
      }
}
