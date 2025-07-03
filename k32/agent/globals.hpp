// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#ifndef K32_AGENT_GLOBALS_
#define K32_AGENT_GLOBALS_

#include "../fwd.hpp"
#include "../common/static/clock.hpp"
#include "../common/static/service.hpp"
#include "static/user_service.hpp"
namespace k32::agent {

extern Clock clock;
extern Service service;
extern User_Service user_service;

}  // namespace k32::agent
#endif
