// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2025
/// @file

#include <secrets.h>

#include <pcrb/mqtt.h>
#include <pcrb/dns.h>

#include <lwip/apps/mqtt.h>
#include <lwip/apps/mqtt_priv.h>

#include <gpico/log.h>

#include <span>
#include <array>
#include <atomic>
#include <cstring>
#include <format>

using gpico::sys_log;

// FIXME this needs to manage a state machine, effectively
class mqtt_manager
{
public:
	mqtt_manager() = default;
	mqtt_manager(const mqtt_manager&) = delete;
	mqtt_manager& operator=(const mqtt_manager&) = delete;

	void get(std::span<std::byte> output)
	{
		while (!ready)
		{
			ulTaskNotifyTake(false, portMAX_DELAY);
		}

		std::copy_n(buffer.begin(), std::min(output.size(), buffer.size()), output.begin());
		ready = false;
	}

	bool is_ready() const
	{
		return ready;
	}

	int connect()
	{
		task_handle = xTaskGetCurrentTaskHandle();

		auto query_result = pcrb::dns_query(MQTT_SERVER);
		if (!query_result)
		{
			return query_result.error();
		}
		std::optional<ip_addr_t> extract_result = pcrb::extract_ip_address(*query_result);
		if (!extract_result)
		{
			return ERR_VAL;
		}

		ip_addr_t ip = *extract_result;

		mqtt_connect_client_info_t client_info = {};
		client_info.client_id = "pcrb_";
		while (!connected)
		{
			err_t err = mqtt_client_connect(&client, &ip, MQTT_PORT, mqtt_connection_cb, this, &client_info);
			if (err != ERR_OK)
			{
				sys_log.push(std::format("mqtt: connection error %d", err));
			}
			else
			{
				// Wait until callback notifies us that we're connected
				ulTaskNotifyTake(false, portMAX_DELAY);
			}
		}
		return 0;
	}

	bool subscribe()
	{
		if (!connected)
			return false;

		mqtt_set_inpub_callback(&client, mqtt_incoming_publish_cb, mqtt_incoming_data_cb, this);
		while (!subscribed)
		{
			err_t error = mqtt_subscribe(&client, "pcrb", 2, mqtt_sub_request_cb, this);
			if (error != ERR_OK)
			{
				sys_log.push(std::format("mqtt: failed to initiate subscribe: {}", error));
			}
			else
			{
				// Wait until callback notifies us that we're subscribed
				ulTaskNotifyTake(false, portMAX_DELAY);
			}
		}
		return true;
	}

private:

	size_t read = 0;
	size_t size = 0;
	std::atomic_bool connected = false;
	std::atomic_bool ready = false;
	std::atomic_bool subscribed = false;
	std::array<std::byte, 1024> buffer;
	mqtt_client_t client = {};
	TaskHandle_t task_handle;

	bool start_incoming(size_t amount)
	{
		if (ready)
			return false;

		size = amount;
		read = 0;
		ready = false;

		return true;
	}

	bool push_incoming(std::span<const std::byte> data)
	{
		if (ready)
		{
			return false;
		}

		size_t amount = std::min(buffer.size() - read, data.size());
		memcpy(buffer.data() + read, data.data(), amount);
		read += data.size();
		if (read == size)
		{
			ready = true;
			xTaskNotifyGive(task_handle);
		}
		return true;
	}

	static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags)
	{
		mqtt_manager* manager = reinterpret_cast<mqtt_manager*>(arg);
		// FIXME what to do if push_incoming fails?
		manager->push_incoming(
			std::span<const std::byte>(
				reinterpret_cast<const std::byte*>(data), len));

		if (flags == MQTT_DATA_FLAG_LAST)
		{
			// FIXME send notification to task that data is ready?

		}
	}

	static void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len)
	{
		mqtt_manager* manager = reinterpret_cast<mqtt_manager*>(arg);
		// FIXME what if start_incoming fails?
		manager->start_incoming(tot_len);
	}

	static void mqtt_sub_request_cb(void *arg, err_t result)
	{
		mqtt_manager* manager = reinterpret_cast<mqtt_manager*>(arg);
		if (result != ERR_OK)
		{
			sys_log.push(std::format("mqtt: subscribe result error: {}", result));
		}
		else
		{
			manager->subscribed = true;
		}
		xTaskNotifyGive(manager->task_handle);
	}

	static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
	{
		mqtt_manager* manager = reinterpret_cast<mqtt_manager*>(arg);
		if (status == MQTT_CONNECT_ACCEPTED)
		{
			manager->connected = true;
		}
		else
		{
			sys_log.push(std::format("mqtt: failed to connect: {}", static_cast<int>(status)));
			// FIXME anything else?
		}
		xTaskNotifyGive(manager->task_handle);
	}
} manager;

namespace pcrb
{

void mqtt_task(void*)
{
	manager.connect();
	manager.subscribe();
	for(;;)
	{
		std::array<std::byte, 1024> buffer;
		manager.get(buffer);
		sys_log.push(std::format("mqtt data: {}", reinterpret_cast<const char*>(buffer.data())));
	}
}

}
