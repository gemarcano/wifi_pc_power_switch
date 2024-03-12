// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2023 - 2024
/// @file

#include <pcrb/log.h>
#include <pcrb/syslog.h>

namespace pcrb
{

safe_syslog<syslog<1024*128>> sys_log;

}
