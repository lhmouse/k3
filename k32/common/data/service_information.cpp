// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#include "../../xprecompiled.hpp"
#define K32_FRIENDS_5B7AEF1F_484C_11F0_A2E3_5254005015D2_
#include "service_information.hpp"
namespace k32 {

const Service_Information& Service_Information::null = *new Service_Information();

Service_Information::
~Service_Information()
  {
  }

}  // namespace k32
