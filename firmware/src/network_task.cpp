// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2023 - 2024
/// @file

#include <pcrb/network_task.h>
#include <pcrb/switch_task.h>
#include <pcrb/server.h>
#include <pcrb/request_handler.h>
#include <pcrb/usb.h>

#include <lwip/sockets.h>

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
				sys_log.push(std::format("unable to listen on server, error {}", strerror(err)));
			}
		} while (err != 0);

		for(;;)
		{
			auto accept_result = server_.accept();
			if (!accept_result)
			{
				sys_log.push(std::format("unable to accept socket, error {}", strerror(accept_result.error())));
				// FIXME what if the error is terminal? Are there any terminal errors?
				continue;
			}
			sys_log.push("new connection accepted");
			std::array<std::byte, 1024> data;
			request_handler handler(std::move(*accept_result));
			auto request_result = handler.read(std::span(data));
			if (request_result)
			{
				// First 4 bytes are a magic field, followed by a 4 byte
				// request. Additional bytes may be required by the request
				// type.
				size_t amount = request_result.value();
				if (amount < 8)
				{
					auto explanation = std::format("Received bad network request with size {}", amount);
					sys_log.push(explanation);
					handler.send(explanation);
					continue;
				}
				uint32_t magic;
				memcpy(&magic, data.data(), 4);
				magic = ntoh(magic);
				if (magic != 0x416E614D)
				{
					auto explanation = std::format("Received bad network request, bad magic {}", magic);
					sys_log.push(explanation);
					handler.send(explanation);
					continue;
				}

				uint32_t request;
				memcpy(&request, data.data() + 4, 4);
				request = ntoh(request);
				switch (request)
				{
					case 0: // toggle, an additional 4 byte field
					{
						if (amount != 12)
						{
							auto explanation = std::format("Received bad network request, bad size {}", amount);
							sys_log.push(explanation);
							handler.send(explanation);
							continue;
						}
						uint32_t time;
						memcpy(&time, data.data() + 8, 4);
						time = ntoh(time);
						auto explanation = std::format("Received network toggle request {}", time);
						sys_log.push(explanation);
						handler.send(explanation);
						xQueueSendToBack(switch_comms.get(), &request, 0);
						break;
					}
					case 1:
					{
						if (amount != 8)
						{
							auto explanation = std::format("Received bad network request, bad size {}", amount);
							sys_log.push(explanation);
							handler.send(explanation);
							continue;

						}
						auto explanation = std::format("boot select: {}", pcrb::get_boot_select());
						sys_log.push(explanation);
						handler.send(explanation);
						break;
					}
					case 2:
					{
						if (amount != 12)
						{
							auto explanation = std::format("Received bad network request, bad size {}", amount);
							sys_log.push(explanation);
							handler.send(explanation);
							continue;
						}
						uint32_t select;
						memcpy(&select, data.data() + 8, 4);
						select = ntoh(select);
						auto explanation = std::format("Received boot select request {}, ", select);
						sys_log.push(explanation);
						handler.send(explanation);
						pcrb::set_boot_select(select);
						explanation = std::format("boot select: {}", pcrb::get_boot_select());
						sys_log.push(explanation);
						handler.send(explanation);
						break;
					}
					default:
					{
						auto explanation = std::format("Received bad network request, unknown command {}", request);
						sys_log.push(explanation);
						handler.send(explanation);
						continue;
					}
				}
			}
			else
			{
				const char *err = request_result.error() == EAGAIN ? "timeout": strerror(request_result.error());
				auto explanation = std::format("failed to handle request: {}", err);
				sys_log.push(explanation);
				handler.send(explanation);
			}
		}

		server_.close();
	}
}

}
