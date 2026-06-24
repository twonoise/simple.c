
/* Std: C99. Based on: Emerald, Copyright 2006 Novell, Inc., License: GPL2+ */

#define PACKAGE      "simple"
#define VERSION      "0.8.18.1"  // Will work with Compiz 0.9.14.2 also.

#define BORDER       1
#define TITLE_H      16
#define GRAD_W       256
#define GRAD_H       TITLE_H
#define ICON_SZ      "16"  // Popup Menu icons: 16, 24, 32, 48, scalable.
#define ICONS_PATH   "/root/.icons/Chicago95/actions/"ICON_SZ
#define BDF_PCF_FONT "xos4 Terminus"  // No .otb and non-bitmaps, please.
#define ACTIONS      "'Mi&nimize', 'Ma&ximize', '&Restore', '&Move','Re&size','&Close'"
#define ACT_ICONS    "'window-minimize', 'window-maximize', 'window-restore', '-','image-crop','process-stop'"

// Huge memory leak is unavoidable. https://github.com/python/cpython/issues/100773
// Total 2,95 Mb PyQt5 and 1,76 Mb PyQt6!
#include <Python.h>

#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <gdk/gdkx.h>
#include <cairo-xlib.h>
#include <decoration.h>

#define WNCK_I_KNOW_THIS_IS_UNSTABLE
#define WIN_POPUP gtk_widget_get_window(window_popup)
#ifdef GTK3
    #include <libwnck/libwnck.h>
    #include <gtk-3.0/gdk/gdk.h>  // -lgdk-3
    #include <gtk-3.0/gtk/gtk.h>  // -I/usr/include/gtk-3.0/
    #define gdk_error_trap_push() gdk_x11_display_error_trap_push(gdkdisplay)
    #define gdk_error_trap_pop()  gdk_x11_display_error_trap_pop(gdkdisplay)
    WnckHandle *wnck_handle;
    #define wnck_window_get(a)    wnck_handle_get_window(wnck_handle, a)
#else
    #include <gtk-2.0/gdk/gdk.h>  // -lgdk-x11-2.0
    #include <gtk-2.0/gtk/gtk.h>  // -I/usr/include/gtk-2.0/
    #include <libwnck/libwnck.h>
#endif

typedef struct _decor
{
    void    (*draw) (struct _decor *d);
    XID              prop_xid;
    Window           event_window;
    int              width;
    int              height;
    int              client_width;
    int              client_height;
    int              decorated;
    int              active;
    uint8_t          hue;
    uint32_t         color[2][3];
    gchar            title[256];           // FIXME! NOTE!
    GdkPixbuf       *icon_pixbuf;
    cairo_pattern_t *icon;
    cairo_surface_t *icon_surface;
    cairo_surface_t *surface;
    cairo_surface_t *buffer_surface;
    cairo_surface_t *p_active_surface;
    cairo_surface_t *p_active_buffer_surface;
    cairo_surface_t *p_inactive_surface;
    cairo_surface_t *p_inactive_buffer_surface;
    cairo_surface_t *decor_normal_surface;
    cairo_surface_t *decor_active_surface;
} decor_t;

static Atom frame_window_atom;
static Atom win_decor_atom;
static Atom wm_move_resize_atom;
static Atom restack_window_atom;
static Atom wm_protocols_atom;
static Atom mwm_hints_atom;
static Atom toolkit_action_atom;
static Atom toolkit_action_window_menu_atom;

static Atom emerald_sigusr1_atom;

static Time dm_sn_timestamp;

static GtkWidget *window_popup;
static GHashTable *frame_table;
static GSList *draw_list = NULL;
static guint draw_idle_id = 0;

GdkDisplay *gdkdisplay;
GdkScreen  *gdkscreen;
Display    *xdisplay;
Window      xroot;

uint8_t bayer[16][16];

PyObject *pArgs, *pFunc;
long int retValue = -2;


static cairo_surface_t *create_surface(int w, int h, int isImageOrXlib)
{
    cairo_surface_t *surface;

    if (w < 0 || h < 0)
        abort();       /* What it means? */

    if (w == 0 || h == 0)
        return NULL;

    if (isImageOrXlib) /* Xlib */
        surface = gdk_window_create_similar_surface(WIN_POPUP, CAIRO_CONTENT_COLOR_ALPHA, w, h);
    else               /* Image */
#ifdef GTK3
        surface = gdk_window_create_similar_image_surface(WIN_POPUP, CAIRO_FORMAT_ARGB32, w, h, 0);
#else
        surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
#endif

    if (surface == NULL || cairo_surface_get_reference_count(surface) <= 0)
        return NULL;

    if (cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS)
        return surface;
    else
    {
        cairo_surface_destroy(surface);
        return NULL;
    }
}

static gboolean destroy_surface_idled(gpointer data)
{
    cairo_surface_t *surface = (cairo_surface_t *) data;

    if (surface != NULL)
        cairo_surface_destroy(surface);

    return FALSE;
}

static int my_add_quad_row(decor_quad_t * q, int width, int ypush, int vgrav, int x0, int y0)
{
    int p1y = (vgrav == GRAVITY_NORTH) ? -ypush : 0;
    int p2y = (vgrav == GRAVITY_NORTH) ? 0 : ypush;

    int fwidth = width - (BORDER + BORDER);

    q->p1.x = -BORDER;
    q->p1.y = p1y;        /* opt: never changes */
    q->p1.gravity = vgrav | GRAVITY_WEST;
    q->p2.x = 0;
    q->p2.y = p2y;
    q->p2.gravity = vgrav | GRAVITY_WEST;
    q->align = 0;       /* opt: never changes */
    q->clamp = 0;
    q->stretch = 0;
    q->max_width = BORDER;
    q->max_height = ypush;      /* opt: never changes */
    q->m.x0 = x0;
    q->m.y0 = y0;       /* opt: never changes */
    q->m.xx = 1;        /* opt: never changes */
    q->m.xy = 0;
    q->m.yy = 1;
    q->m.yx = 0;        /* opt: never changes */

    q++;

    q->p1.x = 0;
    q->p1.y = p1y;
    q->p1.gravity = vgrav | GRAVITY_WEST;
    q->p2.x = 0;
    q->p2.y = p2y;
    q->p2.gravity = vgrav | GRAVITY_EAST;
    q->align = ALIGN_LEFT | ALIGN_TOP;
    q->clamp = 0;
    q->stretch = STRETCH_X;
    q->max_width = fwidth;
    q->max_height = ypush;
    q->m.x0 = x0 + BORDER;
    q->m.y0 = y0;
    q->m.xx = 1;
    q->m.xy = 0;
    q->m.yy = 1;
    q->m.yx = 0;

    q++;

    q->p1.x = 0;
    q->p1.y = p1y;
    q->p1.gravity = vgrav | GRAVITY_EAST;
    q->p2.x = BORDER;
    q->p2.y = p2y;
    q->p2.gravity = vgrav | GRAVITY_EAST;
    q->max_width = BORDER;
    q->max_height = ypush;
    q->align = 0;
    q->clamp = 0;
    q->stretch = 0;
    q->m.x0 = x0 + BORDER + fwidth;
    q->m.y0 = y0;
    q->m.xx = 1;
    q->m.yy = 1;
    q->m.xy = 0;
    q->m.yx = 0;

    return 3;
}
static int my_add_quad_col(decor_quad_t * q, int height, int xpush, int hgrav, int x0, int y0)
{
    int p1x = (hgrav == GRAVITY_WEST) ? -xpush : 0;
    int p2x = (hgrav == GRAVITY_WEST) ? 0 : xpush;

    q->p1.x = p1x;
    q->p1.y = 0;
    q->p1.gravity = GRAVITY_NORTH | hgrav;
    q->p2.x = p2x;
    q->p2.y = 0;
    q->p2.gravity = GRAVITY_SOUTH | hgrav;
    q->max_width = xpush;
    q->max_height = height;
    q->align = 0;
    q->clamp = CLAMP_VERT;
    q->stretch = STRETCH_Y;
    q->m.x0 = x0;
    q->m.y0 = y0;
    q->m.xx = 1;
    q->m.xy = 0;
    q->m.yy = 1;
    q->m.yx = 0;

    return 1;
}

static int my_set_window_quads(decor_quad_t * q, int width, int height)
{
    int nq;
    int mnq = 0;

    /* top quad */
    nq = my_add_quad_row(q, width, TITLE_H + BORDER, GRAVITY_NORTH, 0, 0);
    q += nq;
    mnq += nq;

    /* bottom quad */
    nq = my_add_quad_row(q, width, BORDER, GRAVITY_SOUTH, 0, height - BORDER);
    q += nq;
    mnq += nq;

    nq = my_add_quad_col(q, height - (TITLE_H + BORDER + BORDER), BORDER, GRAVITY_WEST, 0, BORDER + TITLE_H);
    q += nq;
    mnq += nq;

    nq = my_add_quad_col(q, height - (TITLE_H + BORDER + BORDER), BORDER, GRAVITY_EAST, width - BORDER, BORDER + TITLE_H);
    q += nq;
    mnq += nq;

    return mnq;
}

#define FIT(x, min, max) (x < min ? min : x > max ? max : x)

#define BYTE(x, n) (((uint8_t *)&x)[n])

#define cairo_set_RGBA(cr, c) cairo_set_source_rgba(cr, BYTE(c, 0) / 255.0, BYTE(c, 1) / 255.0, BYTE(c, 2) / 255.0, BYTE(c, 3) / 255.0)

#define cairo_show_textXY(dx, dy, s) { cairo_move_to(cr, TITLE_H + BORDER + TITLE_H / 4 + dx, TITLE_H + BORDER - 3 + dy); cairo_show_text(cr, s); }

// NOTE Will not work with -O3 magic, only -O2 allowed.
uint16_t k (uint8_t N, uint8_t h)
{
    uint32_t result = (N * 255 + (h-1) * 12) % (12*255); // N = 0 (R), 8 (G), 4 (B)
    return (uint16_t) result;
}

/*  NOTE h=0 is special one: forces not red, but gray color. Use 1 for red.  */
uint32_t ahsl2abgr(uint8_t a, uint8_t h, uint8_t s, uint8_t l) // Full range 0..255 each
{
    if (h == 0)
        s = 0;

    uint16_t A = (s * MIN(l, 255 - l) * 259) >> 8;

#define f(N) (uint8_t) ((l * (257*255) - A * MAX(MIN(MIN(k(N, h) - 3*255, 9*255 - k(N, h)), 255), -255)) / (255*255))

    uint32_t result = (((((a << 8) + f(4)) << 8) + f(8)) << 8) + f(0);
    return result;
}

static void draw_window_decoration(decor_t * d)
{
    if (d->active)
    {
        d->surface = d->p_active_surface;
        d->buffer_surface = d->p_active_buffer_surface;
    }
    else
    {
        d->surface = d->p_inactive_surface;
        d->buffer_surface = d->p_inactive_buffer_surface;
    }

    if (d->surface == NULL)
        return;

    cairo_t *cr;
    cr = cairo_create(d->buffer_surface != NULL ? d->buffer_surface : d->surface);
    if (cr == NULL)
        return;

    cairo_set_RGBA(cr, d->color[d->active][1]);
    cairo_rectangle(cr, 0, 0, d->width, TITLE_H + BORDER * 2);
    cairo_fill(cr);

    if (d->active)
    {
        int grad_x = MAX(TITLE_H + BORDER, d->width - BORDER * 2 - GRAD_W);
        int grad_w = MIN(GRAD_W, d->width - BORDER * 3 - TITLE_H);

        if (grad_w > 0)
        {
            cairo_surface_t *gradient;
            gradient = cairo_image_surface_create(CAIRO_FORMAT_RGB24, grad_w, GRAD_H);
            int* gradient_pixels = (int *) cairo_image_surface_get_data (gradient);

            /*  We need BGR24 or ABGR32, but Cairo does not offer it.  */
            uint32_t color[2];
            color[0] = __builtin_bswap32(d->color[d->active][0] << 8);
            color[1] = __builtin_bswap32(d->color[d->active][1] << 8);

            for (int x = 0; x < grad_w; x++)
                for (int y = 0; y < GRAD_H; y++)
                    gradient_pixels[x + grad_w * y] = color[(x > bayer[x%16][y%16])];

            cairo_save(cr);
            cairo_translate(cr, BORDER + grad_x, BORDER);
            cairo_set_source_surface(cr, gradient, 0, 0);
            cairo_rectangle(cr, 0, 0, grad_w, GRAD_H);
            cairo_clip(cr); /* How it works? NOTE */
            cairo_paint(cr);
            cairo_restore(cr);
            cairo_surface_destroy(gradient);
        }

        cairo_save(cr);
        cairo_translate(cr, BORDER, BORDER);
        cairo_set_RGBA(cr, d->color[d->active][0]);
        cairo_rectangle(cr, 0, 0, grad_x, TITLE_H);
        cairo_fill(cr);
        cairo_restore(cr);
    }

    cairo_select_font_face(cr, BDF_PCF_FONT, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, TITLE_H);

    if (1) // FIXME make only when necessary (gradient overlay)
    {
        cairo_set_RGBA(cr, d->color[d->active][0]);
        int i;
        for (i = 0; i < 9; i++)
            cairo_show_textXY(-1 + i%3, -1 + i/3, d->title);
    }

    cairo_set_RGBA(cr, d->color[d->active][2]);
    cairo_show_textXY(0, 0, d->title);

    if (d->icon)
    {
        cairo_translate(cr, BORDER, BORDER);
        cairo_set_source(cr, d->icon);
        cairo_rectangle(cr, 0, 0, TITLE_H, TITLE_H);
        cairo_clip(cr);
        cairo_paint(cr);
    }

    cairo_destroy(cr);
    cr = NULL;

    cr = cairo_create(d->surface);
    // cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr, d->buffer_surface, 0, 0);
    cairo_rectangle(cr, 0, 0, d->width, d->height);
    cairo_clip(cr);
    cairo_paint(cr);
    cairo_destroy(cr);

    if (d->prop_xid)
    {
        long *data = NULL;

        decor_extents_t extents;
        unsigned int nQuad;
        decor_quad_t quads[N_QUADS_MAX];

        nQuad = my_set_window_quads(quads, d->width, d->height);
        extents.bottom = extents.left = extents.right = 0;
        extents.top = TITLE_H;

        data = decor_alloc_property(1, WINDOW_DECORATION_TYPE_PIXMAP);

        decor_quads_to_property(data, 0, cairo_xlib_surface_get_drawable(d->surface), &extents, &extents, &extents, &extents, 0, 0, quads, nQuad, 0xffffff, 0, 0);

        gdk_error_trap_push();
        XChangeProperty(xdisplay, d->prop_xid, win_decor_atom, XA_INTEGER, 32, PropModeReplace, (guchar *) data, PROP_HEADER_SIZE + BASE_PROP_SIZE + QUAD_PROP_SIZE * N_QUADS_MAX);

        XSync(xdisplay, FALSE);

        (void) gdk_error_trap_pop();

        // d->prop_xid = 0; // FIXME Why it was?
    }
}

static gboolean draw_decor_list(void *data)
{
    GSList *list;
    decor_t *d;

    draw_idle_id = 0;

    for (list = draw_list; list; list = list->next)
    {
        d = (decor_t *) list->data;
        (*d->draw) (d);
    }

    g_slist_free(draw_list);
    draw_list = NULL;

    return FALSE;
}

static void queue_decor_draw(decor_t * d)
{
    draw_list = g_slist_append(draw_list, d);

    if (!draw_idle_id)
        draw_idle_id = g_idle_add(draw_decor_list, NULL);
}

static gboolean get_window_prop(Window xwindow, Atom atom, Window * val)
{
    Atom type;
    int format;
    gulong nitems;
    gulong bytes_after;
    Window *w;
    int err, result;

    *val = 0;

    gdk_error_trap_push();

    type = None;
    result = XGetWindowProperty(xdisplay, xwindow, atom, 0, G_MAXLONG, False, XA_WINDOW, &type, &format, &nitems, &bytes_after, (void *)&w);
    err = gdk_error_trap_pop();

    if (err != Success || result != Success)
        return FALSE;

    if (type != XA_WINDOW)
    {
        XFree(w);
        return FALSE;
    }

    *val = *w;
    XFree(w);

    return TRUE;
}

#define MWM_DECOR_ALL      (1L << 0)
#define MWM_DECOR_BORDER   (1L << 1)
#define MWM_DECOR_HANDLE   (1L << 2)
#define MWM_DECOR_TITLE    (1L << 3)
#define MWM_DECOR_MENU     (1L << 4)
#define MWM_DECOR_MINIMIZE (1L << 5)
#define MWM_DECOR_MAXIMIZE (1L << 6)

static unsigned int get_mwm_prop(Window xwindow)
{
    typedef struct {
        unsigned long flags;
        unsigned long functions;
        unsigned long decorations;
    } MwmHints;

    Atom actual;
    int err, result, format;
    unsigned long n, left;
    unsigned char *hints_ret;
    MwmHints *mwm_hints;
    unsigned int decor = MWM_DECOR_ALL;

    gdk_error_trap_push();

    result = XGetWindowProperty(xdisplay, xwindow, mwm_hints_atom, 0L, 20L, FALSE, mwm_hints_atom, &actual, &format, &n, &left, &hints_ret);
    mwm_hints = (MwmHints *) hints_ret;

    err = gdk_error_trap_pop();
    if (err != Success || result != Success)
        return decor;

    if (mwm_hints)
    {
        if (n >= 3 && mwm_hints->flags & (1L << 1)) // PROP_MOTIF_WM_HINT_ELEMENTS, MWM_HINTS_DECORATIONS
            decor = mwm_hints->decorations; // Is it initialised? NOTE
        XFree(mwm_hints);
    }

    return decor;
}

#define D decor_t *d = g_object_get_data(G_OBJECT(win), "decor");

static void update_event_windows(WnckWindow * win)
{
    D;
    gint x0, y0, width, height;

    wnck_window_get_client_window_geometry(win, &x0, &y0, &width, &height);

    gdk_error_trap_push();
    XMapWindow(xdisplay, d->event_window);
    XMoveResizeWindow(xdisplay, d->event_window, BORDER, BORDER, width, TITLE_H);
    XSync(xdisplay, FALSE);
    (void) gdk_error_trap_pop();
}

static void update_window_decoration_icon(WnckWindow * win)
{
    D;
    if (d->icon)
    {
        cairo_pattern_destroy(d->icon);
        d->icon = NULL;
    }

    if (d->icon_surface != NULL)
        cairo_surface_destroy(d->icon_surface);

    d->icon_surface = NULL;

    if (d->icon_pixbuf)
        g_object_unref(G_OBJECT(d->icon_pixbuf));

    d->icon_pixbuf = wnck_window_get_mini_icon(win);

    if (d->icon_pixbuf)
    {
        cairo_t *cr;

        g_object_ref(G_OBJECT(d->icon_pixbuf));

        d->icon_surface = create_surface(gdk_pixbuf_get_width(d->icon_pixbuf),
                                         gdk_pixbuf_get_height(d->icon_pixbuf), 0);
        if (d->icon_surface && cairo_surface_get_reference_count(d->icon_surface) > 0)
        {
            cr = cairo_create(d->icon_surface);
            gdk_cairo_set_source_pixbuf(cr, d->icon_pixbuf, 0, 0);
            cairo_paint(cr);
            d->icon = cairo_pattern_create_for_surface(cairo_get_target(cr));
            cairo_destroy(cr);
        }
    }
}

static void update_window_decoration_state(WnckWindow * win)
{
    // D;
    // d->state = wnck_window_get_state(win); // FIXME What it does?
}


#define Uw width = BORDER * 2 + MAX(w, 1)
#define Uh height = BORDER * 2 + TITLE_H

static int update_window_decoration_size(WnckWindow * win)
{
    D;
    cairo_surface_t *surface = NULL, *buffer_surface = NULL;
    cairo_surface_t *isurface = NULL, *ibuffer_surface = NULL;
    gint width, height, w, h;
    int size_changed;
    const gchar *title;
    glong title_length;

    wnck_window_get_client_window_geometry(win, NULL, NULL, &w, &h);

    Uw;
    Uh;

    if (width == d->width && height == d->height)
    {
        size_changed = 0;
        goto update_window_decoration_name;
    }
    else
        size_changed = 1;

    int max_w_h = MAX(width, height);
#define U max_w_h, (TITLE_H + BORDER * 4)

    surface = create_surface(U, 1);
    if (surface == NULL)
        return FALSE;

    buffer_surface = create_surface(U, 0);
    if (buffer_surface == NULL)
        goto fail1;

    isurface = create_surface(U, 1);
    if (isurface == NULL)
        goto fail2;

    ibuffer_surface = create_surface(U, 0);
    if (ibuffer_surface == NULL)
    {
        cairo_surface_destroy(isurface);
fail2:
        cairo_surface_destroy(buffer_surface);
fail1:
        cairo_surface_destroy(surface);
        return FALSE;
    }

    /* wait until old surfaces are not used for sure,
       one second should be enough */
    if (d->p_active_surface != NULL)
        g_timeout_add_seconds(1, destroy_surface_idled, d->p_active_surface);
    if (d->p_inactive_surface != NULL)
        g_timeout_add_seconds(1, destroy_surface_idled, d->p_inactive_surface);

    if (d->p_active_buffer_surface != NULL)
        cairo_surface_destroy(d->p_active_buffer_surface);

    if (d->p_inactive_buffer_surface != NULL)
        cairo_surface_destroy(d->p_inactive_buffer_surface);

    d->surface                   = d->active ? surface : isurface;
    d->buffer_surface            = d->active ? buffer_surface : ibuffer_surface;
    d->p_active_surface          = surface;
    d->p_active_buffer_surface   = buffer_surface;
    d->p_inactive_surface        = isurface;
    d->p_inactive_buffer_surface = ibuffer_surface;

    d->width = width;
    d->height = height;

    d->prop_xid = wnck_window_get_xid(win);

    d->hue = d->prop_xid % 255 + 1;

    /*                       ....BrigNormDark  */
    /*  Active, Inactive:    ....A I A I A I   */
    const uint64_t satur = 0x0000A020C410C410;
    const uint64_t luma  = 0x0000E0C060602020;

    for (int i = 0; i < 6; i++)
        d->color[i%2][i/2] = ahsl2abgr(255, d->hue, BYTE(satur, i), BYTE(luma, i));

update_window_decoration_name:
    title = wnck_window_get_name(win);
    if (title && (title_length = strlen(title)))
    {
        strncpy(d->title, title, 100); // FIXME and handle UTF
// printf("title: '%s'\n", d->title);
    }

    if (size_changed)
        queue_decor_draw(d);

    return size_changed;
}

static void add_frame_window(WnckWindow * win, Window frame)
{
    XSetWindowAttributes attr;
    gulong xid = wnck_window_get_xid(win);
    D;

    d->active = wnck_window_is_active(win);

    memset(&attr, 0, sizeof(XSetWindowAttributes));

    attr.event_mask = ButtonPressMask; // NOTE or ButtonPressMask | EnterWindowMask | LeaveWindowMask;
    attr.override_redirect = 1;

    gdk_error_trap_push();

    d->event_window = XCreateWindow(xdisplay, frame, 0, 0, 1, 1, 0, CopyFromParent, CopyFromParent, CopyFromParent, CWOverrideRedirect | CWEventMask, &attr);

    // NOTE attr.event_mask |= ButtonReleaseMask;

    XSync(xdisplay, FALSE);
    if (! gdk_error_trap_pop())
    {
        // if (get_mwm_prop(xid) & (MWM_DECOR_ALL | MWM_DECOR_TITLE))  BUG?
        //     d->decorated = 1;                                       BUG?
        d->decorated = (get_mwm_prop(xid) & (MWM_DECOR_ALL | MWM_DECOR_TITLE));

        g_hash_table_insert(frame_table, GUINT_TO_POINTER(d->event_window), GUINT_TO_POINTER(xid));

        update_window_decoration_state(win);
        update_window_decoration_icon(win);
        update_window_decoration_size(win);
        update_event_windows(win);
    }
    else
        memset((void *)d->event_window, 0, sizeof(d->event_window));
}

static void remove_frame_window(WnckWindow * win)
{
    D;

    if (d->p_active_surface != NULL)
        cairo_surface_destroy(d->p_active_surface);
    d->p_active_surface = NULL;

    if (d->p_active_buffer_surface != NULL)
        cairo_surface_destroy(d->p_active_buffer_surface);
    d->p_active_buffer_surface = NULL;

    if (d->p_inactive_surface != NULL)
        cairo_surface_destroy(d->p_inactive_surface);
    d->p_inactive_surface = NULL;

    if (d->p_inactive_buffer_surface != NULL)
        cairo_surface_destroy(d->p_inactive_buffer_surface);
    d->p_inactive_buffer_surface = NULL;

    if (d->icon)
    {
        cairo_pattern_destroy(d->icon);
        d->icon = NULL;
    }

    if (d->icon_surface != NULL)
        cairo_surface_destroy(d->icon_surface);

    d->icon_surface = NULL;

    if (d->icon_pixbuf)
    {
        g_object_unref(G_OBJECT (d->icon_pixbuf));
        d->icon_pixbuf = NULL;
    }

    d->width = d->height = d->decorated = 0;
    draw_list = g_slist_remove(draw_list, d);
}

#define DR  D; if (! d->decorated) return;

static void window_name_changed(WnckWindow * win)
{
    DR;
    if (! update_window_decoration_size(win))
        queue_decor_draw(d);
}

static void window_geometry_changed(WnckWindow * win)
{
    DR;
    int width, height;
    wnck_window_get_client_window_geometry(win, NULL, NULL, &width, &height);

    if ((width != d->client_width) || (height != d->client_height))
    {
        d->client_width = width;
        d->client_height = height;
        update_window_decoration_size(win);
        update_event_windows(win);
    }
}

static void window_icon_changed(WnckWindow * win)
{
    DR;
    update_window_decoration_icon(win);
    queue_decor_draw(d);
}

static void window_state_changed(WnckWindow * win)
{
    DR;
    update_window_decoration_state(win);
    update_window_decoration_size(win);
    update_event_windows(win);

    d->prop_xid = wnck_window_get_xid(win);
    queue_decor_draw(d);
}

static void active_window_changed_1(WnckWindow *win)
{
    if (win)
    {
        D;
        if (d != NULL && d->surface != NULL && d->decorated)
        {
            d->active = wnck_window_is_active(win);
            if (!g_slist_find(draw_list, d))
                d->prop_xid = wnck_window_get_xid(win);
            queue_decor_draw(d);
        }
    }
}

static void active_window_changed(WnckScreen *screen)
{
    active_window_changed_1 (wnck_screen_get_previously_active_window(screen));
    active_window_changed_1 (wnck_screen_get_active_window(screen));
}

static void window_opened(WnckScreen *screen, WnckWindow *win)
{
    decor_t *d;
    Window window;

    d = g_malloc0(sizeof(decor_t));
    if (!d)
        return;

    wnck_window_get_client_window_geometry(win, NULL, NULL, &d->client_width, &d->client_height);

    d->draw = draw_window_decoration;

    g_object_set_data(G_OBJECT(win), "decor", d);

    g_signal_connect_object(win, "name_changed",
                G_CALLBACK(window_name_changed),     0, 0);
    g_signal_connect_object(win, "geometry_changed",
                G_CALLBACK(window_geometry_changed), 0, 0);
    g_signal_connect_object(win, "icon_changed",
                G_CALLBACK(window_icon_changed),     0, 0);
    g_signal_connect_object(win, "state_changed",
                G_CALLBACK(window_state_changed),    0, 0);

    if (get_window_prop(wnck_window_get_xid(win), frame_window_atom, &window))
        add_frame_window(win, window);
}

static void window_closed(WnckScreen * screen, WnckWindow *win)
{
    D;
    gdk_error_trap_push();
    XDeleteProperty(xdisplay, wnck_window_get_xid(win), win_decor_atom);
    XSync(xdisplay, FALSE);
    (void) gdk_error_trap_pop();

    g_object_set_data(G_OBJECT(win), "decor", NULL);

    g_free(d);
}

static void move_resize_restack_window(WnckWindow * win, Atom atom, int l0, int l1, int l2, int l3, int l4, guint32 time)
{
    XEvent ev;

    ev.xclient.type = ClientMessage;
    ev.xclient.display = xdisplay;

    ev.xclient.serial = 0;
    ev.xclient.send_event = TRUE;

    ev.xclient.window = wnck_window_get_xid(win);
    ev.xclient.message_type = atom;
    ev.xclient.format = 32;

    ev.xclient.data.l[0] = l0;
    ev.xclient.data.l[1] = l1;
    ev.xclient.data.l[2] = l2;
    ev.xclient.data.l[3] = l3;
    ev.xclient.data.l[4] = l4;

    if (time)
    {
        XUngrabPointer(xdisplay,  (Time) time);
        XUngrabKeyboard(xdisplay, (Time) time);
    }

    XSendEvent(xdisplay, xroot, FALSE, SubstructureRedirectMask | SubstructureNotifyMask, &ev);

    XSync(xdisplay, FALSE);
}



/* This is Window menu: right mouse button on title, or Alt+Space */

/*   NOTE that original Emerald uses GTK2; this code also work with it.
 * With GTK3, gtk_menu_popup() is deprecated. Trying to use suggested
 * replacements like gtk_menu_popup_at_rect() is quite complex, but
 * still will not work with right mouse button on title, unless one
 * will re-implement here the whole (way more complex) gtk_menu_popup()
 * yet fix its nested deprecations.
 * See 'transfer window' at GTK's gtkmenu.c.
 * So, only Alt+Space will work with GTK3, but no mouse.
 *   Hope I am wrong with above; but, no any example I found for mouse
 * popup menu on title with GTK3.  */
static void action_menu_map(WnckWindow *win, XEvent *xevent)
{
    /* Check if previous menu is still displayed */
    if (retValue == -3)
        return;

    gint x, y;

#ifdef GTK3
// XUngrabPointer...
// XUngrabKeyboard...

    GdkDevice *mouse_device = gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display_get_default ()));
    GdkWindow* window = WIN_POPUP;
    gdk_window_get_device_position (window, mouse_device, &x, &y, NULL);
#else
    GdkModifierType state;
    gdk_display_get_pointer(gdkdisplay, &gdkscreen, &x, &y, &state);
#endif

    PyTuple_SetItem(pArgs, 0, PyLong_FromLong(x));
    PyTuple_SetItem(pArgs, 1, PyLong_FromLong(y));

    retValue = -3;

    retValue = PyLong_AsLong(PyObject_CallObject(pFunc, pArgs));

    switch (retValue)
    {
        case 0:
            wnck_window_minimize(win);
            break;
        case 1:
            wnck_window_maximize(win);
            break;
        case 2:
            wnck_window_unmaximize(win);
            break;
        case 3:
            wnck_window_keyboard_move(win);
            break;
        case 4:
            wnck_window_keyboard_size(win);
            break;
        case 5:
            wnck_window_close(win, (guint32) xevent->xbutton.time);
            break;
        default:
            break;
    }
}

#define WNCK_MAX_UNMAX if (wnck_window_is_maximized(win)) wnck_window_unmaximize(win); else wnck_window_maximize(win)

typedef void (*event_callback) (WnckWindow *win, XEvent *xevent, GdkXEvent *gdkxevent);
static void title_event        (WnckWindow *win, XEvent *xevent, GdkXEvent *gdkxevent)
{
    static unsigned int last_button_num = 0;
    static Window last_button_xwindow = None;
    static Time last_button_time = 0;

    if (xevent->type != ButtonPress)
        return;

    if (xevent->xbutton.x <= (BORDER + TITLE_H))  /* Window Icon */
        action_menu_map(win, xevent);
    else if (xevent->xbutton.button == 1)
    {
        gint double_click_timeout = 0;
        g_object_get(G_OBJECT(gtk_settings_get_for_screen(gtk_widget_get_screen(window_popup))), "gtk-double-click-time", &double_click_timeout, NULL);

        if (xevent->xbutton.button == last_button_num &&
            xevent->xbutton.window == last_button_xwindow &&
            xevent->xbutton.time   <  last_button_time + double_click_timeout)
        {
            WNCK_MAX_UNMAX;
            last_button_num = 0;
            last_button_xwindow = None;
            last_button_time = 0;
        }
        else
        {
            last_button_num = xevent->xbutton.button;
            last_button_xwindow = xevent->xbutton.window;
            last_button_time = xevent->xbutton.time;

            move_resize_restack_window(win, restack_window_atom, 2, None, Above, 0, 0, 0);
            move_resize_restack_window(win, wm_move_resize_atom, xevent->xbutton.x_root, xevent->xbutton.y_root, 8, xevent->xbutton.button, 1, xevent->xbutton.time); // 8 is WM_MOVERESIZE_MOVE
        }
    }
    else if (xevent->xbutton.button == 2)
        WNCK_MAX_UNMAX;
    else if (xevent->xbutton.button == 3)
        action_menu_map(win, xevent);
}


static GdkFilterReturn event_filter_func(GdkXEvent * gdkxevent, GdkEvent * event, gpointer data)
{
    XEvent *xevent = gdkxevent;
    Window xid = None;

    switch (xevent->type)
    {
        case ButtonPress:
        case ButtonRelease:
        case EnterNotify:
        case LeaveNotify:
        case MotionNotify:
            xid = GPOINTER_TO_UINT(g_hash_table_lookup(frame_table, GUINT_TO_POINTER(xevent->xany.window)));
            break;
        case PropertyNotify:
            if (xevent->xproperty.atom == frame_window_atom)
            {
                WnckWindow *win;

                xid = xevent->xany.window;

                win = wnck_window_get(xid);
                if (win)
                {
                    Window frame;

                    if (get_window_prop(xid, frame_window_atom, &frame))
                        add_frame_window(win, frame);
                    else
                        remove_frame_window(win);
                }
            }
            break;
        case DestroyNotify:
            g_hash_table_remove(frame_table, GUINT_TO_POINTER(xevent->xany.window));
            break;
        case ClientMessage:
            if (xevent->xclient.message_type == toolkit_action_atom)
            {
                unsigned long action;

                action = xevent->xclient.data.l[0];
                if (action == toolkit_action_window_menu_atom)
                {
                    WnckWindow *win;

                    win = wnck_window_get(xevent->xany.window);
                    if (win)
                        /* Alt+Space */
                        action_menu_map(win, xevent);
                }
            }
            break;
        default:
            break;
    }
    if (xid != None)
    {
        WnckWindow *win;

        win = wnck_window_get(xid);
        if (win)
        {
            static event_callback callback = title_event;
            D;

            if (d->decorated)
                if (d->event_window == xevent->xany.window)
                    (*callback) (win, xevent, gdkxevent);
        }
    }
    return GDK_FILTER_CONTINUE;
}

static void update_default_decorations(void)
{
    decor_t d;
    decor_quad_t quads[N_QUADS_MAX];
    decor_extents_t extents;

    memset(&d, 0, sizeof(decor_t));

    Atom bareAtom = XInternAtom(xdisplay, DECOR_BARE_ATOM_NAME, FALSE);
    Atom activeAtom = XInternAtom(xdisplay, DECOR_ACTIVE_ATOM_NAME, FALSE);

    long *data = NULL;
    data = decor_alloc_property(1, WINDOW_DECORATION_TYPE_PIXMAP);

    XDeleteProperty(xdisplay, xroot, bareAtom);

    int w = 200; // What is it? FIXME
    d.Uw;
    d.Uh;

    extents.bottom = extents.left = extents.right = 0;
    extents.top = TITLE_H;

    d.surface = NULL;
    d.buffer_surface = NULL;
    d.icon = NULL;
    d.prop_xid = 0;
    d.draw = draw_window_decoration;

    if (d.decor_normal_surface != NULL)
        cairo_surface_destroy(d.decor_normal_surface);
    if (d.decor_active_surface != NULL)
        cairo_surface_destroy(d.decor_active_surface);

    uint32_t nQuad = my_set_window_quads(quads, d.width, d.height);

    int max_w_h = MAX(d.width, d.height);
    d.decor_normal_surface = create_surface(U, 1);
    d.decor_active_surface = create_surface(U, 1);

    if (d.decor_normal_surface != NULL &&
        d.decor_active_surface != NULL)
    {
        d.p_inactive_surface = d.decor_normal_surface;
        d.p_active_surface = d.decor_active_surface;
        d.p_active_buffer_surface = NULL;
        d.p_inactive_buffer_surface = NULL;
        d.active = FALSE;

        (*d.draw) (&d);

        decor_quads_to_property(data, 0, cairo_xlib_surface_get_drawable(d.p_inactive_surface), &extents, &extents, &extents, &extents, 0, 0, quads, nQuad, 0xffffff, 0, 0);

        decor_quads_to_property(data, 0, cairo_xlib_surface_get_drawable(d.p_active_surface), &extents, &extents, &extents, &extents, 0, 0, quads, nQuad, 0xffffff, 0, 0);

        XChangeProperty(xdisplay, xroot, activeAtom, XA_INTEGER, 32, PropModeReplace, (guchar *) data, PROP_HEADER_SIZE + BASE_PROP_SIZE + QUAD_PROP_SIZE * N_QUADS_MAX);
    }
    else
    {
        if (d.decor_normal_surface != NULL)
        {
            cairo_surface_destroy(d.decor_normal_surface);
            d.decor_normal_surface = NULL;
        }
        if (d.decor_active_surface != NULL)
        {
            cairo_surface_destroy(d.decor_active_surface);
            d.decor_active_surface = NULL;
        }
    }

    if (data)
        free(data);
}

static void signal_handler(int sig)
{
  exit(1);
}



int main(int argc, char *argv[])
{
    gboolean replace = FALSE;
    char *program_name = argv[0];

    gint i;
    for (i = 0; i < argc; i++)
    {
        if (g_strcmp0(argv[i], "--replace") == 0)
            replace = TRUE;
        else if (g_strcmp0(argv[i], "--version") == 0)
        {
            printf("%s: %s, version %s\n", program_name, PACKAGE, VERSION);
            return 0;
        }
        else if (g_strcmp0(argv[i], "--help") == 0)
        {
            fprintf(stderr, "%s [--replace] [--help] [--version]\n", program_name);
            return 0;
        }
    }

    Py_Initialize();

    const char PyMenuSrc[] =
        "from PyQt6.QtCore import QPoint\n"
        "from PyQt6.QtWidgets import QApplication, QMenu\n"
        "from PyQt6.QtGui import QIcon\n"
        "app = QApplication([])\n"
        "def windowActionsMenu(x, y):\n"
        "  q = 6\n"
        "  n = ["ACTIONS"]\n"
        "  p = ["ACT_ICONS"]\n"
        "  m = QMenu()\n"
        "  r = -1\n"
        "  a = [0]*q\n"
        "  for i in range(q):\n"
        "    a[i] = m.addAction(QIcon('"ICONS_PATH"/'+p[i]+'.png'), n[i])\n"
        "    if i == q - 2:\n"
        "      m.addSeparator()\n"
        "  d = m.exec(QPoint (x, y))\n"
        "  for i in range(q):\n"
        "    if d == a[i]:\n"
        "      r = i\n"
        "      break\n"
        "  del a\n"
        "  del m\n"
        "  return r\n" ;

    pFunc = PyObject_GetAttrString(PyImport_ExecCodeModule("", Py_CompileString(PyMenuSrc, "", Py_file_input)), "windowActionsMenu"); // https://stackoverflow.com/a/56563704

    if (!pFunc || !PyCallable_Check(pFunc)) {
        if (PyErr_Occurred())
            PyErr_Print();
        fprintf(stderr, "Cannot find Python function windowActionsMenu.\n");
        return 2;
    }

    pArgs = PyTuple_New(2);

    // I write this to conform the "raw" matrix. To be similar to "compensated"
    // https://stackoverflow.com/a/68192472  (No 255 value, but two 127 values),
    // one need to subtract 1 from >128 values.
    // I believe it can be way shorter, but efficiency not matter here much.
    for (int x = 0; x < 16; x++)
        for (int y = 0; y < 16; y++)
            bayer[x][y] = ((!!(y & 8) *   2 - !!(x & 8) *  1) &   3)
                    + ((!!(y & 4) *   8 - !!(x & 4) *  4) &  12)
                    + ((!!(y & 2) *  32 - !!(x & 2) * 16) &  48)
                    + ((!!(y & 1) * 128 - !!(x & 1) * 64) & 192);

    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);

    gtk_init(&argc, &argv);

    gdkdisplay = gdk_display_get_default();
    gdkscreen = gdk_display_get_default_screen(gdkdisplay);
    xdisplay = gdk_x11_display_get_xdisplay(gdkdisplay);
    xroot = RootWindowOfScreen(gdk_x11_screen_get_xscreen(gdkscreen));
    gdk_error_trap_push();

    frame_window_atom = XInternAtom(xdisplay, DECOR_INPUT_FRAME_ATOM_NAME, FALSE);
    win_decor_atom = XInternAtom(xdisplay, DECOR_WINDOW_ATOM_NAME, FALSE);
    wm_move_resize_atom = XInternAtom(xdisplay, "_NET_WM_MOVERESIZE", FALSE);
    restack_window_atom = XInternAtom(xdisplay, "_NET_RESTACK_WINDOW", FALSE);
    // select_window_atom = XInternAtom(xdisplay, DECOR_SWITCH_WINDOW_ATOM_NAME, FALSE);
    mwm_hints_atom = XInternAtom(xdisplay, "_MOTIF_WM_HINTS", FALSE);
    wm_protocols_atom = XInternAtom(xdisplay, "WM_PROTOCOLS", FALSE);

    toolkit_action_atom = XInternAtom(xdisplay, "_COMPIZ_TOOLKIT_ACTION", FALSE);
    toolkit_action_window_menu_atom = XInternAtom(xdisplay, "_COMPIZ_TOOLKIT_ACTION_WINDOW_MENU", FALSE);

    emerald_sigusr1_atom = XInternAtom(xdisplay, "emerald-sigusr1", FALSE);

    int status = decor_acquire_dm_session(xdisplay, DefaultScreen(xdisplay), PACKAGE, replace, &dm_sn_timestamp);

    if (status != DECOR_ACQUIRE_STATUS_SUCCESS)
    {
        if (status == DECOR_ACQUIRE_STATUS_FAILED)
            fprintf(stderr,
                "%s: Could not acquire decoration manager "
                "selection on screen %d// Display \"%s\"\n",
                program_name, DefaultScreen(xdisplay),
                DisplayString(xdisplay));
        else if (status == DECOR_ACQUIRE_STATUS_OTHER_DM_RUNNING)
            fprintf(stderr,
                "%s: Screen %d on// Display \"%s\" already "
                "has a decoration manager; try using the "
                "--replace option to replace the current "
                "decoration manager.\n",
                program_name, DefaultScreen(xdisplay),
                DisplayString(xdisplay));

        return 1;
    }

    frame_table = g_hash_table_new(NULL, NULL);

#ifdef GTK3
    wnck_handle = wnck_handle_new(WNCK_CLIENT_TYPE_PAGER);
    WnckScreen *wnck_screen = wnck_handle_get_default_screen(wnck_handle);
#else
    wnck_set_client_type(WNCK_CLIENT_TYPE_PAGER);
    WnckScreen *wnck_screen = wnck_screen_get_default();
#endif

    gdk_window_add_filter(NULL, event_filter_func, NULL);

    g_signal_connect_object(G_OBJECT(wnck_screen), "active_window_changed",
                                    G_CALLBACK(active_window_changed), 0, 0);
    g_signal_connect_object(G_OBJECT(wnck_screen), "window_opened",
                                    G_CALLBACK(window_opened),         0, 0);
    g_signal_connect_object(G_OBJECT(wnck_screen), "window_closed",
                                    G_CALLBACK(window_closed),         0, 0);

    window_popup = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_widget_realize(window_popup);

    decor_set_dm_check_hint(xdisplay, DefaultScreen(xdisplay), WINDOW_DECORATION_TYPE_PIXMAP);

    update_default_decorations();

    GList *windows = wnck_screen_get_windows(wnck_screen);
    while (windows)
    {
        window_opened(wnck_screen, windows->data);

        WnckWindow* win = windows->data;
        D;

        if (d->decorated)
        {
            d->width = d->height = 0;
            update_window_decoration_size(WNCK_WINDOW(windows->data));
            update_event_windows(WNCK_WINDOW(windows->data));
        }
        windows = windows->next;
    }

    // g_timeout_add(500, reload_if_needed, NULL);

    gtk_main();

#ifdef GTK3
    g_clear_object (&wnck_handle);
#endif

    (void) gdk_error_trap_pop();

    Py_XDECREF(pFunc);
    PyGC_Collect();
    Py_Finalize();

    return 0;
}
