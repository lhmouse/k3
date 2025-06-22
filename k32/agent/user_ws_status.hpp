// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#ifndef K32_AGENT_USER_WS_STATUS_
#define K32_AGENT_USER_WS_STATUS_

#include "../fwd.hpp"
namespace k32::agent {

enum User_WS_Status : uint16_t
  {
    user_ws_status_authentication_failure    = 4301,
    user_ws_status_login_conflict            = 4302,
    user_ws_status_unknown_opcode            = 4303,
    user_ws_status_message_rate_limit        = 4304,
    user_ws_status_ping_timeout              = 4305,
  };

}  // namespace k32::agent
#endif
