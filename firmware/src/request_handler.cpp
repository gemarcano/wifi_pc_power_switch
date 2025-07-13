// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2025
/// @file

#include <pcrb/request_handler.h>
#include <pcrb/server.h>

#include <gpico/log.h>

#include <lwip/sockets.h>

#include <string>
#include <format>
#include <vector>
#include <span>

#include <errno.h>
#undef errno
extern int errno;

using gpico::sys_log;

namespace pcrb
{

request_handler::request_handler(socket socket_)
:socket_(std::move(socket_))
{}

std::expected<std::size_t, int> request_handler::read(std::span<std::byte> data)
{
	uint16_t size = 0;
	for (ssize_t amount = 0, received = 0; received < 2; received += amount)
	{
		amount = recv(socket_.get(), reinterpret_cast<std::byte*>(&size) + received, 2 - received, 0);
		if (amount == -1)
		{
			return std::unexpected(errno);
		}
	}
	size = ntoh(size);

	size = std::min<uint16_t>(size, data.size());

	for (size_t amount = 0, received = 0; received < size; received += amount)
	{
		amount = recv(socket_.get(), data.data() + received, size - received, 0);
		if (amount == -1)
		{
			return std::unexpected(errno);
		}
	}

	return size;
}

int request_handler::send(std::span<std::byte> data)
{
	return ::send(socket_.get(), data.data(), data.size(), 0);
}

int request_handler::send(std::string_view data)
{
	return ::send(socket_.get(), data.data(), data.size(), 0);
}

}
