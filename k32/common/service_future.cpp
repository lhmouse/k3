// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#include "../xprecompiled.hpp"
#define K32_FRIENDS_5B7AEF1F_484C_11F0_A2E3_5254005015D2_
#include "service_future.hpp"
namespace k32 {

constexpr ::poseidon::UUID multicast_uuid = POSEIDON_UUID(c8585663,4c4f,11f0,b169,5254005015d2);
constexpr ::poseidon::UUID randomcast_uuid = POSEIDON_UUID(c8585775,4c4f,11f0,9b95,5254005015d2);
constexpr ::poseidon::UUID broadcast_uuid = POSEIDON_UUID(c85857a5,4c4f,11f0,b5da,5254005015d2);

Service_Future::
Service_Future(multicast_selector_t&& selector, const cow_string& opcode,
               const ::taxon::Value& request_data)
  {
    this->m_target_service_uuid = selector.target_service_uuid;
    this->m_target_service_type = move(selector.target_service_type);
    this->m_opcode = opcode;
    this->m_request_data = request_data;
  }

Service_Future::
Service_Future(const ::poseidon::UUID& target_service_uuid,
               const cow_string& opcode, const ::taxon::Value& request_data)
  {
    this->m_target_service_uuid = target_service_uuid;
    this->m_opcode = opcode;
    this->m_request_data = request_data;
  }

Service_Future::
~Service_Future()
  {
  }

void
Service_Future::
do_on_abstract_future_initialize()
  {
  }

}  // namespace k32
