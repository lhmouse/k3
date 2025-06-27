// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#include "../xprecompiled.hpp"
#include "globals.hpp"
#include <poseidon/base/config_file.hpp>
namespace k32::agent {

Clock& clock = *new Clock;
Service& service = *new Service;
User_Service& user_service = *new User_Service;

}  // namespace k32::agent

using namespace k32;
using namespace k32::agent;

void
poseidon_module_main(void)
  {
    ::poseidon::Config_File conf_file;
    conf_file.reload(&"k32.conf");
    service.reload(conf_file, &"agent");
    user_service.reload(conf_file);

/*TEST*/

user_service.add_http_handler(
  &"/11",
  +[](::poseidon::Abstract_Fiber& fiber,
      cow_string& response_content_type, cow_string& response_data,
      cow_string&& request_raw_query)
    {
      POSEIDON_LOG_FATAL(("HTTP: $1"), request_raw_query);

      ::taxon::V_object req_data;
      req_data.try_emplace(&"roid", 505);

      auto req1 = new_sh<Service_Future>(randomcast(&"monitor"), &"/role/load", req_data);
      service.launch(req1);
      fiber.yield(req1);

      response_content_type = &"text/plain";
      response_data = ::taxon::Value(req1->responses().at(0).response_data).to_string();
      POSEIDON_LOG_FATAL(("RESP => $1"), req1->responses().at(0).response_data);
    });

user_service.add_http_handler(
  &"/22",
  +[](::poseidon::Abstract_Fiber& fiber,
      cow_string& response_content_type, cow_string& response_data,
      cow_string&& request_raw_query)
    {
      POSEIDON_LOG_FATAL(("HTTP: $1"), request_raw_query);

      ::taxon::V_object req_data;
      req_data.try_emplace(&"roid", 475);

      auto req1 = new_sh<Service_Future>(randomcast(&"monitor"), &"/role/load", req_data);
      service.launch(req1);
      fiber.yield(req1);

      response_content_type = &"text/plain";
      response_data = ::taxon::Value(req1->responses().at(0).response_data).to_string();
      POSEIDON_LOG_FATAL(("RESP => $1"), req1->responses().at(0).response_data);
    });

user_service.add_http_handler(
  &"/33",
  +[](::poseidon::Abstract_Fiber& fiber,
      cow_string& response_content_type, cow_string& response_data,
      cow_string&& request_raw_query)
    {
      POSEIDON_LOG_FATAL(("HTTP: $1"), request_raw_query);

      ::taxon::V_object req_data;
      req_data.try_emplace(&"username", &"test01001");

      auto req1 = new_sh<Service_Future>(randomcast(&"monitor"), &"/role/list", req_data);
      service.launch(req1);
      fiber.yield(req1);

      response_content_type = &"text/plain";
      response_data = ::taxon::Value(req1->responses().at(0).response_data).to_string();
      POSEIDON_LOG_FATAL(("RESP => $1"), req1->responses().at(0).response_data);
    });

user_service.add_http_handler(
  &"/44",
  +[](::poseidon::Abstract_Fiber& fiber,
      cow_string& response_content_type, cow_string& response_data,
      cow_string&& request_raw_query)
    {
      POSEIDON_LOG_FATAL(("HTTP: $1"), request_raw_query);

      ::taxon::V_object req_data;
      req_data.try_emplace(&"roid", 505);

      auto req1 = new_sh<Service_Future>(randomcast(&"monitor"), &"/role/flush", req_data);
      service.launch(req1);
      fiber.yield(req1);

      response_content_type = &"text/plain";
      response_data = ::taxon::Value(req1->responses().at(0).response_data).to_string();
      POSEIDON_LOG_FATAL(("RESP => $1"), req1->responses().at(0).response_data);
    });

/*TEST*/


  }
