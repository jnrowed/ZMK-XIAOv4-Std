cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(skreecustom)

if(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    target_sources(app PRIVATE src/layer_rgb_central.c)
else()
    target_sources(app PRIVATE src/layer_rgb_peripheral.c)
endif()
