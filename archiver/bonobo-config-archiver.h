/**
 * bonobo-config-archiver.h: xml configuration database implementation, with
 * interface to the archiver
 *
 * Author:
 *   Dietmar Maurer  (dietmar@ximian.com)
 *   Bradford Hovinen  <hovinen@ximian.com>
 *
 * Copyright 2000 Ximian, Inc.
 */
#ifndef __BONOBO_CONFIG_ARCHIVER_H__
#define __BONOBO_CONFIG_ARCHIVER_H__

#include <stdio.h>
#include <bonobo-conf/bonobo-config-database.h>
#include <gnome-xml/tree.h>
#include <gnome-xml/parser.h>
#include <bonobo/bonobo-event-source.h>

#include "archive.h"
#include "location.h"

BEGIN_GNOME_DECLS

#define BONOBO_CONFIG_ARCHIVER_TYPE        (bonobo_config_archiver_get_type ())
#define BONOBO_CONFIG_ARCHIVER(o)	   (GTK_CHECK_CAST ((o), BONOBO_CONFIG_ARCHIVER_TYPE, BonoboConfigArchiver))
#define BONOBO_CONFIG_ARCHIVER_CLASS(k)    (GTK_CHECK_CLASS_CAST((k), BONOBO_CONFIG_ARCHIVER_TYPE, BonoboConfigArchiverClass))
#define BONOBO_IS_CONFIG_ARCHIVER(o)       (GTK_CHECK_TYPE ((o), BONOBO_CONFIG_ARCHIVER_TYPE))
#define BONOBO_IS_CONFIG_ARCHIVER_CLASS(k) (GTK_CHECK_CLASS_TYPE ((k), BONOBO_CONFIG_ARCHIVER_TYPE))

typedef struct _DirData DirData;

struct _DirData {
	char       *name;
	GSList     *entries;
	GSList     *subdirs;
	xmlNodePtr  node;
	DirData    *dir;
};

typedef struct {
	char       *name;
	CORBA_any  *value;
	xmlNodePtr  node;
	DirData    *dir;
} DirEntry;

typedef struct _BonoboConfigArchiver        BonoboConfigArchiver;

struct _BonoboConfigArchiver {
	BonoboConfigDatabase  base;
	
	char                 *filename;
	FILE                 *fp;
	xmlDocPtr             doc;
	DirData              *dir;
	guint                 time_id;

	Archive              *archive;
	Location             *location;
	gchar                *backend_id;
	gchar                *real_name;

	BonoboEventSource    *es;
};

typedef struct {
	BonoboConfigDatabaseClass parent_class;
} BonoboConfigArchiverClass;


GtkType		      
bonobo_config_archiver_get_type  (void);

Bonobo_ConfigDatabase
bonobo_config_archiver_new (const char *backend_id, const char *location_id);

END_GNOME_DECLS

#endif /* ! __BONOBO_CONFIG_ARCHIVER_H__ */
