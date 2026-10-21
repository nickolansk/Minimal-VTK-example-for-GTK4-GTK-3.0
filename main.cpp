// VTK 9.x embedded in GtkGLArea (GTK4 + libadwaita)
#include <adwaita.h>
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


static void to_vtk(App* a, double x, double y, int& ox, int& oy) {
  int s = gtk_widget_get_scale_factor(a->area);
  int h = gtk_widget_get_height(a->area);
  ox = int(x * s);
  oy = int((h - y) * s);  // VTK origin is bottom-left
}

#if VTK_MAJOR_VERSION > 9 || (VTK_MAJOR_VERSION == 9 && VTK_MINOR_VERSION >= 2)
#define VTK_HAS_GLAD 1
// VTK >= 9.2 (glad): resolve GL entry points through libepoxy.
static vtkOpenGLRenderWindow::VTKOpenGLAPIProc gl_loader(void*, const char* name) {
    return reinterpret_cast<vtkOpenGLRenderWindow::VTKOpenGLAPIProc>(
        epoxy_glXGetProcAddress((const GLubyte*) name));
}
#endif

static void on_realize(GtkGLArea* area, App* a) {
  gtk_gl_area_make_current(area);
  if (gtk_gl_area_get_error(area)) {
    g_printerr("GtkGLArea error: %s\n", gtk_gl_area_get_error(area)->message);
    return;
  }

  // VTK from distro packages (GLEW/GLX) can only load GL entry points from a
  // GLX context. Refuse an EGL context instead of crashing in glClearColor.
  if (glXGetCurrentContext() == nullptr) {
    g_printerr("VTK needs a GLX context: run under X11 (GDK_BACKEND=x11) or "
               "use a VTK build with EGL support.\n");
    return;
  }

  a->rw = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
  a->rw->SetMultiSamples(0);
  a->rw->SetFrameBlitModeToBlitToCurrent();  // blit VTK's FBO into GTK's FBO
  a->rw->SetOwnContext(0);                   // the context belongs to GTK
  a->rw->SetMapped(1);
  a->rw->SetIsCurrent(true);
#ifdef VTK_HAS_GLAD
  // vtkGenericOpenGLRenderWindow::OpenGLInit() calls state->Reset() (glClearColor)
  // BEFORE glad has loaded anything, so the symbols must be loaded up front.
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
  ren->SetBackgroundAlpha(1.0);  // GTK composites with alpha; VTK's default is 0
  a->rw->AddRenderer(ren);
  ren->ResetCamera();
  a->home = vtkSmartPointer<vtkCamera>::New();
  a->home->DeepCopy(ren->GetActiveCamera());

  a->iren = vtkSmartPointer<vtkGenericRenderWindowInteractor>::New();
  a->iren->SetRenderWindow(a->rw);
  a->iren->EnableRenderOff();
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
  int w = gtk_widget_get_width(GTK_WIDGET(area)) * s;
  int h = gtk_widget_get_height(GTK_WIDGET(area)) * s;
  a->rw->SetSize(w, h);
  a->iren->SetSize(w, h);
  a->rw->SetIsCurrent(true);
  a->rw->Render();
  return TRUE;
}

static void on_resize(GtkGLArea*, int, int, App*) {}

static void on_motion(GtkEventControllerMotion*, double x, double y, App* a) {
  if (!a->iren) return;
  int ix, iy; to_vtk(a, x, y, ix, iy);
  a->iren->SetEventInformation(ix, iy);
  a->iren->InvokeEvent(vtkCommand::MouseMoveEvent);
}

static void press_release(GtkGestureClick* g, double x, double y, App* a, bool press) {
  if (!a->iren) return;
  gtk_widget_grab_focus(a->area);
  int ix, iy; to_vtk(a, x, y, ix, iy);
  a->iren->SetEventInformation(ix, iy);
  unsigned long ev = 0;
  switch (gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(g))) {
    case 1: ev = press ? vtkCommand::LeftButtonPressEvent : vtkCommand::LeftButtonReleaseEvent; break;
    case 2: ev = press ? vtkCommand::MiddleButtonPressEvent : vtkCommand::MiddleButtonReleaseEvent; break;
    case 3: ev = press ? vtkCommand::RightButtonPressEvent : vtkCommand::RightButtonReleaseEvent; break;
  }
  if (ev) a->iren->InvokeEvent(ev);
}
static void on_press(GtkGestureClick* g, int, double x, double y, App* a) { press_release(g, x, y, a, true); }
static void on_release(GtkGestureClick* g, int, double x, double y, App* a) { press_release(g, x, y, a, false); }

static gboolean on_scroll(GtkEventControllerScroll*, double, double dy, App* a) {
  if (!a->iren) return FALSE;
  a->iren->InvokeEvent(dy < 0 ? vtkCommand::MouseWheelForwardEvent
                              : vtkCommand::MouseWheelBackwardEvent);
  return TRUE;
}

static gboolean on_key(GtkEventControllerKey*, guint keyval, guint, GdkModifierType, App* a) {
  switch (keyval) {
    case GDK_KEY_r: case GDK_KEY_R: case GDK_KEY_Home: act_reset_view(a); return TRUE;
    case GDK_KEY_p: case GDK_KEY_P: act_toggle_projection(a); return TRUE;
    case GDK_KEY_w: case GDK_KEY_W: act_toggle_wireframe(a); return TRUE;
  }
  return FALSE;
}
static void on_btn_reset(GtkButton*, App* a) { act_reset_view(a); }
static void on_btn_proj(GtkButton*, App* a) { act_toggle_projection(a); }

static void activate(GtkApplication* gapp, App* a) {
  GtkWidget* win = adw_application_window_new(gapp);
  gtk_window_set_default_size(GTK_WINDOW(win), 900, 650);

  a->area = gtk_gl_area_new();
  gtk_gl_area_set_required_version(GTK_GL_AREA(a->area), 3, 2);
  gtk_gl_area_set_has_depth_buffer(GTK_GL_AREA(a->area), TRUE);
  gtk_gl_area_set_auto_render(GTK_GL_AREA(a->area), FALSE);
  gtk_widget_set_hexpand(a->area, TRUE);
  gtk_widget_set_vexpand(a->area, TRUE);
  gtk_widget_set_focusable(a->area, TRUE);

  g_signal_connect(a->area, "realize", G_CALLBACK(on_realize), a);
  g_signal_connect(a->area, "unrealize", G_CALLBACK(on_unrealize), a);
  g_signal_connect(a->area, "render", G_CALLBACK(on_render), a);
  g_signal_connect(a->area, "resize", G_CALLBACK(on_resize), a);

  auto* mc = gtk_event_controller_motion_new();
  g_signal_connect(mc, "motion", G_CALLBACK(on_motion), a);
  gtk_widget_add_controller(a->area, mc);

  auto* click = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
  g_signal_connect(click, "pressed", G_CALLBACK(on_press), a);
  g_signal_connect(click, "released", G_CALLBACK(on_release), a);
  gtk_widget_add_controller(a->area, GTK_EVENT_CONTROLLER(click));

  auto* sc = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
  g_signal_connect(sc, "scroll", G_CALLBACK(on_scroll), a);
  gtk_widget_add_controller(a->area, sc);

  GtkWidget* tv = adw_toolbar_view_new();
  GtkWidget* hb = adw_header_bar_new();
  GtkWidget* b1 = gtk_button_new_with_label("Сброс вида (R)");
  GtkWidget* b2 = gtk_button_new_with_label("Проекция (P)");
  gtk_widget_set_focusable(b1, FALSE);  // keep keyboard focus on the view
  gtk_widget_set_focusable(b2, FALSE);
  g_signal_connect(b1, "clicked", G_CALLBACK(on_btn_reset), a);
  g_signal_connect(b2, "clicked", G_CALLBACK(on_btn_proj), a);
  adw_header_bar_pack_start(ADW_HEADER_BAR(hb), b1);
  adw_header_bar_pack_start(ADW_HEADER_BAR(hb), b2);
  adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tv), hb);

  // Hotkeys on the whole window (capture phase), so they work without clicking the view first
  auto* kc = gtk_event_controller_key_new();
  gtk_event_controller_set_propagation_phase(kc, GTK_PHASE_CAPTURE);
  g_signal_connect(kc, "key-pressed", G_CALLBACK(on_key), a);
  gtk_widget_add_controller(win, kc);
  adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tv), a->area);
  adw_application_window_set_content(ADW_APPLICATION_WINDOW(win), tv);
  gtk_window_present(GTK_WINDOW(win));
}

int main(int argc, char** argv) {
  // Distro VTK is GLX-only: use the X11 backend and make GDK create GLX contexts.
  g_setenv("GDK_BACKEND", "x11", FALSE);
  // Make GDK create GLX contexts. The knob changed between GTK versions.
  if (gtk_get_minor_version() < 16)
    g_setenv("GDK_DEBUG", "gl-glx", FALSE);   // GTK <= 4.14
  else
    g_setenv("GDK_DISABLE", "egl", FALSE);    // GTK >= 4.16
  App app;
  AdwApplication* gapp = adw_application_new("org.example.vtk", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(gapp, "activate", G_CALLBACK(activate), &app);
  int r = g_application_run(G_APPLICATION(gapp), argc, argv);
  g_object_unref(gapp);
  return r;
}
