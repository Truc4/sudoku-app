cmake_minimum_required(VERSION 3.18)
project(sudoku C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# android/app/src/main/cpp -> repo root
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)

# sokol_app.h implements ANativeActivity_onCreate itself -- this .so IS the NativeActivity,
# no Java glue code needed (see AndroidManifest.xml's android.app.lib_name meta-data).
add_library(sudoku SHARED
    ${ROOT}/ui/backend_sokol.c
    ${ROOT}/ui/ui.c
    ${ROOT}/ui/font.c
    ${ROOT}/src/core.c
    ${ROOT}/src/solver.c
    ${ROOT}/src/gen.c
    ${ROOT}/src/engine.c
)

find_library(log-lib log)
find_library(android-lib android)
find_library(GLESv3-lib GLESv3)
find_library(EGL-lib EGL)

target_link_libraries(sudoku ${log-lib} ${android-lib} ${GLESv3-lib} ${EGL-lib})
