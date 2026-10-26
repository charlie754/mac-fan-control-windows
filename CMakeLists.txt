cmake_minimum_required(VERSION 3.16)
project(MacFanCtl CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  # The sources are UTF-8 without a BOM (the GUI strings carry an em dash and a degree
  # sign); without /utf-8 MSVC reads them in the ANSI code page and garbles them.
  # NOMINMAX stops windows.h's min/max macros breaking std::min / std::max.
  # The static CRT matches the MinGW branch's -static: no Visual C++ redistributable needed.
  add_compile_options(/utf-8)
  add_compile_definitions(NOMINMAX)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CORE_SOURCES
  src/smc.cpp
  src/sensordb.cpp
  src/fans.cpp
  src/controller.cpp
  src/config.cpp
)

# --- GUI application --------------------------------------------------------

enable_language(RC)
add_executable(MacFanCtl WIN32
  ${CORE_SOURCES}
  src/curvectrl.cpp
  src/kofibtn.cpp
  src/goatcambanner.cpp
  src/startup.cpp
  src/gui.cpp
  resources/app.rc
)
target_include_directories(MacFanCtl PRIVATE src)
target_compile_definitions(MacFanCtl PRIVATE UNICODE _UNICODE)
target_link_libraries(MacFanCtl PRIVATE comctl32 shell32 advapi32 gdi32 user32 ole32 gdiplus)

if(MINGW)
  # -municode selects the wWinMain-aware crt0; without it the linker looks for WinMain.
  target_link_options(MacFanCtl PRIVATE -static -static-libgcc -static-libstdc++ -mwindows -municode)
  # windres needs to find app.manifest and goatcam-coffee.jpg next to app.rc
  set_source_files_properties(resources/app.rc PROPERTIES
    COMPILE_FLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}/resources")
endif()

# --- command line tool ------------------------------------------------------

add_executable(macfanctl-cli ${CORE_SOURCES} src/cli.cpp)
target_include_directories(macfanctl-cli PRIVATE src)
target_link_libraries(macfanctl-cli PRIVATE advapi32 shell32 ole32)
if(MINGW)
  target_link_options(macfanctl-cli PRIVATE -static -static-libgcc -static-libstdc++)
endif()
