// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#include "../../xprecompiled.hpp"
#define K32_FRIENDS_6A8BAC8C_B8F6_4BDA_BD7F_B90D5BF07B81_
#include "user_information.hpp"
namespace k32 {

const User_Information& User_Information::null = *new User_Information();

User_Information::
~User_Information()
  {
  }

}  // namespace k32
