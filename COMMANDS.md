# Table of Contents

1. [General Status Codes](#general-status-codes)
2. [Agent Service Commands](#agent-service-commands)
   1. [`/user/kick`](#userkick)

## General Status Codes

Whenever `status` occurs as a response parameter, it may either be null, or one
string of the following:

- TODO

## Agent Service Commands

### `/user/kick`

* Request Parameters

  - `username` <sub><i>string , required</i></sub>
    : Name of user to kick.
  - `ws_status` <sub><i>number , optional</i></sub>
    : WebSocket status code.
  - `reason` <sub><i>string , optional</i></sub>
    : Additional reason string.

* Response Parameters

  - _None_

* Description

  Terminates the connection from `username` by sending a WebSocket closure
  notification of `ws_status` and `reason`. The default value for `ws_status` is
  `1008` (_Policy Violation_).
