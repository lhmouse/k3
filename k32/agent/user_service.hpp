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
    using handler_type = ::rocket::shared_function<
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

    // Adds a new handler for requests from users. If a new handler already
    // exists, an exception is thrown.
    void
    add_handler(const phcow_string& code, const handler_type& handler);

    // Adds a new handler, or replaces an existing one, for requests from users.
    // If a new handler has been added, `true` is returned. If an existent
    // handler has been overwritten, `false` is returned.
    bool
    set_handler(const phcow_string& code, const handler_type& handler);

    // Removes a handler for requests from users.
    bool
    remove_handler(const phcow_string& code) noexcept;

    // Gets properties of a user.
    const User_Information*
    find_user_opt(const phcow_string& username) const noexcept;

    // Reloads configuration.
    void
    reload(const ::poseidon::Config_File& conf_file);
  };

}  // namespace k32::agent
#endif
