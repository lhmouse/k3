// This file is part of k3.
// Copyleft 2024, LH_Mouse. All wrongs reserved.

#ifndef K3VR5NZE_COMMON_SERVICE_
#define K3VR5NZE_COMMON_SERVICE_

#include "../fwd.hpp"
namespace k3 {

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
    cow_string m_app_type;
    uint16_t m_app_port = 0;
    ::taxon::V_object m_props;

    // remote
    snapshot_map m_remotes;

  public:
    Service();
    Service(const Service&) = delete;
    Service& operator=(const Service&) & = delete;
    ~Service();

    // The application name shall be a string matching `[A-Za-z0-9.-_~]+`. This
    // limitation exists because it is used as a prefix for Redis keys. A new
    // random UUID is generated for each request to alter the application name.
    const ::poseidon::UUID&
    uuid() const noexcept
      { return this->m_uuid;  }

    cow_stringR
    application_name() const noexcept
      { return this->m_app_name;  }

    void
    set_application_name(cow_stringR app_name);

    cow_stringR
    application_type() const noexcept
      { return this->m_app_type;  }

    void
    set_application_type(cow_stringR app_type);

    uint16_t
    private_port() const noexcept
      { return this->m_app_port;  }

    void
    set_private_port(uint16_t app_port);

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

}  // namespace k3
#endif
