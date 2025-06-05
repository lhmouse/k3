// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#ifndef K32_AGENT_ACCOUNT_SUPERVISOR_
#define K32_AGENT_ACCOUNT_SUPERVISOR_

#include "../fwd.hpp"
namespace k32 {

class Account_Supervisor
  {
  public:
    Account_Supervisor();
    Account_Supervisor(const Account_Supervisor&) = delete;
    Account_Supervisor& operator=(const Account_Supervisor&) & = delete;
    ~Account_Supervisor();

  };

}  // namespace k32
#endif
