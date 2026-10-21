// VTK 9.x embedded in GtkGLArea (GTK 3 + libhandy)
#include <gtk/gtk.h>
#include <handy.h>
#include <epoxy/gl.h>

#include <vtkActor.h>
#include <vtkCallbackCommand.h>
#include <vtkCamera.h>
#include <vtkConeSource.h>
#include <vtkProperty.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkGenericRenderWindowInteractor.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkNew.h>
#include <vtkPolyDataMapper.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>
#include <vtkVersion.h>
#include <vtkOpenGLRenderWindow.h>

// Must come after the VTK headers: it pulls in Xlib, whose macros
// (Bool, None, Status, ...) break VTK's headers.
#include <epoxy/glx.h>

struct App {
  GtkWidget* area = nullptr;
  vtkSmartPointer<vtkGenericOpenGLRenderWindow> rw;
  vtkSmartPointer<vtkGenericRenderWindowInteractor> iren;
  vtkSmartPointer<vtkRenderer> ren;
  vtkSmartPointer<vtkActor> actor;
  vtkSmartPointer<vtkCamera> home;  // initial camera, restored by "reset view"
};

// ---- application actions: called from hotkeys AND header-bar buttons --------
static void redraw(App* a) { gtk_gl_area_queue_render(GTK_GL_AREA(a->area)); }

static void act_reset_view(App* a) {
  if (!a->ren) return;
  // ResetCamera() alone only refits zoom/center and keeps the orientation,
  // so restore the saved initial camera completely (incl. projection mode).
  if (a->home) a->ren->GetActiveCamera()->DeepCopy(a->home);
  a->ren->ResetCamera();
  redraw(a);
}
static void act_toggle_projection(App* a) {
  if (!a->ren) return;
  vtkCamera* cam = a->ren->GetActiveCamera();
  cam->SetParallelProjection(!cam->GetParallelProjection());
  a->ren->ResetCameraClippingRange();
  redraw(a);
}
static void act_toggle_wireframe(App* a) {
  if (!a->actor) return;
  vtkProperty* p = a->actor->GetProperty();
  if (p->GetRepresentation() == VTK_WIREFRAME) p->SetRepresentationToSurface();
  else p->SetRepresentationToWireframe();
  redraw(a);
}


#if VTK_MAJOR_VERSION > 9 || (VTK_MAJOR_VERSION == 9 && VTK_MINOR_VERSION >= 2)
#define VTK_HAS_GLAD 1
// VTK >= 9.2 (glad): resolve GL entry points through libepoxy (GLX and EGL).
static vtkOpenGLRenderWindow::VTKOpenGLAPIProc gl_loader(void*, const char* name) {
    return reinterpret_cast<vtkOpenGLRenderWindow::VTKOpenGLAPIProc>(
        epoxy_glXGetProcAddress((const GLubyte*) name));
}
#endif

static void to_vtk(App* a, double x, double y, int& ox, int& oy) {
  int s = gtk_widget_get_scale_factor(a->area);
  int h = gtk_widget_get_allocated_height(a->area);
  ox = int(x * s);
  oy = int((h - y) * s);  // VTK origin is bottom-left
}

static void on_realize(GtkGLArea* area, App* a) {
  gtk_gl_area_make_current(area);
  if (gtk_gl_area_get_error(area)) {
    g_printerr("GtkGLArea error: %s\n", gtk_gl_area_get_error(area)->message);
    return;
  }
  // Distro VTK is GLX-only: refuse an EGL context instead of crashing.
  if (glXGetCurrentContext() == nullptr) {
    g_printerr("VTK needs a GLX context: run under X11 (GDK_BACKEND=x11).\n");
    return;
  }

  a->rw = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
  a->rw->SetMultiSamples(0);
  a->rw->SetFrameBlitModeToBlitToCurrent();  // blit VTK's FBO into GTK's FBO
  a->rw->SetOwnContext(0);                   // the context belongs to GTK
  a->rw->SetMapped(1);
  a->rw->SetIsCurrent(true);
#ifdef VTK_HAS_GLAD
  // OpenGLInit() calls state->Reset() before glad loads anything: load up front.
  a->rw->SetOpenGLSymbolLoader(gl_loader, nullptr);
  a->rw->OpenGLInitContext();
#endif
  a->rw->InitializeFromCurrentContext();

  vtkNew<vtkConeSource> cone;
  vtkNew<vtkPolyDataMapper> mapper;
  mapper->SetInputConnection(cone->GetOutputPort());
  auto actor = vtkSmartPointer<vtkActor>::New();
  a->actor = actor;
  actor->SetMapper(mapper);
  auto ren = vtkSmartPointer<vtkRenderer>::New();
  a->ren = ren;
  ren->AddActor(actor);
  ren->SetBackground(0.12, 0.12, 0.14);
  ren->SetBackgroundAlpha(1.0);
  a->rw->AddRenderer(ren);
  ren->ResetCamera();
  a->home = vtkSmartPointer<vtkCamera>::New();
  a->home->DeepCopy(ren->GetActiveCamera());

  a->iren = vtkSmartPointer<vtkGenericRenderWindowInteractor>::New();
  a->iren->SetRenderWindow(a->rw);
  a->iren->EnableRenderOff();  // Render() only emits RenderEvent
  vtkNew<vtkInteractorStyleTrackballCamera> style;
  a->iren->SetInteractorStyle(style);

  vtkNew<vtkCallbackCommand> cb;
  cb->SetClientData(a->area);
  cb->SetCallback([](vtkObject*, unsigned long, void* cd, void*) {
    gtk_gl_area_queue_render(GTK_GL_AREA(cd));
  });
  a->iren->AddObserver(vtkCommand::RenderEvent, cb);
  a->iren->Initialize();
}

static void on_unrealize(GtkGLArea* area, App* a) {
  gtk_gl_area_make_current(area);
  a->iren = nullptr;
  a->rw = nullptr;
  a->ren = nullptr;
  a->actor = nullptr;
  a->home = nullptr;
}

static gboolean on_render(GtkGLArea* area, GdkGLContext*, App* a) {
  if (!a->rw || !a->iren) return FALSE;
  int s = gtk_widget_get_scale_factor(GTK_WIDGET(area));
  int w = gtk_widget_get_allocated_width(GTK_WIDGET(area)) * s;
  int h = gtk_widget_get_allocated_height(GTK_WIDGET(area)) * s;
  a->rw->SetSize(w, h);
  a->iren->SetSize(w, h);
  a->rw->SetIsCurrent(true);
  a->rw->Render();
  return TRUE;
}

// ---- input: GTK3 delivers everything as widget events ----------------------
static gboolean on_motion(GtkWidget*, GdkEventMotion* e, App* a) {
  if (!a->iren) return FALSE;
  int ix, iy; to_vtk(a, e->x, e->y, ix, iy);
  a->iren->SetEventInformation(ix, iy);
  a->iren->InvokeEvent(vtkCommand::MouseMoveEvent);
  return TRUE;
}

static gboolean button(GdkEventButton* e, App* a, bool press) {
  if (!a->iren) return FALSE;
  if (press) gtk_widget_grab_focus(a->area);
  int ix, iy; to_vtk(a, e->x, e->y, ix, iy);
  a->iren->SetEventInformation(ix, iy);
  unsigned long ev = 0;
  switch (e->button) {
    case 1: ev = press ? vtkCommand::LeftButtonPressEvent   : vtkCommand::LeftButtonReleaseEvent; break;
    case 2: ev = press ? vtkCommand::MiddleButtonPressEvent : vtkCommand::MiddleButtonReleaseEvent; break;
    case 3: ev = press ? vtkCommand::RightButtonPressEvent  : vtkCommand::RightButtonReleaseEvent; break;
  }
  if (ev) a->iren->InvokeEvent(ev);
  return TRUE;
}
static gboolean on_press(GtkWidget*, GdkEventButton* e, App* a) { return button(e, a, true); }
static gboolean on_release(GtkWidget*, GdkEventButton* e, App* a) { return button(e, a, false); }

static gboolean on_scroll(GtkWidget*, GdkEventScroll* e, App* a) {
  if (!a->iren) return FALSE;
  double dy = 0;
  if (e->direction == GDK_SCROLL_UP) dy = -1;
  else if (e->direction == GDK_SCROLL_DOWN) dy = 1;
  else if (e->direction == GDK_SCROLL_SMOOTH) dy = e->delta_y;
  if (dy == 0) return FALSE;
  a->iren->InvokeEvent(dy < 0 ? vtkCommand::MouseWheelForwardEvent
                              : vtkCommand::MouseWheelBackwardEvent);
  return TRUE;
}

static gboolean on_key(GtkWidget*, GdkEventKey* e, App* a) {
  switch (e->keyval) {
    case GDK_KEY_r: case GDK_KEY_R: case GDK_KEY_Home: act_reset_view(a); return TRUE;
    case GDK_KEY_p: case GDK_KEY_P: act_toggle_projection(a); return TRUE;
    case GDK_KEY_w: case GDK_KEY_W: act_toggle_wireframe(a); return TRUE;
  }
  return FALSE;
}
static void on_btn_reset(GtkButton*, App* a) { act_reset_view(a); }
static void on_btn_proj(GtkButton*, App* a) { act_toggle_projection(a); }

static void activate(GtkApplication* gapp, App* a) {
  GtkWidget* win = gtk_application_window_new(gapp);
  gtk_window_set_default_size(GTK_WINDOW(win), 900, 650);

  GtkWidget* hb = hdy_header_bar_new();
  hdy_header_bar_set_title(HDY_HEADER_BAR(hb), "VTK + GTK3 + libhandy");
  hdy_header_bar_set_show_close_button(HDY_HEADER_BAR(hb), TRUE);
  GtkWidget* b1 = gtk_button_new_with_label("Сброс вида (R)");
  GtkWidget* b2 = gtk_button_new_with_label("Проекция (P)");
  gtk_widget_set_can_focus(b1, FALSE);  // keep keyboard focus on the view
  gtk_widget_set_can_focus(b2, FALSE);
  g_signal_connect(b1, "clicked", G_CALLBACK(on_btn_reset), a);
  g_signal_connect(b2, "clicked", G_CALLBACK(on_btn_proj), a);
  hdy_header_bar_pack_start(HDY_HEADER_BAR(hb), b1);
  hdy_header_bar_pack_start(HDY_HEADER_BAR(hb), b2);
  gtk_window_set_titlebar(GTK_WINDOW(win), hb);
  g_signal_connect(win, "key-press-event", G_CALLBACK(on_key), a);

  a->area = gtk_gl_area_new();
  gtk_gl_area_set_required_version(GTK_GL_AREA(a->area), 3, 2);
  gtk_gl_area_set_has_depth_buffer(GTK_GL_AREA(a->area), TRUE);
  gtk_gl_area_set_auto_render(GTK_GL_AREA(a->area), FALSE);
  gtk_widget_set_can_focus(a->area, TRUE);
  gtk_widget_add_events(a->area, GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK |
                                     GDK_BUTTON_RELEASE_MASK | GDK_SCROLL_MASK |
                                     GDK_SMOOTH_SCROLL_MASK | GDK_KEY_PRESS_MASK |
                                     GDK_KEY_RELEASE_MASK);

  g_signal_connect(a->area, "realize", G_CALLBACK(on_realize), a);
  g_signal_connect(a->area, "unrealize", G_CALLBACK(on_unrealize), a);
  g_signal_connect(a->area, "render", G_CALLBACK(on_render), a);
  g_signal_connect(a->area, "motion-notify-event", G_CALLBACK(on_motion), a);
  g_signal_connect(a->area, "button-press-event", G_CALLBACK(on_press), a);
  g_signal_connect(a->area, "button-release-event", G_CALLBACK(on_release), a);
  g_signal_connect(a->area, "scroll-event", G_CALLBACK(on_scroll), a);

  gtk_container_add(GTK_CONTAINER(win), a->area);
  gtk_widget_show_all(win);
}

int main(int argc, char** argv) {
  // Distro VTK is GLX-only: use the X11 backend (GTK3 creates GLX contexts there).
  g_setenv("GDK_BACKEND", "x11", FALSE);
  App app;
  gtk_init(&argc, &argv);
  hdy_init();
  GtkApplication* gapp = gtk_application_new("org.example.vtk3", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(gapp, "activate", G_CALLBACK(activate), &app);
  int r = g_application_run(G_APPLICATION(gapp), argc, argv);
  g_object_unref(gapp);
  return r;
}
