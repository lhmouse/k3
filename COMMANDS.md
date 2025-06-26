# Table of Contents

1. [General Status Codes](#general-status-codes)
2. [Agent Commands](#agent-commands)
   1. [`/user/ban/set`](#userbanset)
   2. [`/user/ban/lift`](#userbanlift)
   3. [`/user/kick`](#userkick)
3. [Monitor Commands](#monitor-commands)
   1. [`/nickname/acquire`](#nicknameacquire)
   2. [`/nickname/release`](#nicknamerelease)
   3. [`/role/create`](#rolecreate)
   4. [`/role/load`](#roleload)
   5. [`/role/unload`](#roleunload)
   6. [`/role/flush`](#roleflush)

## General Status Codes

Whenever `status` occurs as a response parameter, it may be one of the following
strings:

|Status Code                 |Description                                    |
|:---------------------------|:----------------------------------------------|
|`gs_ok`                     |Operation completed successfully.              |
|`gs_user_not_online`        |User not online.                               |
|`gs_user_not_found`         |User not found in database.                    |
|`gs_nickname_conflict`      |Nickname already exists in database.           |
|`gs_nickname_not_found`     |Nickname not found in database.                |
|`gs_nickname_length_error`  |Nickname length out of range.                  |
|`gs_roid_conflict`          |Role ID already exists in database.            |
|`gs_roid_not_found`         |Role ID not found in database.                 |

## Agent Commands

### `/user/ban/set`

* Request Parameters

  - `username` <sub>string</sub> : Name of user to ban.
  - `until` <sub>timestamp</sub> : Ban in effect until this time point.

* Response Parameters

  - `status` <sub>string</sub> : [General status code.](#general-status-codes)

* Description

  Sets a ban on a user until a given time point. If the user is online, they are
  kicked with `reason`.

[back to table of contents](#table-of-contents)

### `/user/ban/lift`

* Request Parameters

  - `username` <sub>string</sub> : Name of user to ban.

* Response Parameters

  - `status` <sub>string</sub> : [General status code.](#general-status-codes)

* Description

  Lifts a ban on a user.

[back to table of contents](#table-of-contents)

### `/user/kick`

* Request Parameters

  - `username` <sub>string</sub> : Name of user to kick.
  - `ws_status` <sub>number, optional</sub> : WebSocket status code.
  - `reason` <sub>string, optional</sub> : Additional reason string.

* Response Parameters

  - `status` <sub>string</sub> : [General status code.](#general-status-codes)

* Description

  Terminates the connection from a user, by sending a WebSocket closure
  notification of `ws_status` and `reason`. The default value for `ws_status` is
  `1008` (_Policy Violation_).

[back to table of contents](#table-of-contents)

## Monitor Commands

### `/nickname/acquire`

* Request Parameters

  - `nickname` <sub>string</sub> : Nickname to acquire.
  - `username` <sub>string</sub> : Owner of new nickname.

* Response Parameters

  - `status` <sub>string</sub> : [General status code.](#general-status-codes)
  - `serial` <sub>integer, optional</sub> : Serial number of new nickname.

* Description

  Attempts to acquire ownership of a nickname and returns its serial number.
  Both the nickname and the serial number are unique within the _user_ database.
  If the nickname already exists under the same username, the old serial
  number is returned. If the nickname already exists under a different username,
  no serial number is returned.

[back to table of contents](#table-of-contents)

### `/nickname/release`

* Request Parameters

  - `nickname` <sub>string</sub> : Nickname to release.

* Response Parameters

  - `status` <sub>string</sub> : [General status code.](#general-status-codes)

* Description

  Releases ownership of a nickname so it can be re-acquired by others.

[back to table of contents](#table-of-contents)

### `/role/create`

* Request Parameters

  - `roid` <sub>integer</sub> : Unique ID of role to create.
  - `nickname` <sub>string</sub> : Nickname of new role.
  - `username` <sub>string</sub> : Owner of new role.

* Response Parameters

  - `status` <sub>string</sub> : [General status code.](#general-status-codes)

* Description

  Creates a new role in the _default_ database. By design， the caller should
  call `/user/nickname/acquire` to acquire ownership of `nickname`, then pass
  `serial` as `roid`. After a role is created, it will be loaded into Redis
  automatically.

[back to table of contents](#table-of-contents)

### `/role/load`

* Request Parameters

  - `roid` <sub>integer</sub> : ID of role to load.

* Response Parameters

  - `status` <sub>string</sub> : [General status code.](#general-status-codes)

* Description

  Loads a role from the _default_ database into Redis. The monitor keeps track
  of roles that have been loaded by itself, and periodically writes snapshots
  from Redis back into the database.

[back to table of contents](#table-of-contents)

### `/role/unload`

* Request Parameters

  - `roid` <sub>integer</sub> : ID of role to unload.

* Response Parameters

  - `status` <sub>string</sub> : [General status code.](#general-status-codes)

* Description

  Writes a role back into the database and unloads it from Redis.

[back to table of contents](#table-of-contents)

### `/role/flush`

* Request Parameters

  - <i>None</i>

* Response Parameters

  - `status` <sub>string</sub> : [General status code.](#general-status-codes)

* Description

  Writes all roles back into the database.

[back to table of contents](#table-of-contents)
