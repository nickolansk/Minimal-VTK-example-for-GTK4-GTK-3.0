# VTK 9.x embedded in GTK 3 / GTK 4 / libadwaita

**English** | [Русский](README.ru.md)

A minimal working example of embedding VTK 9.x into a `GtkGLArea` with full
interactivity (rotate, pan, zoom with the mouse, hotkeys):

| Variant | File | Stack |
|---|---|---|
| GTK4 | `main.cpp` | GTK 4 + libadwaita |
| GTK3 | `main_gtk3.cpp` | GTK 3 + libhandy |

The idea: `GtkGLArea` owns the OpenGL context, and `vtkGenericOpenGLRenderWindow`
renders into that foreign context. GTK events are forwarded by hand to a
`vtkGenericRenderWindowInteractor`.

## Features

- Rotate (left button), pan (middle button), zoom (right button and wheel),
  using `vtkInteractorStyleTrackballCamera`.
- HiDPI support (window scale factor) and Y-axis flip (VTK's origin is at the bottom).
- Hotkeys and header-bar buttons:

  | Action | Key | Button |
  |---|---|---|
  | Reset view | `R` / `Home` | "Reset view (R)" |
  | Toggle parallel projection | `P` | "Projection (P)" |
  | Toggle wireframe | `W` | — |

  Keys are handled at the GTK window level and call VTK directly. They are *not*
  forwarded to VTK's default interactor style, so its built-in `w`, `s`, `e`, `q`
  bindings do not interfere.

> The UI labels in the source are in Russian (e.g. "Сброс вида (R)"); change the
> strings in `activate()` if you prefer another language.

## Build

Dependencies (Ubuntu/Debian):

```bash
# common
sudo apt install build-essential cmake libvtk9-dev libepoxy-dev libopenmpi-dev libfreetype-dev
# GTK4
sudo apt install libgtk-4-dev libadwaita-1-dev
# GTK3
sudo apt install libgtk-3-dev libhandy-1-dev
```

```bash
# GTK4 + libadwaita
cmake -S . -B build -DUSE_GTK4=ON && cmake --build build -j && ./build/app

# GTK3 + libhandy
cmake -S . -B build3 -DUSE_GTK4=OFF && cmake --build build3 -j && ./build3/app
```

Use **separate build directories** for GTK3 and GTK4. Otherwise `CMakeCache.txt`
keeps the flags of the other variant and the compiler cannot find the headers
(`handy.h`, `adwaita.h`).

If VTK is not in a system path: `-DVTK_DIR=/path/to/lib/cmake/vtk-9.x`.

## Tested

- VTK 9.5, GTK 4.22, libadwaita 1.9 and GTK 3, Ubuntu, Wayland session (via XWayland).
- VTK 9.1, GTK 4.14, libadwaita 1.5, GTK 3.24, libhandy 1.8 (Ubuntu 24.04, Xvfb + Mesa).

Native Wayland (EGL) is **not supported**, see below.

## Pitfalls this code works around

These five are the usual reasons for "nothing renders" or a segfault at startup.

1. **GLX vs EGL.** A distro-packaged VTK (GLEW/GLX) can only load GL functions from a
   GLX context, but GTK4 creates an EGL context by default, even on X11. The result
   is a call through a null pointer (`vtkOpenGLState::ResetGLClearColorState`).
   Fix: force X11 and GLX. `main()` sets `GDK_BACKEND=x11`, and the way to disable EGL
   depends on the GTK4 version:
   - GTK <= 4.14: `GDK_DEBUG=gl-glx`
   - GTK >= 4.16: `GDK_DISABLE=egl` (the `gl-*` values of `GDK_DEBUG` were removed)

   GTK3 uses GLX on X11 anyway. If the context is not GLX the program prints a clear
   message (a `glXGetCurrentContext()` check) instead of crashing.
2. **Initialization order in VTK >= 9.2 (glad).** `vtkGenericOpenGLRenderWindow::OpenGLInit()`
   calls `state->Reset()` (which calls `glClearColor`) *before* loading any GL symbols.
   With a foreign context you must load them up front:

   ```cpp
   a->rw->SetOpenGLSymbolLoader(gl_loader, nullptr);  // gl_loader -> epoxy_get_proc_address
   a->rw->OpenGLInitContext();
   a->rw->InitializeFromCurrentContext();
   ```
3. **White background instead of the one you set.** `vtkRenderer` defaults to
   `BackgroundAlpha = 0`, and GTK composites the GL area using alpha. Call
   `ren->SetBackgroundAlpha(1.0)`.
4. **Rendering into GTK's FBO.** There is no `SetDefaultFrameBufferId` in VTK. What works:
   `SetFrameBlitModeToBlitToCurrent()` — VTK renders into its own FBO and blits into the
   current one (GTK's). Also `SetOwnContext(0)`, `SetMapped(1)`, `SetIsCurrent(true)`,
   `SetMultiSamples(0)`.
5. **Xlib macros break VTK headers.** `epoxy/glx.h` pulls in `Xlib.h` (`Bool`, `None`,
   `Status`, ...). Include it **after** all VTK headers.

Also:

- `vtk_module_autoinit(...)` in CMake is mandatory; without it VTK 9 does not register the
  OpenGL2 rendering factories (empty window).
- Call `rw->Render()` only from the `render` signal. To request a redraw from VTK use
  `EnableRenderOff()` plus a `RenderEvent` observer that calls `gtk_gl_area_queue_render()`.
- A distro VTK needs `project(... C CXX)`: its config requires `MPI::MPI_C`.

## How the code is organized

```
GtkGLArea  --realize---> create vtkGenericOpenGLRenderWindow, renderer, interactor
           --render----> SetSize(w*scale, h*scale) -> rw->Render()  (blit into GTK's FBO)
           --unrealize-> release VTK while the context is still alive
GTK events --> SetEventInformation(x, y_flipped) -> InvokeEvent(vtkCommand::...)
VTK RenderEvent --> gtk_gl_area_queue_render()
```

To add your own action: write `act_...(App*)` that changes the camera or a VTK object and
calls `redraw(a)`, then bind it to a key in `on_key` and to a button in the header bar.

## Limitations

- Native Wayland without XWayland is not supported (it needs a VTK built with EGL, or
  off-screen rendering copied via `GdkMemoryTexture`). On Wayland the window opens
  through XWayland.
- Keyboard input is intentionally not forwarded to the VTK interactor (`KeyPressEvent`).

## License

Project is licensed under the Apache License 2.0 — see the LICENSE file for details.

## Acknowledgements

Developed together with Claude (Anthropic): finding the cause of the segfaults from
backtraces, checking the VTK 9.5 API against its sources, building and testing in a container.
