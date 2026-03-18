cmake_minimum_required(VERSION 3.20.0)

set(ZEPHYR_EXTRA_MODULES 
    "${CMAKE_SOURCE_DIR}/modules/wifi_utilities" 
    "${CMAKE_SOURCE_DIR}/modules/tcp_socket"
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sample_with_mcuboot)

# Verify that this sample is built through sysbuild to ensure MCUboot is
# automatically included and that sample specific MCUboot configurations are
# used when building MCUboot.
test_sysbuild()

target_sources(app PRIVATE src/main.c)
