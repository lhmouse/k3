// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#ifndef K32_COMMON_SERVICE_
#define K32_COMMON_SERVICE_

#include "../fwd.hpp"
#include "remote_service_information.hpp"
#include "service_future.hpp"
namespace k32 {

class Service
  {
  public:
    using handler_type = ::rocket::shared_function<
            void (
              ::poseidon::Abstract_Fiber& fiber,
              const ::poseidon::UUID& request_service_uuid,
              ::taxon::Value& response_data,  // output parameter
              ::taxon::Value&& request_data)>;

  private:
    struct X_Implementation;
    shptr<X_Implementation> m_impl;

  public:
    Service();

  public:
    Service(const Service&) = delete;
    Service& operator=(const Service&) & = delete;
    ~Service();

    // Adds a new handler for requests from other servers. If a new handler
    // already exists, an exception is thrown.
    void
    add_handler(const phcow_string& opcode, const handler_type& handler);

    // Adds a new handler, or replaces an existing one, for requests from other
    // servers. If a new handler has been added, `true` is returned. If an
    // existent handler has been overwritten, `false` is returned.
    bool
    set_handler(const phcow_string& opcode, const handler_type& handler);

    // Removes a handler for requests from other servers.
    bool
    remove_handler(const phcow_string& opcode) noexcept;

    // Returns the UUID of the active service. If there is no active service, a
    // zero UUID is returned.
    const ::poseidon::UUID&
    service_uuid() const noexcept;

    // Gets properties of a remote service.
    const Remote_Service_Information*
    find_remote_service_opt(const ::poseidon::UUID& remote_service_uuid) const noexcept;

    // Reloads configuration. If `application_name` or `application_password`
    // is changed, a new service (with a new UUID) is initiated.
    void
    reload(const cow_string& service_type, const ::poseidon::Config_File& conf_file);

    // Enqueues a service request. After this function returns, the caller shall
    // wait on the future. If this function fails, an exception is thrown, and
    // there is no effect.
    void
    enqueue(const shptr<Service_Future>& req);
  };

}  // namespace k32
#endif
