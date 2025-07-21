#include <pcrb/mqtt.h>

#include <lwip/apps/mqtt.h>
#include <lwip/apps/mqtt_priv.h>


void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
	printf("WE GOT DATA %s\r\n", data);

}

void mqtt_incoming_publish_cb(void *arg, const char *topic, u32_t tot_len)
{
	printf("WE GOT DATA?\r\n");
}

static void mqtt_sub_request_cb(void *arg, err_t result)
{
	/* Just print the result code here for simplicity,
	normal behaviour would be to take some action if subscribe fails like
	notifying user, retry subscribe or disconnect from server */
	printf("Subscribe result: %d\n", result);
}

void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status)
{
	printf("2AAA\r\n");
	if (status == MQTT_CONNECT_ACCEPTED)
	{
		printf("3AAA\r\n");
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
	return mqtt_client_connect(client, &ip, MQTT_PORT, mqtt_connection_cb, 0, &client_info);
}

namespace pcrb
{

void mqtt_task(void*)
{
	mqtt_client_t m_client = {};
	err_t error = do_connect(&m_client);
	printf("mqtt: %i\r\n", error);
	for(;;)
	{}
}

}
