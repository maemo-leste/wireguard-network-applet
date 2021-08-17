AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS  = -I m4

SUBDIRS = \
	status-applet \
	control-applet \
	data

install-exec-hook:
	find $(DESTDIR) -type f -name \*-wireguard.la -delete
