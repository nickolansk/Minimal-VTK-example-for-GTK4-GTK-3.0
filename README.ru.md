# VTK 9.x в окне GTK 3 / GTK 4 / libadwaita

[English](README.md) | **Русский**

Минимальный рабочий пример встраивания VTK 9.x в `GtkGLArea` с полноценной
интерактивностью (вращение, панорама, зум мышью, горячие клавиши):

| Вариант | Файл | Стек |
|---|---|---|
| GTK4 | `main.cpp` | GTK 4 + libadwaita |
| GTK3 | `main_gtk3.cpp` | GTK 3 + libhandy |

Идея: `GtkGLArea` владеет OpenGL-контекстом, а `vtkGenericOpenGLRenderWindow`
рисует в этом чужом контексте. События GTK вручную передаются в
`vtkGenericRenderWindowInteractor`.

## Возможности

- Вращение (левая кнопка), панорама (средняя), зум (правая кнопка и колесо) —
  стиль `vtkInteractorStyleTrackballCamera`.
- Учёт HiDPI (масштаб окна) и переворот оси Y (у VTK начало внизу).
- Горячие клавиши и кнопки в заголовке окна:

  | Действие | Клавиша | Кнопка |
  |---|---|---|
  | Сброс вида | `R` / `Home` | «Сброс вида (R)» |
  | Параллельная проекция вкл/выкл | `P` | «Проекция (P)» |
  | Каркас вкл/выкл | `W` | — |

  Клавиши обрабатываются на уровне окна GTK и напрямую вызывают VTK; в стандартный
  стиль VTK они не пробрасываются, поэтому встроенные `w`, `s`, `e`, `q` не мешают.

## Сборка

Зависимости (Ubuntu/Debian):

```bash
# общие
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

Для GTK3 и GTK4 используйте **разные каталоги сборки**: иначе в `CMakeCache.txt`
останутся флаги другой версии и компилятор не найдёт заголовки (`handy.h`, `adwaita.h`).

Если VTK стоит не в системных путях: `-DVTK_DIR=/путь/к/lib/cmake/vtk-9.x`.

## Проверено

- VTK 9.5, GTK 4.22, libadwaita 1.9 и GTK 3, Ubuntu, сеанс Wayland (через XWayland).
- VTK 9.1, GTK 4.14, libadwaita 1.5, GTK 3.24, libhandy 1.8 (Ubuntu 24.04, Xvfb + Mesa).

Нативный Wayland (EGL) **не поддерживается**, см. ниже.

## Грабли, на которых это обычно ломается

Все пять ниже — причины, из-за которых «ничего не работает» или сегфолт при старте.

1. **GLX против EGL.** VTK из дистрибутива (GLEW/GLX) может загрузить GL-функции
   только из GLX-контекста, а GTK4 по умолчанию создаёт EGL, даже на X11.
   Результат — вызов по нулевому указателю (`vtkOpenGLState::ResetGLClearColorState`).
   Решение: принудительно X11 и GLX. В `main()` ставится `GDK_BACKEND=x11`, а способ
   запретить EGL зависит от версии GTK4:
   - GTK ≤ 4.14: `GDK_DEBUG=gl-glx`
   - GTK ≥ 4.16: `GDK_DISABLE=egl` (значения `gl-*` из `GDK_DEBUG` убраны)

   В GTK3 на X11 контекст и так GLX. Если контекст не GLX, приложение печатает
   понятное сообщение (проверка `glXGetCurrentContext()`) вместо падения.
2. **Порядок инициализации в VTK ≥ 9.2 (glad).** `vtkGenericOpenGLRenderWindow::OpenGLInit()`
   сначала вызывает `state->Reset()` (там `glClearColor`) и только потом загружает
   GL-символы. Для чужого контекста нужно загрузить их заранее:

   ```cpp
   a->rw->SetOpenGLSymbolLoader(gl_loader, nullptr);  // gl_loader -> epoxy_get_proc_address
   a->rw->OpenGLInitContext();
   a->rw->InitializeFromCurrentContext();
   ```
3. **Белый фон вместо заданного.** У `vtkRenderer` по умолчанию `BackgroundAlpha = 0`,
   а GTK композитит GL-область с учётом альфы. Нужно `ren->SetBackgroundAlpha(1.0)`.
4. **Рисование в FBO GTK.** Метода `SetDefaultFrameBufferId` в VTK нет. Рабочий путь:
   `SetFrameBlitModeToBlitToCurrent()` — VTK рисует в свой FBO и блитит в текущий (GTK).
   Также `SetOwnContext(0)`, `SetMapped(1)`, `SetIsCurrent(true)`, `SetMultiSamples(0)`.
5. **Макросы Xlib ломают заголовки VTK.** `epoxy/glx.h` тянет `Xlib.h` (`Bool`, `None`,
   `Status`…). Подключайте его **после** всех заголовков VTK.

Кроме того:

- `vtk_module_autoinit(...)` в CMake обязателен, иначе в VTK 9 не регистрируются
  фабрики OpenGL2-рендеринга (пустое окно).
- `rw->Render()` вызывается только из сигнала `render`; для перерисовки из VTK
  `EnableRenderOff()` + наблюдатель `RenderEvent` → `gtk_gl_area_queue_render()`.
- Для сборки VTK из дистрибутива нужен `project(... C CXX)`: его конфиг требует `MPI::MPI_C`.

## Как устроен код

```
GtkGLArea  ──realize──▶ создать vtkGenericOpenGLRenderWindow, рендерер, интерактор
           ──render───▶ SetSize(w*scale, h*scale) → rw->Render()  (блит в FBO GTK)
           ──unrealize▶ освободить VTK при живом контексте
события GTK ──▶ SetEventInformation(x, y_flipped) → InvokeEvent(vtkCommand::…)
VTK RenderEvent ──▶ gtk_gl_area_queue_render()
```

Чтобы добавить своё действие: напишите `act_...(App*)`, которая меняет камеру/объект VTK
и вызывает `redraw(a)`; привяжите её к клавише в `on_key` и к кнопке в заголовке.

## Ограничения

- Нативный Wayland без XWayland не поддерживается (нужен VTK, собранный с EGL, либо
  офскрин-рендер с копированием через `GdkMemoryTexture`). На Wayland окно открывается
  через XWayland.
- Клавиатурный ввод в VTK-интерактор (`KeyPressEvent`) не пробрасывается намеренно.

## Лицензия

Проект распространяется по лицензии Apache License 2.0 — подробности см. в файле LICENSE.

## Благодарности

Код отлажен совместно с Claude (Anthropic): поиск причин сегфолтов по backtrace,
сверка API VTK 9.5 с исходниками, сборка и проверка в контейнере.
