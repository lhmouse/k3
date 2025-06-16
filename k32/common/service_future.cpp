// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#include "../xprecompiled.hpp"
#define K32_FRIENDS_5B7AEF1F_484C_11F0_A2E3_5254005015D2_
#include "service_future.hpp"
namespace k32 {

Service_Future::
Service_Future(const Target_Service_Descriptor& target_descriptor,
               const cow_string& opcode, const ::taxon::Value& request_data)
  {
    this->m_target_service_uuid = target_descriptor.service_uuid;
    this->m_target_service_type = target_descriptor.service_type;
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
