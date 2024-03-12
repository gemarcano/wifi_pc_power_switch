// SPDX-License-Identifier: GPL-2.0-or-later OR LGPL-2.1-or-later
// SPDX-FileCopyrightText: Gabriel Marcano, 2023
/// @file

#ifndef PCRB_LOG_H_
#define PCRB_LOG_H_

#include <pcrb/syslog.h>

namespace pcrb
{

extern safe_syslog<syslog<1024*128>> sys_log;

}

#endif//PCRB_LOG_H_
