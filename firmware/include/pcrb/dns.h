// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2025
/// @file

#ifndef PCRB_DNS_H_
#define PCRB_DNS_H_

#include <pcrb/server.h>

#include <lwip/netdb.h>

#include <expected>
#include <optional>
#include <string_view>

namespace pcrb
{

std::expected<addrinfo_ptr, int> dns_query(std::string_view hostname);
std::optional<ip_addr_t> extract_ip_address(addrinfo_ptr& ptr);

}

#endif//PCRB_DNS_H_
