// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// Copyright: Gabriel Marcano, 2023
/// @file

extern "C" {
#include <FreeRTOS.h>
#include <task.h>

#include <pico/stdlib.h>
#include <pico/cyw43_arch.h>

#include <lwip/dns.h>
#include <lwip/pbuf.h>
#include <lwip/udp.h>
}

#include <cstring>
#include <ctime>
#include <memory>
#include <expected>
#include <atomic>

constexpr const char* NTP_SERVER = "pool.ntp.org";
constexpr const int NTP_MSG_LEN = 48;
constexpr const int NTP_PORT = 123;
constexpr const int NTP_DELTA = 2208988800; // seconds between 1 Jan 1900 and 1 Jan 1970
constexpr const int ntpEST_TIME = (30 * 1000);
constexpr const int NTP_FAIL_TIME = (10 * 1000);

// This secrets.h includes strings for WIFI_SSID and WIFI_PASSWORD
#include "secrets.h"

class ntp_client
{
public:
	ntp_client(std::unique_ptr<udp_pcb>&& ntp_net_control)
	:ntp_pcb(std::move(ntp_net_control))
	{
		// Register callback of what to do on receipt
		udp_recv(ntp_pcb.get(), &ntp_client::recv, this);
	}

	int request()
	{
		// FIXME this is race-y-- two threads can both check and see that there
		// are no on-going requests, and then proceed. The Pico doesn't have
		// test and set instructions...
		if (ongoing_request)
			return 1;
		ongoing_request = true;

		// Look up IP address of NTP server first, in case we're looking at an
		// NTP pool
		std::expected<ip_addr_t, const char*> lookup = dns_lookup();
		if (!lookup.has_value())
		{
			printf("%s", lookup.error());
			result(-1, NULL);
			return -1;
		}

		// Set alarm in case udp requests are lost
		ntp_resend_alarm = add_alarm_in_ms(NTP_FAIL_TIME, &ntp_client::failed_handler, this, true);
		auto address = lookup.value();

		cyw43_arch_lwip_begin();
		struct pbuf *packet = pbuf_alloc(PBUF_TRANSPORT, NTP_MSG_LEN, PBUF_RAM);
		uint8_t *request = (uint8_t *) packet->payload;
		memset(request, 0, NTP_MSG_LEN);
		request[0] = 0x1b;
		udp_sendto(ntp_pcb.get(), packet, &address, NTP_PORT);
		pbuf_free(packet);
		cyw43_arch_lwip_end();

		ntp_timeout_time = make_timeout_time_ms(ntpEST_TIME);

		return 0;
	}
	
	bool time_elapsed() const
	{
		return absolute_time_diff_us(get_absolute_time(), ntp_timeout_time) < 0;
	}

private:
	ip_addr_t ntp_server_address;
	std::atomic_bool dns_request_sent = false;
	std::atomic_bool ongoing_request = false;
	std::unique_ptr<udp_pcb> ntp_pcb;
	absolute_time_t ntp_timeout_time = 0;
	alarm_id_t ntp_resend_alarm;

	void receive(pbuf *packet, const ip_addr_t *address, u16_t port)
	{
		uint8_t mode = pbuf_get_at(packet, 0) & 0x7;
		uint8_t stratum = pbuf_get_at(packet, 1);

		// Check the result
		if (ip_addr_cmp(address, &ntp_server_address) &&
			port == NTP_PORT &&
			packet->tot_len == NTP_MSG_LEN &&
			mode == 0x4 &&
			stratum != 0)
		{
			uint8_t seconds_buf[4] = {};
			pbuf_copy_partial(packet, seconds_buf, sizeof(seconds_buf), 40);
			uint32_t seconds_since_1900 = seconds_buf[0] << 24 | seconds_buf[1] << 16 | seconds_buf[2] << 8 | seconds_buf[3];
			uint32_t seconds_since_1970 = seconds_since_1900 - NTP_DELTA;
			time_t epoch = seconds_since_1970;
			result(0, &epoch);
		}
		else
		{
			printf("invalid ntp response\n");
			result(-1, NULL);
		}
		pbuf_free(packet);

	}

	// NTP data received callback
	static void recv(
		void *arg,
		struct udp_pcb *pcb,
		struct pbuf *packet,
		const ip_addr_t *address,
		u16_t port)
	{
		ntp_client *client = reinterpret_cast<ntp_client*>(arg);
		client->receive(packet, address, port);
	}

	// Call back with a DNS result
	static void dns_found(const char *hostname, const ip_addr_t *ipaddr, void *arg)
	{
		ntp_client *client = reinterpret_cast<ntp_client*>(arg);
		if (ipaddr)
		{
			client->ntp_server_address = *ipaddr;
		}
		else
		{
			// Set IP to "any", as it makes no sense for a DNS request to
			// respond with that, in case of an error.
			ip_addr_set_zero(&client->ntp_server_address);
		}
		client->dns_request_sent = false;
	}

	static int64_t failed_handler(alarm_id_t id, void *user_data)
	{
		ntp_client* client = reinterpret_cast<ntp_client*>(user_data);
		printf("ntp request failed\n");
		client->result(-1, NULL);
		return 0;
	}

	void result(int status, time_t* result)
	{
		if (status == 0 && result) {
			struct tm *utc = gmtime(result);
			printf("got ntp response: %02d/%02d/%04d %02d:%02d:%02d\n", utc->tm_mday, utc->tm_mon + 1, utc->tm_year + 1900,
				   utc->tm_hour, utc->tm_min, utc->tm_sec);
		}

		if (ntp_resend_alarm > 0) {
			cancel_alarm(ntp_resend_alarm);
			ntp_resend_alarm = 0;
		}

		ongoing_request = false;
	}

	std::expected<ip_addr_t, const char*> dns_lookup()
	{
		dns_request_sent = true;

		cyw43_arch_lwip_begin();
		int err = dns_gethostbyname(NTP_SERVER, &ntp_server_address, &ntp_client::dns_found, this);
		cyw43_arch_lwip_end();

		if (err == ERR_INPROGRESS)
		{
			while(dns_request_sent); // FIXME is there a better way to block?
		}
		else
		{
			dns_request_sent = false;
		}

		std::expected<ip_addr_t, const char*> result;
		if (ip_addr_isany_val(ntp_server_address) || (err != ERR_OK && err != ERR_INPROGRESS))
		{
			return result = std::unexpected<const char*>("dns request failed\n");
		}
		return result = ntp_server_address;
	}
};

void main_task(void*)
{
	if (cyw43_arch_init())
	{
		printf("failed to initialise\n");
		return;
	}

	cyw43_arch_enable_sta_mode();

	printf("connecting...\n");
	if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 10000)) {
		printf("failed to connect\n");
		return;
	}
	printf("Connected!\n");

	std::unique_ptr<ntp_client> client;
	{
		auto ntp_pcb = std::unique_ptr<udp_pcb>(udp_new_ip_type(IPADDR_TYPE_ANY));
		if (!ntp_pcb) {
			printf("failed to create pcb\n");
			return;
		}

		client = std::make_unique<ntp_client>(std::move(ntp_pcb));
		if (!client)
		{
			printf("Failed to init ntp_client\n");
			return;
		}
	}

	while(true) {
		if (client->time_elapsed()) {
			while (client->request() == 1);
			printf("High water mark: %lu\n", uxTaskGetStackHighWaterMark(NULL));
		}
		// if you are not using pico_cyw43_arch_poll, then WiFI driver and lwIP work
		// is done via interrupt in the background. This sleep is just an example of some (blocking)
		// work you might be doing.
		vTaskDelay(1000);
	}

	cyw43_arch_deinit();
}

__attribute__((constructor))
void initialization()
{
	stdio_init_all();
}

int main()
{
	TaskHandle_t handle;
	xTaskCreate(main_task, "main", 1024/4, nullptr, tskIDLE_PRIORITY+1, &handle);
	vTaskCoreAffinitySet(handle, (1 << 1) | (1 << 0));
	vTaskStartScheduler();
	for(;;);
	return 0;
}
