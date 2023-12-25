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
#include <memory>
#include <expected>

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
				shutdown();
				close();
			}
		}

		void shutdown()
		{
			::shutdown(socket_, SHUT_RDWR);
		}

		void close()
		{
			::close(socket_);
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

	class addrinfo_deleter
	{
	public:
		void operator()(addrinfo *info)
		{
			if (info)
				freeaddrinfo(info);
		}
	};

	using addrinfo_ptr = std::unique_ptr<addrinfo, addrinfo_deleter>;

	class server
	{
	public:

		~server()
		{
			if (socket_ipv4.get() != -1)
			{
				close();
			}
		}

		int listen(uint16_t port)
		{
			// Look up IP address of NTP server first, in case we're looking at an
			// NTP pool
			const addrinfo hints = {
				.ai_flags = 0,
				.ai_family = AF_UNSPEC,
				.ai_socktype = SOCK_STREAM,
				.ai_protocol = 0,
			};
			addrinfo_ptr result = NULL;
			addrinfo *result_ = NULL;
			int err = getaddrinfo("0.0.0.0", std::to_string(port).c_str(), &hints, &result_);
			if (err == -1)
				return errno;
			result.reset(result_);
			result_ = NULL;

			socket sock{::socket(result->ai_family, result->ai_socktype, result->ai_protocol)};
			if (sock.get() == -1)
				return errno;

			err = bind(sock.get(), result->ai_addr, result->ai_addrlen);
			if (err == -1)
				return errno;

			// FIXME Should we only have a queue depth of 1?
			err = ::listen(sock.get(), 1);
			if (err == -1)
				return errno;

			socket_ipv4 = std::move(sock);

			return 0;
		}

		std::expected<socket, int> accept()
		{
			struct sockaddr_storage remote_addr;
			socklen_t addr_size = sizeof(remote_addr);
			int sock = ::accept(socket_ipv4.get(), reinterpret_cast<sockaddr*>(&remote_addr), &addr_size);
			if (sock == -1)
				return std::unexpected(errno);
			return socket(sock);
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

		void close()
		{
			socket_ipv4.shutdown();
			socket_ipv4.close();
		}

	private:
		socket socket_ipv4{-1};
	};

}

#endif//SERVER_H_
