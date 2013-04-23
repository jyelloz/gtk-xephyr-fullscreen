AC_INIT([gtk-xephyr-fullscreen], [0.1])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])

AC_PREREQ([2.69])

AC_PROG_CC
AM_PROG_CC_C_O
AC_CHECK_HEADERS(
    [stdlib.h]
)
PKG_CHECK_MODULES([GLIB], [glib-2.0])
PKG_CHECK_MODULES([GTK], [gtk+-3.0])
PKG_CHECK_MODULES([GTK_X11], [gtk+-x11-3.0])

# AC_PATH_X
AC_CONFIG_FILES([
    Makefile
])

AC_OUTPUT
