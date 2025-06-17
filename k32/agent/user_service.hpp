// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#ifndef K32_AGENT_USER_SERVICE_
#define K32_AGENT_USER_SERVICE_

#include "../fwd.hpp"
#include "user_information.hpp"
namespace k32::agent {

class User_Service
  {
  public:
    using http_handler_type = ::rocket::shared_function<
            void (
              const phcow_string& username,
              ::poseidon::Abstract_Fiber& fiber,
              cow_string& response_content_type,  // output parameter
              cow_string& response_data,  // output parameter
              cow_string&& request_query)>;

    using ws_handler_type = ::rocket::shared_function<
            void (
              const phcow_string& username,
              ::poseidon::Abstract_Fiber& fiber,
              ::taxon::Value& response_data,  // output parameter
              ::taxon::Value&& request_data)>;

  private:
    struct X_Implementation;
    shptr<X_Implementation> m_impl;

  public:
    User_Service();

  public:
    User_Service(const User_Service&) = delete;
    User_Service& operator=(const User_Service&) & = delete;
    ~User_Service();

    // Adds a new HTTP handler for requests from users. If a new handler
    // already exists, an exception is thrown.
    void
    add_http_handler(const phcow_string& path, const http_handler_type& handler);

    // Adds a new HTTP handler, or replaces an existing one, for requests from
    // users. If a new handler has been added, `true` is returned. If an existent
    // handler has been overwritten, `false` is returned.
    bool
    set_http_handler(const phcow_string& path, const http_handler_type& handler);

    // Removes an HTTP handler for requests from users.
    bool
    remove_http_handler(const phcow_string& path) noexcept;

    // Adds a new WebSocket handler for requests from users. If a new handler
    // already exists, an exception is thrown.
    void
    add_ws_handler(const phcow_string& opcode, const ws_handler_type& handler);

    // Adds a new WebSocket handler, or replaces an existing one, for requests
    // from users. If a new handler has been added, `true` is returned. If an
    // existent handler has been overwritten, `false` is returned.
    bool
    set_ws_handler(const phcow_string& opcode, const ws_handler_type& handler);

    // Removes a WebSocket handler for requests from users.
    bool
    remove_ws_handler(const phcow_string& opcode) noexcept;

    // Gets properties of a user.
    const User_Information*
    find_user_opt(const phcow_string& username) const noexcept;

    // Reloads configuration.
    void
    reload(const ::poseidon::Config_File& conf_file);

    // Sends
  };

}  // namespace k32::agent
#endif
