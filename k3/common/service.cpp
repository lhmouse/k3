// This file is part of k3.
// Copyleft 2024, LH_Mouse. All wrongs reserved.

#include "../xprecompiled.hpp"
#include "service.hpp"
#include <poseidon/socket/ipv6_address.hpp>
#include <poseidon/fiber/redis_query_future.hpp>
#include <poseidon/fiber/redis_scan_and_get_future.hpp>
#include <poseidon/static/task_executor.hpp>
#include <poseidon/static/fiber_scheduler.hpp>
#include <sys/types.h>
#include <net/if.h>
#include <ifaddrs.h>
namespace k3 {

Service::
Service()
  {
  }

Service::
~Service()
  {
  }

void
Service::
set_application_name(cow_stringR name)
  {
    if(name.empty() || !all_of(name,
        [](char ch) {
          return ((ch >= 'a') && (ch <= 'z')) || ((ch >= 'A') && (ch <= 'Z'))
                 || ((ch >= '0') && (ch <= '9'))
                 || (ch == '.') || (ch == '-') || (ch == '_') || (ch == '~');
        }))
      POSEIDON_THROW(("Invalid application name `$1`"), name);

    this->m_uuid = ::poseidon::UUID::random();
    this->m_app_name = name;
  }

void
Service::
set_private_type(cow_stringR type)
  {
    this->m_prv_type = type;
  }

void
Service::
set_private_port(uint16_t port)
  {
    this->m_prv_port = port;
  }

void
Service::
set_property(phsh_stringR name, ::taxon::Value value)
  {
    this->m_props.insert_or_assign(name, move(value));
  }

bool
Service::
unset_property(phsh_stringR name)
  {
    return this->m_props.erase(name);
  }

void
Service::
synchronize_services_with_redis(::poseidon::Abstract_Fiber& fiber, seconds ttl)
  {
    if(this->m_app_name.empty())
      POSEIDON_THROW(("Missing application name"));

    ::taxon::Value taxon;
    ::rocket::tinyfmt_str fmt;
    ::taxon::V_array addr_array;
    ::poseidon::UUID uuid;
    ::taxon::Parser_Context pctx;
    snapshot_map remotes;

    // Get all operational non-loopback network addresses.
    ::ifaddrs* ifap;
    if(::getifaddrs(&ifap) != 0)
      POSEIDON_THROW(("Failed to list network interfaces: ${errno:full}]"));

    const ::rocket::unique_ptr<::ifaddrs, void (::ifaddrs*)> ifguard(ifap, ::freeifaddrs);
    ::poseidon::IPv6_Address ipaddr;
    for(;  ifap;  ifap = ifap->ifa_next)
      if(ifap->ifa_addr && (ifap->ifa_flags & IFF_RUNNING) && !(ifap->ifa_flags & IFF_LOOPBACK))
        switch(ifap->ifa_addr->sa_family)
          {
          case AF_INET:
            ::memcpy(ipaddr.mut_data(), ::poseidon::ipv4_loopback.data(), 12);
            ::memcpy(ipaddr.mut_data() + 12, &(reinterpret_cast<const ::sockaddr_in*>(ifap->ifa_addr)->sin_addr), 4);
            ipaddr.set_port(this->m_prv_port);
            fmt << ipaddr;
            addr_array.emplace_back(fmt.extract_string());
            break;

          case AF_INET6:
            ::memcpy(ipaddr.mut_data(), &(reinterpret_cast<const ::sockaddr_in6*>(ifap->ifa_addr)->sin6_addr), 16);
            ipaddr.set_port(this->m_prv_port);
            fmt << ipaddr;
            addr_array.emplace_back(fmt.extract_string());
            break;
          }

    // Upload my service information.
    taxon.mut_object().try_emplace(&"private_address", move(addr_array));
    taxon.mut_object().try_emplace(&"private_type", this->m_prv_type);
    taxon.mut_object().try_emplace(&"properties", this->m_props);
    taxon.mut_object().try_emplace(&"process_id", static_cast<double>(::getpid()));
    taxon.mut_object().try_emplace(&"timestamp", time_point_cast<duration<double, ::std::milli>>(system_clock::now()).time_since_epoch().count());

    // Publish myself.
    cow_vector<cow_string> cmd;
    cmd.resize(5);
    cmd.mut(0) = &"SET";
    fmt << this->m_app_name << "/services/" << this->m_uuid;
    cmd.mut(1) = fmt.extract_string();
    fmt << taxon;
    cmd.mut(2) = fmt.extract_string();
    cmd.mut(3) = &"EX";
    fmt << ttl.count();
    cmd.mut(4) = fmt.extract_string();

    auto task_publish = new_sh<::poseidon::Redis_Query_Future>(::poseidon::redis_connector, move(cmd));
    ::poseidon::task_executor.enqueue(task_publish);
    ::poseidon::fiber_scheduler.yield(fiber, task_publish);

    // Get a list of all services.
    auto task_scan = new_sh<::poseidon::Redis_Scan_and_Get_Future>(::poseidon::redis_connector, this->m_app_name + "/services/*");
    ::poseidon::task_executor.enqueue(task_scan);
    ::poseidon::fiber_scheduler.yield(fiber, task_scan);

    for(const auto& r : task_scan->result().pairs) {
      if(r.first.size() < 36)  // ??
        continue;

      if(uuid.parse_partial(r.first.data() + r.first.size() - 36) != 36) {
        POSEIDON_LOG_WARN(("Invalid service name `$1`"), r.first);
        continue;
      }

      taxon.parse_with(pctx, r.second);
      if(pctx.error || !taxon.is_object()) {
        POSEIDON_LOG_WARN(("Invalid service: `$1` = $2"), r.first, r.second);
        continue;
      }

      POSEIDON_LOG_TRACE(("Received service: `$1` = $2"), uuid, taxon);
      remotes.try_emplace(uuid, move(taxon.mut_object()));
    }

    // Update `m_remotes` atomically.
    this->m_remotes = move(remotes);
    POSEIDON_LOG_TRACE(("Services synchronized: size = $1"), this->m_remotes.size());
  }

}  // namespace k3
