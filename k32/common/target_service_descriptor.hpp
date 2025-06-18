// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#ifndef K32_COMMON_TARGET_SERVICE_DESCRIPTOR_
#define K32_COMMON_TARGET_SERVICE_DESCRIPTOR_

#include "../fwd.hpp"
namespace k32 {

struct Target_Service_Descriptor
  {
    ::poseidon::UUID service_uuid;
    cow_string service_type;

#ifdef K32_FRIENDS_5B7AEF1F_484C_11F0_A2E3_5254005015D2_
    Target_Service_Descriptor() noexcept = default;
#endif
    Target_Service_Descriptor(const Target_Service_Descriptor&) = default;
    Target_Service_Descriptor(Target_Service_Descriptor&&) = default;
    Target_Service_Descriptor& operator=(const Target_Service_Descriptor&) & = default;
    Target_Service_Descriptor& operator=(Target_Service_Descriptor&&) & = default;
    ~Target_Service_Descriptor();

    explicit operator bool() const noexcept { return !this->service_uuid.is_nil();  }
  };

// `multicast_uuid(service_type)` causes the message to be sent to all
// instance of `service_type`.
extern const ::poseidon::UUID multicast_uuid;

Target_Service_Descriptor
multicast(const cow_string& service_type);

// `randomcast(service_type)` causes the message to be sent to a random
// instance of `service_type`.
extern const ::poseidon::UUID randomcast_uuid;

Target_Service_Descriptor
randomcast(const cow_string& service_type);

// `broadcast()` causes the message to be sent to all instances. This should
// be used with caution.
extern const ::poseidon::UUID broadcast_uuid;

Target_Service_Descriptor
broadcast() noexcept;

}  // namespace k32
#endif
