m4_define([unity_control_center_version], 14.04.3)
AC_INIT([unity-control-center], [unity_control_center_version])

AC_CONFIG_SRCDIR([shell])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([1.11 no-dist-gzip dist-xz tar-ustar check-news])
AM_MAINTAINER_MODE([enable])
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

# Check for programs
AC_PROG_CC
AM_PROG_CC_C_O
AC_HEADER_STDC

# Initialize libtool
LT_PREREQ([2.2])
LT_INIT

# .so version for libunity-control-center
LIBUNITYCONTROLCENTER_CURRENT=1
LIBUNITYCONTROLCENTER_REVISION=0
LIBUNITYCONTROLCENTER_AGE=0
AC_SUBST(LIBUNITYCONTROLCENTER_CURRENT)
AC_SUBST(LIBUNITYCONTROLCENTER_REVISION)
AC_SUBST(LIBUNITYCONTROLCENTER_AGE)

# Internationalization support

IT_PROG_INTLTOOL([0.40.1])

GETTEXT_PACKAGE=unity-control-center
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Gettext package])

GNOME_DEBUG_CHECK
GNOME_COMPILE_WARNINGS([maximum])

AC_PATH_XTRA
x_libs="$X_PRE_LIBS $X_LIBS -lX11 $X_EXTRA_LIBS"

AC_PATH_PROG([GLIB_MKENUMS],[glib-mkenums])

AC_ARG_ENABLE(documentation,
              AC_HELP_STRING([--enable-documentation],
                             [build documentation]),,
              enable_documentation=yes)
if test x$enable_documentation = xyes; then
   AC_PATH_PROG([XSLTPROC], [xsltproc])
   if test x$XSLTPROC = x; then
      AC_MSG_ERROR([xsltproc is required to build documentation])
   fi
fi
AM_CONDITIONAL(BUILD_DOCUMENTATION, test x$enable_documentation = xyes)

dnl Region panel
savecppflags=$CPPFLAGS
CPPFLAGS="$CPPFLAGS $X_CFLAGS"
AC_CHECK_HEADERS([X11/Xlib.h])
AC_CHECK_LIB(Xxf86misc, XF86MiscQueryExtension, [
  AC_CHECK_HEADERS([X11/extensions/xf86misc.h], [XF86MISC_LIBS="-lXxf86misc"],[],
[#if HAVE_X11_XLIB_H
#include <X11/Xlib.h>
#endif
])])
AC_SUBST(XF86MISC_LIBS)
AC_CHECK_HEADERS(X11/extensions/XKB.h)
CPPFLAGS=$savecppflags

AC_CHECK_LIB(m, floor)

# IBus support
IBUS_REQUIRED_VERSION=1.4.99

AC_ARG_ENABLE(ibus,
        AS_HELP_STRING([--disable-ibus],
                       [Disable IBus support]),
        enable_ibus=$enableval,
        enable_ibus=yes)

if test "x$enable_ibus" = "xyes" ; then
        IBUS_MODULE="ibus-1.0 >= $IBUS_REQUIRED_VERSION"
        AC_DEFINE(HAVE_IBUS, 1, [Defined if IBus support is enabled])
else
        IBUS_MODULE=
fi

# Fcitx support

AC_ARG_ENABLE(fcitx,
        AS_HELP_STRING([--disable-fcitx],
                       [Disable Fcitx support]),
        enable_fcitx=$enableval,
        enable_fcitx=yes)

if test "x$enable_fcitx" = "xyes" ; then
        FCITX_MODULE="fcitx-config fcitx-gclient"
        AC_DEFINE(HAVE_FCITX, 1, [Defined if Fcitx support is enabled])
else
        FCITX_MODULE=
fi

AM_CONDITIONAL(HAVE_FCITX, test "x$enable_fcitx" == "xyes")

dnl ==============================================
dnl Check that we meet the  dependencies
dnl ==============================================

GLIB_REQUIRED_VERSION=2.31.2
GTK_REQUIRED_VERSION=3.10.0
PA_REQUIRED_VERSION=2.0
CANBERRA_REQUIRED_VERSION=0.13
GDKPIXBUF_REQUIRED_VERSION=2.23.0
POLKIT_REQUIRED_VERSION=0.103
NETWORK_MANAGER_REQUIRED_VERSION=0.8.992
LIBNOTIFY_REQUIRED_VERSION=0.7.3
GNOME_DESKTOP_REQUIRED_VERSION=3.5.91
SCHEMAS_REQUIRED_VERSION=3.15.4
LIBWACOM_REQUIRED_VERSION=0.7
CLUTTER_REQUIRED_VERSION=1.11.3
GOA_REQUIRED_VERSION=3.18.0
ACCOUNTSSERVICE_REQUIRED_VERSION=0.6.30

COMMON_MODULES="gtk+-3.0 >= $GTK_REQUIRED_VERSION
 glib-2.0 >= $GLIB_REQUIRED_VERSION
 gthread-2.0
 gio-2.0
 gio-unix-2.0
 gsettings-desktop-schemas >= $SCHEMAS_REQUIRED_VERSION
 libnotify >= $LIBNOTIFY_REQUIRED_VERSION"

PKG_CHECK_MODULES(LIBUNITY_CONTROL_CENTER, $COMMON_MODULES
                  polkit-gobject-1 >= $POLKIT_REQUIRED_VERSION)
PKG_CHECK_MODULES(LIBLANGUAGE, $COMMON_MODULES gnome-desktop-3.0 fontconfig)
PKG_CHECK_MODULES(LIBSHORTCUTS, $COMMON_MODULES x11)
PKG_CHECK_MODULES(SHELL, $COMMON_MODULES libgnome-menu-3.0 gio-unix-2.0 x11
                  polkit-gobject-1 >= $POLKIT_REQUIRED_VERSION)
PKG_CHECK_MODULES(APPEARANCE_PANEL, $COMMON_MODULES libxml-2.0 gnome-desktop-3.0
                  gdk-pixbuf-2.0 >= $GDKPIXBUF_REQUIRED_VERSION)
PKG_CHECK_MODULES(DATETIME_PANEL, $COMMON_MODULES timezonemap geonames
                  polkit-gobject-1 >= $POLKIT_REQUIRED_VERSION)
PKG_CHECK_MODULES(DISPLAY_PANEL, $COMMON_MODULES libunity-settings-daemon x11)
PKG_CHECK_MODULES(INFO_PANEL, $COMMON_MODULES libgtop-2.0 gl x11
		  polkit-gobject-1 >= $POLKIT_REQUIRED_VERSION webkit2gtk-4.0)
PKG_CHECK_MODULES(KEYBOARD_PANEL, $COMMON_MODULES
                  gnome-desktop-3.0 >= $GNOME_DESKTOP_REQUIRED_VERSION
                  x11)
PKG_CHECK_MODULES(MEDIA_PANEL, $COMMON_MODULES)
PKG_CHECK_MODULES(MOUSE_PANEL, $COMMON_MODULES xi >= 1.2
                  unity-settings-daemon x11)
PKG_CHECK_MODULES(NETWORK_PANEL, $COMMON_MODULES)
PKG_CHECK_MODULES(POWER_PANEL, $COMMON_MODULES upower-glib >= 0.9.1
                  unity-settings-daemon)
PKG_CHECK_MODULES(COLOR_PANEL, $COMMON_MODULES colord >= 0.1.8)
PKG_CHECK_MODULES(PRINTERS_PANEL, $COMMON_MODULES
                  polkit-gobject-1 >= $POLKIT_REQUIRED_VERSION)
PKG_CHECK_MODULES(REGION_PANEL, $COMMON_MODULES
                  polkit-gobject-1 >= $POLKIT_REQUIRED_VERSION
                  gnome-desktop-3.0 >= $GNOME_DESKTOP_REQUIRED_VERSION
                  $IBUS_MODULE
                  $FCITX_MODULE
                  x11)
PKG_CHECK_MODULES(REMOTE_LOGIN_HELPER, glib-2.0 >= $GLIB_REQUIRED_VERSION gio-2.0)
PKG_CHECK_MODULES(SCREEN_PANEL, $COMMON_MODULES)
PKG_CHECK_MODULES(SHARING_PANEL, $COMMON_MODULES)
PKG_CHECK_MODULES(SOUND_PANEL, $COMMON_MODULES libxml-2.0
                  libcanberra-gtk3 >= $CANBERRA_REQUIRED_VERSION
                  libpulse >= $PA_REQUIRED_VERSION
                  libpulse-mainloop-glib >= $PA_REQUIRED_VERSION)
PKG_CHECK_MODULES(UNIVERSAL_ACCESS_PANEL, $COMMON_MODULES)
PKG_CHECK_MODULES(USER_ACCOUNTS_PANEL, $COMMON_MODULES
                  polkit-gobject-1 >= $POLKIT_REQUIRED_VERSION
                  gnome-desktop-3.0
                  gdk-pixbuf-2.0 >= $GDKPIXBUF_REQUIRED_VERSION
                  pwquality
                  accountsservice >= $ACCOUNTSSERVICE_REQUIRED_VERSION)
PKG_CHECK_MODULES(BLUETOOTH_PANEL, $COMMON_MODULES)

AM_PROG_VALAC([0.20.0])

GDESKTOP_PREFIX=`$PKG_CONFIG --variable prefix gsettings-desktop-schemas`
AC_SUBST(GDESKTOP_PREFIX)

# Check for NetworkManager ~0.9
PKG_CHECK_MODULES(NETWORK_MANAGER, NetworkManager >= $NETWORK_MANAGER_REQUIRED_VERSION
                  libnm-glib >= $NETWORK_MANAGER_REQUIRED_VERSION
                  libnm-util >= $NETWORK_MANAGER_REQUIRED_VERSION
                  libnm-gtk >= $NETWORK_MANAGER_REQUIRED_VERSION,
                  [have_networkmanager=yes], have_networkmanager=no)
if test "x$have_networkmanager" = xno ; then
        AC_MSG_WARN(*** Network panel will not be built (NetworkManager ~0.9 or newer not found) ***)
fi
AM_CONDITIONAL(BUILD_NETWORK, [test x$have_networkmanager = xyes])

# Check for CUPS 1.4 or newer
AC_ARG_ENABLE([cups],
              AS_HELP_STRING([--disable-cups], [disable CUPS support (default: enabled)]),,
              [enable_cups=yes])

if test x"$enable_cups" != x"no" ; then
  AC_PROG_SED

  AC_PATH_PROG(CUPS_CONFIG, cups-config)

  if test x$CUPS_CONFIG = x; then
    AC_MSG_ERROR([cups-config not found but CUPS support requested])
  fi

  CUPS_API_VERSION=`$CUPS_CONFIG --api-version`
  CUPS_API_MAJOR=`echo $ECHO_N $CUPS_API_VERSION | cut -d . -f 1`
  CUPS_API_MINOR=`echo $ECHO_N $CUPS_API_VERSION | cut -d . -f 2`

  AC_CHECK_HEADERS([cups/cups.h cups/http.h cups/ipp.h cups/ppd.h],,
                   AC_MSG_ERROR([CUPS headers not found but CUPS support requested]))

  if ! test $CUPS_API_MAJOR -gt 1 -o \
            $CUPS_API_MAJOR -eq 1 -a $CUPS_API_MINOR -ge 4 ; then
    AC_MSG_ERROR([CUPS 1.4 or newer not found, but CUPS support requested])
  fi

  CUPS_CFLAGS=`$CUPS_CONFIG --cflags | $SED -e 's/-O\w*//g' -e 's/-m\w*//g'`
  CUPS_LIBS=`$CUPS_CONFIG --libs`
  AC_SUBST(CUPS_CFLAGS)
  AC_SUBST(CUPS_LIBS)
fi

AM_CONDITIONAL(BUILD_PRINTERS, [test x"$enable_cups" = x"yes"])

# Optional dependency for the user accounts panel
AC_ARG_WITH([cheese],
            AS_HELP_STRING([--with-cheese], [enable cheese webcam support]),,
            with_cheese=auto)

if test x"$with_cheese" != x"no" ; then
   PKG_CHECK_MODULES(CHEESE, cheese-gtk >= 3.5.91 cheese clutter-gtk-1.0, [have_cheese=yes], [have_cheese=no])
   if test x${have_cheese} = xyes; then
     AC_DEFINE(HAVE_CHEESE, 1, [Define to 1 to enable cheese webcam support])
   fi
   if test x${with_cheese} = xyes && test x${have_cheese} = xno; then
     AC_MSG_ERROR([Cheese configured but not found])
   fi
else
   have_cheese=no
fi
AM_CONDITIONAL(BUILD_CHEESE, test x${have_cheese} = xyes)

# wacom is disabled for s390/s390x and non Linux platforms (needs udev)
case $host_os in
  linux*)
    if test "$host_cpu" = s390 -o "$host_cpu" = s390x; then
      have_wacom=no
    else
      PKG_CHECK_MODULES(WACOM_PANEL, $COMMON_MODULES
                        unity-settings-daemon
                        xi >= 1.2 x11 libwacom >= $LIBWACOM_REQUIRED_VERSION
                        libunity-settings-daemon)
      AC_DEFINE(BUILD_WACOM, 1, [Define to 1 to build the Wacom panel])
      have_wacom=yes
    fi
    ;;
  *)
    have_wacom=no
    ;;
esac
AM_CONDITIONAL(BUILD_WACOM, [test x"$have_wacom" = x"yes"])

# Online accounts
AC_ARG_ENABLE(onlineaccounts, AC_HELP_STRING([--disable-onlineaccounts], [disable online-accounts panel]),
              [case "${enableval}" in
                      yes) enable_onlineaccounts=yes ;;
                      no) enable_onlineaccounts=no ;;
                      *) AC_MSG_ERROR(bad value ${enableval} for --disable-onlineaccounts) ;;
              esac],
              [enable_onlineaccounts=yes]) dnl Enabled by default

if test "x$enable_onlineaccounts" = xyes; then
    PKG_CHECK_MODULES(ONLINE_ACCOUNTS_PANEL, $COMMON_MODULES goa-1.0
                  goa-backend-1.0 >= $GOA_REQUIRED_VERSION,
                      [have_onlineaccounts=yes], have_onlineaccounts=no)
    if test "x$have_onlineaccounts" = xno ; then
        AC_MSG_ERROR(*** Gnome Online Accounts not found ***)
    fi

    AC_DEFINE(BUILD_ONLINE_ACCOUNTS, 1, [Define to 1 to build the online-accounts panel])

    if test x${have_onlineaccounts} = xyes; then
      AC_DEFINE(HAVE_ONLINE_ACCOUNTS, 1, [Define to 1 if Gnome Online Accounts is available])
    fi
fi

AM_CONDITIONAL(BUILD_ONLINE_ACCOUNTS, [test x$have_onlineaccounts = xyes])
AC_SUBST(ONLINE_ACCOUNTS_CFLAGS)
AC_SUBST(ONLINE_ACCOUNTS_LIBS)

# This is a hard-dependency for the region and user-accounts panels
PKG_CHECK_MODULES(ISOCODES, iso-codes)

AC_DEFINE_UNQUOTED([ISO_CODES_PREFIX],["`$PKG_CONFIG --variable=prefix iso-codes`"],[ISO codes prefix])
ISO_CODES=iso-codes

# Kerberos kerberos support
AC_PATH_PROG(KRB5_CONFIG, krb5-config, no)
if test "$KRB5_CONFIG" = "no"; then
  AC_MSG_ERROR([krb5-config executable not found in your path - should be installed with the kerberos libraries])
fi

AC_MSG_CHECKING(for krb5 libraries and flags)
KRB5_CFLAGS="`$KRB5_CONFIG --cflags`"
KRB5_LIBS="`$KRB5_CONFIG --libs`"
AC_MSG_RESULT($KRB5_CFLAGS $KRB5_LIBS)

AC_SUBST(KRB5_CFLAGS)
AC_SUBST(KRB5_LIBS)

USER_ACCOUNTS_PANEL_CFLAGS="$USER_ACCOUNTS_PANEL_CFLAGS $KRB5_CFLAGS"
USER_ACCOUNTS_PANEL_LIBS="$USER_ACCOUNTS_PANEL_LIBS $KRB5_LIBS"

dnl ==============================================
dnl End: Check that we meet the  dependencies
dnl ==============================================

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal, no)

if test x"$GLIB_GENMARSHAL" = xno; then
  AC_MSG_ERROR([glib-genmarshal executable not found in your path - should be installed with glib])
fi

AC_SUBST(GLIB_GENMARSHAL)

dnl =======================================
dnl Panels
dnl =======================================

PANELS_DIR="${libdir}/unity-control-center-1/panels"
AC_SUBST(PANELS_DIR)

PANEL_CFLAGS="-I\$(top_srcdir)/ -DG_LOG_DOMAIN=\"\\\"\$(cappletname)-cc-panel\\\"\""
AC_SUBST(PANEL_CFLAGS)

PANEL_LIBS="\$(top_builddir)/shell/libunity-control-center.la"
AC_SUBST(PANEL_LIBS)

PANEL_LDFLAGS="-export_dynamic -avoid-version -module -no-undefined -export-symbols-regex '^g_io_module_(load|unload)'"
AC_SUBST(PANEL_LDFLAGS)

dnl ==============================================
dnl libsocialweb
dnl ==============================================

AC_MSG_CHECKING([Enable libsocialweb support])
AC_ARG_WITH([libsocialweb],
   AS_HELP_STRING([--with-libsocialweb],
                  [enable libsocialweb support]),,
   [with_libsocialweb=no])
AC_MSG_RESULT([$with_libsocialweb])

if test "x$with_libsocialweb" == "xyes"; then
  PKG_CHECK_MODULES(SOCIALWEB, libsocialweb-client)
  AC_DEFINE(HAVE_LIBSOCIALWEB, 1, [Defined if libsocialweb is available])
fi
AM_CONDITIONAL(WITH_LIBSOCIALWEB, test "x$with_libsocialweb" = "xyes")


dnl =======================================
dnl Update Mime Database
dnl =======================================

AC_PATH_PROG(UPDATE_MIME_DATABASE, update-mime-database, no)

AC_ARG_ENABLE(update-mimedb,
   AS_HELP_STRING([--disable-update-mimedb],
                  [do not update mime database after installation]),,
                   enable_update_mimedb=yes)
AM_CONDITIONAL(ENABLE_UPDATE_MIMEDB, test x$enable_update_mimedb = xyes)

CONTROL_CENTER_VERSION=unity_control_center_version
AC_SUBST(CONTROL_CENTER_VERSION)

dnl =======================================
dnl Finish
dnl =======================================

# Turn on the additional warnings last

AC_ARG_ENABLE(more-warnings,
              AS_HELP_STRING([--enable-more-warnings],
                             [Maximum compiler warnings]),
              set_more_warnings="$enableval",[
	      if test -d $srcdir/.git; then
	        set_more_warnings=yes
	      else
	        set_more_warnings=no
              fi])

AC_MSG_CHECKING(for more warnings)
if test "$GCC" = "yes" -a "$set_more_warnings" != "no"; then
        AC_MSG_RESULT(yes)
        CFLAGS="\
        -Wall -Wclobbered -Wempty-body -Wignored-qualifiers \
        -Wmissing-field-initializers -Wmissing-parameter-type \
        -Wold-style-declaration -Woverride-init -Wtype-limits \
        -Wuninitialized \
        -Wchar-subscripts -Wmissing-declarations -Wmissing-prototypes \
        -Wnested-externs -Wpointer-arith \
        -Wcast-align -Wsign-compare \
        $CFLAGS"

	# Only add this when optimizing is enabled (default)
	AC_MSG_CHECKING([whether optimization is enabled])
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#if __OPTIMIZE__ == 0
	                                     #error No optimization
	                                     #endif
	                                   ]], [[]])],
	                  [has_optimization=yes],
	                  [has_optimization=no])
	if test $has_optimization = yes; then
		CFLAGS="$CFLAGS -Wp,-D_FORTIFY_SOURCE=2"
	fi
	AC_MSG_RESULT($has_optimization)

        for option in -Wno-strict-aliasing -Wno-sign-compare; do
                SAVE_CFLAGS="$CFLAGS"
                CFLAGS="$CFLAGS $option"
                AC_MSG_CHECKING([whether gcc understands $option])
                AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[]])],
                                  [has_option=yes],
                                  [has_option=no])
                if test $has_option = no; then
                        CFLAGS="$SAVE_CFLAGS"
                fi
                AC_MSG_RESULT($has_option)
                unset has_option
                unset SAVE_CFLAGS
        done
        unset option
else
        AC_MSG_RESULT(no)
fi


AC_OUTPUT([
Makefile
shell/libunity-control-center.pc
panels/Makefile
panels/common/Makefile
panels/appearance/Makefile
panels/appearance/unity-appearance-panel.desktop.in
panels/bluetooth/Makefile
panels/bluetooth/gnome-bluetooth/Makefile
panels/bluetooth/gnome-bluetooth/lib/Makefile
panels/bluetooth/gnome-bluetooth/wizard/Makefile
panels/bluetooth/unity-bluetooth-panel.desktop.in
panels/datetime/Makefile
panels/datetime/unity-datetime-panel.desktop.in
panels/display/Makefile
panels/display/unity-display-panel.desktop.in
panels/keyboard/Makefile
panels/keyboard/unity-keyboard-panel.desktop.in
panels/keyboard/unity-keybindings.pc
panels/region/Makefile
panels/region/unity-region-panel.desktop.in
panels/mouse/Makefile
panels/mouse/unity-mouse-panel.desktop.in
panels/sound/Makefile
panels/sound/data/Makefile
panels/sound/data/unity-sound-panel.desktop.in
panels/sound/data/symbolic-icons/Makefile
panels/sound/data/symbolic-icons/scalable/Makefile
panels/sound/data/symbolic-icons/scalable/status/Makefile
panels/sound/data/icons/Makefile
panels/sound/data/icons/16x16/Makefile
panels/sound/data/icons/16x16/apps/Makefile
panels/sound/data/icons/16x16/devices/Makefile
panels/sound/data/icons/16x16/status/Makefile
panels/sound/data/icons/22x22/Makefile
panels/sound/data/icons/22x22/apps/Makefile
panels/sound/data/icons/22x22/status/Makefile
panels/sound/data/icons/24x24/Makefile
panels/sound/data/icons/24x24/apps/Makefile
panels/sound/data/icons/24x24/devices/Makefile
panels/sound/data/icons/24x24/status/Makefile
panels/sound/data/icons/32x32/Makefile
panels/sound/data/icons/32x32/apps/Makefile
panels/sound/data/icons/32x32/devices/Makefile
panels/sound/data/icons/32x32/status/Makefile
panels/sound/data/icons/48x48/Makefile
panels/sound/data/icons/48x48/apps/Makefile
panels/sound/data/icons/48x48/devices/Makefile
panels/sound/data/icons/256x256/Makefile
panels/sound/data/icons/256x256/apps/Makefile
panels/sound/data/icons/scalable/Makefile
panels/sound/data/icons/scalable/devices/Makefile
panels/screen/Makefile
panels/screen/unity-screen-panel.desktop.in
panels/sharing/Makefile
panels/sharing/unity-sharing-panel.desktop.in
panels/info/Makefile
panels/info/unity-info-panel.desktop.in
panels/power/Makefile
panels/power/unity-power-panel.desktop.in
panels/power/icons/Makefile
panels/power/icons/24x24/Makefile
panels/power/icons/48x48/Makefile
panels/color/Makefile
panels/color/unity-color-panel.desktop.in
panels/color/icons/Makefile
panels/color/icons/16x16/Makefile
panels/color/icons/22x22/Makefile
panels/color/icons/24x24/Makefile
panels/color/icons/32x32/Makefile
panels/color/icons/48x48/Makefile
panels/color/icons/64x64/Makefile
panels/color/icons/256x256/Makefile
panels/color/icons/scalable/Makefile
panels/printers/Makefile
panels/printers/unity-printers-panel.desktop.in
panels/network/Makefile
panels/network/unity-network-panel.desktop.in
panels/universal-access/Makefile
panels/universal-access/unity-universal-access-panel.desktop.in
panels/user-accounts/Makefile
panels/user-accounts/data/Makefile
panels/user-accounts/data/unity-user-accounts-panel.desktop.in
panels/user-accounts/data/faces/Makefile
panels/wacom/Makefile
panels/wacom/calibrator/Makefile
panels/wacom/unity-wacom-panel.desktop.in
panels/online-accounts/Makefile
panels/online-accounts/unity-online-accounts-panel.desktop.in
panels/online-accounts/icons/Makefile
panels/online-accounts/icons/16x16/Makefile
panels/online-accounts/icons/22x22/Makefile
panels/online-accounts/icons/24x24/Makefile
panels/online-accounts/icons/32x32/Makefile
panels/online-accounts/icons/48x48/Makefile
panels/online-accounts/icons/256x256/Makefile
po/Makefile.in
shell/appdata/Makefile
shell/Makefile
shell/unity-control-center.desktop.in
man/Makefile
])

AC_MSG_NOTICE([unity-control-center was configured with the following options:])
if test "x$have_networkmanager" = "xyes"; then
	AC_MSG_NOTICE([** NetworkManager (Network panel)])
else
	AC_MSG_NOTICE([   Network panel disabled])
fi
if test "x$enable_cups" = "xyes"; then
	AC_MSG_NOTICE([** CUPS (Printers panel)])
else
	AC_MSG_NOTICE([   Printers panel disabled])
fi
if test "x$have_cheese" = "xyes"; then
	AC_MSG_NOTICE([** Cheese (Users panel webcam support)])
else
	AC_MSG_NOTICE([   Users panel webcam support disabled])
fi
if test "x$with_libsocialweb" = "xyes"; then
	AC_MSG_NOTICE([** libsocialweb (Appearance panel Flickr support)])
else
	AC_MSG_NOTICE([   Appearance panel Flickr support disabled])
fi
if test "x$have_wacom" = "xyes"; then
	AC_MSG_NOTICE([** wacom (Wacom tablet panel)])
else
	AC_MSG_NOTICE([   Wacom panel disabled])
fi
if test "x$enable_ibus" == "xyes"; then
	AC_MSG_NOTICE([** IBus (Region panel IBus support)])
else
	AC_MSG_NOTICE([   Region panel IBus support disabled])
fi
if test "x$enable_fcitx" == "xyes"; then
	AC_MSG_NOTICE([** Fcitx (Region panel Fcitx support)])
else
	AC_MSG_NOTICE([   Region panel Fcitx support disabled])
fi
if test "x$have_onlineaccounts" = "xyes"; then
	AC_MSG_NOTICE([** Online-Accounts support (Online-Accounts panel)])
else
	AC_MSG_NOTICE([   Online-Accounts support disabled])
fi
AC_MSG_NOTICE([End options])
