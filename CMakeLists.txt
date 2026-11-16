cmake_minimum_required(VERSION 3.20)
project(vimwhere VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick Qml DBus)
find_package(KF6GlobalAccel REQUIRED)
find_package(LayerShellQt REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ATSPI REQUIRED IMPORTED_TARGET atspi-2)
pkg_check_modules(GOBJECT REQUIRED IMPORTED_TARGET gobject-2.0)

qt_add_executable(vimwhere
    src/main.cpp
    src/AppController.cpp src/AppController.h
    src/KWinBridge.cpp src/KWinBridge.h
    src/TargetScanner.cpp src/TargetScanner.h
    src/OverlayView.cpp src/OverlayView.h
    src/PortalClicker.cpp src/PortalClicker.h
)
target_link_libraries(vimwhere PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml Qt6::DBus
    KF6::GlobalAccel LayerShellQt::Interface PkgConfig::ATSPI PkgConfig::GOBJECT)
target_compile_definitions(vimwhere PRIVATE VIMWHERE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
install(TARGETS vimwhere RUNTIME DESTINATION bin)
install(FILES qml/Overlay.qml DESTINATION share/vimwhere/qml)
install(FILES kwin/main.js DESTINATION share/vimwhere/kwin)

include(CTest)
if(BUILD_TESTING)
    qt_add_executable(navigation-test tests/navigation.cpp)
    target_link_libraries(navigation-test PRIVATE Qt6::Core Qt6::Gui)
    add_test(NAME navigation COMMAND navigation-test)
    qt_add_executable(qml-smoke tests/qml_smoke.cpp)
    target_link_libraries(qml-smoke PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml)
    target_compile_definitions(qml-smoke PRIVATE VIMWHERE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME qml_smoke COMMAND qml-smoke)
    set_tests_properties(qml_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
