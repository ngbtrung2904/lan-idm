add_library(dc_core STATIC
    Types.h
    ConfigStore.h
    ConfigStore.cpp
    SshOptions.h
    SshOptions.cpp
    KeyAgent.h
    KeyAgent.cpp
    PathSanitizer.h
    PathSanitizer.cpp
    HostProbe.h
    HostProbe.cpp
    RsyncLister.h
    RsyncLister.cpp
    TimeFilter.h
    TimeFilter.cpp
    SpaceEstimator.h
    SpaceEstimator.cpp
    UsbMonitor.h
    UsbMonitor.cpp
    Manifest.h
    Manifest.cpp
    TransferEngine.h
    TransferEngine.cpp
)

target_include_directories(dc_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(dc_core PUBLIC Qt6::Core)
target_compile_options(dc_core PRIVATE -Wall -Wextra)
