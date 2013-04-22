#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>

#include <glib.h>
#include <glib/gprintf.h>

#include <gtk/gtk.h>
#include <gtk/gtkx.h>
#include <gdk/gdkx.h>

#include <X11/Xlib.h>

#define XEPHYR_COMMAND "Xephyr"
#define XEPHYR_DISPLAY ":3"
#define WM_COMMAND     "metacity"

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

gint
main (gint argc, gchar **argv)
{

    gtk_init (&argc, &argv);

    GdkDisplayManager *const manager = gdk_display_manager_get ();
    GSList *const displays = gdk_display_manager_list_displays (manager);

    GSList *current;
    for (current = displays; current != NULL; current = current->next){
        g_printf (
            "display: \"%s\"\n",
            gdk_display_get_name (GDK_DISPLAY (current->data))
        );
    }

    /* TODO: choose the biggest display and put the window in there. */

    GtkWidget *const window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    GtkWidget *const socket = gtk_socket_new ();

    gtk_widget_set_size_request (socket, 800, 450);
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

    g_return_if_fail (window != NULL);
    g_return_if_fail (event != NULL);
    g_return_if_fail (socket != NULL);

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

    g_return_if_fail (window != NULL);
    g_return_if_fail (socket != NULL);

}

static void
socket_plug_added_cb   (GtkSocket *const socket,
                        gpointer         user_data)
{

    g_debug ("socket plugged, starting window manager");

    launch_window_manager (GTK_WIDGET (socket));

}

static void
socket_plug_removed_cb (GtkSocket *const socket,
                        gpointer         user_data)
{

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

    GError *error = NULL;
    GPid xephyr_pid;

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

    g_spawn_async_with_pipes (
        NULL,
        xephyr_argv,
        NULL,
        G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
        NULL,
        NULL,
        &xephyr_pid,
        NULL,
        NULL,
        NULL,
        &error
    );

    if (error){
        g_printf ("failed to start " XEPHYR_COMMAND ": %s\n", error->message);
    } else {
        g_debug ("window XID is %lu", window_xid);
        g_debug ("launched " XEPHYR_COMMAND " with pid %ld", (long) xephyr_pid);
    }

    g_strfreev (xephyr_argv);
}

static void
launch_window_manager (GtkWidget *const socket)
{

    /*
     * TODO: It is probably better to start some kind of X session also since
     * the keyboard is not initialized properly in my experience.
     */

    GError *error = NULL;
    GPid wm_pid;

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

    g_spawn_async_with_pipes (
        NULL,
        wm_argv,
        wm_envp,
        G_SPAWN_SEARCH_PATH,
        NULL,
        NULL,
        &wm_pid,
        NULL,
        NULL,
        NULL,
        &error
    );

    if (error){
        g_printf ("failed to start " WM_COMMAND ": %s\n", error->message);
    } else {
        g_debug ("launched " WM_COMMAND " with pid %ld", (long) wm_pid);
    }

    g_strfreev (wm_argv);
    g_strfreev (wm_envp);

}
