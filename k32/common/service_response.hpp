// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#ifndef K32_COMMON_SERVICE_RESPONSE_
#define K32_COMMON_SERVICE_RESPONSE_

#include "../fwd.hpp"
namespace k32 {

struct Service_Response
  {
    ::poseidon::UUID service_uuid;
    ::poseidon::UUID request_uuid;
    ::taxon::Value response_data;
    cow_string error;
    bool complete = false;

#ifdef K32_FRIENDS_5B7AEF1F_484C_11F0_A2E3_5254005015D2_
    Service_Response() noexcept = default;
#endif
    Service_Response(const Service_Response&) = default;
    Service_Response(Service_Response&&) = default;
    Service_Response& operator=(const Service_Response&) & = default;
    Service_Response& operator=(Service_Response&&) & = default;
    ~Service_Response();

    explicit operator bool() const noexcept { return this->complete && this->error.empty();  }
  };

extern const Service_Response null_service_response;

}  // namespace k32
#endif
