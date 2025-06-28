// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#include "../../xprecompiled.hpp"
#define K32_FRIENDS_FC7DDE3B_4A8E_11F0_BB68_5254005015D2_
#include "user_information.hpp"
namespace k32 {

const User_Information& User_Information::null = *new User_Information();

User_Information::
~User_Information()
  {
  }

}  // namespace k32
