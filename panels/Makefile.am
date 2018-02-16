SUBDIRS= \
	appearance \
	common \
	datetime \
	screen \
	power \
	color \
	display \
	mouse \
	region \
	info \
	sound \
	keyboard \
	universal-access \
	user-accounts \
	bluetooth \
	screen-sharing

if BUILD_WACOM
SUBDIRS += wacom
endif

if BUILD_PRINTERS
SUBDIRS += printers
endif

if BUILD_NETWORK
SUBDIRS += network
endif

if BUILD_ONLINE_ACCOUNTS
SUBDIRS += online-accounts
endif

-include $(top_srcdir)/git.mk
