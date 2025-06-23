// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#ifndef K32_AGENT_GLOBALS_
#define K32_AGENT_GLOBALS_

#include "../fwd.hpp"
#include "user_service.hpp"
#include "../common/clock.hpp"
#include "../common/service.hpp"
namespace k32::agent {

extern Clock& clock;
extern Service& service;
extern User_Service& user_service;

}  // namespace k32::agent
#endif
