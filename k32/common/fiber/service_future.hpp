// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#ifndef K32_COMMON_FIBER_SERVICE_FUTURE_
#define K32_COMMON_FIBER_SERVICE_FUTURE_

#include "../../fwd.hpp"
#include "../data/service_response.hpp"
#include <poseidon/fiber/abstract_future.hpp>
#include <taxon.hpp>
namespace k32 {

class Service_Future
  :
    public ::poseidon::Abstract_Future
  {
  private:
    ::poseidon::UUID m_target_service_uuid;
    cow_string m_target_service_type;
    phcow_string m_opcode;
    ::taxon::V_object m_request;
    cow_vector<Service_Response> m_responses;

  public:
    struct multicast_selector_t
      {
        ::poseidon::UUID target_service_uuid;
        cow_string target_service_type;
      };

    Service_Future(multicast_selector_t&& selector, const phcow_string& opcode,
                   const ::taxon::V_object& request);

    Service_Future(const ::poseidon::UUID& target_service_uuid,
                   const phcow_string& opcode, const ::taxon::V_object& request);

  private:
    virtual
    void
    do_on_abstract_future_initialize() override;

  public:
#ifdef K32_FRIENDS_5B7AEF1F_484C_11F0_A2E3_5254005015D2_
    cow_vector<Service_Response>& mf_responses() noexcept { return this->m_responses;  }
    void mf_abstract_future_complete() { this->do_abstract_future_initialize_once();  }
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

    // Gets the request opcode. This field is set by the constructor.
    const cow_string&
    opcode() const noexcept
      { return this->m_opcode;  }

    // Gets the request data. This field is set by the constructor.
    const ::taxon::V_object&
    request() const noexcept
      { return this->m_request;  }

    // Gets a vector of all target services with their responses, after all
    // operations have completed successfully. If `successful()` yields `false`,
    // an exception is thrown, and there is no effect.
    const cow_vector<Service_Response>&
    responses() const
      {
        this->check_success();
        return this->m_responses;
      }

    // Gets the number of target services. If `successful()` yields `false`, an
    // exception is thrown, and there is no effect.
    size_t
    response_count() const
      {
        this->check_success();
        return this->m_responses.size();
      }

    // Gets the response of a target service. If `successful()` yields `false`,
    // an exception is thrown, and there is no effect.
    const Service_Response&
    response(size_t index) const
      {
        this->check_success();
        return this->m_responses.at(index);
      }
  };

extern const ::poseidon::UUID multicast_uuid;
extern const ::poseidon::UUID randomcast_uuid;
extern const ::poseidon::UUID broadcast_uuid;
extern const ::poseidon::UUID loopback_uuid;

inline
Service_Future::multicast_selector_t
multicast(const cow_string& target_service_type)
  {
    Service_Future::multicast_selector_t selector;
    selector.target_service_uuid = multicast_uuid;
    selector.target_service_type = target_service_type;
    return selector;
  }

inline
Service_Future::multicast_selector_t
randomcast(const cow_string& target_service_type)
  {
    Service_Future::multicast_selector_t selector;
    selector.target_service_uuid = randomcast_uuid;
    selector.target_service_type = target_service_type;
    return selector;
  }

}  // namespace k32
#endif
