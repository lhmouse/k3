// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#include "../xprecompiled.hpp"
#include "globals.hpp"
#include <poseidon/static/main_config.hpp>
namespace k32::agent {

Service service;
Clock clock;

}  // namespace k32::agent

using namespace k32;
using namespace k32::agent;

/*TEST*/

#include <poseidon/easy/easy_timer.hpp>
#include <poseidon/static/fiber_scheduler.hpp>

static ::poseidon::Easy_Timer test_timer;

static void
test_timer_callback(const shptr<::poseidon::Abstract_Timer>& timer,
                    ::poseidon::Abstract_Fiber& fiber,
                    ::std::chrono::steady_clock::time_point now)
  {
    ::taxon::V_object root;
    root.try_emplace(&"foo", &"hello");
    root.try_emplace(&"bar", 42.0);
    auto req = new_sh<Service_Future>(broadcast_uuid, &"code1", root);
    service.enqueue(req);

    POSEIDON_LOG_FATAL(("test_timer_callback 111"));
    ::poseidon::fiber_scheduler.yield(fiber, req);
    POSEIDON_LOG_FATAL(("test_timer_callback 2222222"));

    for(const auto& r : req->responses())
      if(r.error == "")
        POSEIDON_LOG_FATAL(("  $1 => DATA: $2"), r.service_uuid, r.response_data);
      else
        POSEIDON_LOG_FATAL(("  $1 => ERROR: $2"), r.service_uuid, r.error);
  }

static void
test_handler_code1(::poseidon::Abstract_Fiber& fiber,
                   ::taxon::Value& response_data,
                   cow_string&& request_code,
                   ::taxon::Value&& request_data)
  {
    POSEIDON_LOG_FATAL(("test_handler_code1 77777777777"));
    response_data.mut_array().emplace_back(&"meo meow");
    response_data.mut_array().emplace_back(12345);
    response_data.mut_array().emplace_back(false);
  }

/*TEST*/

void
poseidon_module_main(void)
  {
    const auto conf_file = ::poseidon::main_config.copy();
    service.reload(conf_file, &"agent");



/*TEST*/
service.set_handler(&"code1", test_handler_code1);
test_timer.start(5s, 30s, test_timer_callback);
/*TEST*/
  }
