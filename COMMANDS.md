# Table of Contents

1. [General Status Codes](#general-status-codes)
2. [Agent Service Commands](#agent-service-commands)
   1. [`/user/ban/set`](#userbanset)
   2. [`/user/ban/lift`](#userbanlift)
   3. [`/user/kick`](#userkick)
   4. [`/user/nickname/acquire`](#usernicknameacquire)
   5. [`/user/nickname/release`](#usernicknamerelease)

## General Status Codes

Whenever `status` occurs as a response parameter, it may be one of the following
strings:

|Status Code                 |Description                                    |
|:---------------------------|:----------------------------------------------|
|`gs_ok`                     |Operation completed successfully.              |
|`gs_user_not_online`        |User not online.                               |
|`gs_user_not_found`         |User not found in database.                    |
|`gs_nickname_exists`        |Nickname already exists in database.           |
|`gs_nickname_not_found`     |Nickname not found in database.                |
|`gs_nickname_length_error`  |Nickname length out of range.                  |

## Agent Service Commands

### `/user/ban/set`

* Request Parameters

  - `username` <sub>string</sub> : Name of user to ban.
  - `until` <sub>timestamp</sub> : Ban in effect until this time point.

* Response Parameters

  - `status` <sub>string</sub> : General status code.

* Description

  Sets a ban on a user until a given time point. If the user is online, they are
  kicked with `reason`.

### `/user/ban/lift`

* Request Parameters

  - `username` <sub>string</sub> : Name of user to ban.

* Response Parameters

  - `status` <sub>string</sub> : General status code.

* Description

  Lifts a ban on a user.

### `/user/kick`

* Request Parameters

  - `username` <sub>string</sub> : Name of user to kick.
  - `ws_status` <sub>number, optional</sub> : WebSocket status code.
  - `reason` <sub>string, optional</sub> : Additional reason string.

* Response Parameters

  - `status` <sub>string</sub> : General status code.

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
