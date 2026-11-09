cmake_minimum_required(VERSION 3.16)
project(console-launcher VERSION 0.11.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(WITH_SDL2 "Suporte a controles via SDL2" ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Network DBus Qml Quick)

qt_add_executable(console-launcher
    src/main.cpp
    src/library.cpp   src/library.h
    src/runner.cpp    src/runner.h
    src/gamepad.cpp   src/gamepad.h
    src/sfo.cpp       src/sfo.h
    src/apps.cpp      src/apps.h
    src/emuconfig.cpp src/emuconfig.h
    src/hotkeys.cpp   src/hotkeys.h
    src/importer.cpp  src/importer.h
    src/systemcontrol.cpp src/systemcontrol.h
    src/display.cpp
    src/fileman.cpp   src/fileman.h
    src/bios.cpp      src/bios.h
    src/gamelogs.cpp  src/gamelogs.h
    src/gamemode.cpp  src/gamemode.h
    src/pointer.cpp   src/pointer.h
    src/wallpaper.cpp src/wallpaper.h
)

qt_add_resources(console-launcher "assets"
    PREFIX "/"
    FILES
        qml/Main.qml
        qml/GameCard.qml
        qml/ButtonGlyph.qml
        qml/Hint.qml
        qml/SideBar.qml
        qml/RowList.qml
        qml/FileManager.qml
        qml/FileIcon.qml
        qml/LogViewer.qml
        qml/Wallpaper.qml
        qml/WallpaperVideo.qml
        qml/Themes.js
        qml/ThemedTitle.qml
        qml/ThemeBackdrop.qml
        data/fonts/LuckiestGuy.ttf
        data/fonts/Exo2.ttf
        data/fonts/Orbitron.ttf
        data/fonts/Michroma.ttf
        data/fonts/TitilliumWeb-Light.ttf
        data/fonts/RussoOne.ttf
        data/fonts/Bungee.ttf
        data/fonts/VarelaRound.ttf
        data/fonts/Rubik.ttf
        data/fonts/PressStart2P.ttf
        data/fonts/TitanOne.ttf
        qml/OptionsPanel.qml
        qml/ImportPanel.qml
        qml/Keyboard.qml
        data/systems.json
)

target_compile_definitions(console-launcher PRIVATE APP_VERSION="${PROJECT_VERSION}")
target_link_libraries(console-launcher PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::DBus Qt6::Qml Qt6::Quick ${CMAKE_DL_LIBS})

# Atalhos globais de teclado (X11)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(XCB QUIET IMPORTED_TARGET xcb)
endif()
if(TARGET PkgConfig::XCB)
    target_link_libraries(console-launcher PRIVATE PkgConfig::XCB)
    target_compile_definitions(console-launcher PRIVATE HAVE_XCB)
    message(STATUS "Atalhos globais: X11 (xcb)")
else()
    message(WARNING "libxcb não encontrada: sem atalhos globais de teclado (instale libxcb1-dev)")
endif()

if(WITH_SDL2)
    find_package(SDL2 QUIET CONFIG)
    if(TARGET SDL2::SDL2)
        target_link_libraries(console-launcher PRIVATE SDL2::SDL2)
        target_compile_definitions(console-launcher PRIVATE HAVE_SDL2)
        message(STATUS "Controles: SDL2 ${SDL2_VERSION}")
    else()
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
        endif()
        if(TARGET PkgConfig::SDL2)
            target_link_libraries(console-launcher PRIVATE PkgConfig::SDL2)
            target_compile_definitions(console-launcher PRIVATE HAVE_SDL2)
            message(STATUS "Controles: SDL2 (pkg-config)")
        else()
            message(WARNING "SDL2 não encontrado: o launcher funcionará só com teclado/mouse. "
                            "Instale libsdl2-dev para suporte a controles.")
        endif()
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS console-launcher RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/console-launcher.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/systems.json DESTINATION ${CMAKE_INSTALL_DATADIR}/console-launcher)
install(FILES data/console-launcher.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
