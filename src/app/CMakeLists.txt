qt_add_executable(data-collector
    main.cpp
    AuthController.h
    AuthController.cpp
    SourcesController.h
    SourcesController.cpp
    TransferController.h
    TransferController.cpp
    stubs/StubControllers.h
    stubs/StubControllers.cpp
    ${CMAKE_SOURCE_DIR}/src/qml/qml.qrc
)

target_include_directories(data-collector PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(data-collector PRIVATE
    dc_core
    Qt6::Core
    Qt6::Quick
    Qt6::QuickControls2
)

target_compile_options(data-collector PRIVATE -Wall -Wextra)

install(TARGETS data-collector
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
