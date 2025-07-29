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

/** Queries a DNS server for the name provided.
 *
 * The DNS server used needs to be configured elsewhere (usually via DHCP).
 *
 * @param[in] hostname Hostname to resolve.
 *
 * @returns A unique_ptr to an addrinfo struct as the DNS response, or an error
 *  code in the case of an error.
 */
std::expected<addrinfo_ptr, int> dns_query(std::string_view hostname);

/** Extract a single IP address from a given addrinfo unique_ptr.
 *
 * This returns the first IP address in the addrinfo linked list.
 *
 * @param[in] ptr addrinfo unique_ptr to extract an IP address from.
 *
 * @returns An IP address as an ip_addr_t, or nothing if there are no valid IP
 *  addresses in the addrinfo provided.
 */
std::optional<ip_addr_t> extract_ip_address(addrinfo_ptr& ptr);

}

#endif//PCRB_DNS_H_
