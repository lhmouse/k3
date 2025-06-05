// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#ifndef K32_AGENT_GLOBALS_
#define K32_AGENT_GLOBALS_

#include "../fwd.hpp"
#include "../common/service.hpp"
#include "../common/clock.hpp"
#include "account_supervisor.hpp"
namespace k32::agent {

extern Service service;
extern Clock clock;
extern Account_Supervisor account_supervisor;

}  // namespace k32::agent
#endif
