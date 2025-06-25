# Table of Contents

1. [General Status Codes](#general-status-codes)
2. [Agent Service Commands](#agent-service-commands)
   1. [`/user/kick`](#userkick)
   2. [`/user/nickname/acquire`](#usernicknameacquire)
   3. [`/user/nickname/release`](#usernicknamerelease)
   4. [`/role/db_list_by_user`](#roledb_list_by_user)

## General Status Codes

Whenever `status` occurs as a response parameter, it may be one of the following
strings:

|Status Code                 |Description                                    |
|:---------------------------|:----------------------------------------------|
|`gs_ok`                     |Operation completed successfully.              |
|`gs_user_not_online`        |User is not online.                            |
|`gs_nickname_exists`        |Nickname already exists in database.           |
|`gs_nickname_not_found`     |Nickname not found in database.                |
|`gs_nickname_length_error`  |Nickname length out of range.                  |

## Agent Service Commands

### `/user/kick`

* Request Parameters

  - `username` <sub>string</sub> : Name of user to kick.
  - `ws_status` <sub>number, optional</sub> : WebSocket status code.
  - `reason` <sub>string, optional</sub> : Additional reason string.

* Response Parameters

  - _None_

* Description

  Terminates the connection from a user, by sending a WebSocket closure
  notification of `ws_status` and `reason`. The default value for `ws_status` is
  `1008` (_Policy Violation_).

### `/user/nickname/acquire`

* Request Parameters

  - `nickname` <sub>string</sub> : Nickname to acquire.
  - `username` <sub>string</sub> : Name of new owner.

* Response Parameters

  - `status` <sub>string</sub> : General status code.
  - `serial` <sub>integer, optional</sub> : Serial number of new nickname.

* Description

  Attempts to acquire ownership of a nickname and returns its serial number.
  Both the nickname and the serial number are unique within the _user_ database.
  If the nickname already exists under the same username, the old serial
  number is returned. If the nickname already exists under a different username,
  no serial number is returned.

### `/user/nickname/release`

* Request Parameters

  - `nickname` <sub>string</sub> : Nickname to release.

* Response Parameters

  - `status` <sub>string</sub> : General status code.

* Description

  Releases ownership of a nickname so it can be re-acquired by others.

### `/role/db_list_by_user`

* Request Parameters

  - `username` <sub>string</sub> : Name of owner of roles.

* Response Parameters

  - `status` <sub>string</sub> : General status code.
  - `avatar_list` <sub>array</sub> : List of avatar data of roles.

* Description

  Searches the _default_ database for all roles owned by `username`, and returns
  a list of avatars of such roles. The result is not cached.
