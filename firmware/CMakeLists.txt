cmake_minimum_required(VERSION 3.20)

set(PICO_BOARD pico_w)
set(TINYUSB_OPT_OS OPT_OS_FREERTOS)

include(pico_sdk_import.cmake)
include(FreeRTOS_Kernel_import.cmake)
include(gpico_import.cmake)

project(pc_remote_button)

pico_sdk_init()

add_executable(pc_remote_button
	src/main.cpp
	src/mqtt.cpp
	src/ntp.cpp
	src/server.cpp
	src/request_handler.cpp
	src/switch_task.cpp
	src/network_task.cpp
	src/cli_task.cpp
	src/wifi_management_task.cpp
	src/monitor_task.cpp
	src/usb_descriptors.cpp
)

target_link_libraries(pc_remote_button
	pico_cyw43_arch_lwip_sys_freertos
	pico_lwip_mqtt
	pico_stdlib
	FreeRTOS-Kernel-Heap4
	gpico
)

target_compile_options(pc_remote_button PRIVATE
	$<$<CXX_COMPILER_ID:MSVC>:/W4>
	$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
	-fstack-usage
	-Wno-psabi
	-DPICO_STDIO_SHORT_CIRCUIT_CLIB_FUNCS=0
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
