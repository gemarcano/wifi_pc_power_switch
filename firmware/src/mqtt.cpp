#include <pcrb/mqtt.h>

#include <lwip/apps/mqtt.h>
#include <lwip/apps/mqtt_priv.h>

#include <span>
#include <array>
#include <atomic>
#include <cstring>

class mqtt_manager
{
public:
	void start_incoming(size_t amount)
	{
		size = amount;
		read = 0;
		ready = false;
	}

	void push_incoming(std::span<const std::byte> data)
	{
		size_t amount = std::min(buffer.size() - read, data.size());
		memcpy(buffer.data() + read, data.data(), amount);
		read += data.size();
		if (read == size)
		{
			ready = true;
		}
	}

	std::span<const std::byte> finalize_incoming()
	{
		ready = false;
		return std::span<const std::byte>(buffer.data(), read);
	}

	bool is_ready()
	{
		return ready;
	}


private:
	size_t read = 0;
	size_t size = 0;
	std::atomic_bool ready = false;
	std::array<std::byte, 1024> buffer;

} manager;


void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
	mqtt_manager* manager = reinterpret_cast<mqtt_manager*>(arg);
	manager->push_incoming(
		std::span<const std::byte>(
			reinterpret_cast<const std::byte*>(data), len));

	if (flags == MQTT_DATA_FLAG_LAST)
	{
		// FIXME send notification to task that data is ready?

	}
}

void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len)
{
	mqtt_manager* manager = reinterpret_cast<mqtt_manager*>(arg);
	manager->start_incoming(tot_len);
}

static void mqtt_sub_request_cb(void *arg, err_t result)
{
	/* Just print the result code here for simplicity,
	normal behaviour would be to take some action if subscribe fails like
	notifying user, retry subscribe or disconnect from server */
	printf("Subscribe result: %d\r\n", result);
}

void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
{
	if (status == MQTT_CONNECT_ACCEPTED)
	{
		mqtt_set_inpub_callback(client, mqtt_incoming_publish_cb, mqtt_incoming_data_cb, arg);
		err_t error = mqtt_subscribe(client, "pcrb", 2, mqtt_sub_request_cb, arg);

		//FIXME error?
	}
	else
	{
		printf("4AAA\r\n");
		// FIXME what do?
	}

}

err_t do_connect(mqtt_client_t *client)
{
	mqtt_connect_client_info_t client_info = {};
	client_info.client_id = "pcrb_";
	// FIXME do DNS lookup for obsidian?
	ip_addr_t ip;
	IP4_ADDR(&ip, 192, 168, 5, 123);
	return mqtt_client_connect(client, &ip, MQTT_PORT, mqtt_connection_cb, &manager, &client_info);
}

namespace pcrb
{

void mqtt_task(void*)
{
	mqtt_client_t m_client = {};
	err_t error = do_connect(&m_client);
	printf("mqtt: %i\r\n", error);
	for(;;)
	{
		if (manager.is_ready())
		{
			std::span<const std::byte> data = manager.finalize_incoming();
			printf("%s\r\n", reinterpret_cast<const char*>(data.data()));
		}
	}
}

}
