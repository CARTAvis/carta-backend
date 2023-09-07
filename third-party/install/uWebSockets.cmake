macro(install_uWebSockets)
    INCLUDE(ExternalProject)

    # Build uWebSockets project
    SET(UWEBSOCKETS_SOURCE_DIR ${CMAKE_SOURCE_DIR}/third-party/uWebSockets)

    ExternalProject_Add(uWebSockets-build
            URL               https://github.com/uNetworking/uWebSockets/archive/refs/tags/v20.46.0.zip
            SOURCE_DIR        ${UWEBSOCKETS_SOURCE_DIR}
            CONFIGURE_COMMAND ""
            BUILD_COMMAND     ""
            INSTALL_COMMAND   "")

    INCLUDE_DIRECTORIES(${UWEBSOCKETS_SOURCE_DIR}/src)

    # Build uSocket project
    SET(USOCKETS_SOURCE_DIR ${CMAKE_SOURCE_DIR}/third-party/uSockets)

    ExternalProject_Add(uSockets-build
            URL               https://github.com/uNetworking/uSockets/archive/refs/tags/v0.8.6.zip
            SOURCE_DIR        ${USOCKETS_SOURCE_DIR}
            CONFIGURE_COMMAND ""
            BUILD_COMMAND     cd ${USOCKETS_SOURCE_DIR} && make
            INSTALL_COMMAND   "")

    INCLUDE_DIRECTORIES(${USOCKETS_SOURCE_DIR}/src)

    SET(USOCKETS_LINK_LIB ${USOCKETS_SOURCE_DIR}/uSockets.a)

endmacro()

