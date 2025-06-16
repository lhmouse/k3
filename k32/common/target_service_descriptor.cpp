// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#include "../xprecompiled.hpp"
#define K32_FRIENDS_5B7AEF1F_484C_11F0_A2E3_5254005015D2_
#include "target_service_descriptor.hpp"
namespace k32 {

Target_Service_Descriptor::
~Target_Service_Descriptor()
  {
  }

extern constexpr ::poseidon::UUID multicast_uuid
  = POSEIDON_UUID(903f1bf8,13a6,4190,f44d,b82b1b54ca61);

Target_Service_Descriptor
multicast(const cow_string& service_type)
  {
    Target_Service_Descriptor target;
    target.service_uuid = multicast_uuid;
    target.service_type = service_type;
    return target;
  }

extern constexpr ::poseidon::UUID randomcast_uuid
  = POSEIDON_UUID(141451c5,1775,4107,f9f1,f0594e621ed4);

Target_Service_Descriptor
randomcast(const cow_string& service_type)
  {
    Target_Service_Descriptor target;
    target.service_uuid = randomcast_uuid;
    target.service_type = service_type;
    return target;
  }

extern constexpr ::poseidon::UUID broadcast_uuid
  = POSEIDON_UUID(789665cc,6fd8,4fb6,e79e,18173472cd9f);

Target_Service_Descriptor
broadcast() noexcept
  {
    Target_Service_Descriptor target;
    target.service_uuid = broadcast_uuid;
    return target;
  }

}  // namespace k32
