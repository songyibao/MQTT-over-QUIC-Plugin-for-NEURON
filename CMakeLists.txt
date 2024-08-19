#
# Copyright 2018 Capitar IT Group BV <info@capitar.com>
# Copyright 2018 Staysail Systems, Inc. <info@staysail.tech>
#
# This software is supplied under the terms of the MIT License, a
# copy of which should be located in the distribution where this
# file was obtained (LICENSE.txt).  A copy of the license may also be
# found online at https://opensource.org/licenses/MIT.

cmake_minimum_required(VERSION 3.13)
set(LIBRARY_OUTPUT_PATH "./")
file(COPY /software/neuron/plugins/mqtt_over_quic/mqtt-quic.json DESTINATION
        /software/neuron/build/plugins/schema)
project(quic_clientv5)

# Call this from your own project's makefile.

find_package(Threads)
find_package(msquic)
find_package(OpenSSL)

if (DEBUG)
    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
    if (ASAN)
        SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
    endif (ASAN)
    if (TSAN)
        SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=thread")
    endif (TSAN)
endif (DEBUG)

add_library(plugin-mqtt-quic SHARED mqtt_quic_plugin.c mqtt_quic_config.c mqtt_quic_sdk.c
        mqtt_quic_handle.c json_rw.c
        quic_conn_status_detect/detector.c
        internal_api/update_interval.c)
if (OPENSSL_FOUND)
    include_directories(${OPENSSL_INCLUDE_DIRS})
    target_link_libraries(plugin-mqtt-quic PRIVATE OpenSSL::Crypto OpenSSL::SSL)
endif ()

target_include_directories(plugin-mqtt-quic PRIVATE
        #        ${CMAKE_SOURCE_DIR}/include/neuron
        /software/neuron/include/neuron
        #        ${CMAKE_CURRENT_SOURCE_DIR}/deps/tquic/include
        /software/neuron/plugins/quic/deps/tquic/include
        /own/lib/include
)
target_link_directories(plugin-mqtt-quic PRIVATE /own/lib/lib)
target_link_libraries(plugin-mqtt-quic PRIVATE nng msquic pthread)
target_link_libraries(plugin-mqtt-quic PRIVATE /usr/local/lib/libcjson.a)

target_link_libraries(plugin-mqtt-quic PRIVATE ev curl)
target_link_libraries(plugin-mqtt-quic PRIVATE /usr/local/lib/neuron/libneuron-base.so)

target_compile_definitions(plugin-mqtt-quic PRIVATE NNG_ELIDE_DEPRECATED)

if (NNG_ENABLE_SQLITE)
    add_definitions(-DNNG_SUPP_SQLITE)
    target_link_libraries(plugin-mqtt-quic dl)
endif (NNG_ENABLE_SQLITE)