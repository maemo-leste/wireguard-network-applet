desktoplib_LTLIBRARIES = status-applet-wireguard.la
desktoplibdir = $(hildondesktoplibdir)

status_applet_wireguard_la_SOURCES = \
	status-applet.c

status_applet_wireguard_la_CFLAGS = \
	$(libhildon_CFLAGS) \
	$(libhildondesktop_CFLAGS) \
	$(gtk2_CFLAGS) \
	$(glib2_CFLAGS) \
	$(libosso_CFLAGS) \
	$(gconf_CFLAGS) \
	-Wall -Werror

status_applet_wireguard_la_LIBADD = \
	$(libhildon_LIBS) \
	$(libhildondesktop_LIBS) \
	$(gtk2_LIBS) \
	$(glib2_LIBS) \
	$(libosso_LIBS) \
	$(gconf_LIBS)

status_applet_wireguard_la_LDFLAGS = -shared -module -avoid-version
