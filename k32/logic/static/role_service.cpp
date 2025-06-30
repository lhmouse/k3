// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#include "../../xprecompiled.hpp"
#define K32_FRIENDS_3532A6AA_9A1A_4E3B_A672_A3C0EB259AE9_
#include "role_service.hpp"
#include "../globals.hpp"
#include <poseidon/base/config_file.hpp>
#include <asteria/library/chrono.hpp>
namespace k32::logic {
namespace {

struct Implementation
  {
    int service_zone_id = 0;
    system_time service_start_time;
  };

}  // namespace

POSEIDON_HIDDEN_X_STRUCT(Role_Service,
    Implementation);

Role_Service::
Role_Service()
  {
  }

Role_Service::
~Role_Service()
  {
  }

int
Role_Service::
service_zone_id() const noexcept
  {
    if(!this->m_impl)
      return 0;

    return this->m_impl->service_zone_id;
  }

system_time
Role_Service::
service_start_time() const noexcept
  {
    if(!this->m_impl)
      return system_time();

    return this->m_impl->service_start_time;
  }

void
Role_Service::
reload(const ::poseidon::Config_File& conf_file)
  {
    if(!this->m_impl)
      this->m_impl = new_sh<X_Implementation>();

    // Define default values here. The operation shall be atomic.
    int64_t service_zone_id = 0, service_start_time_ms = 0;

    // `logic.service_zone_id`
    auto conf_value = conf_file.query(&"logic.service_zone_id");
    if(conf_value.is_integer())
      service_zone_id = conf_value.as_integer();
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `logic.service_zone_id`: expecting an `integer`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    if((service_zone_id < 1) || (service_zone_id > 99999999))
      POSEIDON_THROW((
          "Invalid `logic.service_zone_id`: value `$1` out of range",
          "[in configuration file '$2']"),
          service_zone_id, conf_file.path());

    // `logic.service_start_time`
    conf_value = conf_file.query(&"logic.service_start_time");
    if(conf_value.is_string())
      service_start_time_ms = ::asteria::std_chrono_parse(conf_value.as_string());
    else if(!conf_value.is_null())
      POSEIDON_THROW((
          "Invalid `logic.service_start_time`: expecting a `string`, got `$1`",
          "[in configuration file '$2']"),
          conf_value, conf_file.path());

    if((service_start_time_ms < 915148800'000) || (service_start_time_ms > 4102444800'000))
      POSEIDON_THROW((
          "Invalid `logic.service_start_time`: value `$1` out of range",
          "[in configuration file '$2']"),
          service_start_time_ms, conf_file.path());

    // Set up new configuration. This operation shall be atomic.
    this->m_impl->service_zone_id = static_cast<int>(service_zone_id);
    this->m_impl->service_start_time = system_time(milliseconds(service_start_time_ms));

    // Set up request handlers.
//    service.set_handler(&"/user/kick", bindw(this->m_impl, do_slash_user_kick));
//    service.set_handler(&"/user/ban/set", bindw(this->m_impl, do_slash_user_ban_set));
//    service.set_handler(&"/user/ban/lift", bindw(this->m_impl, do_slash_user_ban_lift));
//    service.set_handler(&"/nickname/acquire", bindw(this->m_impl, do_slash_nickname_acquire));
//    service.set_handler(&"/nickname/release", bindw(this->m_impl, do_slash_nickname_release));

    // Restart the service.
//    this->m_impl->service_timer.start(150ms, 7001ms, bindw(this->m_impl, do_service_timer_callback));
//    this->m_impl->user_server.start(this->m_impl->client_port, bindw(this->m_impl, do_server_hws_callback));

  }

}  // namespace k32::logic
