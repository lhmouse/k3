// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#ifndef K32_COMMON_REMOTE_SERVICE_INFORMATION_
#define K32_COMMON_REMOTE_SERVICE_INFORMATION_

#include "../fwd.hpp"
#include <poseidon/socket/ipv6_address.hpp>
namespace k32 {

struct Remote_Service_Information
  {
    ::poseidon::UUID service_uuid;
    cow_string service_type;
    cow_string hostname;
    cow_vector<::poseidon::IPv6_Address> addresses;

#ifdef K32_FRIENDS_5B7AEF1F_484C_11F0_A2E3_5254005015D2_
    Remote_Service_Information() noexcept = default;
#endif
    Remote_Service_Information(const Remote_Service_Information&) = default;
    Remote_Service_Information(Remote_Service_Information&&) = default;
    Remote_Service_Information& operator=(const Remote_Service_Information&) & = default;
    Remote_Service_Information& operator=(Remote_Service_Information&&) & = default;
    ~Remote_Service_Information();
  };

}  // namespace k32
#endif
