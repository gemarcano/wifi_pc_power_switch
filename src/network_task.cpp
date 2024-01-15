// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2023 - 2024
/// @file

#include <network_task.h>
#include <switch_task.h>
#include <server.h>

#include <cstdint>
#include <cstdio>

void network_task(void*)
{
	// Loop endlessly, restarting the server if there are errors
	pc_remote_button::server server_;

	for(;;)
	{
		// FIXME maybe move wifi initialization here?
		int err;
		do
		{
			err = server_.listen(48686);
			if (err != 0)
			{
				fprintf(stderr, "unable to listen on server, error %i\r\n", err);
			}
		} while (err != 0);

		for(;;)
		{
			auto result = server_.accept();
			if (!result)
			{
				fprintf(stderr, "unable to accept socket, error %i\r\n", result.error());
				// FIXME what if the error is terminal? Are there any terminal errors?
				break;
			}
			int32_t data = server_.handle_request(std::move(*result));
			xQueueSendToBack(switch_comms.get(), &data, 0);
		}

		server_.close();
	}
}


