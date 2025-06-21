// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#include "../xprecompiled.hpp"
#include "globals.hpp"
#include "../common/http_requestor.hpp"  /*TEST*/
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
    user_service.reload(conf_file, service.service_index());

/*TEST*/

service.add_handler(
  &"req_op",
  +[](::poseidon::Abstract_Fiber& fiber,
      const ::poseidon::UUID& request_service_uuid,
      ::taxon::Value& response_data,
      ::taxon::Value&& request_data)
    {
      response_data = "<<<< " + request_data.as_string();
    });

static HTTP_Requestor requestor;

user_service.add_http_handler(
  &"/aa/bb",
  +[](::poseidon::Abstract_Fiber& fiber,
      cow_string& response_content_type, cow_string& response_data,
      cow_string&& request_raw_query)
    {
      POSEIDON_LOG_FATAL(("HTTP: $1"), request_raw_query);

      auto req1 = new_sh<HTTP_Future>(&"http://www.baidu.com/?wd=meow");
      requestor.enqueue(req1);
      ::poseidon::fiber_scheduler.yield(fiber, req1);
      POSEIDON_LOG_FATAL(("HTTP RESP => $1"), req1->response_status_code());

      response_content_type = req1->response_content_type();
      response_data = req1->response_payload();
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
  &"test1",
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
