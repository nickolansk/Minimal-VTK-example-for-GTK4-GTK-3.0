cmake_minimum_required(VERSION 3.16)
# [ENG]: The C language is required: the VTK build from the distribution was compiled with MPI, and its configuration necessitates MPI::MPI_C.
# [RUS]: Язык C нужен: VTK из дистрибутива собран с MPI, и его конфиг требует MPI::MPI_C
project("MinimalGTKVTKExample" C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(USE_GTK4 "GTK4 + libadwaita (OFF = GTK3 + libhandy)" ON)

find_package(VTK REQUIRED COMPONENTS
  CommonCore
  FiltersSources
  InteractionStyle
  RenderingCore
  RenderingFreeType
  RenderingOpenGL2)

find_package(PkgConfig REQUIRED)
if(USE_GTK4)
  pkg_check_modules(DEPS REQUIRED IMPORTED_TARGET gtk4 libadwaita-1 epoxy)
  set(APP_SRC main.cpp)
else()
  pkg_check_modules(DEPS REQUIRED IMPORTED_TARGET gtk+-3.0 libhandy-1 epoxy)
  set(APP_SRC main_gtk3.cpp)
endif()

add_executable(app ${APP_SRC})
target_link_libraries(app PRIVATE ${VTK_LIBRARIES} PkgConfig::DEPS)

# [ENG]: Required for VTK 9: registers OpenGL2 rendering factories
# [RUS]: Обязательно для VTK 9: регистрирует фабрики OpenGL2-рендеринга
vtk_module_autoinit(TARGETS app MODULES ${VTK_LIBRARIES})
