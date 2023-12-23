// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2023
/// @file

#ifndef SERVER_H_
#define SERVER_H_

#include <pico/stdlib.h>

#include <lwip/dns.h>
#include <lwip/pbuf.h>
#include <lwip/udp.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>

#include <string>
#include <cstdint>

namespace pc_remote_button
{
	class socket
	{
	public:
		socket()
		:socket_(-1)
		{}

		socket(int sock)
		:socket_(sock)
		{}

		~socket()
		{
			if (socket_ != -1)
			{
				close(socket_);
			}
		}

		socket(socket&& sock)
		{
			std::swap(socket_, sock.socket_);
		}

		socket& operator=(socket&& sock)
		{
			std::swap(socket_, sock.socket_);
			return *this;
		}

		socket(const socket&) = delete;
		socket& operator=(const socket&) = delete;

		int get()
		{
			return socket_;
		}
	private:
		int socket_;
	};

	class server
	{
	public:
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
			int s = ::socket(result->ai_family, result->ai_socktype, result->ai_protocol);
			socket_ipv4 = socket(s);
			bind(socket_ipv4.get(), result->ai_addr, result->ai_addrlen);
			freeaddrinfo(result);
			::listen(s, 1);

			return true;
		}

		socket accept()
		{
			struct sockaddr_storage remote_addr;
			socklen_t addr_size = sizeof(remote_addr);
			socket req_socket(::accept(socket_ipv4.get(), reinterpret_cast<sockaddr*>(&remote_addr), &addr_size));

			return req_socket;
		}

		static int32_t handle_request(socket&& sock)
		{
			int32_t result = -1;
			char buffer[16] = {};
			ssize_t amount = recv(sock.get(), buffer, 4, 0);
			if (amount != 4)
			{
				return result;
			}
			memcpy(&result, buffer, sizeof(result));
			result = ntohl(result);

			return result;
		}

	private:
		socket socket_ipv4{-1};
	};

}

#endif//SERVER_H_
