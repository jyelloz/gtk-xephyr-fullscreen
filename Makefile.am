ACLOCAL_AMFLAGS = -I m4

AM_CFLAGS = -std=gnu99 -pedantic -Wall

NULL =

bin_PROGRAMS = gtk-xephyr-fullscreen

gtk_xephyr_fullscreen_SOURCES = gtk-xephyr-fullscreen.c
gtk_xephyr_fullscreen_CPPFLAGS = \
	$(GLIB_CFLAGS) \
	$(GDK_CFLAGS) $(GDK_X11_CFLAGS) \
	$(GTK_CFLAGS) $(GTK_X11_CFLAGS) \
	$(NULL)

gtk_xephyr_fullscreen_LDADD = $(GLIB_LIBS) $(GTK_LIBS) $(GTK_X11_LIBS)
