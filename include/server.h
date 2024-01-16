// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2023 - 2024
/// @file

#ifndef SERVER_H_
#define SERVER_H_

#include <cstdint>
#include <memory>
#include <expected>

// forward declaration?
struct addrinfo;

namespace pc_remote_button
{
	class socket
	{
	public:
		socket();

		socket(int sock);

		~socket();

		void shutdown();

		void close();

		socket(socket&& sock);

		socket& operator=(socket&& sock);

		socket(const socket&) = delete;
		socket& operator=(const socket&) = delete;

		int get();
	private:
		int socket_;
	};

	class addrinfo_deleter
	{
	public:
		void operator()(addrinfo *info);
	};

	using addrinfo_ptr = std::unique_ptr<addrinfo, addrinfo_deleter>;

	class server
	{
	public:
		int listen(uint16_t port);

		std::expected<socket, int> accept();

		static std::expected<unsigned, int> handle_request(socket sock);

		void close();

	private:
		socket socket_ipv4;
	};
}

#endif//SERVER_H_
