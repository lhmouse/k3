// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#ifndef K32_COMMON_RESPONSE_FUTURE_
#define K32_COMMON_RESPONSE_FUTURE_

#include "../fwd.hpp"
#include <poseidon/fiber/abstract_future.hpp>
#include <poseidon/base/uuid.hpp>
#include <taxon.hpp>
namespace k32 {

class Response_Future
  :
    public ::poseidon::Abstract_Future
  {
  protected:
    ::poseidon::UUID m_service_uuid;
    ::taxon::Value m_data;

  public:
    Response_Future(const ::poseidon::UUID& service_uuid);

  private:
    virtual
    void
    do_on_abstract_future_initialize() = 0;

  public:
    Response_Future(const Response_Future&) = delete;
    Response_Future& operator=(const Response_Future&) = delete;
    virtual ~Response_Future();

    // This field is set in the constructor.
    const ::poseidon::UUID&
    service_uuid() const noexcept
      { return this->m_service_uuid;  }

    // Get the response data. If no response has been received from the target
    // server, an exception is thrown.
    const ::taxon::Value&
    data() const
      {
        this->check_success();
        return this->m_data;
      }

    ::taxon::Value&
    mut_data()
      {
        this->check_success();
        return this->m_data;
      }
  };

}  // namespace k32
#endif
