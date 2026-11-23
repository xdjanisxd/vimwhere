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
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0)
pkg_check_modules(GOBJECT REQUIRED IMPORTED_TARGET gobject-2.0)

qt_add_executable(vimwhere
    src/main.cpp
    src/AccessibilityStatus.cpp src/AccessibilityStatus.h
    src/AppController.cpp src/AppController.h
    src/HintScanRetry.cpp src/HintScanRetry.h
    src/HintScanWorker.cpp src/HintScanWorker.h
    src/HintScanProtocol.cpp src/HintScanProtocol.h
    src/HintScanChild.cpp src/HintScanChild.h
    src/KWinBridge.cpp src/KWinBridge.h
    src/TargetScanner.cpp src/TargetScanner.h
    src/OverlayView.cpp src/OverlayView.h
    src/PortalClicker.cpp src/PortalClicker.h
)
target_link_libraries(vimwhere PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml Qt6::DBus
    KF6::GlobalAccel LayerShellQt::Interface PkgConfig::ATSPI PkgConfig::GLIB PkgConfig::GOBJECT)
target_compile_definitions(vimwhere PRIVATE VIMWHERE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
install(TARGETS vimwhere RUNTIME DESTINATION bin)
install(FILES qml/Overlay.qml DESTINATION share/vimwhere/qml)
install(FILES kwin/main.js DESTINATION share/vimwhere/kwin)

include(CTest)
if(BUILD_TESTING)
    qt_add_executable(target-scanner-test tests/target_scanner.cpp src/TargetScanner.cpp)
    target_link_libraries(target-scanner-test PRIVATE Qt6::Core PkgConfig::ATSPI PkgConfig::GOBJECT)
    add_test(NAME target_scanner COMMAND target-scanner-test)
    qt_add_executable(hint-scan-retry-test tests/hint_scan_retry.cpp
        src/HintScanRetry.cpp src/HintScanWorker.cpp src/HintScanProtocol.cpp)
    target_link_libraries(hint-scan-retry-test PRIVATE Qt6::Core)
    add_test(NAME hint_scan_retry COMMAND hint-scan-retry-test)
    qt_add_executable(accessibility-status-test tests/accessibility_status.cpp src/AccessibilityStatus.cpp)
    target_link_libraries(accessibility-status-test PRIVATE Qt6::Core Qt6::DBus PkgConfig::ATSPI)
    add_test(NAME accessibility_status COMMAND accessibility-status-test)
    qt_add_executable(navigation-test tests/navigation.cpp)
    target_link_libraries(navigation-test PRIVATE Qt6::Core Qt6::Gui)
    add_test(NAME navigation COMMAND navigation-test)
    add_executable(kwin-snapshot-test tests/kwin_snapshot.cpp)
    target_link_libraries(kwin-snapshot-test PRIVATE Qt6::Core Qt6::Qml)
    target_compile_definitions(kwin-snapshot-test PRIVATE VIMWHERE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME kwin_snapshot COMMAND kwin-snapshot-test)
    add_executable(portal-session-test tests/portal_session.cpp src/PortalClicker.h)
    target_link_libraries(portal-session-test PRIVATE Qt6::Core Qt6::Gui Qt6::DBus)
    add_test(NAME portal_session_decode COMMAND portal-session-test --decode-only)
    set_tests_properties(portal_session_decode PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    find_program(DBUS_RUN_SESSION dbus-run-session)
    if(DBUS_RUN_SESSION)
        add_test(NAME portal_session COMMAND "${DBUS_RUN_SESSION}" -- $<TARGET_FILE:portal-session-test>)
        add_test(NAME portal_session_unpositioned
            COMMAND "${DBUS_RUN_SESSION}" -- $<TARGET_FILE:portal-session-test> --unpositioned)
        add_test(NAME portal_session_restart
            COMMAND "${DBUS_RUN_SESSION}" -- $<TARGET_FILE:portal-session-test> --restart-portal)
        set_tests_properties(portal_session portal_session_unpositioned portal_session_restart
            PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
    qt_add_executable(qml-smoke tests/qml_smoke.cpp)
    target_link_libraries(qml-smoke PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml)
    target_compile_definitions(qml-smoke PRIVATE VIMWHERE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME qml_smoke COMMAND qml-smoke)
    set_tests_properties(qml_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
