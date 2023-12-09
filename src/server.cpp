// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// Copyright: Gabriel Marcano, 2023
/// @file

#include <pico/stdlib.h>

#include <lwip/dns.h>
#include <lwip/pbuf.h>
#include <lwip/udp.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>

#include <string>
#include <cstdint>

bool listen(uint16_t port)
{
	// Look up IP address of NTP server first, in case we're looking at an
	// NTP pool
	const addrinfo hints = {
		.ai_flags = 0,
		.ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
		.ai_protocol = 0,
	};
	addrinfo *result = NULL;
	int err = getaddrinfo("0.0.0.0", std::to_string(port).c_str(), &hints, &result);
	int s = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	bind(s, result->ai_addr, result->ai_addrlen);
	freeaddrinfo(result);
	::listen(s, 1);

	socket_ipv4 = s;
	return true;
}

int32_t handle_request()
{
	int32_t result = -1;
	struct sockaddr_storage remote_addr;
	socklen_t addr_size = sizeof(remote_addr);
	int req_socket = accept(socket_ipv4, reinterpret_cast<sockaddr*>(&remote_addr), &addr_size);

	char buffer[16] = {};
	ssize_t amount = recv(req_socket, buffer, 4, 0);
	if (amount != 4)
	{
		goto terminate;
	}
	memcpy(&result, buffer, sizeof(result));
	result = ntohl(result);

terminate:
	close(req_socket);
	return result;
}
