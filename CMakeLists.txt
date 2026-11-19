cmake_minimum_required(VERSION 3.21)

project(DataCollector VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(BUILD_TESTS "Build unit and integration tests" ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Quick QuickControls2)

if(BUILD_TESTS)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Test)
    enable_testing()
endif()

add_compile_definitions(
    APP_VERSION="${PROJECT_VERSION}"
    DC_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

include(GNUInstallDirs)

add_subdirectory(src/core)
add_subdirectory(src/app)

if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Install rules
install(
    FILES scripts/askpass.sh
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/data-collector"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                GROUP_READ GROUP_EXECUTE
                WORLD_READ WORLD_EXECUTE
)

install(
    FILES packaging/data-collector.desktop
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
)

install(
    FILES config.example.json
          docs/user-guide.md
          docs/provisioning.md
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/data-collector"
)

# CPack DEB configuration
set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "data-collector")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "Data Collector Maintainers <ops@example.invalid>")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Data Collector Maintainers <ops@example.invalid>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Collect logs and screenshots from headless PCs onto a USB drive")
set(CPACK_PACKAGE_DESCRIPTION "Linux desktop application (Qt 6) that pulls application logs and screenshots from headless PCs onto a removable USB drive using key-based SSH and rsync.")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "openssh-client, rsync, udisks2")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
include(CPack)

