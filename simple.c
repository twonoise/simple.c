
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
#include <locale.h>
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

#define MAX_TITLE_STRLEN 1024

typedef struct _decor
{
    void    (*draw) (struct _decor *d);
    XID              prop_xid;
    Window           titlebar_window; /* For mouse events */
    int              client_width;
    int              client_height;
    int              decorated;
    int              active;
    uint8_t          hue;
    uint32_t         color[2][3];
    gchar            title[MAX_TITLE_STRLEN]; /* In bytes, not UTF glyphs */
    GdkPixbuf       *icon_gdkpixbuf;
    cairo_surface_t *icon_surface;
    cairo_surface_t *surface;
    cairo_surface_t *buffer_surface;
    cairo_surface_t *p_surface[2];
    cairo_surface_t *p_buffer_surface[2];
} decor_t;

long *decor_alloc_property_data;

static Atom frame_window_atom;
static Atom win_decor_atom;
static Atom active_atom;
static Atom wm_move_resize_atom;
static Atom restack_window_atom;
static Atom wm_protocols_atom;
static Atom mwm_hints_atom;
static Atom toolkit_action_atom;
static Atom toolkit_menu_atom;

static Time dm_sn_timestamp;

static GtkWidget *window_popup;
static GHashTable *frame_table;
static GSList *draw_list = NULL;
static guint draw_idle_id = 0;

GdkDisplay *gdkdisplay;
GdkScreen  *gdkscreen;
Display    *xdisplay;
Window      xroot;

int utf;
uint8_t bayer[16][16];

PyObject *pArgs, *pFunc;
long int retValue = -2;


static cairo_surface_t *create_surface(int w, int h, int isImageOrXlib)
{
    cairo_surface_t *surface;

    if (w <= 0 || h <= 0)
        return NULL;

    if (isImageOrXlib) /* Xlib */
#ifdef GTK3
        surface = gdk_window_create_similar_surface(WIN_POPUP, CAIRO_CONTENT_COLOR_ALPHA, w, h);
#else
        surface = gdk_window_create_similar_surface(WIN_POPUP, CAIRO_CONTENT_COLOR_ALPHA, w, h + 1); /* BUG One extra pixel for GDK2 only, looks like critical bug. */
#endif
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

static int my_set_window_quads(decor_quad_t * q, int width)
{
#define XXXYYYYX { q->m.xx = 1; q->m.xy = 0; q->m.yy = 1; q->m.yx = 0; }

#define my_add_quad_row(width, ypush, vgrav, X0, Y0) { \
        int p1y = (vgrav == GRAVITY_NORTH) ? -ypush : 0; \
        int p2y = (vgrav == GRAVITY_NORTH) ? 0 : ypush; \
        q->p1.x = -BORDER; \
        q->p1.y = p1y; \
        q->p1.gravity = vgrav | GRAVITY_WEST; \
        q->p2.x = 0; \
        q->p2.y = p2y; \
        q->p2.gravity = vgrav | GRAVITY_WEST; \
        q->align = 0; \
        q->clamp = 0; \
        q->stretch = 0; \
        q->max_width = BORDER; \
        q->max_height = ypush; \
        q->m.x0 = X0; \
        q->m.y0 = Y0; \
        XXXYYYYX; \
        q++; \
        q->p1.x = 0; \
        q->p1.y = p1y; \
        q->p1.gravity = vgrav | GRAVITY_WEST; \
        q->p2.x = 0; \
        q->p2.y = p2y; \
        q->p2.gravity = vgrav | GRAVITY_EAST; \
        q->align = ALIGN_LEFT | ALIGN_TOP; \
        q->clamp = 0; \
        q->stretch = STRETCH_X; \
        q->max_width = width; \
        q->max_height = ypush; \
        q->m.x0 = X0 + BORDER; \
        q->m.y0 = Y0; \
        XXXYYYYX; \
        q++; \
        q->p1.x = 0; \
        q->p1.y = p1y; \
        q->p1.gravity = vgrav | GRAVITY_EAST; \
        q->p2.x = BORDER; \
        q->p2.y = p2y; \
        q->p2.gravity = vgrav | GRAVITY_EAST; \
        q->max_width = BORDER; \
        q->max_height = ypush; \
        q->align = 0; \
        q->clamp = 0; \
        q->stretch = 0; \
        q->m.x0 = X0 + BORDER + width; \
        q->m.y0 = Y0; \
        XXXYYYYX; \
        q++; }

#define my_add_quad_col(height, xpush, hgrav, X0, Y0) { \
        int p1x = (hgrav == GRAVITY_WEST) ? -xpush : 0; \
        int p2x = (hgrav == GRAVITY_WEST) ? 0 : xpush; \
        q->p1.x = p1x; \
        q->p1.y = 0; \
        q->p1.gravity = GRAVITY_NORTH | hgrav; \
        q->p2.x = p2x; \
        q->p2.y = 0; \
        q->p2.gravity = GRAVITY_SOUTH | hgrav; \
        q->max_width = xpush; \
        q->max_height = height; \
        q->align = 0; \
        q->clamp = CLAMP_VERT; \
        q->stretch = STRETCH_Y; \
        q->m.x0 = X0; \
        q->m.y0 = Y0; \
        XXXYYYYX; \
        q++; }

    my_add_quad_row(width,    (TITLE_H + BORDER), GRAVITY_NORTH, 0, 0);
    my_add_quad_row(width,                BORDER, GRAVITY_SOUTH, 0, (TITLE_H + BORDER));
    my_add_quad_col((TITLE_H + BORDER*2), BORDER, GRAVITY_WEST,  0, 0);
    my_add_quad_col((TITLE_H + BORDER*2), BORDER, GRAVITY_EAST,  (width + BORDER), 0);

    return 8;
}

#define FIT(x, min, max) (x < min ? min : x > max ? max : x)

#define BYTE(x, n) (((uint8_t *)&x)[n])

#define cairo_set_RGBA(cr, c) cairo_set_source_rgba(cr, BYTE(c, 0) / 255.0, BYTE(c, 1) / 255.0, BYTE(c, 2) / 255.0, BYTE(c, 3) / 255.0)

#define cairo_show_textXY(dx, dy, s) { cairo_move_to(cr, TITLE_H + BORDER + TITLE_H / 4 + dx, TITLE_H + BORDER - 4 + dy); cairo_show_text(cr, s); }

// NOTE Will not work with -O3 magic, only -O2 allowed.
uint16_t k (uint8_t N, uint8_t h)
{
    uint32_t result = (N * 255 + (h-1) * 12) % (12*255); // N = 0 (R), 8 (G), 4 (B)
    return (uint16_t) result;
}

/*  NOTE h=0 is special one: forces not red, but gray color. Use 1 for red.  */
uint32_t ahsl2abgr(uint8_t a, uint8_t h, uint8_t s, uint8_t l) // Full range 0..255 each
{
    /* Equibright curve is hue->brightness func for fully saturate. */
    /* This should be tested on various todays display monitors as there is high variation of real perceptual brightness; then rewrite this with beziers (as they are perfectly integer/linear). */
    int q = (MAX(50-MIN(h,255-h),0) + MAX(30-abs(h-85),0) + MAX(110-abs(h-170),0) - 20); /* Range is: -20 (yellow) to 90 (blue) */

    /* Decompensate the curve for not full saturation. */
    int p = q * s * MIN(l, 255 - l) >> 15;

    l = MIN(255, (l + p)); /* Hope we never overflow uint8 here? */
    s = MAX(  0, (s - p));

    if (h == 0)
        s = 0;

    uint16_t A = (s * MIN(l, 255 - l) * 259) >> 8;

#define f(N) (uint8_t) ((l * (257*255) - A * MAX(MIN(MIN(k(N, h) - 3*255, 9*255 - k(N, h)), 255), -255)) / (255*255))

    uint32_t result = (((((a << 8) + f(4)) << 8) + f(8)) << 8) + f(0);

// printf("%x %x %x -> %d\n", h, s, l, p);

    return result;
}

static void init_decor(XID xid, int client_width, cairo_surface_t *surface, int type)
{
    decor_quad_t quads[N_QUADS_MAX];
    unsigned int nQuad = my_set_window_quads(quads, client_width);
    decor_extents_t extents = {.bottom = 0, .left = 0, .right = 0, .top = TITLE_H};

    decor_quads_to_property(decor_alloc_property_data, 0, cairo_xlib_surface_get_drawable(surface), &extents, &extents, &extents, &extents, 0, 0, quads, nQuad, 0xffffff, 0, 0);

    gdk_error_trap_push();
    XChangeProperty(xdisplay, xid, type ? win_decor_atom : active_atom, XA_INTEGER, 32, PropModeReplace, (guchar *) decor_alloc_property_data, PROP_HEADER_SIZE + BASE_PROP_SIZE + QUAD_PROP_SIZE * N_QUADS_MAX);
    (void) gdk_error_trap_pop();
}

static void draw_window_decoration(decor_t * d)
{
    d->surface = d->p_surface[d->active];
    d->buffer_surface = d->p_buffer_surface[d->active];

    if (! d->surface)
        return;

    cairo_t *cr;
    cr = cairo_create(d->buffer_surface != NULL ? d->buffer_surface : d->surface);
    if (! cr)
        return;

    /* TODO Must be replaced to four lines */
    cairo_set_RGBA(cr, d->color[d->active][1]);
    cairo_rectangle(cr, 0, 0, d->client_width + BORDER * 2, TITLE_H + BORDER * 2);
    cairo_fill(cr);

    if (d->active)
    {
        int grad_x = MAX(TITLE_H + BORDER, d->client_width - GRAD_W);
        int grad_w = MIN(GRAD_W, d->client_width - BORDER - TITLE_H);

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

            cairo_set_source_surface(cr, gradient, BORDER + grad_x, BORDER);
            cairo_paint(cr);
            cairo_surface_destroy(gradient);
        }

        cairo_set_RGBA(cr, d->color[d->active][0]);
        cairo_rectangle(cr, BORDER, BORDER, grad_x, TITLE_H);
        cairo_fill(cr);
    }

    cairo_select_font_face(cr, BDF_PCF_FONT, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, TITLE_H);

    if (1) // FIXME make only when necessary (gradient overlay)
    {
        cairo_set_RGBA(cr, d->color[d->active][0]);
        int i;
        for (i = 0; i < 9; i++)
            if (i != 4)
                cairo_show_textXY(-1 + i%3, -1 + i/3, d->title);
    }

    cairo_set_RGBA(cr, d->color[d->active][2]);
    cairo_show_textXY(0, 0, d->title);

    if (d->icon_surface)
    {
        cairo_set_source_surface(cr, d->icon_surface, BORDER, BORDER);
        cairo_paint(cr);
    }

    cairo_destroy(cr);

    cr = cairo_create(d->surface);
    cairo_set_source_surface(cr, d->buffer_surface, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* I believe it should be possible not each time FIXME */
    init_decor(d->prop_xid, d->client_width, d->surface, 1);

    XSync(xdisplay, FALSE);
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
#define DECOR_SIZE (w + BORDER * 2), (TITLE_H + BORDER * 2)

static void update_titlebar_windows(WnckWindow * win)
{
    D;
    gint x0, y0, width, height;

    wnck_window_get_client_window_geometry(win, &x0, &y0, &width, &height);

    gdk_error_trap_push();
    XMapWindow(xdisplay, d->titlebar_window);
    XMoveResizeWindow(xdisplay, d->titlebar_window, BORDER, BORDER, width, TITLE_H);
    XSync(xdisplay, FALSE);
    (void) gdk_error_trap_pop();
}

static void update_window_decoration_icon(WnckWindow * win)
{
    D;

    if (d->icon_surface != NULL)
        cairo_surface_destroy(d->icon_surface);

    d->icon_surface = NULL;

    if (d->icon_gdkpixbuf)
        g_object_unref(G_OBJECT(d->icon_gdkpixbuf));

    d->icon_gdkpixbuf = wnck_window_get_mini_icon(win);

    if (d->icon_gdkpixbuf)
    {
        g_object_ref(G_OBJECT(d->icon_gdkpixbuf));

        d->icon_surface = create_surface(gdk_pixbuf_get_width(d->icon_gdkpixbuf),
                                         gdk_pixbuf_get_height(d->icon_gdkpixbuf), 0);
        if (d->icon_surface && cairo_surface_get_reference_count(d->icon_surface) > 0)
        {
            cairo_t *cr = cairo_create(d->icon_surface);
            gdk_cairo_set_source_pixbuf(cr, d->icon_gdkpixbuf, 0, 0);
            cairo_paint(cr);
            cairo_destroy(cr);
        }
    }
}

static void update_window_decoration_state(WnckWindow * win)
{
    // D;
    // d->state = wnck_window_get_state(win); // FIXME What it does?
}

static void update_decoration_size_or_title(WnckWindow * win)
{
    D;
    cairo_surface_t *surface[2] = {NULL, NULL}, *buffer_surface[2] = {NULL, NULL};
    gint w, h;
    int i;

    wnck_window_get_client_window_geometry(win, NULL, NULL, &w, &h);
    w = MAX(w, 1);
    h = MAX(h, 0);

    if ((w != d->client_width) || (h != d->client_height))
    {
        d->client_width = w;
        d->client_height = h;

        surface[0] = create_surface(DECOR_SIZE, 1);
        if (surface[0] == NULL)
            return;

        buffer_surface[0] = create_surface(DECOR_SIZE, 0);
        if (buffer_surface[0] == NULL)
            goto fail1;

        surface[1] = create_surface(DECOR_SIZE, 1);
        if (surface[1] == NULL)
            goto fail2;

        buffer_surface[1] = create_surface(DECOR_SIZE, 0);
        if (buffer_surface[1] == NULL)
        {
            cairo_surface_destroy(surface[1]);
fail2:
            cairo_surface_destroy(buffer_surface[0]);
fail1:
            cairo_surface_destroy(surface[0]);
            return;
        }

        /* wait until old surfaces are not used for sure,
        one second should be enough */
        if (d->p_surface[0] != NULL)
            g_timeout_add_seconds(1, destroy_surface_idled, d->p_surface[0]);
        if (d->p_surface[1] != NULL)
            g_timeout_add_seconds(1, destroy_surface_idled, d->p_surface[1]);

        if (d->p_buffer_surface[0] != NULL)
            cairo_surface_destroy(d->p_buffer_surface[0]);

        if (d->p_buffer_surface[1] != NULL)
            cairo_surface_destroy(d->p_buffer_surface[1]);

        d->surface             = surface[d->active];
        d->buffer_surface      = buffer_surface[d->active];
        d->p_surface[0]        = surface[0];
        d->p_buffer_surface[0] = buffer_surface[0];
        d->p_surface[1]        = surface[1];
        d->p_buffer_surface[1] = buffer_surface[1];

        d->prop_xid = wnck_window_get_xid(win);

        d->hue = d->prop_xid % 255 + 1;

        /*                       ....BrigNormDark  */
        /*  Active, Inactive:    ....A I A I A I   */
        const uint64_t satur = 0x0000A020C410C410;
        const uint64_t luma  = 0x0000E0C060602020;

        for (i = 0; i < 6; i++)
            d->color[i%2][i/2] = ahsl2abgr(255, d->hue, BYTE(satur, i), BYTE(luma, i));
    }

    const char* title = wnck_window_get_name(win);

    int title_glyphs = MAX(((d->client_width - TITLE_H - BORDER) / (TITLE_H/2) - 1), 0);

    if (utf)
    {
        /* UTF slows down operation. It should be avoided at hi-rel apps. */
        i = 0;
        int glyphs = 0;
        while ((title[i]) && (i < (MAX_TITLE_STRLEN - 1)))
        {
            if ((title[i] & 0xC0) != 0x80)
                if (++glyphs > title_glyphs)
                    break;

            d->title[i] = title[i];
            i++;
        }
    }
    else
    {
        i = MIN(MAX_TITLE_STRLEN, title_glyphs);
        strncpy(d->title, title, i);
    }
    d->title[i] = '\0';

    queue_decor_draw(d);
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

    d->titlebar_window = XCreateWindow(xdisplay, frame, 0, 0, 1, 1, 0, CopyFromParent, CopyFromParent, CopyFromParent, CWOverrideRedirect | CWEventMask, &attr);

    // NOTE attr.event_mask |= ButtonReleaseMask;

    XSync(xdisplay, FALSE);
    if (! gdk_error_trap_pop())
    {
        d->decorated = (get_mwm_prop(xid) & (MWM_DECOR_ALL | MWM_DECOR_TITLE));

        g_hash_table_insert(frame_table, GUINT_TO_POINTER(d->titlebar_window), GUINT_TO_POINTER(xid));

        update_window_decoration_state(win);
        update_window_decoration_icon(win);
        update_decoration_size_or_title(win);
        update_titlebar_windows(win);
    }
    else
        memset((void *)d->titlebar_window, 0, sizeof(d->titlebar_window));
}

static void remove_frame_window(WnckWindow * win)
{
    D;

    if (d->p_surface[0] != NULL)
        cairo_surface_destroy(d->p_surface[0]);
    d->p_surface[0] = NULL;

    if (d->p_buffer_surface[0] != NULL)
        cairo_surface_destroy(d->p_buffer_surface[0]);
    d->p_buffer_surface[0] = NULL;

    if (d->p_surface[1] != NULL)
        cairo_surface_destroy(d->p_surface[1]);
    d->p_surface[1] = NULL;

    if (d->p_buffer_surface[1] != NULL)
        cairo_surface_destroy(d->p_buffer_surface[1]);
    d->p_buffer_surface[1] = NULL;

    if (d->icon_surface != NULL)
        cairo_surface_destroy(d->icon_surface);

    d->icon_surface = NULL;

    if (d->icon_gdkpixbuf)
    {
        g_object_unref(G_OBJECT (d->icon_gdkpixbuf));
        d->icon_gdkpixbuf = NULL;
    }

    d->client_width = d->client_height = d->decorated = 0;
    draw_list = g_slist_remove(draw_list, d);
}

#define DR  D; if (! d->decorated) return;

static void window_name_changed(WnckWindow * win)
{
    DR;
    update_decoration_size_or_title(win);
}

static void window_geometry_changed(WnckWindow * win)
{
    DR;
    int w, h;
    wnck_window_get_client_window_geometry(win, NULL, NULL, &w, &h);

    if ((w != d->client_width) || (h != d->client_height))
    {
        update_decoration_size_or_title(win);
        update_titlebar_windows(win);
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
    update_decoration_size_or_title(win);
    update_titlebar_windows(win);

    // d->prop_xid = wnck_window_get_xid(win);
}

static void active_window_changed_1(WnckWindow *win)
{
    if (win)
    {
        D;
        if (d != NULL && d->surface != NULL && d->decorated)
        {
            d->active = wnck_window_is_active(win);
            // if (!g_slist_find(draw_list, d))
            //     d->prop_xid = wnck_window_get_xid(win);
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

/* Now it completely reworked using PyQt. */
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
                if (action == toolkit_menu_atom)
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
                if (d->titlebar_window == xevent->xany.window)
                    (*callback) (win, xevent, gdkxevent);
        }
    }
    return GDK_FILTER_CONTINUE;
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

    utf = 0;
    if(setlocale(LC_ALL, ""))
    {
        char *locale = setlocale(LC_MESSAGES, NULL);
        if (locale && (strstr(locale, "UTF") || strstr(locale, "utf")))
            utf = 1;
        else
            printf("%s: LC_MESSAGES set to single-byte: '%s'\n", program_name, locale);
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
    gdkscreen  = gdk_display_get_default_screen(gdkdisplay);
    xdisplay   = gdk_x11_display_get_xdisplay(gdkdisplay);
    xroot      = RootWindowOfScreen(gdk_x11_screen_get_xscreen(gdkscreen));

    gdk_error_trap_push();

    frame_window_atom   = XInternAtom(xdisplay, DECOR_INPUT_FRAME_ATOM_NAME, FALSE);
    win_decor_atom      = XInternAtom(xdisplay, DECOR_WINDOW_ATOM_NAME, FALSE);
    active_atom         = XInternAtom(xdisplay, DECOR_ACTIVE_ATOM_NAME, FALSE);
    // select_window_atom = XInternAtom(xdisplay, DECOR_SWITCH_WINDOW_ATOM_NAME, FALSE);
    wm_move_resize_atom = XInternAtom(xdisplay, "_NET_WM_MOVERESIZE", FALSE);
    restack_window_atom = XInternAtom(xdisplay, "_NET_RESTACK_WINDOW", FALSE);
    mwm_hints_atom      = XInternAtom(xdisplay, "_MOTIF_WM_HINTS", FALSE);
    wm_protocols_atom   = XInternAtom(xdisplay, "WM_PROTOCOLS", FALSE);

    toolkit_action_atom = XInternAtom(xdisplay, "_COMPIZ_TOOLKIT_ACTION", FALSE);
    toolkit_menu_atom   = XInternAtom(xdisplay, "_COMPIZ_TOOLKIT_ACTION_WINDOW_MENU", FALSE);

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

    Atom bareAtom = XInternAtom(xdisplay, DECOR_BARE_ATOM_NAME, FALSE);
    XDeleteProperty(xdisplay, xroot, bareAtom);

    cairo_surface_t *temp_surface = create_surface(1, 1, 1);

    if (! temp_surface)
    {
        fprintf(stderr, "%s: Cairo can't create surfaces.\n", program_name);
        return 1;
    }

    decor_alloc_property_data = decor_alloc_property(1, WINDOW_DECORATION_TYPE_PIXMAP);

    init_decor(xroot, 1, temp_surface, 0);

    GList *windows = wnck_screen_get_windows(wnck_screen);
    while (windows)
    {
        window_opened(wnck_screen, windows->data);

        WnckWindow* win = windows->data;
        D;

        if (d->decorated)
        {
            d->client_width = d->client_height = 0;
            update_decoration_size_or_title(WNCK_WINDOW(windows->data));
            update_titlebar_windows(WNCK_WINDOW(windows->data));
        }
        windows = windows->next;
    }

    // g_timeout_add(500, reload_if_needed, NULL);

    gtk_main();

    free(decor_alloc_property_data);
    cairo_surface_destroy(temp_surface);

#ifdef GTK3
    g_clear_object (&wnck_handle);
#endif

    (void) gdk_error_trap_pop();

    Py_XDECREF(pFunc);
    PyGC_Collect();
    Py_Finalize();

    return 0;
}
