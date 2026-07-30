cmake_minimum_required(VERSION 3.20)

project(OpenWyd LANGUAGES C CXX)

option(OPENWYD_BUILD_SERVERS "Build the headless Linux game servers" ON)

if(OPENWYD_BUILD_SERVERS)
    add_subdirectory(Servidor)
endif()
