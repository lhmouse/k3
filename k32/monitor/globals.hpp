// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#ifndef K32_MONITOR_GLOBALS_
#define K32_MONITOR_GLOBALS_

#include "../fwd.hpp"
#include "../common/static/clock.hpp"
#include "../common/static/service.hpp"
#include "static/role_service.hpp"
namespace k32::monitor {

extern Clock clock;
extern Service service;
extern Role_Service role_service;

}  // namespace k32::monitor
#endif
