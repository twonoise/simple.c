
/* Std: C99. Based on: Emerald, Copyright 2006 Novell, Inc., License: GPL2+ */

#define PACKAGE      "simple"
#define VERSION      "0.8.18.1"  /* Will work with Compiz 0.9.14.2 also. */

#define BORDER       (1 * scale)
#define TITLE_H      (16 * scale)
#define GRAD_W       (256 * scale)
#define GRAD_H       (16 * scale)
#define ICON_SZ      "16"        /* Popup Menu icons: 16, 24, 32, 48, scalable. */
#define ICONS_PATH   "/root/.icons/Chicago95/actions/"ICON_SZ
#define BDF_PCF_FONT "xos4 Terminus"  /* No .otb and non-bitmaps, please. */
#define ACTIONS      "'Mi&nimize', 'Ma&ximize', '&Restore', '&Move', 'Re&size', 'Copy &Info', '&Close'"
#define ACT_ICONS    "'window-minimize', 'window-maximize', 'window-restore', '-', 'image-crop', 'edit-copy', 'process-stop'"

// Huge memory leak is unavoidable. https://github.com/python/cpython/issues/100773
// Total 2,95 Mb PyQt5 and 1,76 Mb PyQt6!
#include <Python.h>

#include <stdlib.h>
#include <getopt.h>
#include <locale.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
// Another huge memory leak is from GTK. Total leak will be ~4 Mb.
#include <gdk/gdkx.h>
#include <cairo-xlib.h>
#include <Xm/MwmUtil.h>
#include <decoration.h>

/*                 ....BrigNormDark  */
/*  Act, Inact:    ....A I A I A I   */
uint64_t satur = 0x0000A000C410C400;
uint64_t luma  = 0x0000E08060602020;
int utf        = 0;
int ebcomp     = 8;
int replace    = 0;
int hicontrast = 0;
int scale      = 1;
int invert     = 0;
int verbose    = 2;

char *program_name;

static void usage(const char *name)
{
    printf("%s is simple, pixel-perfect window decorator for Compiz 0.8.18, 0.9.14.2\n"
    "options:\n"
    " -h, --help               print help\n"
    " -v, --version            print version\n"
    " -r, --replace            try to replace current window decorator, if any\n"
    " -l, --luma=UINT64        4 paired hex luma values for active & inactive state:\n"
    "                            Reserved, Bright, Normal, Dark color\n"
    "                            Default: %lx\n"
    " -s, --saturation=UINT64  same, but for saturation of these colors\n"
    "                            Default: %lx\n"
    " -e, --eb-comp=INT        equibright compensation strength, 0..8\n"
    "                            Default: 8\n"
    " -c, --high-contrast      reduce saturation, increase contrast\n"
    " -i, --invert-colors      in case if negate filter inverts decoration\n"
    " -d, --hidpi              enlarge pixels as 2x2 dots\n"
    " -m, --verbose=N          message filter, 0..4. Default: 2\n",
    program_name, luma, satur);
}

static const char *shortopts =
    "hvrl:s:e:cidm:";

static const struct option longopts[] = {
    {"help",          0, 0, 'h'},
    {"version",       0, 0, 'v'},
    {"replace",       0, 0, 'r'},
    {"luma",          1, 0, 'l'},
    {"saturation",    1, 0, 's'},
    {"eb-comp",       1, 0, 'e'},
    {"high-contrast", 0, 0, 'c'},
    {"invert-colors", 0, 0, 'i'},
    {"hidpi",         0, 0, 'd'},
    {"verbose",       1, 0, 'm'},
    {0, 0, 0, 0}
};

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

#define cFmt(color,S) "\e[0;3"color"m%s:\e[0m "S"\n", program_name
#define DBV(S,...) if (verbose>3) printf(cFmt("5",S), ##__VA_ARGS__);
#define DBG(S,...) if (verbose>2) printf(cFmt("4",S), ##__VA_ARGS__);
#define MSG(S,...) if (verbose>1) printf(cFmt("2",S), ##__VA_ARGS__);
#define WRN(S,...)       fprintf(stderr, cFmt("3",S), ##__VA_ARGS__);
#define ERR(S,...)     { fprintf(stderr, cFmt("1",S), ##__VA_ARGS__); exit (1); }

#define MAX_TITLE_STRLEN 1024

typedef struct _decor
{
    void    (*draw) (struct _decor *d);
    XID              prop_xid;
    int              pid;
    Window           titlebar_window; /* For mouse events */
    int              client_width;
    int              client_height;
    int              decorated;
    int              active;
    uint8_t          hue;
    uint32_t         color[2][3];
    gchar            title[MAX_TITLE_STRLEN]; /* In bytes, not UTF glyphs */
    int              title_glyphs;
    char             process[PATH_MAX];
    GdkPixbuf       *icon_gdkpixbuf;
    cairo_surface_t *icon_surface;
    cairo_surface_t *surface;
    cairo_surface_t *buffer_surface;
    cairo_surface_t *p_surface[2];
    cairo_surface_t *p_buffer_surface[2];
} decor_t;

long *decor_alloc_property_data;

static Atom frame_window_atom;
static Atom select_window_atom;
static Atom win_decor_atom;
static Atom active_atom;
static Atom wm_move_resize_atom;
static Atom restack_window_atom;
static Atom wm_protocols_atom;
static Atom mwm_hints_atom;
static Atom toolkit_action_atom;
static Atom toolkit_menu_atom;

static GtkWidget *window_popup;
static GHashTable *frame_table;
static GSList *draw_list = NULL;
static guint draw_idle_id = 0;

GdkDisplay *gdkdisplay = NULL;
GdkScreen  *gdkscreen  = NULL;
Display    *xdisplay   = NULL;
Window      xroot;

uint8_t bayer[16][16];
int double_click_timeout;
cairo_surface_t *temp_surface;

PyObject *pArgs, *pFunc = NULL;
long int retValue = -2;

#define FIT(x, min, max) (x < min ? min : x > max ? max : x)
#define BYTE(x, n) (((uint8_t *)&x)[n])

static int set_decor(XID xid, int client_width, cairo_surface_t *surface, int type)
{
    decor_quad_t q[N_QUADS_MAX];

    decor_set_horz_quad_line (q+0, BORDER, 0, BORDER, 0, -(TITLE_H + BORDER), 0, GRAVITY_NORTH, BORDER * 2 + client_width, 0, 0, 0, 0);
    decor_set_horz_quad_line (q+3, BORDER, 0, BORDER, 0, 0, BORDER, GRAVITY_SOUTH, BORDER * 2 + client_width, 0, 0, 0, BORDER);
    decor_set_vert_quad_row (q+6, 0, 0, 0, 0, -BORDER, 0, GRAVITY_WEST, TITLE_H, 0, 0, 0, 0, 0);
    decor_set_vert_quad_row (q+9, 0, 0, 0, 0, 0, BORDER, GRAVITY_EAST, TITLE_H, 0, 0, 0, 0, 0);

    q[1].m.xx = 1; /* Is it BUG with decor_set_horz_quad_line() ? */

    decor_extents_t extents = {.bottom = 0, .left = 0, .right = 0, .top = TITLE_H};

    decor_quads_to_property(decor_alloc_property_data, 0, cairo_xlib_surface_get_drawable(surface), &extents, &extents, &extents, &extents, 0, 0, q, 12, 0xffffff, 0, 0);

    gdk_error_trap_push();
    XChangeProperty(xdisplay, xid, type ? win_decor_atom : active_atom, XA_INTEGER, 32, PropModeReplace, (guchar *) decor_alloc_property_data, PROP_HEADER_SIZE + BASE_PROP_SIZE + QUAD_PROP_SIZE * N_QUADS_MAX);
    return gdk_error_trap_pop();
}

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

    if (invert)
        l = 255 - l;

    if (hicontrast)
    {
        s = s / 4;
        l = (uint8_t)FIT(l * 4 - 312, 0, 255);
    }

    /* Equibright curve is hue->brightness func for fully saturate. */
    /* This should be tested on various todays display monitors as there is high variation of real perceptual brightness; then rewrite this with beziers (as they are perfectly integer/linear). */
    int q = (MAX(50-MIN(h,255-h),0) + MAX(30-abs(h-85),0) + MAX(110-abs(h-170),0) - 20); /* Range is: -20 (yellow) to 90 (blue) */

    /* Decompensate the curve for not full saturation. */
    int p = q * s * MIN(l, 255 - l) * ebcomp >> 18;

    l = MIN(255, (l + p)); /* Hope we never overflow uint8 here? */
    s = MAX(  0, (s - p));

    uint16_t A = (s * MIN(l, 255 - l) * 259) >> 8;

    #define f(N) (uint8_t) ((l * (255*255) - A * MAX(MIN(MIN(k(N, h) - 3*255, 9*255 - k(N, h)), 255), -255)) / (255*255))

    uint32_t result = (((((a << 8) + f(4)) << 8) + f(8)) << 8) + f(0);

    DBV("%x %x %x -> %d %x", h, s, l, p, result);

    return result;
}

static void draw_window_decoration(decor_t * d)
{
    #define cairo_set_RGBA(cr, c) cairo_set_source_rgba(cr, BYTE(c, 0) / 255.0, BYTE(c, 1) / 255.0, BYTE(c, 2) / 255.0, BYTE(c, 3) / 255.0)
    #define cairo_show_textXY(dx, dy, s) { cairo_move_to(cr, TITLE_H + BORDER + TITLE_H / 4 + (dx) * scale, TITLE_H + BORDER + (-4 + (dy)) * scale); cairo_show_text(cr, s); }

    DBG("draw_window_decoration()");
    int i;
    d->surface = d->p_surface[d->active];
    d->buffer_surface = d->p_buffer_surface[d->active];

    if (! d->surface)
        return;

    cairo_t *cr = cairo_create(d->buffer_surface != NULL ? d->buffer_surface : d->surface);
    if (! cr)
        return;

    cairo_set_RGBA(cr, d->color[d->active][1]);
    for (i = 0; i < BORDER; i++)
    {
        cairo_rectangle(cr, i, i, d->client_width+(BORDER-i)*2, TITLE_H+(BORDER-i)*2);
        cairo_stroke(cr);
    }

    int grad_x = MAX(TITLE_H + BORDER, d->client_width - GRAD_W);
    int grad_w = MIN(GRAD_W, d->client_width - BORDER - TITLE_H);

    if ((d->active) && (grad_w > 0))
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
                gradient_pixels[x + grad_w * y] =
                            color[x / scale > bayer[x / scale % 16][y / scale % 16]];

        cairo_set_source_surface(cr, gradient, BORDER + grad_x, BORDER);
        cairo_paint(cr);
        cairo_surface_destroy(gradient);
    }

    cairo_set_RGBA(cr, d->color[d->active][0]);
    cairo_rectangle(cr, BORDER, BORDER, d->active ? grad_x : d->client_width, TITLE_H);
    cairo_fill(cr);

    cairo_select_font_face(cr, BDF_PCF_FONT, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, TITLE_H);

    if ((d->active) && ((d->title_glyphs * (TITLE_H/2) + TITLE_H + BORDER) > grad_x))
    {
        cairo_set_RGBA(cr, d->color[d->active][0]);
        for (i = 0; i < 9; i++)
            if (i != 4)
                cairo_show_textXY(-1 + i%3, -1 + i/3, d->title);
    }

    cairo_set_RGBA(cr, d->color[d->active][2]);
    cairo_show_textXY(0, 0, d->title);

    if (d->icon_surface)
    {
        DBV(" Draw Icon");
        cairo_set_source_surface(cr, d->icon_surface, BORDER, BORDER);
        cairo_paint(cr);
    }

    cairo_destroy(cr);

    cr = cairo_create(d->surface);
    cairo_set_source_surface(cr, d->buffer_surface, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);

    set_decor(d->prop_xid, d->client_width, d->surface, 1);

    XSync(xdisplay, FALSE);
}

static int draw_decor_list(void *data)
{
    draw_idle_id = 0;

    for (GSList *list = draw_list; list; list = list->next)
    {
        decor_t *d = (decor_t *) list->data;
        (*d->draw) (d);
    }

    g_slist_free(draw_list);
    draw_list = NULL;
    return 0;
}

static void queue_decor_draw(decor_t * d)
{
    draw_list = g_slist_append(draw_list, d);

    if (!draw_idle_id)
        draw_idle_id = g_idle_add(draw_decor_list, NULL);
}

static int destroy_surface(gpointer data)
{
    DBG("destroy_surface()");
    cairo_surface_t *surface = (cairo_surface_t *) data;

    if (surface)
        cairo_surface_destroy(surface);

    return 0;
}

static cairo_surface_t *create_surface(int w, int h, int type)
{
    DBG("create_surface() %d %d", w, h);
    cairo_surface_t *surface;

    if (w <= 0 || h <= 0)
        return NULL;

    if (type)
    {
        DBG(" XLib");
#ifdef GTK3
        surface = gdk_window_create_similar_surface(WIN_POPUP, CAIRO_CONTENT_COLOR_ALPHA, w, h);
#else
        surface = gdk_window_create_similar_surface(WIN_POPUP, CAIRO_CONTENT_COLOR_ALPHA, w, h + 1); /* BUG One extra pixel for GDK2 only, looks like critical bug. */
#endif
    }
    else
    {
        DBG(" Image");
#ifdef GTK3
        surface = gdk_window_create_similar_image_surface(WIN_POPUP, CAIRO_FORMAT_ARGB32, w, h, 0);
#else
        surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
#endif
    }

    if ((surface) && (cairo_surface_get_reference_count(surface) > 0) && (cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS))
        return surface;

    destroy_surface(surface);
    return NULL;
}

#define Dd decor_t *d = g_object_get_data(G_OBJECT(win), "decor")
#define DESTROY(s) { if (d->s != NULL) { cairo_surface_destroy(d->s); } d->s = NULL; }

static void update_window_decoration_icon(WnckWindow * win)
{
    DBG("update_window_decoration_icon()");
    Dd; // NOTE Should be DR ?

    DESTROY(icon_surface);

    if (d->icon_gdkpixbuf)
        g_object_unref(G_OBJECT(d->icon_gdkpixbuf));

    d->icon_gdkpixbuf = (TITLE_H < 32) ? wnck_window_get_mini_icon(win) : wnck_window_get_icon(win);

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

static void update_decoration_size_or_title(WnckWindow * win)
{
    #define DECOR_SIZE (w + BORDER * 2), (TITLE_H + BORDER * 2)
    Dd; // NOTE Should be DR ?
    gint w, h;
    int repaint = 0;
    int i;

    wnck_window_get_client_window_geometry(win, NULL, NULL, &w, &h);

    /* BUG For xterm, wrong width (at least) returned, less 2 px, both WNCK1 & 3. */
    // if (strstr(d->process, "xterm"))
    //     w = w + 2;

    w = MAX(w, 1);
    h = MAX(h, 0);

    DBG("update_decoration_size_or_title() %d %d -> %d %d", d->client_width, d->client_height, w, h);

    if ((w != d->client_width) || (h != d->client_height))
    {
        cairo_surface_t *surface[2] = {NULL, NULL}, *buffer_surface[2] = {NULL, NULL};

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

        /* wait until old surfaces are not used for sure, one second should be enough */
        if (d->p_surface[0] != NULL)
            g_timeout_add_seconds(1, destroy_surface, d->p_surface[0]);
        if (d->p_surface[1] != NULL)
            g_timeout_add_seconds(1, destroy_surface, d->p_surface[1]);

        DESTROY(p_buffer_surface[0]);
        DESTROY(p_buffer_surface[1]);

        d->surface             = surface[d->active];
        d->buffer_surface      = buffer_surface[d->active];
        d->p_surface[0]        = surface[0];
        d->p_buffer_surface[0] = buffer_surface[0];
        d->p_surface[1]        = surface[1];
        d->p_buffer_surface[1] = buffer_surface[1];

        d->hue = d->prop_xid % 255 + 1;

        for (i = 0; i < 6; i++)
            d->color[i%2][i/2] = ahsl2abgr(255, d->hue, BYTE(satur, i), BYTE(luma, i));

        repaint = 1;
    }

    const char* title = wnck_window_get_name(win);

    d->title_glyphs = MAX(((d->client_width - TITLE_H - BORDER) / (TITLE_H/2) - 1), 0);

    if (utf)
    {
        /* UTF slows down operation. It should be avoided at hi-rel apps. */
        i = 0;
        int glyphs = 0;
        while ((title[i]) && (i < (MAX_TITLE_STRLEN - 1)))
        {
            if ((title[i] & 0xC0) != 0x80)
                if (++glyphs > d->title_glyphs)
                    break;

            if (d->title[i] != title[i])
            {
                d->title[i] = title[i];
                repaint = 1;
            }
            i++;
        }
        d->title_glyphs = glyphs;
        repaint = repaint || (d->title[i] != '\0');
    }
    else
    {
        i = MIN(MAX_TITLE_STRLEN, d->title_glyphs);
        repaint = strncmp(d->title, title, i);
        strncpy(d->title, title, i);
    }
    d->title[i] = '\0';

    gdk_error_trap_push();
    XMapWindow(xdisplay, d->titlebar_window);
    XMoveResizeWindow(xdisplay, d->titlebar_window, BORDER, BORDER, d->client_width, TITLE_H);
    XSync(xdisplay, FALSE);
    i = gdk_error_trap_pop();

    if (repaint)
        queue_decor_draw(d);
}

/*  0: remove, 1: add, 2: if can't add, then remove  */
static void add_remove_frame_window(WnckWindow *win, int what)
{
    DBG("add_remove_frame_window() %d", what);

    Atom type = None;
    int format;
    gulong nitems, bytes_after;
    Window *w, frame = 0;
    int err, result;

    gdk_error_trap_push();

    result = XGetWindowProperty(xdisplay, wnck_window_get_xid(win), what ? frame_window_atom : select_window_atom, 0, G_MAXLONG, 0, XA_WINDOW, &type, &format, &nitems, &bytes_after, (void *)&w);

    err = gdk_error_trap_pop();

    if ((err == Success) && (result == Success))
    {
        if (type == XA_WINDOW)
            frame = *w;

        XFree(w);
    }

    if ((! frame) && (what == 2))
        what = 0;

    if (! what)
    {
        if (frame)
            return;

        DBG(" Remove frame window.");
        Dd;

        DESTROY(p_surface[0]);
        DESTROY(p_buffer_surface[0]);
        DESTROY(p_surface[1]);
        DESTROY(p_buffer_surface[1]);
        DESTROY(icon_surface);

        if (d->icon_gdkpixbuf)
        {
            g_object_unref(G_OBJECT (d->icon_gdkpixbuf));
            d->icon_gdkpixbuf = NULL;
        }

        d->decorated = 0;
        draw_list = g_slist_remove(draw_list, d);
    }
    else
    {
        if (! frame)
            return;

        DBG(" Add frame window.");
        Dd;
        d->active = wnck_window_is_active(win);

        XSetWindowAttributes attr;
        memset(&attr, 0, sizeof(XSetWindowAttributes));

        attr.event_mask = ButtonPressMask;
        attr.override_redirect = 1;

        gdk_error_trap_push();

        d->titlebar_window = XCreateWindow(xdisplay, frame, 0, 0, 1, 1, 0, CopyFromParent, CopyFromParent, CopyFromParent, CWOverrideRedirect | CWEventMask, &attr);

        XSync(xdisplay, FALSE);
        if (! gdk_error_trap_pop())
        {
            d->pid = wnck_window_get_pid (win);
            if (! d->pid)
                d->pid = wnck_application_get_pid(wnck_window_get_application(win));

            sprintf(d->process, "/proc/%d/cmdline", d->pid);  /* Reuse memory */

            int n = 0;
            char c;
            FILE *fp = fopen(d->process, "r");
            if (fp) {
                while (((c = fgetc(fp)) != EOF) && (n < (PATH_MAX - 1)))
                    d->process[n++] = (c == '\0' ? ' ' : c);
                d->process[n] = 0;
                fclose(fp);
            }
            if (n == 0)
                strcpy(d->process, "(unknown)");

            d->prop_xid = wnck_window_get_xid(win);

            g_hash_table_insert(frame_table, GUINT_TO_POINTER(d->titlebar_window), GUINT_TO_POINTER(d->prop_xid));

            d->decorated = 1;

            update_window_decoration_icon(win);
            update_decoration_size_or_title(win);
        }
        else
            memset((void *)d->titlebar_window, 0, sizeof(d->titlebar_window));
    }
}

#define DR  Dd; if (! d->decorated) return;

static void cb_name_changed(WnckWindow * win)
{
    DBG("cb_name_changed()");
    DR;
    update_decoration_size_or_title(win);
}

static void cb_geometry_changed(WnckWindow * win)
{
    DBV("cb_geometry_changed()");
    DR;
    update_decoration_size_or_title(win);
}

static void cb_icon_changed(WnckWindow * win)
{
    DR;
    update_window_decoration_icon(win);
    queue_decor_draw(d);
}

static void cb_state_changed(WnckWindow * win)
{
    DBG("cb_state_changed()");
    DR;
    update_decoration_size_or_title(win);
}

static void cb_active_window_changed(WnckScreen *screen)
{
    #define SET_ACTIVE(w) win = w; if (win) { Dd; if (d && d->surface && d->decorated) { d->active = wnck_window_is_active(win); queue_decor_draw(d); } }

    WnckWindow *win;
    SET_ACTIVE (wnck_screen_get_previously_active_window(screen));
    SET_ACTIVE (wnck_screen_get_active_window(screen));
}

static void cb_window_opened(WnckScreen *screen, WnckWindow *win)
{
    DBG("cb_window_opened()");

    decor_t *d = g_malloc0(sizeof(decor_t));
    if (!d)
        return;

    d->draw = draw_window_decoration;

    g_object_set_data(G_OBJECT(win), "decor", d);

    g_signal_connect_object(win, "name_changed",
                    G_CALLBACK(cb_name_changed),     0, 0);
    g_signal_connect_object(win, "geometry_changed",
                    G_CALLBACK(cb_geometry_changed), 0, 0);
    g_signal_connect_object(win, "icon_changed",
                    G_CALLBACK(cb_icon_changed),     0, 0);
    g_signal_connect_object(win, "state_changed",
                    G_CALLBACK(cb_state_changed),    0, 0);

    add_remove_frame_window(win, 1);
}

static int cb_window_closed(WnckScreen * screen, WnckWindow *win)
{
    DBG("cb_window_closed()");
    Dd;
    gdk_error_trap_push();
    XDeleteProperty(xdisplay, wnck_window_get_xid(win), win_decor_atom);
    XSync(xdisplay, FALSE);

    add_remove_frame_window(win, 0);

    g_object_set_data(G_OBJECT(win), "decor", NULL);
    g_free(d);
    return gdk_error_trap_pop();
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
        XUngrabPointer (xdisplay, (Time) time);
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
    if ((retValue == -3) || (! win))
        return;

    gint x, y;

#ifdef GTK3
    GdkWindow* window = WIN_POPUP;
    gdk_window_get_device_position (window, gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display_get_default ())), &x, &y, NULL);
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
            Dd;
            char str[MAX_TITLE_STRLEN + PATH_MAX + 16];
            sprintf(str, "%s\n%s\npid: %d\n", d->title, d->process, d->pid);
            GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
            gtk_clipboard_set_text(clipboard, str, -1);
            break;
        case 6:
            wnck_window_close(win, (guint32) xevent->xbutton.time);
            break;
        default:
            break;
    }
}

typedef void (*event_callback) (WnckWindow *win, XEvent *xevent, GdkXEvent *gdkxevent);
static void title_event        (WnckWindow *win, XEvent *xevent, GdkXEvent *gdkxevent)
{
    #define WNCK_MAX_UNMAX if (wnck_window_is_maximized(win)) wnck_window_unmaximize(win); else wnck_window_maximize(win)
    static unsigned int last_button_num = 0;
    static Window last_button_xwindow = None;
    static Time last_button_time = 0;

    if (xevent->type != ButtonPress)
        return;

    if (xevent->xbutton.x <= (BORDER + TITLE_H))  /* Window Icon */
        action_menu_map(win, xevent);
    else if (xevent->xbutton.button == 1)
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
            move_resize_restack_window(win, wm_move_resize_atom, xevent->xbutton.x_root, xevent->xbutton.y_root, 8, xevent->xbutton.button, 1, xevent->xbutton.time); // 8 is (_NET_)WM_MOVERESIZE_MOVE
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
                xid = xevent->xany.window;

                WnckWindow *win = wnck_window_get(xid);
                if (win)
                    add_remove_frame_window(win, 2);
            }
            break;
        case DestroyNotify:
            g_hash_table_remove(frame_table, GUINT_TO_POINTER(xevent->xany.window));
            break;
        case ClientMessage:  /* Alt+Space */
            if ((xevent->xclient.message_type == toolkit_action_atom) &&
                (xevent->xclient.data.l[0] == toolkit_menu_atom))
                    action_menu_map(wnck_window_get(xevent->xany.window), xevent);
            break;
        default:
            break;
    }

    if (xid != None)
    {
        WnckWindow *win = wnck_window_get(xid);
        if (win)
        {
            static event_callback callback = title_event;
            Dd;

            if ((d->decorated) && (d->titlebar_window == xevent->xany.window))
                (*callback) (win, xevent, gdkxevent);
        }
    }

    return GDK_FILTER_CONTINUE;
}

static void signal_handler(int sig)
{
    WRN("Signal received. Will quit.");
    gtk_main_quit ();
}

static void cleanup()
{
    MSG("Cleanup.");
    free(decor_alloc_property_data);
    cairo_surface_destroy(temp_surface);

#ifdef GTK3
    g_clear_object (&wnck_handle);
#endif
    int i = gdkdisplay ? gdk_error_trap_pop() : 0;

    if (pFunc)
    {
        Py_XDECREF(pFunc);
        PyGC_Collect();
// Py_Finalize(); // BUG Segfault like https://stackoverflow.com/questions/67533541
    }
    DBG("Cleanup done, should exit now. %d", i);
}


int main(int argc, char *argv[])
{
    program_name = argv[0];
    int opt;

    while ((opt = getopt_long_only(argc, argv, shortopts, longopts, NULL)) != -1)
        switch (opt)
        {
            case 'v': MSG("%s, version %s", PACKAGE, VERSION); return -1;
            case 'l':       luma = strtoull(optarg, NULL, 16); break;
            case 's':      satur = strtoull(optarg, NULL, 16); break;
            case 'e':     ebcomp = MIN(strtoul(optarg, NULL, 10), 8); break;
            case 'm':    verbose = MIN(strtoul(optarg, NULL, 10), 4); break;
            case 'r':    replace = 1; break;
            case 'c': hicontrast = 1; break;
            case 'i':     invert = 1; break;
            case 'd':      scale = 2; break;
            case 'h':
                usage(argv[0]);
            default:
                return -1;
        }

    utf = 0;
    if(setlocale(LC_ALL, ""))
    {
        char *locale = setlocale(LC_MESSAGES, NULL);
        if (locale && (strstr(locale, "UTF") || strstr(locale, "utf")))
            utf = 1;
        else
            WRN("LC_MESSAGES set to single-byte: '%s'", locale);
    }

    atexit(cleanup);

    Py_Initialize();

    const char PyMenuSrc[] =
        "from PyQt6.QtCore import QPoint\n"
        "from PyQt6.QtWidgets import QApplication, QMenu\n"
        "from PyQt6.QtGui import QIcon\n"
        "app = QApplication([])\n"
        "def windowActionsMenu(x, y):\n"
        "  q = 7\n"
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
        ERR("Cannot find Python function windowActionsMenu.");
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

    select_window_atom  = XInternAtom(xdisplay, "_COMPIZ_SWITCH_SELECT_WINDOW", FALSE);
    toolkit_action_atom = XInternAtom(xdisplay, "_COMPIZ_TOOLKIT_ACTION", FALSE);
    toolkit_menu_atom   = XInternAtom(xdisplay, "_COMPIZ_TOOLKIT_ACTION_WINDOW_MENU", FALSE);
    win_decor_atom      = XInternAtom(xdisplay, "_COMPIZ_WINDOW_DECOR", FALSE);
    active_atom         = XInternAtom(xdisplay, "_COMPIZ_WINDOW_DECOR_ACTIVE", FALSE);
    mwm_hints_atom      = XInternAtom(xdisplay, "_MOTIF_WM_HINTS", FALSE);
    frame_window_atom   = XInternAtom(xdisplay, "_NET_FRAME_WINDOW", FALSE);
    restack_window_atom = XInternAtom(xdisplay, "_NET_RESTACK_WINDOW", FALSE);
    wm_move_resize_atom = XInternAtom(xdisplay, "_NET_WM_MOVERESIZE", FALSE);
    wm_protocols_atom   = XInternAtom(xdisplay, "WM_PROTOCOLS", FALSE);

    Time dm_sn_timestamp;
    int status = decor_acquire_dm_session(xdisplay, DefaultScreen(xdisplay), PACKAGE, replace, &dm_sn_timestamp);

    if (status == DECOR_ACQUIRE_STATUS_FAILED)
        ERR("Could not acquire decoration manager selection on screen %d Display '%s'", DefaultScreen(xdisplay), DisplayString(xdisplay));

    if (status == DECOR_ACQUIRE_STATUS_OTHER_DM_RUNNING)
        ERR("Screen %d on Display '%s' already has a decoration manager; try using the --replace option to replace the current decoration manager.", DefaultScreen(xdisplay), DisplayString(xdisplay));

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
                                      G_CALLBACK(cb_active_window_changed), 0, 0);
    g_signal_connect_object(G_OBJECT(wnck_screen), "window_opened",
                                      G_CALLBACK(cb_window_opened),         0, 0);
    g_signal_connect_object(G_OBJECT(wnck_screen), "window_closed",
                                      G_CALLBACK(cb_window_closed),         0, 0);

    window_popup = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_widget_realize(window_popup);
    g_object_get(G_OBJECT(gtk_settings_get_for_screen(gtk_widget_get_screen(window_popup))), "gtk-double-click-time", &double_click_timeout, NULL);

    decor_set_dm_check_hint(xdisplay, DefaultScreen(xdisplay), WINDOW_DECORATION_TYPE_PIXMAP);

    Atom bareAtom = XInternAtom(xdisplay, DECOR_BARE_ATOM_NAME, FALSE);
    XDeleteProperty(xdisplay, xroot, bareAtom);

    temp_surface = create_surface(1, 1, 1);

    if (! temp_surface)
        ERR("Cairo can't create surfaces.");

    decor_alloc_property_data = decor_alloc_property(1, WINDOW_DECORATION_TYPE_PIXMAP);

    set_decor(xroot, 1, temp_surface, 0);

    GList *windows = wnck_screen_get_windows(wnck_screen);
    while (windows)
    {
        cb_window_opened(wnck_screen, windows->data);
        windows = windows->next;
    }

    // g_timeout_add(500, reload_if_needed, NULL);

    gtk_main();

    DBG("Here atexit() starts cleanup().");
    return (0);
}
