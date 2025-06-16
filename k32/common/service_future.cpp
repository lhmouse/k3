// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#include "../xprecompiled.hpp"
#include "service_future.hpp"
namespace k32 {

constexpr ::poseidon::UUID multicast_uuid = POSEIDON_UUID(903f1bf8,13a6,4190,f44d,b82b1b54ca61);
constexpr ::poseidon::UUID randomcast_uuid = POSEIDON_UUID(141451c5,1775,4107,f9f1,f0594e621ed4);
constexpr ::poseidon::UUID broadcast_uuid = POSEIDON_UUID(789665cc,6fd8,4fb6,e79e,18173472cd9f);

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
