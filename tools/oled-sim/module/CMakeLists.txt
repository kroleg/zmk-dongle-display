if(CONFIG_OLED_SIM)
    zephyr_library()
    zephyr_library_sources(oled_sim.c scenario.c stubs.c)
    zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include) # zmk/app headers
    # the dongle display sizes its battery rows by this; there is no split BLE on native
    zephyr_compile_definitions(ZMK_SPLIT_BLE_PERIPHERAL_COUNT=2)
endif()
