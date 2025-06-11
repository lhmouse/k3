// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#include "../xprecompiled.hpp"
#include "response_future.hpp"
namespace k32 {

Response_Future::
Response_Future(const ::poseidon::UUID& service_uuid)
  {
    this->m_service_uuid = service_uuid;
  }

Response_Future::
~Response_Future()
  {
  }

}  // namespace k32
