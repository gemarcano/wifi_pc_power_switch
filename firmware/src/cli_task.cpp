// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2023 - 2024
/// @file

#include <pcrb/cli_task.h>
#include <pcrb/dns.h>
#include <pcrb/monitor_task.h>
#include <pcrb/switch_task.h>
#include <pcrb/usb.h>

#include <lwip/netdb.h>

#include <gpico/log.h>
#include <gpico/reset.h>

#include <pico/bootrom.h>
#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>
#include <pico/unique_id.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <span>

using gpico::sys_log;

void toggle(uint32_t time)
{
	unsigned data =
		std::clamp<unsigned>(time, 0, std::numeric_limits<unsigned>::max());
	xQueueSendToBack(pcrb::switch_comms.get(), &data, 0);
}

static void command(std::string_view input, std::span<char> output)
{
	if (input.starts_with("toggle"))
	{
		unsigned long ms = 0;
		std::from_chars(
			input.substr(7).data(),
			input.substr(7).data() + input.substr(7).size(), ms
		);
		if (ms != 0)
		{
			snprintf(
				output.data(), output.size(),
				"Toggling switch for %lu milliseconds\r\n", ms
			);
			toggle(ms);
		}
	}

	else if (input == "sense")
	{
		snprintf(
			output.data(), output.size(), "sense: %u\r\n",
			pcrb::current_pc_state()
		);
	}

	else if (input == "get_boot")
	{
		snprintf(
			output.data(), output.size(), "boot select: %u\r\n",
			pcrb::get_boot_select()
		);
	}

	else if (input.starts_with("set_boot"))
	{
		unsigned long select = 0;
		std::from_chars(
			input.substr(9).data(),
			input.substr(9).data() + input.substr(9).size(), select
		);
		pcrb::set_boot_select(select);
		snprintf(
			output.data(), output.size(), "set boot select: %lu, actual %u\r\n",
			select, pcrb::get_boot_select()
		);
	}

	else if (input == "status")
	{
		size_t amount = 0;
		amount += snprintf(
			output.data() + amount, output.size() - amount,
			"IP Address: %s\r\n", ip4addr_ntoa(netif_ip4_addr(netif_list))
		);
		amount += snprintf(
			output.data() + amount, output.size() - amount,
			"default instance: 0x%p\r\n", netif_default
		);
		amount += snprintf(
			output.data() + amount, output.size() - amount,
			"NETIF is up? %s\r\n", netif_is_up(netif_default) ? "yes" : "no"
		);
		amount += snprintf(
			output.data() + amount, output.size() - amount,
			"NETIF flags: 0x%02X\r\n", netif_default->flags
		);
		amount += snprintf(
			output.data() + amount, output.size() - amount,
			"Wifi state: %d\r\n",
			cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA)
		);

		int32_t rssi = 0;
		cyw43_wifi_get_rssi(&cyw43_state, &rssi);
		amount += snprintf(
			output.data() + amount, output.size() - amount, "  RSSI: %ld\r\n",
			rssi
		);
		uint32_t pm_state = 0;
		cyw43_wifi_get_pm(&cyw43_state, &pm_state);
		amount += snprintf(
			output.data() + amount, output.size() - amount,
			"power mode: 0x%08lX\r\n", pm_state
		);
		amount += snprintf(
			output.data() + amount, output.size() - amount, "ticks: %lu\r\n",
			xTaskGetTickCount()
		);
		amount += snprintf(
			output.data() + amount, output.size() - amount,
			"FreeRTOS Heap Free: %u\r\n", xPortGetFreeHeapSize()
		);
		UBaseType_t number_of_tasks = uxTaskGetNumberOfTasks();
		amount += snprintf(
			output.data() + amount, output.size() - amount,
			"Tasks active: %lu\r\n", number_of_tasks
		);
		std::vector<TaskStatus_t> tasks(number_of_tasks);
		uxTaskGetSystemState(tasks.data(), tasks.size(), nullptr);
		for (auto& status : tasks)
		{
			amount += snprintf(
				output.data() + amount, output.size() - amount,
				"  task name: %s\r\n", status.pcTaskName
			);
			amount += snprintf(
				output.data() + amount, output.size() - amount,
				"    task mark: %lu\r\n", status.usStackHighWaterMark
			);
			amount += snprintf(
				output.data() + amount, output.size() - amount,
				"    task counter: %lu\r\n", status.ulRunTimeCounter
			);
			amount += snprintf(
				output.data() + amount, output.size() - amount,
				"    task priority: %lu\r\n", status.uxCurrentPriority
			);
		}

		char foo[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
		pico_get_unique_board_id_string(foo, sizeof(foo));
		amount += snprintf(
			output.data() + amount, output.size() - amount, "unique id: %s\r\n",
			foo
		);

		amount += snprintf(
			output.data() + amount, output.size() - amount, "log size: %u\r\n",
			sys_log.size()
		);
		for (size_t i = 0; i < sys_log.size(); ++i)
		{
			amount += snprintf(
				output.data() + amount, output.size() - amount,
				"log %u: %s\r\n", i, sys_log[i].c_str()
			);
		}
	}

	else if (input == "programming")
	{
		snprintf(
			output.data(), output.size(),
			"Rebooting into programming mode...\r\n"
		);
		gpico::bootsel_reset();
	}

	else if (input == "reboot")
	{
		snprintf(output.data(), output.size(), "Killing (hanging)...\r\n");
		gpico::flash_reset();
	}

	else if (input.starts_with("dns"))
	{
		std::string_view name = input.substr(4).data();
		if (name.size())
		{
			ssize_t amount = snprintf(
				output.data(), output.size(), "Looking up %s\r\n", name.data()
			);
			auto result = pcrb::dns_query(name);
			if (result)
			{
				std::optional<ip_addr_t> address =
					pcrb::extract_ip_address(*result);
#if LWIP_IPV6
				std::array<char, INET6_ADDRSTRLEN> host;
#else
				std::array<char, INET_ADDRSTRLEN> host;
#endif
				if (address)
				{
					ipaddr_ntoa_r(&*address, host.data(), host.size());
					snprintf(
						output.data() + amount, output.size() - amount,
						"  Resolved %s as %s\r\n", name.data(), host.data()
					);
				}
				else
				{
					snprintf(
						output.data() + amount, output.size() - amount,
						"  No valid IP found\r\n"
					);
				}
			}
		}
	}
}

static void run(const char *line, std::span<char> buffer)
{
	command(line, buffer);
	printf("%s", buffer.data());
	fflush(stdout);
}

namespace pcrb
{

void cli_task(void *)
{
	std::array<char, 4 * 1024> buffer = {};

	char line[33] = {0};
	size_t pos = 0;
	printf("> ");
	for (;;)
	{
		fflush(stdout);
		int c = fgetc(stdin);
		if (c != EOF)
		{
			if (c == '\r')
			{
				printf("\r\n");
				line[pos] = '\0';
				run(line, buffer);
				memset(line, 0, sizeof(line));
				pos = 0;
				printf("> ");
				continue;
			}
			if (c == '\b')
			{
				if (pos > 0)
				{
					--pos;
					printf("\b \b");
				}
				continue;
			}

			if (pos < (sizeof(line) - 1))
			{
				line[pos++] = c;
				printf("%c", c);
			}
		}
		else
		{
			printf("WTF, we got an EOF?\r\n");
		}
	}
}

} // namespace pcrb
