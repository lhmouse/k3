// This file is part of k32.
// Copyright (C) 2024-2025, LH_Mouse. All wrongs reserved.

#include "../../xprecompiled.hpp"
#define K32_FRIENDS_3543B0B1_DC5A_4F34_B9BB_CAE513821771_
#include "role_record.hpp"
namespace k32 {

const Role_Record Role_Record::null;

Role_Record::
~Role_Record()
  {
  }

void
Role_Record::
parse_from_string(const cow_string& str)
  {
    ::taxon::Value temp_value;
    POSEIDON_CHECK(temp_value.parse(str));
    ::taxon::V_object root = temp_value.as_object();
    temp_value.clear();

    this->roid = root.at(&"roid").as_integer();
    this->username = root.at(&"username").as_string();
    this->nickname = root.at(&"nickname").as_string();
    this->update_time = root.at(&"update_time").as_time();
    this->avatar = root.at(&"avatar").as_string();   // JSON as string
    this->profile = root.at(&"profile").as_string();   // JSON as string
    this->whole = root.at(&"whole").as_string();   // JSON as string

    this->home_host = root.at(&"@home_host").as_string();
    this->home_db = root.at(&"@home_db").as_string();
    this->home_srv = ::poseidon::UUID(root.at(&"@home_srv").as_string());
  }

cow_string
Role_Record::
serialize_to_string() const
  {
    ::taxon::V_object root;

    root.try_emplace(&"roid", this->roid);
    root.try_emplace(&"username", this->username.rdstr());
    root.try_emplace(&"nickname", this->nickname);
    root.try_emplace(&"update_time", this->update_time);
    root.try_emplace(&"avatar", this->avatar);  // JSON as string
    root.try_emplace(&"profile", this->profile);  // JSON as string
    root.try_emplace(&"whole", this->whole);  // JSON as string

    root.try_emplace(&"@home_host", this->home_host);
    root.try_emplace(&"@home_db", this->home_db);
    root.try_emplace(&"@home_srv", this->home_srv.to_string());

    return ::taxon::Value(root).to_string();
  }

}  // namespace k32
