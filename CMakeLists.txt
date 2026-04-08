cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(inklab_blinky)
zephyr_include_directories(include src)


target_sources(app PRIVATE 
    src/main.c
    src/bus_manager.c
    src/rtos/usb_task.c
    src/proto/frontend_api.c
    src/hal/battery_defs.c
    src/hal/battery_soc.c
    src/hal/bq25798.c
    src/hal/diagnostics.c
    src/hal/error_manager.c
    src/hal/ext_interfaces.c
    src/hal/fpga.c
    src/hal/joystick.c
    src/hal/led_manager.c
    src/hal/powerMonitor.c
    src/hal/sys_power.c
)