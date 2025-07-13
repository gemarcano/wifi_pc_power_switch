// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2023 - 2024
/// @file

#include <pcrb/network_task.h>
#include <pcrb/switch_task.h>
#include <pcrb/server.h>

#include <gpico/log.h>

#include <cstdint>
#include <cstdio>
#include <format>
#include <cstring>

using gpico::sys_log;

namespace pcrb
{

void network_task(void*)
{
	// Loop endlessly, restarting the server if there are errors
	server server_;

	for(;;)
	{
		// FIXME maybe move wifi initialization here?
		int err;
		do
		{
			err = server_.listen(48686);
			if (err != 0)
			{
				sys_log.push(std::format("unable to listen on server, error {}", err));
			}
		} while (err != 0);

		for(;;)
		{
			auto accept_result = server_.accept();
			if (!accept_result)
			{
				sys_log.push(std::format("unable to accept socket, error {}", accept_result.error()));
				// FIXME what if the error is terminal? Are there any terminal errors?
				continue;
			}
			sys_log.push("new connection accepted");
			std::array<std::byte, 1024> data;
			auto request_result = server_.handle_request(std::move(*accept_result), std::span(data));
			if (request_result)
			{
				// First 4 bytes are a magic field, followed by a 4 byte
				// request. Additional bytes may be required by the request
				// type.
				size_t amount = request_result.value();
				if (amount < 8)
				{
					sys_log.push(std::format("Received bad network request with size {}", amount));
					continue;
				}
				uint32_t magic;
				memcpy(&magic, data.data(), 4);
				magic = ntoh(magic);
				if (magic != 0x416E614D)
				{
					sys_log.push(std::format("Received bad network request, bad magic {}", magic));
					continue;
				}

				uint32_t request;
				memcpy(&request, data.data() + 4, 4);
				request = ntoh(request);
				switch (request)
				{
					case 0: // toggle, an additional 4 byte field
						if (amount != 12)
						{
							sys_log.push(std::format("Received bad network request, bad size {}", amount));
							continue;
						}
						memcpy(&request, data.data() + 8, 4);
						request = ntoh(request);
						sys_log.push(std::format("Received network toggle request {}", request));
						xQueueSendToBack(switch_comms.get(), &request, 0);
						break;
					default:
						sys_log.push(std::format("Received bad network request, unknown command {}", request));
						continue;
				}
			}
			else
			{
				sys_log.push(std::format("failed to handle request: {}", request_result.error()));
			}
		}

		server_.close();
	}
}

}
