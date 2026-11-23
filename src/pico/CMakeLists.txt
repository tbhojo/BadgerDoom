add_library(common_pico INTERFACE)
target_sources(common_pico INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/i_glob.c
        ${CMAKE_CURRENT_LIST_DIR}/i_input.c
        ${CMAKE_CURRENT_LIST_DIR}/i_system.c
        ${CMAKE_CURRENT_LIST_DIR}/i_timer.c
        ${CMAKE_CURRENT_LIST_DIR}/i_video.c
        ${CMAKE_CURRENT_LIST_DIR}/stubs.c

)
if (PICO_ON_DEVICE)
    target_sources(common_pico INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}/blit.S
            ${CMAKE_CURRENT_LIST_DIR}/picoflash.c
            )
    pico_wrap_function(common_pico malloc)
    pico_wrap_function(common_pico calloc)
    pico_wrap_function(common_pico free)
    target_compile_definitions(common_pico INTERFACE
            PICO_HEAP_SIZE=0
            USE_ZONE_FOR_MALLOC=1
    )
endif()
target_include_directories(common_pico INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/..
        ${CMAKE_CURRENT_LIST_DIR}/../../textscreen
        )

target_compile_definitions(common_pico INTERFACE
        NO_USE_MOUSE=1
        PICO_USE_STACK_GUARDS=0 # todo we can actually use these sensibly, but right now we do overflow!
)

target_link_libraries(common_pico INTERFACE pico_stdlib pico_multicore)
if (BADGER_DOOM)
    target_sources(common_pico INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}/badger_display.c
            ${CMAKE_CURRENT_LIST_DIR}/badger_input.c
            )
    # RP2350A stalled in the SDK's software lock acquisition. Use SIO
    # spinlocks with the SDK 2.3 numbering that avoids the RP2350-E2 aliases.
    target_compile_definitions(common_pico INTERFACE BADGER_DOOM=1 NO_USE_UART=1 PICO_USE_SW_SPIN_LOCKS=0)
    if (PICO_SDK_VERSION_STRING VERSION_LESS "2.3.0")
        target_compile_definitions(common_pico INTERFACE
                PICO_SPINLOCK_ID_IRQ=5
                PICO_SPINLOCK_ID_TIMER=6
                PICO_SPINLOCK_ID_HARDWARE_CLAIM=7
                PICO_SPINLOCK_ID_RAND=10
                PICO_SPINLOCK_ID_ATOMIC=11
                PICO_SPINLOCK_ID_OS1=18
                PICO_SPINLOCK_ID_OS2=19
                PICO_SPINLOCK_ID_STRIPED_FIRST=20
                PICO_SPINLOCK_ID_STRIPED_LAST=25
                PICO_SPINLOCK_ID_CLAIM_FREE_FIRST=26
                )
    endif()
    target_link_libraries(common_pico INTERFACE hardware_flash hardware_interp hardware_spi)
else()
    target_sources(common_pico INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}/piconet.c
            ${CMAKE_CURRENT_LIST_DIR}/i_picosound.c
            )
    target_compile_definitions(common_pico INTERFACE
            PICO_AUDIO_I2S_PIO=1 PICO_AUDIO_I2S_DMA_IRQ=1 PICO_USE_SW_SPIN_LOCKS=0)
    pico_generate_pio_header(common_pico ${CMAKE_CURRENT_LIST_DIR}/video_doom.pio)
    target_link_libraries(common_pico INTERFACE pico_scanvideo_dpi)
    if (PICO_ON_DEVICE)
        target_link_libraries(common_pico INTERFACE hardware_i2c)
    endif()
endif()

add_library(pico_cd INTERFACE)
target_include_directories(pico_cd INTERFACE "${CMAKE_CURRENT_LIST_DIR}/usb_host")
if (TARGET tinyusb_host)
    target_link_libraries(pico_cd INTERFACE tinyusb_host)
endif()