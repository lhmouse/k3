// This file is part of k3.
// Copyleft 2024, LH_Mouse. All wrongs reserved.

#ifndef K3VR5NZE_COMMON_MESSAGE_
#define K3VR5NZE_COMMON_MESSAGE_

#include "../fwd.hpp"
namespace k3 {

struct Message
  {
    cow_vector<cow_string> path;
    cow_vector<cow_string> cookies;
    tinyfmt_str payload;

    Message&
    swap(Message& other) noexcept
      {
        this->path.swap(other.path);
        this->cookies.swap(other.cookies);
        this->payload.swap(other.payload);
      }

    void
    clear() noexcept
      {
        this->path.clear();
        this->cookies.clear();
        this->payload.clear_string();
      }

    void
    encode(tinyfmt& fmt) const;
  };

inline
void
swap(Message& lhs, Message& rhs) noexcept
  { lhs.swap(rhs);  }

}  // namespace k3
#endif
