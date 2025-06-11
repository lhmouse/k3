// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#include "../xprecompiled.hpp"
#include "service.hpp"
#include <poseidon/base/config_file.hpp>
#include <poseidon/easy/easy_ws_server.hpp>
#include <poseidon/easy/easy_ws_client.hpp>
#include <poseidon/easy/easy_timer.hpp>
#include <poseidon/fiber/redis_query_future.hpp>
#include <poseidon/fiber/redis_scan_and_get_future.hpp>
#include <poseidon/static/task_executor.hpp>
#include <poseidon/static/fiber_scheduler.hpp>
#include <sys/types.h>
#include <net/if.h>
#include <ifaddrs.h>
namespace k32 {
namespace {

struct Implementation
  {
    ::poseidon::UUID service_uuid = ::poseidon::UUID::random();
    cow_dictionary<Service::handler_type> handlers;

    ::poseidon::Easy_WS_Server private_server;
    ::poseidon::Easy_WS_Client private_client;
    ::poseidon::Easy_Timer sync_timer;

    // local data
    cow_string service_type;
    cow_string application_name;
    cow_string application_password;

    // remote data from redis
    cow_uuid_dictionary<::taxon::V_object> services;
  };

void
do_private_server_callback(const wkptr<Implementation>& weak_impl,
                           const shptr<::poseidon::WS_Server_Session>& session,
                           ::poseidon::Abstract_Fiber& fiber,
                           ::poseidon::Easy_WS_Event event, linear_buffer&& data)
  {
    const auto impl = weak_impl.lock();
    if(!impl)
      return;

    POSEIDON_LOG_FATAL(("do_private_server_callback"));
  }

void
do_private_client_callback(const wkptr<Implementation>& weak_impl,
                           const shptr<::poseidon::WS_Client_Session>& session,
                           ::poseidon::Abstract_Fiber& fiber,
                           ::poseidon::Easy_WS_Event event, linear_buffer&& data)
  {
    const auto impl = weak_impl.lock();
    if(!impl)
      return;

    POSEIDON_LOG_FATAL(("do_private_client_callback"));
  }

void
do_synchronize_services(const wkptr<Implementation>& weak_impl,
                        ::poseidon::Abstract_Fiber& fiber)
  {
    const auto impl = weak_impl.lock();
    if(!impl)
      return;

    const auto redis_key_prefix = impl->application_name + "services/";

    ::taxon::Value value;
    ::taxon::V_array array;
    cow_vector<cow_string> cmd;
    cow_uuid_dictionary<::taxon::V_object> services;
    ::poseidon::UUID uuid;

    // Set my service information.
    value.mut_object().try_emplace(&"service_uuid", impl->service_uuid.print_to_string());
    value.mut_object().try_emplace(&"service_type", impl->service_type);
    value.mut_object().try_emplace(&"application_name", impl->application_name);

    auto private_addr = impl->private_server.local_address();
    if(private_addr.port() != 0) {
      // Get all running network interfaces.
      ::ifaddrs* ifa;
      if(::getifaddrs(&ifa) != 0) {
        POSEIDON_LOG_ERROR(("Network configuration error: ${errno:full}]"));
        return;
      }

      const auto ifa_guard = ::rocket::make_unique_handle(ifa, ::freeifaddrs);
      for(ifa = ifa_guard;  ifa;  ifa = ifa->ifa_next)
        if(!(ifa->ifa_flags & IFF_RUNNING) || !ifa->ifa_addr)
          continue;
        else if(ifa->ifa_addr->sa_family == AF_INET) {
          // IPv4
          auto sa = reinterpret_cast<::sockaddr_in*>(ifa->ifa_addr);
          ::poseidon::IPv6_Address addr;
          addr = ::poseidon::ipv4_unspecified;
          ::memcpy(addr.mut_data() + 12, &(sa->sin_addr), 4);
          addr.set_port(private_addr.port());
          array.emplace_back(addr.print_to_string());
        }
        else if(ifa->ifa_addr->sa_family == AF_INET6) {
          // IPv6
          auto sa = reinterpret_cast<::sockaddr_in6*>(ifa->ifa_addr);
          ::poseidon::IPv6_Address addr;
          addr.set_addr(sa->sin6_addr);
          addr.set_port(private_addr.port());
          array.emplace_back(addr.print_to_string());
        }

      value.mut_object().try_emplace(&"hostname", ::poseidon::hostname);
      value.mut_object().try_emplace(&"address_list", array);
    }


    cmd.clear();
    cmd.emplace_back(&"SET");
    cmd.emplace_back(sformat("$1$2", redis_key_prefix, impl->service_uuid));
    cmd.emplace_back(value.print_to_string());
    cmd.emplace_back(&"EX");
    cmd.emplace_back(&"60");  // one minute

    auto task1 = new_sh<::poseidon::Redis_Query_Future>(::poseidon::redis_connector, cmd);
    ::poseidon::task_executor.enqueue(task1);
    ::poseidon::fiber_scheduler.yield(fiber, task1);
    POSEIDON_LOG_DEBUG(("Published service `$1`: $2"), cmd.at(1), cmd.at(2));

    // Download all the other services.
    auto task2 = new_sh<::poseidon::Redis_Scan_and_Get_Future>(
                    ::poseidon::redis_connector, redis_key_prefix + "*");
    ::poseidon::task_executor.enqueue(task2);
    ::poseidon::fiber_scheduler.yield(fiber, task2);

    for(const auto& r : task2->result().pairs) {
      if(r.first.size() != redis_key_prefix.size() + 36)
        continue;

      if(uuid.parse_partial(r.first.data() + redis_key_prefix.size()) != 36) {
        POSEIDON_LOG_WARN(("Invalid service `$1`"), r.first);
        continue;
      }

      ::taxon::Parser_Context pctx;
      value.parse_with(pctx, r.second);
      if(pctx.error || (value.type() != ::taxon::t_object)) {
        POSEIDON_LOG_WARN(("Invalid service `$1`: $2"), r.first, r.second);
        continue;
      }

      services.try_emplace(uuid, value.as_object());
      POSEIDON_LOG_DEBUG(("Received service: `$1`: $2"), uuid, value);
    }

    impl->services = services;
    POSEIDON_LOG_DEBUG(("Synchronized $1 services"), impl->services.size());
  }

}  // namespace

POSEIDON_HIDDEN_X_STRUCT(Service,
    Implementation);

Service::
Service()
  {
  }

Service::
~Service()
  {
  }

const ::poseidon::UUID&
Service::
service_uuid() const noexcept
  {
    if(!this->m_impl)
      return ::poseidon::UUID::min();

    return this->m_impl->service_uuid;
  }

bool
Service::
set_handler(const phcow_string& key, const handler_type& handler)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    return this->m_impl->handlers.insert_or_assign(key, handler).second;
  }

bool
Service::
remove_handler(const phcow_string& key) noexcept
  {
    if(!this->m_impl)
      return false;

    return this->m_impl->handlers.erase(key);
  }

void
Service::
reload(const ::poseidon::Config_File& conf_file, const cow_string& service_type)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    // Define default values here. The operation shall be atomic.
    cow_string application_name, application_password;

    // `application_name`
    auto conf_value = conf_file.query(&"k32.application_name");
    if(conf_value.is_string())
      application_name = conf_value.as_string();
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `k32.application_name`: expecting a `string`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    if(application_name.empty())
      POSEIDON_THROW((
          "Invalid `k32.application_name`: empty name not valid",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    for(char ch : application_name)
      switch(ch)
        {
        case 'A' ... 'Z':
        case 'a' ... 'z':
        case '0' ... '9':
        case '_':
        case '.':
        case '-':
        case '~':
        case '!':
        case '(':
        case ')':
        case ' ':  // space
          continue;

        default:
          POSEIDON_THROW((
              "Invalid `k32.application_name`: character `$1` not allowed",
              "[in configuration file '$2']"),
              ch, conf_file.path());
        }

    // `application_password`
    conf_value = conf_file.query(&"k32.application_password");
    if(conf_value.is_string())
      application_password = conf_value.as_string();
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `k32.application_password`: expecting a `string`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    // Set up new configuration. This operation shall be atomic.
    this->m_impl->service_type = service_type;
    this->m_impl->application_name = application_name;
    this->m_impl->application_password = application_password;

    POSEIDON_LOG_INFO(("Reloaded service: $1/$2"), application_name, service_type);

    if(service_type != "")
      this->m_impl->private_server.start_any(0,
             ::poseidon::Easy_WS_Server::callback_type(
                [weak_impl = wkptr<Implementation>(this->m_impl)]
                   (const shptr<::poseidon::WS_Server_Session>& session,
                    ::poseidon::Abstract_Fiber& fiber,
                    ::poseidon::Easy_WS_Event event, linear_buffer&& data)
                  {
                    if(auto impl = weak_impl.lock())
                      do_private_server_callback(impl, session, fiber, event, move(data));
                  }));

    if(this->m_impl->sync_timer.running() == false)
      this->m_impl->sync_timer.start(1s, 10s,
             ::poseidon::Easy_Timer::callback_type(
                [weak_impl = wkptr<Implementation>(this->m_impl)]
                   (const shptr<::poseidon::Abstract_Timer>& /*timer*/,
                    ::poseidon::Abstract_Fiber& fiber,
                    ::std::chrono::steady_clock::time_point /*now*/)
                  {
                    if(auto impl = weak_impl.lock())
                      do_synchronize_services(impl, fiber);
                  }));
  }

}  // namespace k32
