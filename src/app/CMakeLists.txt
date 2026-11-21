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

qt_add_resources(data-collector "fonts"
    PREFIX "/fonts"
    BASE "${CMAKE_SOURCE_DIR}/resources/fonts"
    FILES
        "${CMAKE_SOURCE_DIR}/resources/fonts/Inter-Regular.ttf"
        "${CMAKE_SOURCE_DIR}/resources/fonts/Inter-Medium.ttf"
        "${CMAKE_SOURCE_DIR}/resources/fonts/Inter-SemiBold.ttf"
        "${CMAKE_SOURCE_DIR}/resources/fonts/JetBrainsMono-Regular.ttf"
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
