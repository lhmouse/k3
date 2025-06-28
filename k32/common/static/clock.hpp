// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved. reserved.

#ifndef K32_COMMON_STATIC_CLOCK_
#define K32_COMMON_STATIC_CLOCK_

#include "../../fwd.hpp"
#include <poseidon/base/datetime.hpp>
namespace k32 {

class Clock
  {
  public:
    struct system_time_fields
      {
        uint32_t year          : 12;  //    0 - 4095
        uint32_t month         :  4;  //    1 - 12
        uint32_t day_of_month  :  5;  //    1 - 31
        uint32_t hour          :  5;  //    0 - 23
        uint32_t minute        :  6;  //    0 - 59
        uint32_t second        :  6;  //    0 - 60 (leap)
        uint32_t milliseconds  : 10;  //    0 - 999
        int32_t tz_offset      : 10;  // -720 - 720
        uint32_t dst           :  1;  // daylight saving time
        uint32_t day_of_week   :  3;  //    1 - 7
        uint32_t reserved      :  2;
      };

  private:
    seconds m_offset = 0s;
    mutable ::time_t m_cached_time = 0;
    mutable system_time_fields m_cached_fields = { };

  public:
    Clock();

  public:
    Clock(const Clock&) = delete;
    Clock& operator=(const Clock&) & = delete;
    ~Clock();

    // The virtual clock offset is the number of seconds from the physical time
    // of the current system to the virtual time of this process.
    seconds
    virtual_offset() const noexcept
      { return this->m_offset;  }

    void
    set_virtual_offset(seconds secs) noexcept;

    // These functions obtain the current virtual time.
    ::time_t
    get_time_t() const noexcept;

    double
    get_double_time_t() const noexcept;

    system_time
    get_system_time() const noexcept;

    ::poseidon::DateTime
    get_date_time() const noexcept;

    system_time_fields
    get_system_time_fields() const noexcept;
  };

}  // namespace k32
#endif
