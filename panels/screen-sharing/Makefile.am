# This is used in PANEL_CFLAGS
cappletname = screen-sharing

uidir = $(pkgdatadir)/ui/screen-sharing
dist_ui_DATA = \
	screen-sharing-panel.ui

INCLUDES =						\
	$(PANEL_CFLAGS)					\
	$(SCREEN_SHARING_PANEL_CFLAGS) \
	-DG_LOG_DOMAIN="\"$(PACKAGE)\"" \
	-DGNOMECC_UI_DIR="\"$(uidir)\""			\
	-DGNOMELOCALEDIR="\"$(datadir)/locale\"" \
	$(NULL)

ccpanelsdir = $(PANELS_DIR)
ccpanels_LTLIBRARIES = libscreen-sharing.la

libscreen_sharing_la_SOURCES = \
	screen-sharing-module.c \
	vino-message-box.c \
	vino-message-box.h \
	cc-screen-sharing-panel.c \
	cc-screen-sharing-panel.h \
	vino-radio-button.c \
	vino-radio-button.h

libscreen_sharing_la_LIBADD = $(PANEL_LIBS) $(SCREEN_SHARING_PANEL_LIBS)
libscreen_sharing_la_LDFLAGS = $(PANEL_LDFLAGS)

@INTLTOOL_DESKTOP_RULE@

desktopdir = $(datadir)/applications
desktop_in_files = unity-screen-sharing-panel.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

CLEANFILES = $(desktop_in_files) $(desktop_DATA)

-include $(top_srcdir)/git.mk
