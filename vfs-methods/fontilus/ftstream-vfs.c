/* -*- mode: C; c-basic-offset: 4 -*-
 * fontilus - a collection of font utilities for GNOME
 * Copyright (C) 2002-2003  James Henstridge <james@daa.com.au>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdlib.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <libgnomevfs/gnome-vfs.h>

static unsigned long
vfs_stream_read(FT_Stream stream,
		unsigned long offset,
		unsigned char *buffer,
		unsigned long count)
{
    GnomeVFSHandle *handle = (GnomeVFSHandle *)stream->descriptor.pointer;
    GnomeVFSFileSize bytes_read = 0;

    if (gnome_vfs_seek(handle, GNOME_VFS_SEEK_START, offset) != GNOME_VFS_OK)
	return 0;
    if (count > 0) {
	if (gnome_vfs_read(handle, buffer, count, &bytes_read) != GNOME_VFS_OK)
	    return 0;
    }
    return bytes_read;
}

static void
vfs_stream_close(FT_Stream stream)
{
    GnomeVFSHandle *handle = (GnomeVFSHandle *)stream->descriptor.pointer;

    if (!handle)
	return;
    gnome_vfs_close(handle);

    stream->descriptor.pointer = NULL;
    stream->size               = 0;
    stream->base               = 0;    
}

static FT_Error
vfs_stream_open(FT_Stream stream,
		const char *uri)
{
    GnomeVFSHandle *handle;
    GnomeVFSFileInfo *finfo;

    if (!stream)
	return FT_Err_Invalid_Stream_Handle;

    if (gnome_vfs_open(&handle, uri,
	       GNOME_VFS_OPEN_READ | GNOME_VFS_OPEN_RANDOM) != GNOME_VFS_OK) {
	g_message("could not open URI");
	return FT_Err_Cannot_Open_Resource;
    }

    finfo = gnome_vfs_file_info_new();
    if (gnome_vfs_get_file_info_from_handle(handle, finfo,0) != GNOME_VFS_OK) {
	g_warning("could not get file info");
	gnome_vfs_file_info_unref(finfo);
	gnome_vfs_close(handle);
	return FT_Err_Cannot_Open_Resource;
    }

    if ((finfo->valid_fields & GNOME_VFS_FILE_INFO_FIELDS_SIZE) == 0) {
	g_warning("file info did not include file size");
	gnome_vfs_file_info_unref(finfo);
	gnome_vfs_close(handle);
	return FT_Err_Cannot_Open_Resource;
    }	
    stream->size = finfo->size;
    gnome_vfs_file_info_unref(finfo);

    stream->descriptor.pointer = handle;
    stream->pathname.pointer   = NULL;
    stream->pos                = 0;

    stream->read  = vfs_stream_read;
    stream->close = vfs_stream_close;

    return FT_Err_Ok;
}

/* load a typeface from a URI */
FT_Error
FT_New_Face_From_URI(FT_Library           library,
		     const gchar*         uri,
		     FT_Long              face_index,
		     FT_Face             *aface)
{
    FT_Open_Args args;
    FT_Stream stream;
    FT_Error error;

    if ((stream = calloc(1, sizeof(*stream))) == NULL)
	return FT_Err_Out_Of_Memory;

    error = vfs_stream_open(stream, uri);
    if (error != FT_Err_Ok) {
	free(stream);
	return error;
    }

    /* freetype-2.1.3 accidentally broke compatibility. */
#if defined(FT_OPEN_STREAM) && !defined(ft_open_stream)
#  define ft_open_stream FT_OPEN_STREAM
#endif
    args.flags  = ft_open_stream;
    args.stream = stream;

    error = FT_Open_Face(library, &args, face_index, aface);

    if (error != FT_Err_Ok) {
	if (stream->close) stream->close(stream);
	free(stream);
	return error;
    }

    /* so that freetype will free the stream */
    (*aface)->face_flags &= ~FT_FACE_FLAG_EXTERNAL_STREAM;

    return error;
}
