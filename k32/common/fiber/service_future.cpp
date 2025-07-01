// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#include "../../xprecompiled.hpp"
#define K32_FRIENDS_5B7AEF1F_484C_11F0_A2E3_5254005015D2_
#include "service_future.hpp"
namespace k32 {

constexpr ::poseidon::UUID multicast_uuid   = POSEIDON_UUID(b2b697c4,23d2,43f2,9704,5a086fbae5f2);
constexpr ::poseidon::UUID randomcast_uuid  = POSEIDON_UUID(e958b64a,9481,4ccd,a04b,9618db2f0ce0);
constexpr ::poseidon::UUID broadcast_uuid   = POSEIDON_UUID(9223dadc,db13,4862,9f05,cd24c201a54e);
constexpr ::poseidon::UUID loopback_uuid    = POSEIDON_UUID(19e9f082,d46d,48e7,a2f7,760c1ed5c8fe);

Service_Future::
Service_Future(multicast_selector_t&& selector, const phcow_string& opcode,
               const ::taxon::V_object& request)
  {
    this->m_target_service_uuid = selector.target_service_uuid;
    this->m_target_service_type = move(selector.target_service_type);
    this->m_opcode = opcode;
    this->m_request = request;
  }

Service_Future::
Service_Future(const ::poseidon::UUID& target_service_uuid,
               const phcow_string& opcode, const ::taxon::V_object& request)
  {
    this->m_target_service_uuid = target_service_uuid;
    this->m_opcode = opcode;
    this->m_request = request;
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
