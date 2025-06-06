// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#ifndef K32_COMMON_MESSAGE_
#define K32_COMMON_MESSAGE_

#include "../fwd.hpp"
namespace k32 {

class Message
  {
  public:
    Message() noexcept;
    Message(const Message&) noexcept = default;
    Message(Message&&) noexcept = default;
    Message& operator=(const Message&) & noexcept = default;
    Message& operator=(Message&&) & noexcept = default;
    ~Message();

  };

}  // namespace k32
#endif
