// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fl_renderer_x11.h"
#include "flutter/shell/platform/linux/egl_utils.h"

struct _FlRendererX11 {
  FlRenderer parent_instance;

  GdkX11Window* window;
};

G_DEFINE_TYPE(FlRendererX11, fl_renderer_x11, fl_renderer_get_type())

static void fl_renderer_x11_dispose(GObject* object) {
  FlRendererX11* self = FL_RENDERER_X11(object);

  g_clear_object(&self->window);

  G_OBJECT_CLASS(fl_renderer_x11_parent_class)->dispose(object);
}

// Implements FlRenderer::setup_window_attr.
static gboolean fl_renderer_x11_setup_window_attr(
    FlRenderer* renderer,
    GtkWidget* widget,
    EGLDisplay egl_display,
    EGLConfig egl_config,
    GdkWindowAttr* window_attributes,
    gint* mask,
    GError** error) {
  EGLint visual_id;
  if (!eglGetConfigAttrib(egl_display, egl_config, EGL_NATIVE_VISUAL_ID,
                          &visual_id)) {
    g_set_error(error, fl_renderer_error_quark(), FL_RENDERER_ERROR_FAILED,
                "Failed to determine EGL configuration visual");
    return FALSE;
  }

  GdkX11Screen* screen = GDK_X11_SCREEN(gtk_widget_get_screen(widget));
  if (!screen) {
    g_set_error(error, fl_renderer_error_quark(), FL_RENDERER_ERROR_FAILED,
                "Widget is not on an X11 screen");
    return FALSE;
  }

  window_attributes->visual = gdk_x11_screen_lookup_visual(screen, visual_id);
  if (window_attributes->visual == nullptr) {
    g_set_error(error, fl_renderer_error_quark(), FL_RENDERER_ERROR_FAILED,
                "Failed to find visual 0x%x", visual_id);
    return FALSE;
  }

  *mask |= GDK_WA_VISUAL;

  return TRUE;
}

// Implements FlRenderer::set_window.
static void fl_renderer_x11_set_window(FlRenderer* renderer,
                                       GdkWindow* window) {
  FlRendererX11* self = FL_RENDERER_X11(renderer);
  g_return_if_fail(GDK_IS_X11_WINDOW(window));
  g_assert_null(self->window);
  self->window = GDK_X11_WINDOW(g_object_ref(window));
}

// Implements FlRenderer::create_display.
static EGLDisplay fl_renderer_x11_create_display(FlRenderer* renderer) {
  // Note the use of EGL_DEFAULT_DISPLAY rather than sharing the existing
  // display connection from GTK. This is because this EGL display is going to
  // be accessed by a thread from Flutter. The GTK/X11 display connection is not
  // thread safe and would cause a crash.
  return eglGetDisplay(EGL_DEFAULT_DISPLAY);
}

// Implements FlRenderer::create_surfaces.
static gboolean fl_renderer_x11_create_surfaces(FlRenderer* renderer,
                                                EGLDisplay display,
                                                EGLConfig config,
                                                EGLSurface* visible,
                                                EGLSurface* resource,
                                                GError** error) {
  FlRendererX11* self = FL_RENDERER_X11(renderer);

  if (!self->window) {
    g_set_error(error, fl_renderer_error_quark(), FL_RENDERER_ERROR_FAILED,
                "Can not create EGL surface: FlRendererX11::window not set");
    return FALSE;
  }

  *visible = eglCreateWindowSurface(
      display, config, gdk_x11_window_get_xid(self->window), nullptr);
  if (*visible == EGL_NO_SURFACE) {
    EGLint egl_error = eglGetError();  // Must be before egl_config_to_string().
    g_autofree gchar* config_string = egl_config_to_string(display, config);
    g_set_error(error, fl_renderer_error_quark(), FL_RENDERER_ERROR_FAILED,
                "Failed to create EGL surface using configuration (%s): %s",
                config_string, egl_error_to_string(egl_error));
    return FALSE;
  }

  const EGLint attribs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
  *resource = eglCreatePbufferSurface(display, config, attribs);
  if (*resource == EGL_NO_SURFACE) {
    EGLint egl_error = eglGetError();  // Must be before egl_config_to_string().
    g_autofree gchar* config_string = egl_config_to_string(display, config);
    g_set_error(error, fl_renderer_error_quark(), FL_RENDERER_ERROR_FAILED,
                "Failed to create EGL resource using configuration (%s): %s",
                config_string, egl_error_to_string(egl_error));
    return FALSE;
  }

  return TRUE;
}

static void fl_renderer_x11_class_init(FlRendererX11Class* klass) {
  G_OBJECT_CLASS(klass)->dispose = fl_renderer_x11_dispose;
  FL_RENDERER_CLASS(klass)->setup_window_attr =
      fl_renderer_x11_setup_window_attr;
  FL_RENDERER_CLASS(klass)->set_window = fl_renderer_x11_set_window;
  FL_RENDERER_CLASS(klass)->create_display = fl_renderer_x11_create_display;
  FL_RENDERER_CLASS(klass)->create_surfaces = fl_renderer_x11_create_surfaces;
}

static void fl_renderer_x11_init(FlRendererX11* self) {}

FlRendererX11* fl_renderer_x11_new() {
  return FL_RENDERER_X11(g_object_new(fl_renderer_x11_get_type(), nullptr));
}
