/**
 * SC Controller - OSD Common - OSD Window Base Class
 *
 * Implements OSDWindow, a GTK base class for on-screen display windows.
 * Provides:
 * - Platform-specific window setup (X11 on Linux, Win32 on Windows)
 * - Click-through capability (mouse events pass through to background)
 * - Custom input event routing to application handlers
 * - Window geometry management and positioning
 * - Proper window stacking and z-order control
 *
 * Platform-specific features:
 * - Linux/X11: Uses XShape and Xfixes extensions for input masking
 * - Windows: Uses Win32 API for transparent click-through windows
 *
 * Extended by OSDKeyboard and OSDMenu for specialized behavior.
 * Keeps windows responsive and prevents interference with user input to underlying apps.
 */
#define LOG_TAG "OSD"
#include "scc/utils/logging.h"
#include "scc/utils/assert.h"
#include "scc/osd/osd_window.h"
#include "../osd.h"
#ifdef _WIN32
	#include <windows.h>
	#include <gdk/gdkwin32.h>
#else
	#include <X11/extensions/Xfixes.h>
	#include <X11/extensions/shape.h>
	#include <X11/Xutil.h>
	#include <gdk/gdkx.h>
#endif
#include <gtk/gtk.h>
#include <stdbool.h>


static void (*gtk_window_show)(GtkWidget* w);
static void osd_window_show(GtkWidget* w);

typedef struct _OSDWindowPrivate		OSDWindowPrivate;

struct _OSDWindow {
	GtkWindow				parent;
	OSDWindowPrivate*		priv;
};

struct _OSDWindowClass {
	GtkWindowClass			parent_class;
};

struct _OSDWindowPrivate {
	struct {
		int						x;
		int						y;
	}						position;
	OSDWindowCallbacks		callbacks;
};

G_DEFINE_TYPE_WITH_CODE(OSDWindow, osd_window, GTK_TYPE_WINDOW, G_ADD_PRIVATE(OSDWindow));

static void osd_window_class_init(OSDWindowClass *klass) {
	// GObjectClass*		g_class;
	GtkWidgetClass*		w_class;
	// GParamSpec*			pspec;
	
	// g_class = G_OBJECT_CLASS(klass);
	w_class = GTK_WIDGET_CLASS(klass);

	gtk_window_show = w_class->show;
	w_class->show = &osd_window_show;
	
	g_signal_new("ready", GTK_TYPE_WINDOW, G_SIGNAL_RUN_FIRST, 0, NULL, NULL,
					g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
	g_signal_new("exit", GTK_TYPE_WINDOW, G_SIGNAL_RUN_FIRST, 0, NULL, NULL,
					g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);
}

static void get_safe_window_position(GdkRectangle *monitor_rect, GdkRectangle *window_rect, int mouse_x, int mouse_y, int *safe_x, int *safe_y) {
    int width, height;
    width = window_rect->width;
	height = window_rect->height;

    // Check if window will go off the right side of the screen
    int right = monitor_rect->x + monitor_rect->width;
    if (mouse_x + width > right) {
        *safe_x = right - width;
    } 
    // Check if window will go off the left side of the screen
    else if (mouse_x < monitor_rect->x) {
        *safe_x = monitor_rect->x;
    } 
    // Otherwise, window can be positioned at the mouse x position
    else {
        *safe_x = mouse_x;
    }

    // Check if window will go off the bottom of the screen
    int bottom = monitor_rect->y + monitor_rect->height;
    if (mouse_y + height > bottom) {
        *safe_y = bottom - height;
    } 
    // Check if window will go off the top of the screen
    else if (mouse_y < monitor_rect->y) {
        *safe_y = monitor_rect->y;
    } 
    // Otherwise, window can be positioned at the mouse y position
    else {
        *safe_y = mouse_y;
    }
}

#ifndef _WIN32  
static gboolean on_map_apply_input_shape(GtkWidget* widget, GdkEvent* e, gpointer u) {  
	GdkWindow* gdk_window = gtk_widget_get_window(widget);  
	if (gdk_window == NULL) return FALSE;  
	Display* dpy = gdk_x11_get_default_xdisplay();  
	XID xid = GDK_WINDOW_XID(gdk_window);  
	XserverRegion reg = XFixesCreateRegion(dpy, NULL, 0);  
	XFixesSetWindowShapeRegion(dpy, xid, ShapeInput, 0, 0, reg);  // only ShapeInput  
	XFixesDestroyRegion(dpy, reg);  
	return FALSE;  
}  
#endif

static void osd_window_show(GtkWidget* widget) {
	GdkScreen* scr = gdk_screen_get_default();

	GdkDisplay *display = gdk_display_get_default();
	GdkSeat *seat = gdk_display_get_default_seat(display);
	GdkDevice *pointer = gdk_seat_get_pointer(seat);
	
	int xm, ym;

#ifdef _WIN32
	POINT pos;
    GetCursorPos(&pos);
	xm = pos.x;
	ym = pos.y;
#else
	gdk_device_get_position(pointer, NULL, &xm, &ym);
#endif

    GdkMonitor *monitor = gdk_display_get_monitor_at_point(display, xm, ym);
	GdkRectangle monitor_rect;
	double monitor_scale_factor = (double)gdk_monitor_get_scale_factor(monitor);
	gdk_monitor_get_geometry(monitor, &monitor_rect);

// bug: first monitor is not scaled correctly, then 
// the second+ monitor are incorrectly scaled. Reversed.
#ifdef _WIN32
	HDC hdc = GetDC(NULL);
	int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
	int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
	ReleaseDC(NULL, hdc);
	monitor_scale_factor = (dpiX / 96.0);
	g_print("Monitor scale factor: %f\n", monitor_scale_factor);
#endif
	monitor_rect.x /= monitor_scale_factor;
	monitor_rect.y /= monitor_scale_factor;
	monitor_rect.width /= monitor_scale_factor;
	monitor_rect.height /= monitor_scale_factor;
	// xm /= monitor_scale_factor;
	// ym /= monitor_scale_factor;
	// DDEBUG("mouse position: %d, %d", xm, ym);
	// g_print("Mouse is on monitor %d: x=%d, y=%d, width=%d, height=%d\n",
	// 		monitor_num, monitor_rect.x, monitor_rect.y, monitor_rect.width, monitor_rect.height);

	int x, y;
	gtk_widget_realize(widget);
	osd_window_compute_position(OSD_WINDOW(widget), &x, &y);
	if (x < 0)	// Negative X position is counted from right border
		x = monitor_rect.x + monitor_rect.width - gtk_widget_get_allocated_width(widget) + x + 1;
	if (y < 0)	// Negative Y position is counted from bottom border
		y = monitor_rect.y + monitor_rect.height - gtk_widget_get_allocated_height(widget) + y + 1;
#ifdef _WIN32
	GdkWindow* gdk_window = gtk_widget_get_window(GTK_WIDGET(widget));
	HWND hwnd = gdk_win32_window_get_handle(gdk_window);
	LONG style = GetWindowLong(hwnd, GWL_EXSTYLE);
	SetWindowLong(hwnd, GWL_EXSTYLE, style | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOACTIVATE);
	SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
	gtk_widget_set_opacity(widget, 0.60);
#else  
	GdkWindow* gdk_window = gtk_widget_get_window(widget);  
	ASSERT(gdk_window != NULL);  
	Display* dpy = gdk_x11_get_default_xdisplay();  
	ASSERT(dpy != NULL);  
	XID xid = GDK_WINDOW_XID(gdk_window);  
	ASSERT(xid != 0);  
  
	// Empty input region => the whole window is transparent to pointer  
	// input, so clicks fall through to the app underneath. Leave the  
	// bounding shape untouched so the window stays visible.  
	XserverRegion reg = XFixesCreateRegion(dpy, NULL, 0);  
	XFixesSetWindowShapeRegion(dpy, xid, ShapeInput, 0, 0, reg);  
	XFixesDestroyRegion(dpy, reg);  
  
	gtk_widget_set_opacity(widget, 0.60);  // optional, mirror Windows  
#endif
	GdkRectangle osd_rect = {x, y, 0, 0};
	osd_rect.width = gtk_widget_get_allocated_width(widget) * 0.3;
	osd_rect.height = gtk_widget_get_allocated_height(widget) * 0.3;
	// DDEBUG("osd size: %d, %d", osd_rect.width, osd_rect.height);

	get_safe_window_position(&monitor_rect, &osd_rect, xm, ym, &x, &y);
	x *= monitor_scale_factor;
	y *= monitor_scale_factor;
	y = y > 0 ? y : 0;
	// DDEBUG("osd position: %d, %d", x, y);

	// gtk_widget_set_size_request(widget, osd_rect.width, osd_rect.height);

	gtk_window_move(GTK_WINDOW(widget), x, y);
	// gtk_window_set_scale_factor(GTK_WINDOW(widget), monitor_scale_factor);

	gtk_window_show(widget);

}

OSDWindow* osd_window_new(const char* wmclass, const OSDWindowCallbacks callbacks) {
	GtkWidget* o = g_object_new(OSD_WINDOW_TYPE, GTK_WINDOW_TOPLEVEL, NULL);
	osd_window_setup(OSD_WINDOW(o), "osd-menu", callbacks);
	return OSD_WINDOW(o);
}

void osd_window_setup(OSDWindow* osdwin, const char* wmclass, const OSDWindowCallbacks callbacks) {
	// TODO: self.argparser = argparse.ArgumentParser(description=__doc__,
			// formatter_class=argparse.RawDescriptionHelpFormatter,
			// epilog="")
	// TODO: add_arguments()
	OSDWindowPrivate* priv = osd_window_get_instance_private(osdwin);
	priv->callbacks = callbacks;
	// self.mainloop = None
	// priv->controller = None
	
	gtk_window_set_title(GTK_WINDOW(osdwin), wmclass);
	gtk_widget_set_name(GTK_WIDGET(osdwin), wmclass);
	gtk_window_set_decorated(GTK_WINDOW(osdwin), FALSE);
	gtk_window_stick(GTK_WINDOW(osdwin));
	gtk_window_set_skip_taskbar_hint(GTK_WINDOW(osdwin), TRUE);
	gtk_window_set_skip_pager_hint(GTK_WINDOW(osdwin), TRUE);
	gtk_window_set_keep_above(GTK_WINDOW(osdwin), TRUE);
	gtk_window_set_type_hint(GTK_WINDOW(osdwin), GDK_WINDOW_TYPE_HINT_DOCK);

	#ifndef _WIN32  
	g_signal_connect(G_OBJECT(osdwin), "map-event",  
	                 G_CALLBACK(on_map_apply_input_shape), NULL);  
	#endif
}

void osd_window_exit(OSDWindow* osdwin, int code) {
	g_signal_emit_by_name(G_OBJECT(osdwin), "exit", code);
}

void osd_window_get_active_screen_geometry(OSDWindow* osdwin, GdkRectangle* geometry) {
	GdkWindow* gdkwin = gtk_widget_get_window(GTK_WIDGET(osdwin));
	GdkDisplay* display = gdk_window_get_display(gdkwin);
	GdkMonitor* m = gdk_display_get_monitor_at_window(display, gdkwin);
	gdk_monitor_get_geometry(m, geometry);
}

void osd_window_compute_position(OSDWindow* osdwin, int* x, int* y) {
	OSDWindowPrivate* priv = osd_window_get_instance_private(osdwin);
	GdkWindow* gdkwin = gtk_widget_get_window(GTK_WIDGET(osdwin));
	GdkRectangle geometry;
	
	osd_window_get_active_screen_geometry(osdwin, &geometry);
	int width  = gdk_window_get_width (gdkwin);
	int height = gdk_window_get_height(gdkwin);
	*x = priv->position.x;
	*y = priv->position.y;
	
	*x = (*x < 0) ? (*x + geometry.x + geometry.width - width) : (*x + geometry.x);
	*y = (*y < 0) ? (*y + geometry.y + geometry.height - height) : (geometry.y + *y);
	// to me 20 minutes from now: have fun decoding that.
}

void osd_window_set_position(OSDWindow* osdwin, int x, int y) {
	OSDWindowPrivate* priv = G_TYPE_INSTANCE_GET_PRIVATE(osdwin, OSD_WINDOW_TYPE, OSDWindowPrivate);
	priv->position.x = x;
	priv->position.y = y;
}

ivec_t osd_window_get_position(OSDWindow* osdwin) {
	OSDWindowPrivate* priv = G_TYPE_INSTANCE_GET_PRIVATE(osdwin, OSD_WINDOW_TYPE, OSDWindowPrivate);
	ivec_t rv = { priv->position.x, priv->position.y };
	return rv;
}

static void osd_window_init(OSDWindow* osdwin) {
	gtk_widget_set_has_window(GTK_WIDGET(osdwin), TRUE);
	install_css_provider();
	
	OSDWindowPrivate* priv = G_TYPE_INSTANCE_GET_PRIVATE(osdwin, OSD_WINDOW_TYPE, OSDWindowPrivate);
	priv->position.x = 20;
	priv->position.y = 20;
	memset(&priv->callbacks, 0, sizeof(OSDWindowCallbacks));
}

