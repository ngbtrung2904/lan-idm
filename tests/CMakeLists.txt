function(dc_add_unit_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE dc_core Qt6::Core Qt6::Test)
    target_compile_options(${target} PRIVATE -Wall -Wextra)
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

dc_add_unit_test(test_core_remote unit/test_core_remote.cpp)
dc_add_unit_test(test_usb_monitor unit/test_usb_monitor.cpp)
dc_add_unit_test(test_transfer_engine unit/test_transfer_engine.cpp)

# Phase 3 & 4: Fake-fleet integration & soak test
add_executable(test_fake_fleet_integration
    integration/test_fake_fleet_integration.cpp
)
target_link_libraries(test_fake_fleet_integration PRIVATE
    dc_core
    Qt6::Core
    Qt6::Test
)
target_compile_options(test_fake_fleet_integration PRIVATE -Wall -Wextra)
add_test(NAME test_fake_fleet_integration COMMAND test_fake_fleet_integration)
set_tests_properties(test_fake_fleet_integration PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# Phase 4: Offscreen GUI controller smoke test
add_executable(test_gui_smoke
    integration/test_gui_smoke.cpp
    ${CMAKE_SOURCE_DIR}/src/app/AuthController.h
    ${CMAKE_SOURCE_DIR}/src/app/AuthController.cpp
    ${CMAKE_SOURCE_DIR}/src/app/SourcesController.h
    ${CMAKE_SOURCE_DIR}/src/app/SourcesController.cpp
    ${CMAKE_SOURCE_DIR}/src/app/TransferController.h
    ${CMAKE_SOURCE_DIR}/src/app/TransferController.cpp
)
target_include_directories(test_gui_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/app
)
target_link_libraries(test_gui_smoke PRIVATE
    dc_core
    Qt6::Core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Test
)
target_compile_options(test_gui_smoke PRIVATE -Wall -Wextra)
add_test(NAME test_gui_smoke COMMAND test_gui_smoke)
set_tests_properties(test_gui_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
