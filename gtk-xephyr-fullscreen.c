#include <stdlib.h>
#include <stdarg.h>

#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gprintf.h>

#include <gtk/gtk.h>
#include <gtk/gtkx.h>
#include <gdk/gdkx.h>

#include <X11/Xlib.h>

#define XEPHYR_COMMAND      "Xephyr"
#define XEPHYR_DISPLAY      ":3"
#define XMODMAP_COMMAND     "xmodmap"
#define WM_COMMAND          "metacity"
#define IBUS_DAEMON_COMMAND "ibus-daemon"

static void
window_visible_cb      (GtkWidget *const window,
                        GtkSocket *const socket);

static void
window_fullscreen_cb   (GtkWidget *const window,
                        GdkEvent  *const event,
                        GtkSocket *const socket);

static void
socket_plug_added_cb   (GtkSocket *const socket,
                        gpointer         user_data);

static void
socket_plug_removed_cb (GtkSocket *const socket,
                        gpointer         user_data);

static gchar **
build_argv             (gchar     *const command, ...);

static void
launch_xephyr          (GtkWidget *const socket);

static void
launch_window_manager  (GtkWidget *const socket);

static void
transfer_xmodmap_keys  (void);

static void
watch_xmodmap_closing  (GPid     const pid,
                        gint     const status,
                        gpointer const user_data);

static void
launch_ibus_daemon     (void);

static GdkRectangle
find_largest_monitor   (GdkScreen *const screen);

gint
main (gint argc, gchar **argv)
{

    gtk_init (&argc, &argv);

    GdkRectangle const largest_monitor = find_largest_monitor (
        gdk_screen_get_default ()
    );

    GtkWidget *const window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    GtkWidget *const socket = gtk_socket_new ();

    gtk_window_move (
        GTK_WINDOW (window),
        largest_monitor.x,
        largest_monitor.y
    );

    gtk_container_add (GTK_CONTAINER (window), socket);

    g_signal_connect (
        window,
        "destroy",
        G_CALLBACK (gtk_main_quit),
        NULL
    );

    g_signal_connect (
        window,
        "window-state-event",
        G_CALLBACK (window_fullscreen_cb),
        socket
    );

    g_signal_connect (
        window,
        "realize",
        G_CALLBACK (window_visible_cb),
        socket
    );

    g_signal_connect (
        socket,
        "plug-added",
        G_CALLBACK (socket_plug_added_cb),
        NULL
    );

    g_signal_connect (
        socket,
        "plug-removed",
        G_CALLBACK (socket_plug_removed_cb),
        NULL
    );

    gtk_widget_show_all (window);

    gtk_window_fullscreen (GTK_WINDOW (window));

    gtk_main ();

    return 0;
}

static void
window_fullscreen_cb   (GtkWidget *const window,
                        GdkEvent  *const event,
                        GtkSocket *const socket)
{

    g_return_if_fail (GTK_IS_WINDOW (window));
    g_return_if_fail (event != NULL);
    g_return_if_fail (GTK_IS_SOCKET (socket));

    if (event->type != GDK_WINDOW_STATE){
        return;
    }

    GdkEventWindowState const window_state = event->window_state;

    const gboolean fullscreen = (
        event->window_state.new_window_state & GDK_WINDOW_STATE_FULLSCREEN
    );

    const gboolean switched_to_fullscreen = (
        fullscreen
        &&
        (window_state.changed_mask & GDK_WINDOW_STATE_FULLSCREEN)
    );

    if (switched_to_fullscreen){
        g_debug ("window is now fullscreen");
        launch_xephyr (GTK_WIDGET (socket));
    } else if (fullscreen) {
        g_debug ("window is already fullscreen");
    } else {
        g_debug ("window is not fullscreen");
    }

}

static void
window_visible_cb      (GtkWidget *const window,
                        GtkSocket *const socket)
{

    g_return_if_fail (GTK_IS_WINDOW (window));
    g_return_if_fail (GTK_IS_SOCKET (socket));

}

static void
socket_plug_added_cb   (GtkSocket *const socket,
                        gpointer         user_data)
{

    g_return_if_fail (GTK_IS_SOCKET (socket));

    g_debug ("socket plugged, starting window manager");

    launch_window_manager (GTK_WIDGET (socket));
    transfer_xmodmap_keys ();

}

static void
socket_plug_removed_cb (GtkSocket *const socket,
                        gpointer         user_data)
{

    g_return_if_fail (GTK_IS_SOCKET (socket));

    g_debug ("socket unplugged");

    gtk_main_quit ();

}

static gchar **
build_argv             (gchar     *const command, ...)
{

    GQueue *const queue = g_queue_new ();

    va_list varargs;
    va_start (varargs, command);
    {
        gchar *arg;
        for (arg = command; arg != NULL; arg = va_arg (varargs, gchar *)){
            g_debug ("got arg %s", arg);
            g_queue_push_tail (queue, arg);
        }
    }
    va_end (varargs);

    gsize const command_length = g_queue_get_length (queue);
    gchar **const argv = g_new0 (gchar *, command_length);
    {
        gchar **argv_iter;
        for (argv_iter = argv; !g_queue_is_empty (queue); argv_iter++){
            *argv_iter = g_queue_pop_head (queue);
        }
    }

    return argv;

}


static void
launch_xephyr (GtkWidget *const socket)
{

    g_return_if_fail (GTK_IS_SOCKET (socket));

    GError *error = NULL;

    const gint width = gtk_widget_get_allocated_width (socket);
    const gint height = gtk_widget_get_allocated_height (socket);

    XID const window_xid = gtk_socket_get_id (GTK_SOCKET (socket));

    gchar **const xephyr_argv = build_argv (
        g_strdup (XEPHYR_COMMAND),
        g_strdup ("-parent"),
        g_strdup_printf ("%lu", window_xid),
        g_strdup ("-screen"),
        g_strdup_printf ("%dx%d", width, height),
        g_strdup (XEPHYR_DISPLAY),
        NULL
    );

    g_spawn_async (
        NULL,
        xephyr_argv,
        NULL,
        G_SPAWN_SEARCH_PATH,
        NULL,
        NULL,
        NULL,
        &error
    );

    if (error){
        g_printf ("failed to start " XEPHYR_COMMAND ": %s\n", error->message);
    }

    g_strfreev (xephyr_argv);
}

static void
launch_window_manager (GtkWidget *const socket)
{

    g_return_if_fail (GTK_IS_SOCKET (socket));

    GError *error = NULL;

    gchar **const wm_envp = g_environ_setenv (
        g_get_environ (),
        g_strdup ("DISPLAY"),
        g_strdup (XEPHYR_DISPLAY),
        TRUE
    );

    gchar **const wm_argv = build_argv (
        g_strdup (WM_COMMAND),
        NULL
    );

    g_spawn_async (
        NULL,
        wm_argv,
        wm_envp,
        G_SPAWN_SEARCH_PATH,
        NULL,
        NULL,
        NULL,
        &error
    );

    if (error){
        g_printf ("failed to start " WM_COMMAND ": %s\n", error->message);
    }

    g_strfreev (wm_argv);
    g_strfreev (wm_envp);

}

static void
transfer_xmodmap_keys  (void)
{

    GError *error = NULL;
    gint xmodmap_out_fd;
    gint xmodmap_in_fd;
    GPid xmodmap_out_pid;
    GPid xmodmap_in_pid;

    gchar **const xmodmap_out_argv = build_argv (
        g_strdup (XMODMAP_COMMAND),
        g_strdup ("-pke"),
        NULL
    );

    gchar **const xmodmap_in_argv = build_argv (
        g_strdup (XMODMAP_COMMAND),
        g_strdup ("-"),
        NULL
    );

    gchar **const xmodmap_in_envp = g_environ_setenv (
        g_get_environ (),
        g_strdup ("DISPLAY"),
        g_strdup (XEPHYR_DISPLAY),
        TRUE
    );

    g_spawn_async_with_pipes (
        NULL,
        xmodmap_out_argv,
        NULL,
        G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
        NULL,
        NULL,
        &xmodmap_out_pid,
        NULL,
        &xmodmap_out_fd,
        NULL,
        &error
    );

    if (error){
        g_printf (
            "failed to launch " XMODMAP_COMMAND " output command: %s\n",
            error->message
        );
    }

    error = NULL;

    g_spawn_async_with_pipes (
        NULL,
        xmodmap_in_argv,
        xmodmap_in_envp,
        G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
        NULL,
        NULL,
        &xmodmap_in_pid,
        &xmodmap_in_fd,
        NULL,
        NULL,
        &error
    );

    if (error){
        g_printf (
            "failed to launch " XMODMAP_COMMAND " input command: %s\n",
            error->message
        );
    }

    g_strfreev (xmodmap_out_argv);
    g_strfreev (xmodmap_in_argv);
    g_strfreev (xmodmap_in_envp);

    guchar buffer[64];
    for ( ; ; ){
        gsize const read_bytes = read (
            xmodmap_out_fd,
            buffer,
            64
        );

        gsize written_bytes = 0;
        while (written_bytes < read_bytes){
            written_bytes += write (
                xmodmap_in_fd,
                buffer + written_bytes,
                read_bytes - written_bytes
            );
        }

        if (read_bytes != 64){
            break;
        }
    }

    close (xmodmap_out_fd);
    close (xmodmap_in_fd);

    g_child_watch_add (
        xmodmap_out_pid,
        watch_xmodmap_closing,
        NULL
    );

    g_child_watch_add (
        xmodmap_in_pid,
        watch_xmodmap_closing,
        NULL
    );

}

static void
watch_xmodmap_closing  (GPid     const pid,
                        gint     const status,
                        gpointer const user_data)
{

    g_debug ("closing pid %ld", (long) pid);
    g_spawn_close_pid (pid);

}


static void
launch_ibus_daemon     (void)
{

    GError *error = NULL;

    gchar **const ibus_daemon_envp = g_environ_setenv (
        g_get_environ (),
        g_strdup ("DISPLAY"),
        g_strdup (XEPHYR_DISPLAY),
        TRUE
    );

    gchar **const ibus_daemon_argv = build_argv (
        g_strdup (IBUS_DAEMON_COMMAND),
        g_strdup ("--replace"),
        g_strdup ("--xim"),
        g_strdup ("--panel"),
        g_strdup ("disable"),
        NULL
    );

    g_spawn_async (
        NULL,
        ibus_daemon_argv,
        ibus_daemon_envp,
        G_SPAWN_SEARCH_PATH,
        NULL,
        NULL,
        NULL,
        &error
    );

    if (error){
        g_printf (
            "failed to start " IBUS_DAEMON_COMMAND ": %s\n", error->message
        );
    }

    g_strfreev (ibus_daemon_argv);
    g_strfreev (ibus_daemon_envp);

}

static GdkRectangle
find_largest_monitor   (GdkScreen *const screen)
{

    GdkRectangle largest_monitor = {
        .x = 0,
        .y = 0,
        .width = 0,
        .height = 0
    };

    const gint n_monitors = gdk_screen_get_n_monitors (
        screen
    );

    gint i;
    gint max_area = 0;
    for (i = 0; i < n_monitors; i++){
        GdkRectangle monitor;
        gdk_screen_get_monitor_geometry (
            screen,
            i,
            &monitor
        );
        gint area = monitor.width * monitor.height;
        if (area > max_area){
            max_area = area;
            largest_monitor = monitor;
        }
    }

    return largest_monitor;

}
