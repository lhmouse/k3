// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#ifndef K32_COMMON_DATA_SERVICE_INFORMATION_
#define K32_COMMON_DATA_SERVICE_INFORMATION_

#include "../../fwd.hpp"
namespace k32 {

struct Service_Information
  {
    ::poseidon::UUID service_uuid;
    cow_string service_type;
    uint32_t service_index = 0;
    cow_string hostname;
    cow_vector<::poseidon::IPv6_Address> addresses;

#ifdef K32_FRIENDS_5B7AEF1F_484C_11F0_A2E3_5254005015D2_
    Service_Information() noexcept = default;
#endif
    Service_Information(const Service_Information&) = default;
    Service_Information(Service_Information&&) = default;
    Service_Information& operator=(const Service_Information&) & = default;
    Service_Information& operator=(Service_Information&&) & = default;
    ~Service_Information();

    explicit operator bool() const noexcept { return this->service_uuid.is_nil() == false;  }
  };

extern const Service_Information& null_service_information;

}  // namespace k32
#endif
