#include <stdlib.h>
#include <stdarg.h>

#include <unistd.h>

#include <sys/types.h>
#include <signal.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gprintf.h>

#include <gio/gio.h>
#include <gio/gunixinputstream.h>
#include <gio/gunixoutputstream.h>

#include <gtk/gtk.h>
#include <gtk/gtkx.h>
#include <gdk/gdkx.h>

#include <X11/Xlib.h>

#define XEPHYR_COMMAND      "Xephyr"
#define XEPHYR_DISPLAY      ":3"
#define XMODMAP_COMMAND     "xmodmap"
#define WM_COMMAND          "metacity"
#define IBUS_DAEMON_COMMAND "ibus-daemon"
#define XRDB_COMMAND        "xrdb"

G_BEGIN_DECLS

typedef struct _GxfSubprocess GxfSubprocess;
typedef struct _GxfContext GxfContext;

struct _GxfContext {

    GtkWidget *window;
    GtkWidget *socket;

    GAsyncQueue *subprocesses;

};

struct _GxfSubprocess {
    GPid pid;
    gchar *proctitle;
};

static GxfContext *
gxf_context_new        (void);

static void
gxf_context_free       (GxfContext *const self);

static void
gxf_subprocess_free    (GxfSubprocess *const self);

static GxfSubprocess *
gxf_subprocess_new     (GPid   const pid,
                        gchar *const proctitle);

static void
activate_cb            (GtkApplication *const application,
                        GxfContext     *const gxf);

static void
shutdown_cb            (GtkApplication *const application,
                        GxfContext     *const gxf);

static void
window_visible_cb      (GtkWidget  *const window,
                        GxfContext *const gxf);

static void
window_fullscreen_cb   (GtkWidget  *const window,
                        GdkEvent   *const event,
                        GxfContext *const gxf);

static void
socket_plug_added_cb   (GtkSocket  *const socket,
                        GxfContext *const gxf);

static void
socket_plug_removed_cb (GtkSocket  *const socket,
                        GxfContext *const gxf);

static gchar **
build_argv             (gchar     *const command, ...);

static void
launch_xephyr          (GtkWidget *const socket,
                        GPid      *const pid);

static void
launch_window_manager  (GtkWidget *const socket,
                        GPid      *const pid);

static gboolean
transfer_xmodmap_keys  (void);

static gboolean
transfer_xrdb          (void);

static void
watch_closing          (GPid     const pid,
                        gint     const status,
                        gpointer const user_data);

static gboolean
launch_ibus_daemon     (GPid *const pid);

static GdkRectangle
find_largest_monitor   (GdkScreen *const screen);

static void
gxf_quit               (GxfContext *const gxf);

G_END_DECLS

static GxfContext *
gxf_context_new        (void)
{

    GxfContext *const self = g_new0 (GxfContext, 1);

    self->subprocesses = g_async_queue_new ();

    return self;

}

static void
gxf_context_free       (GxfContext *const self)
{

    if (self == NULL){
        return;
    }

    GAsyncQueue *const subprocesses = self->subprocesses;

    g_async_queue_unref (subprocesses);

    g_free (self);

}


static void
gxf_quit               (GxfContext *const gxf)
{

    g_return_if_fail (gxf != NULL);

    GAsyncQueue *const subprocesses = gxf->subprocesses;

    g_async_queue_lock (subprocesses);

    while (TRUE){

        GxfSubprocess *const subprocess = (
            (GxfSubprocess *) g_async_queue_try_pop_unlocked (subprocesses)
        );

        if (subprocess == NULL){
            break;
        }

        GPid const pid = subprocess->pid;
        gchar *const proctitle = subprocess->proctitle;

        if (pid < 1){
            continue;
        }

        kill (pid, SIGINT);
        g_debug ("sent a SIGINT to %s:%d", proctitle, pid);

        gxf_subprocess_free (subprocess);

    }

    g_async_queue_unlock (subprocesses);

}

static void
gxf_subprocess_free    (GxfSubprocess *const self)
{

    if (self == NULL){
        return;
    }

    GPid const pid = self->pid;
    gchar *const proctitle = self->proctitle;

    g_spawn_close_pid (pid);
    if (proctitle != NULL){
        g_free (proctitle);
    }

    g_free (self);

}

static GxfSubprocess *
gxf_subprocess_new     (GPid   const pid,
                        gchar *const proctitle)

{

    GxfSubprocess *const self = g_new0 (GxfSubprocess, 1);

    self->pid = pid;
    self->proctitle = proctitle;

    return self;

}

static GPrivate application_priv = G_PRIVATE_INIT (g_object_unref);

static void
sigint_handler (const gint       signal)
{

    g_debug ("SIGINT caught");

    GApplication *const application = G_APPLICATION (
        g_private_get (&application_priv)
    );;

    g_application_quit (application);

}

gint
main (gint argc, gchar **argv)
{

    GtkApplication *const application = gtk_application_new (
        "me.yelloz.jordan.gtk-xephyr-fullscreen",
        0
    );
    GxfContext *const gxf = gxf_context_new ();

    g_private_set (&application_priv, application);

    struct sigaction sigint_handler_sa;
    sigint_handler_sa.sa_handler = sigint_handler;
    sigint_handler_sa.sa_flags = 0;

    sigaction (SIGINT, &sigint_handler_sa, NULL);

    g_signal_connect (
        application,
        "activate",
        G_CALLBACK (activate_cb),
        gxf
    );

    g_signal_connect (
        application,
        "shutdown",
        G_CALLBACK (shutdown_cb),
        gxf
    );

    const gint status = g_application_run (
        G_APPLICATION (application),
        argc,
        argv
    );

    g_object_unref (application);
    gxf_context_free (gxf);

    return status;

}

static void
activate_cb            (GtkApplication *const application,
                        GxfContext     *const gxf)
{

    GList *const windows = gtk_application_get_windows (application);

    if (windows){
        gtk_window_present (GTK_WINDOW (windows->data));
        return;
    }

    GdkRectangle const largest_monitor = find_largest_monitor (
        gdk_screen_get_default ()
    );

    GtkWidget *const window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    GtkWidget *const socket = gtk_socket_new ();

    gxf->window = window;
    gxf->socket = socket;

    gtk_window_move (
        GTK_WINDOW (window),
        largest_monitor.x,
        largest_monitor.y
    );

    gtk_container_add (GTK_CONTAINER (window), socket);

    g_signal_connect (
        window,
        "window-state-event",
        G_CALLBACK (window_fullscreen_cb),
        gxf
    );

    g_signal_connect (
        window,
        "realize",
        G_CALLBACK (window_visible_cb),
        gxf
    );

    g_signal_connect (
        socket,
        "plug-added",
        G_CALLBACK (socket_plug_added_cb),
        gxf
    );

    g_signal_connect (
        socket,
        "plug-removed",
        G_CALLBACK (socket_plug_removed_cb),
        gxf
    );

    gtk_window_set_application (GTK_WINDOW (window), application);

    gtk_widget_show_all (window);

    gtk_window_fullscreen (GTK_WINDOW (window));

}

static void
shutdown_cb            (GtkApplication *const application,
                        GxfContext     *const gxf)
{

    gxf_quit (gxf);

}

static void
window_fullscreen_cb   (GtkWidget  *const window,
                        GdkEvent   *const event,
                        GxfContext *const gxf)
{

    GPid xephyr_pid;

    GtkSocket *const socket = GTK_SOCKET (gxf->socket);
    GAsyncQueue *const subprocesses = gxf->subprocesses;

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
        launch_xephyr (GTK_WIDGET (socket), &xephyr_pid);
        g_async_queue_push (
            subprocesses,
            gxf_subprocess_new (
                xephyr_pid,
                g_strdup (XEPHYR_COMMAND)
            )
        );
    } else if (fullscreen) {
        g_debug ("window is already fullscreen");
    } else {
        g_debug ("window is not fullscreen");
    }

}

static void
window_visible_cb      (GtkWidget  *const window,
                        GxfContext *const gxf)
{

    g_return_if_fail (GTK_IS_WINDOW (window));
    g_return_if_fail (gxf != NULL);

}

static void
socket_plug_added_cb   (GtkSocket  *const socket,
                        GxfContext *const gxf)
{

    GPid wm_pid;
    GPid ibus_pid;

    g_return_if_fail (GTK_IS_SOCKET (socket));
    g_return_if_fail (gxf != NULL);

    GAsyncQueue *const subprocesses = gxf->subprocesses;

    g_debug ("socket plugged, starting window manager");

    transfer_xrdb ();

    launch_window_manager (GTK_WIDGET (socket), &wm_pid);

    g_async_queue_push (
        subprocesses,
        gxf_subprocess_new (
            wm_pid,
            g_strdup (WM_COMMAND)
        )
    );

    if (launch_ibus_daemon (&ibus_pid)){
        g_async_queue_push (
            subprocesses,
            gxf_subprocess_new (
                ibus_pid,
                g_strdup (IBUS_DAEMON_COMMAND)
            )
        );
        return;
    }

    g_warning ("failed to start ibus-daemon, trying xmodmap instead");

    if (transfer_xmodmap_keys ()){
        return;
    }

    g_warning (
        "failed to transfer xmodmap key bindings, "
        "keyboard might not work correctly."
    );

}

static void
socket_plug_removed_cb (GtkSocket  *const socket,
                        GxfContext *const gxf)
{

    g_return_if_fail (GTK_IS_SOCKET (socket));
    g_debug ("socket unplugged");

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
launch_xephyr (GtkWidget *const socket,
               GPid      *const pid)
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
        pid,
        &error
    );

    if (error){
        g_printf ("failed to start " XEPHYR_COMMAND ": %s\n", error->message);
    }

    g_strfreev (xephyr_argv);
}

static void
launch_window_manager (GtkWidget *const socket,
                       GPid      *const pid)
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
        pid,
        &error
    );

    if (error){
        g_printf ("failed to start " WM_COMMAND ": %s\n", error->message);
    }

    g_strfreev (wm_argv);
    g_strfreev (wm_envp);

}

static gboolean
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

    const gboolean xmodmap_out_result = g_spawn_async_with_pipes (
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

    const gboolean xmodmap_in_result = g_spawn_async_with_pipes (
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

    GInputStream *const xmodmap_stdout = g_unix_input_stream_new (
        xmodmap_out_fd,
        TRUE
    );

    GOutputStream *const xmodmap_stdin = g_unix_output_stream_new (
        xmodmap_in_fd,
        TRUE
    );

    g_output_stream_splice (
        xmodmap_stdin,
        xmodmap_stdout,
        (
            G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE |
            G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET
        ),
        NULL,
        NULL
    );

    g_child_watch_add (
        xmodmap_out_pid,
        watch_closing,
        NULL
    );

    g_child_watch_add (
        xmodmap_in_pid,
        watch_closing,
        NULL
    );

    return xmodmap_out_result && xmodmap_in_result;

}

static gboolean
transfer_xrdb          (void)
{

    GError *error = NULL;
    gint xrdb_out_fd;
    gint xrdb_in_fd;
    GPid xrdb_out_pid;
    GPid xrdb_in_pid;

    gchar **const xrdb_out_argv = build_argv (
        g_strdup (XRDB_COMMAND),
        g_strdup ("-query"),
        NULL
    );

    gchar **const xrdb_in_argv = build_argv (
        g_strdup (XRDB_COMMAND),
        g_strdup ("-merge"),
        NULL
    );

    gchar **const xrdb_in_envp = g_environ_setenv (
        g_get_environ (),
        g_strdup ("DISPLAY"),
        g_strdup (XEPHYR_DISPLAY),
        TRUE
    );

    const gboolean xrdb_out_result = g_spawn_async_with_pipes (
        NULL,
        xrdb_out_argv,
        NULL,
        G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
        NULL,
        NULL,
        &xrdb_out_pid,
        NULL,
        &xrdb_out_fd,
        NULL,
        &error
    );

    if (error){
        g_printf (
            "failed to launch " XRDB_COMMAND " output command: %s\n",
            error->message
        );
    }

    error = NULL;

    const gboolean xrdb_in_result = g_spawn_async_with_pipes (
        NULL,
        xrdb_in_argv,
        xrdb_in_envp,
        G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
        NULL,
        NULL,
        &xrdb_in_pid,
        &xrdb_in_fd,
        NULL,
        NULL,
        &error
    );

    if (error){
        g_printf (
            "failed to launch " XRDB_COMMAND " input command: %s\n",
            error->message
        );
    }

    g_strfreev (xrdb_out_argv);
    g_strfreev (xrdb_in_argv);
    g_strfreev (xrdb_in_envp);

    GInputStream *const xrdb_stdout = g_unix_input_stream_new (
        xrdb_out_fd,
        TRUE
    );

    GOutputStream *const xrdb_stdin = g_unix_output_stream_new (
        xrdb_in_fd,
        TRUE
    );

    g_output_stream_splice (
        xrdb_stdin,
        xrdb_stdout,
        (
            G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE |
            G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET
        ),
        NULL,
        NULL
    );

    g_child_watch_add (
        xrdb_out_pid,
        watch_closing,
        NULL
    );

    g_child_watch_add (
        xrdb_in_pid,
        watch_closing,
        NULL
    );

    return xrdb_out_result && xrdb_in_result;

}

static void
watch_closing          (GPid     const pid,
                        gint     const status,
                        gpointer const user_data)
{

    g_debug ("closing pid %ld", (long) pid);
    g_spawn_close_pid (pid);

}


static gboolean
launch_ibus_daemon     (GPid *const pid)
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
        g_strdup ("--panel=disable"),
        NULL
    );

    const gboolean ibus_daemon_result = g_spawn_async (
        NULL,
        ibus_daemon_argv,
        ibus_daemon_envp,
        G_SPAWN_SEARCH_PATH,
        NULL,
        NULL,
        pid,
        &error
    );

    if (error){
        g_printf (
            "failed to start " IBUS_DAEMON_COMMAND ": %s\n", error->message
        );
    }

    g_strfreev (ibus_daemon_argv);
    g_strfreev (ibus_daemon_envp);

    return ibus_daemon_result;

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
