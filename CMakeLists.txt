cmake_minimum_required(VERSION 3.21)
project(EcsPdfConverter VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ── Qt6 ──────────────────────────────────────────────────────────────
# Qt6::Pdf is optional — if not installed, we fall back to pdftocairo
# for page count + raster rendering.
find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)
find_package(Qt6 COMPONENTS Pdf)

if(Qt6Pdf_FOUND)
    message(STATUS "Qt6::Pdf found — native PDF rendering enabled")
    set(HAVE_QT_PDF ON)
else()
    message(STATUS "Qt6::Pdf NOT found — will use pdftocairo for raster rendering")
    set(HAVE_QT_PDF OFF)
endif()

# ── Application ───────────────────────────────────────────────────────
set(APP_SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/pdfconverter.cpp
    src/pdfconverter.h
    src/vectorconverter.cpp
    src/vectorconverter.h
)

set(APP_HEADERS
    src/mainwindow.h
    src/pdfconverter.h
    src/vectorconverter.h
)

# Platform-specific: suppress console on Windows
if(WIN32)
    add_executable(ecs_pdf_converter WIN32 ${APP_SOURCES})
else()
    add_executable(ecs_pdf_converter ${APP_SOURCES})
endif()

set_target_properties(ecs_pdf_converter PROPERTIES
    OUTPUT_NAME "ECSPdfConverter"
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "ECS PDF Converter"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.ecs.pdftoimage"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    WIN32_EXECUTABLE_NAME "ECS PDF Converter"
)

target_link_libraries(ecs_pdf_converter PRIVATE
    Qt6::Widgets
    Qt6::Concurrent
)

if(HAVE_QT_PDF)
    target_link_libraries(ecs_pdf_converter PRIVATE Qt6::Pdf)
    target_compile_definitions(ecs_pdf_converter PRIVATE HAVE_QT_PDF)
endif()

target_include_directories(ecs_pdf_converter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ── Installation ──────────────────────────────────────────────────────
if(WIN32)
    install(TARGETS ecs_pdf_converter RUNTIME DESTINATION .)
    # windeployqt is run manually in the build script
elseif(APPLE)
    install(TARGETS ecs_pdf_converter BUNDLE DESTINATION .)
endif()

# ── Deployment helpers (post-build) ──────────────────────────────────
if(WIN32)
    find_program(WINDEPLOYQT windeployqt HINTS ${Qt6_DIR}/../../../bin)
    if(WINDEPLOYQT)
        add_custom_command(TARGET ecs_pdf_converter POST_BUILD
            COMMAND ${WINDEPLOYQT} --no-translations --no-system-d3d-compiler
                    --no-opengl-sw --release
                    "$<TARGET_FILE:ecs_pdf_converter>"
            COMMENT "Running windeployqt..."
        )
    endif()

    # Copy bundled Poppler tools (pdftocairo, pdfinfo) into build directory
    set(POPLER_DIR "" CACHE PATH "Path to Poppler Library/bin directory")
    if(EXISTS "${POPLER_DIR}/pdftocairo.exe")
        add_custom_command(TARGET ecs_pdf_converter POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ecs_pdf_converter>/tools"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${POPLER_DIR}" "$<TARGET_FILE_DIR:ecs_pdf_converter>/tools/"
            COMMENT "Bundling Poppler tools..."
        )
    endif()
elseif(APPLE)
    find_program(MACDEPLOYQT macdeployqt HINTS ${Qt6_DIR}/../../../bin)
    if(MACDEPLOYQT)
        add_custom_command(TARGET ecs_pdf_converter POST_BUILD
            COMMAND ${MACDEPLOYQT} "$<TARGET_BUNDLE_DIR:ecs_pdf_converter>"
            COMMENT "Running macdeployqt..."
        )
    endif()
endif()