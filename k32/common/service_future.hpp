// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#ifndef K32_COMMON_SERVICE_FUTURE_
#define K32_COMMON_SERVICE_FUTURE_

#include "../fwd.hpp"
#include <poseidon/fiber/abstract_future.hpp>
#include <poseidon/base/uuid.hpp>
#include <taxon.hpp>
namespace k32 {

class Service_Future
  :
    public ::poseidon::Abstract_Future
  {
  public:
    struct Target_Descriptor
      {
        ::poseidon::UUID service_uuid;
        cow_string service_type;
        uintptr_t reserved_must_be_zero = 0;
      };

    struct Response
      {
        ::poseidon::UUID service_uuid;
        ::poseidon::UUID request_uuid;
        ::taxon::Value response_data;
        cow_string error;
        bool response_received = false;
      };

  private:
    friend class Service;

    ::poseidon::UUID m_target_service_uuid;
    cow_string m_target_service_type;
    cow_string m_request_code;
    ::taxon::Value m_request_data;
    cow_vector<Response> m_responses;

  public:
    Service_Future(const Target_Descriptor& target_descriptor,
                   const cow_string& request_code, const ::taxon::Value& request_data);

    Service_Future(const ::poseidon::UUID& target_service_uuid,
                   const cow_string& request_code, const ::taxon::Value& request_data);

  private:
    virtual
    void
    do_on_abstract_future_initialize() override;

  public:
#ifdef K32_DARK_MAGIC_5B7AEF1F_484C_11F0_A2E3_5254005015D2_
    cow_vector<Response>& mf_responses() noexcept { return this->m_responses;  }
    void mf_abstract_future_initialize_once() { this->do_abstract_future_initialize_once();  }
#endif
    Service_Future(const Service_Future&) = delete;
    Service_Future& operator=(const Service_Future&) = delete;
    virtual ~Service_Future();

    // Gets the target service UUID. This field is set by the constructor.
    const ::poseidon::UUID&
    target_service_uuid() const noexcept
      { return this->m_target_service_uuid;  }

    // Gets the target service type. This is required for multicast or randomcast
    // messages, and is ignored otherwise.  This field is set by the constructor.
    const cow_string&
    target_service_type() const noexcept
      { return this->m_target_service_type;  }

    // Gets the request code. This field is set by the constructor.
    const cow_string&
    request_code() const noexcept
      { return this->m_request_code;  }

    // Gets the request data. This field is set by the constructor.
    const ::taxon::Value&
    request_data() const noexcept
      { return this->m_request_data;  }

    // Gets a vector of all target services with their responses, after all
    // operations have completed successfully. If `successful()` yields `false`,
    // an exception is thrown, and there is no effect.
    const cow_vector<Response>&
    responses() const
      {
        this->check_success();
        return this->m_responses;
      }
  };

// `multicast_uuid(service_type)` causes the message to be sent to all
// instance of `service_type`.
extern const ::poseidon::UUID multicast_uuid;

ROCKET_ALWAYS_INLINE
Service_Future::Target_Descriptor
multicast(const cow_string& service_type)
  {
    Service_Future::Target_Descriptor target;
    target.service_uuid = multicast_uuid;
    target.service_type = service_type;
    return target;
  }

// `randomcast(service_type)` causes the message to be sent to a random
// instance of `service_type`.
extern const ::poseidon::UUID randomcast_uuid;

ROCKET_ALWAYS_INLINE
Service_Future::Target_Descriptor
randomcast(const cow_string& service_type)
  {
    Service_Future::Target_Descriptor target;
    target.service_uuid = randomcast_uuid;
    target.service_type = service_type;
    return target;
  }

// `broadcast()` causes the message to be sent to all instances. This should
// be used with caution.
extern const ::poseidon::UUID broadcast_uuid;

ROCKET_ALWAYS_INLINE
Service_Future::Target_Descriptor
broadcast()
  {
    Service_Future::Target_Descriptor target;
    target.service_uuid = broadcast_uuid;
    return target;
  }

}  // namespace k32
#endif
