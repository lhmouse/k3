// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#include "../xprecompiled.hpp"
#include "globals.hpp"
#include <poseidon/static/main_config.hpp>
namespace k32::agent {

Clock clock;
Service service;
User_Service user_service;

}  // namespace k32::agent

using namespace k32;
using namespace k32::agent;

void
poseidon_module_main(void)
  {
    const auto conf_file = ::poseidon::main_config.copy();
    service.reload(&"agent", conf_file);
    user_service.reload(conf_file);

/*TEST*/

user_service.add_http_handler(
  &"/aa/bb",
  +[](::poseidon::Abstract_Fiber& fiber,
      cow_string& response_content_type, cow_string& response_data,
      cow_string&& request_raw_query)
     {
       POSEIDON_LOG_FATAL(("HTTP: $1"), request_raw_query);
       response_content_type = &"text/plain";
       response_data = &"meow meow meow";
     });

user_service.add_ws_authenticator(
  &"/aa/bb",
  +[](::poseidon::Abstract_Fiber& fiber,
      phcow_string& username,
      cow_string&& request_raw_query)
    {
       POSEIDON_LOG_FATAL(("WS AUTH: $1"), request_raw_query);
       username = &"test_user";
    });

user_service.add_ws_handler(
  &"/aa/bb",
  +[](::poseidon::Abstract_Fiber& fiber,
      const phcow_string& username,
      ::taxon::Value& response_data,
      ::taxon::Value&& request_data)
    {
       POSEIDON_LOG_FATAL(("WS MSG: $1"), request_data);
       response_data.open_array() = { 1, 2, 3 };
    });

/*TEST*/

  }
