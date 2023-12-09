cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)
include(FreeRTOS_Kernel_import.cmake)

project(pc_remote_button)

pico_sdk_init()

add_executable(pc_remote_button
	src/main.cpp
	src/ntp.cpp
)

target_link_libraries(pc_remote_button
	pico_cyw43_arch_lwip_sys_freertos
	pico_stdlib
	FreeRTOS-Kernel-Heap4
)

target_compile_options(pc_remote_button PRIVATE
	-fstack-usage
)

target_include_directories(pc_remote_button PRIVATE
	${CMAKE_SOURCE_DIR}/include
)

target_compile_features(pc_remote_button PRIVATE
	cxx_std_23
)

if (DEFINED HOSTNAME)
	add_compile_definitions(CYW43_HOST_NAME=\"${HOSTNAME}\")
endif()

pico_add_extra_outputs(pc_remote_button)

pico_enable_stdio_usb(pc_remote_button 1)
pico_enable_stdio_uart(pc_remote_button 0)
