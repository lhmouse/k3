# k32 Internal Commands

### `/user/kick`

* Service Type

  - `agent`

* Request Parameters

  - `username` \(_string_, _required_): Name of user to kick.
  - `status` \(_integer_, _optional_): WebSocket status code to send to the
    client. The default value is `1008`.
  - `reason` \(_string_, _optional_): Additional reason string, sent in the
    WebSocket closure notification.

* Response Parameters

  - _None_

* Operation

  - Terminates a connection from a user.
