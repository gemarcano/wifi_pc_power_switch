// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2025
/// @file

#ifndef PCRB_REQUEST_HANDLER_H_
#define PCRB_REQUEST_HANDLER_H_

#include <cstdint>
#include <memory>
#include <expected>
#include <vector>
#include <span>
#include <string_view>

#include <pcrb/server.h>

// forward declaration of addrinfo, so we don't need to pull in the complete
// networking headers
struct addrinfo;

namespace pcrb
{

class request_handler
{
public:
	request_handler(socket socket_);

	std::expected<std::size_t, int> read(std::span<std::byte> data);
	int send(std::span<std::byte> data);
	int send(std::string_view data);
private:
	socket socket_;
};

}

#endif//PCRB_REQUEST_HANDLER_H_
