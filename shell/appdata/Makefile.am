@INTLTOOL_XML_RULE@
appdatadir = $(datadir)/appdata
appdata_DATA = $(appdata_in_files:.xml.in=.xml)
appdata_in_files =						\
	unity-control-center.appdata.xml.in

test:
EXTRA_DIST = $(appdata_in_files)

CLEANFILES =							\
	unity-control-center.appdata.xml

-include $(top_srcdir)/git.mk
