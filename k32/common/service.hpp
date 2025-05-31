// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#ifndef K32_COMMON_SERVICE_
#define K32_COMMON_SERVICE_

#include "../fwd.hpp"
namespace k32 {

class Service
  {
  public:
    // Redis snapshot map
    using snapshot_map = cow_hashmap<::poseidon::UUID, ::taxon::V_object,
                                     ::poseidon::UUID::hash,
                                     ::rocket::equal>;

  private:
    // local
    ::poseidon::UUID m_uuid;
    cow_string m_app_name;
    cow_string m_priv_type;
    uint16_t m_priv_port = 0;
    ::taxon::V_object m_props;

    // remote
    snapshot_map m_remotes;

  public:
    Service();
    Service(const Service&) = delete;
    Service& operator=(const Service&) & = delete;
    ~Service();

    // The application name shall be a string matching `[-._~A-Za-z0-9]+`. This
    // limitation exists because it is used as a prefix for Redis keys. A new
    // random UUID is generated for each request to alter the application name.
    const ::poseidon::UUID&
    uuid() const noexcept
      { return this->m_uuid;  }

    cow_stringR
    application_name() const noexcept
      { return this->m_app_name;  }

    void
    set_application_name(cow_stringR name);

    cow_stringR
    private_type() const noexcept
      { return this->m_priv_type;  }

    void
    set_private_type(cow_stringR type);

    uint16_t
    private_port() const noexcept
      { return this->m_priv_port;  }

    void
    set_private_port(uint16_t port);

    // Each service may have its own properties, which are published to Redis
    // and shared amongst all services.
    const ::taxon::V_object&
    properties() const noexcept
      { return this->m_props;  }

    const ::taxon::Value&
    property(phsh_stringR name) const noexcept
      {
        auto pval = this->m_props.ptr(name);
        if(!pval)
          pval = &::taxon::null;
        return *pval;
      }

    bool
    has_property(phsh_stringR name) const noexcept
      { return this->m_props.count(name);  }

    void
    set_property(phsh_stringR name, ::taxon::Value value);

    bool
    unset_property(phsh_stringR name);

    // A list of all services is maintained for service discovery. It is
    // synchronized with Redis periodically.
    const snapshot_map&
    remote_services() const noexcept
      { return m_remotes;  }

    const ::taxon::V_object&
    remote_service(::poseidon::UUID srv_uuid) const noexcept
      {
        auto psrv = this->m_remotes.ptr(srv_uuid);
        if(!psrv)
          psrv = &::taxon::empty_object;
        return *psrv;
      }

    // This function uploads the current service to Redis, then downloads all
    // services and stores them into `m_remotes`. If `set_application_name()`
    // has not been called, an exception is thrown.
    // The operation is atomic. If an exception is thrown, there is no effect.
    void
    synchronize_services_with_redis(::poseidon::Abstract_Fiber& fiber, seconds ttl);
  };

}  // namespace k32
#endif
