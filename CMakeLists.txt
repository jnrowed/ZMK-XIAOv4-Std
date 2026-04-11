cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(skreecustom)

target_sources(app PRIVATE src/layer_rgb.c)
