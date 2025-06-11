// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#ifndef K32_COMMON_SERVICE_
#define K32_COMMON_SERVICE_

#include "../fwd.hpp"
#include "response_future.hpp"
namespace k32 {

class Service
  {
  public:
    using handler_type = ::rocket::shared_function<
            void (
              const ::poseidon::UUID& service_uuid,
              ::poseidon::Abstract_Fiber& fiber,
              ::taxon::Value& response_data,  // output parameter
              const ::poseidon::UUID& request_service_uuid,
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

    // Returns the UUID of the active service. If there is no active service, a
    // zero UUID is returned.
    const ::poseidon::UUID&
    service_uuid() const noexcept;

    // Registers a handler for requests from other servers. If a new handler has
    // been added, `true` is returned. If an existent handler has been overwritten,
    // `false` is returned.
    bool
    set_handler(const phcow_string& key, const handler_type& handler);

    // Removes a handler for requests from other servers.
    bool
    remove_handler(const phcow_string& key) noexcept;

    // Reloads configuration. If `application_name` or `application_password`
    // is changed, a new service (with a new UUID) is initiated.
    void
    reload(const ::poseidon::Config_File& conf_file, const cow_string& service_type);

    // Sends a message to a service, and returns a future of its response. If no
    // such service exists, a future that always fails is returned.
    shptr<Response_Future>
    request_single(const ::poseidon::UUID& service_uuid, const ::taxon::Value& data);

    // Sends a message to a random matching service, and returns a future of its
    // response. If no such service exists, a future that always fails is returned.
    shptr<Response_Future>
    request_random(const cow_string& service_type, const ::taxon::Value& data);

    // Sends a message to all matching services in parallel, and returns a vector
    // of futures of their responses. If no such service exists, an empty vector
    // is returned.
    cow_bivector<::poseidon::UUID, shptr<Response_Future>>
    request_multiple(const cow_string& service_type, const ::taxon::Value& data);

    // Sends a message to all services in parallel, and returns a vector of
    // futures of their responses. If no such service exists, an empty vector is
    // returned.
    cow_bivector<::poseidon::UUID, shptr<Response_Future>>
    request_broadcast(const ::taxon::Value& data);

    // Sends a message to a service without waiting for a response. If no such
    // service exists, a zero UUID is returned.
    ::poseidon::UUID
    notify_single(const ::poseidon::UUID& service_uuid, const ::taxon::Value& data);

    // Sends a message to a random matching service without waiting for a response,
    // and returns its UUID. If no such service exists, a zero UUID is returned.
    ::poseidon::UUID
    notify_random(const cow_string& service_type, const ::taxon::Value& data);

    // Sends a message to all matching services in parallel, and returns a vector
    // of their UUIDs. If no such service exists, an empty vector is returned.
    cow_vector<::poseidon::UUID>
    notify_multiple(const cow_string& service_type, const ::taxon::Value& data);

    // Sends a message to all services in parallel, and returns a vector of
    // their UUIDs. If no such service exists, an empty vector is returned.
    cow_vector<::poseidon::UUID>
    notify_broadcast(const ::taxon::Value& data);
  };

}  // namespace k32
#endif
