/*
 * Copyright © 2003 Jonathan Blandford <jrb@gnome.org>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Red Hat not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  Red Hat makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * Authors:  Jonathan Blandford
 */

#ifndef TYPING_BREAK_SETTINGS_H
#define TYPING_BREAK_SETTINGS_H

#include <gconf/gconf.h>

void gnome_settings_typing_break_init (GConfClient *client);
void gnome_settings_typing_break_load (GConfClient *client);

#endif
