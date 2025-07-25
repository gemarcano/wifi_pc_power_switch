// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2025
/// @file

#include <pcrb/server.h>

#include <lwip/netdb.h>

#include <expected>
#include <array>
#include <algorithm>
#include <optional>

namespace pcrb
{

std::expected<addrinfo_ptr, int> dns_query(std::string_view hostname)
{
	std::array<char, 257> buffer = {};
	std::copy_n(hostname.begin(), std::min(buffer.size(), hostname.size()), buffer.begin());
	addrinfo_ptr result;
	int err = getaddrinfo(buffer.data(), nullptr, nullptr, std::out_ptr(result));
	if (err == -1)
		return std::unexpected(errno);
	return result;
}

std::optional<ip_addr_t> extract_ip_address(addrinfo_ptr& ptr)
{
	for (addrinfo *p = ptr.get(); p != nullptr; p = p->ai_next)
	{
		switch (p->ai_family)
		{
			case AF_INET:
			{
				ip_addr_t result;
				auto *v4 = reinterpret_cast<sockaddr_in*>(p->ai_addr);
				ip4_addr_t ip;
				ip.addr = v4->sin_addr.s_addr;
				ip_addr_copy_from_ip4(result, ip);
				return result;
			}
			break;

#if LWIP_IPV6
			case AF_INET6:
			{
				ip_addr_t result;
				auto *v6 = reinterpret_cast<sockaddr_in6*>(p->ai_addr);
				ip6_addr_t ip;
				std::copy_n(v6->sin6_addr.s6_addr, sizeof(ip.addr), ip.addr);
				ip6.zone = 0;
				ip_addr_copy_from_ip6(result, ip);
				return result;
			}
			break;
#endif

			default:
				continue;
		}
	}
	return std::nullopt;
}

}
