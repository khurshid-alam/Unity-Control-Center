NULL =

@INTLTOOL_DESKTOP_RULE@

appsdir = $(datadir)/applications
apps_in_files = unity-sound-panel.desktop.in
apps_DATA = $(apps_in_files:.desktop.in=.desktop)

EXTRA_DIST =					\
	unity-sound-panel.desktop.in.in		\
	$(NULL)

CLEANFILES =					\
	unity-sound-panel.desktop		\
	$(NULL)

-include $(top_srcdir)/git.mk
