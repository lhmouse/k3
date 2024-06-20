# The k3 Protocol Specification

This document defines the syntax of messages between hosts within a network. The
ways how network connections between hosts are established, and the semantics of
individual messages, are implementation-defined and vary between applications.

## Terms and Definitions

1. A _host_ is a process in a network that is capable of sending and receiving
   data.
2. A _connection_ is a binary relationship between two hosts for direct exchange
   of data. A connection may be established from a host to itself.
3. A _handshake_ is a special piece of data that a host sends via a connection
   for authentication.
4. The _client peer_ of a connection is the host that initiates connections and
   sends handshakes.
5. The _server peer_ of a connection is the host that listens for incoming
   connections and receives handshakes.
6. A _peer_ is either a client peer or a server peer.
7. A _message_ is a piece of data that can be sent from one host to another via
   a connection.
8. The _path_ of a message is a string that starts with "/", which is used to
   find a handler for the message. A message may have zero or one path.
9. The _cookie_ of a message is a string that starts with "#", which is used to
   determine the correspondence of two messages between two peers. A message
   may have zero or more cookies. If a peer has received a message with cookies,
   it must eventually send back with another message with the same sequence of
   cookies. A cookie shall not affect the meaning of a message.
10. The _payload_ of a message is a (possibly empty) sequence of bytes of a
    message that defines the meaning of the message.

## Connection Management

1. When authentication is required, a client peer shall include authentication
   information within URI queries of the HTTP requests or WebSocket handshakes
   that it sends.
2. A peer of a WebSocket connection which wishes to shut a connection down
   should send a Close frame with an appropriate status code and error message.
   It can then close the connection and release associated resources without
   having to await a response from the other peer.
3. A peer of a WebSocket connection which has received a Close frame shall close
   the connection and release associated resources. The closure is normal if and
   only if a Close frame with status 1000 has been received.

## Message Delivery

1. A message may be delivered as an HTTP or WebSocket text message.
2. A server peer that has received an HTTP GET or HEAD request shall translate
   it to a message as follows:
   * The request URI produces _path_.
   * The query of the request URI produces _payload_, as an object of strings.
   * There shall be no cookie.
   * The fragment of the request URI, the HTTP request headers and the request
     payload shall be ignored.
3. A client peer that has received an HTTP response shall translate it to a
   message as follows:
   * If the status code is not 200, then the message is empty. If the status
     code is 200, further actions apply.
   * The response payload is decomposed to lines.
   * If a line starts with "#", then it produces a _cookie_.
   * If a line starts with "/", then it produces a _path_.
   * Otherwise, this line and all the remaining lines are joined to produce
     _payload_.
   * The HTTP response headers shall be ignored.
4. A peer that has received a WebSocket text message shall translate it to a
   message as follows:
   * The message payload is decomposed to lines.
   * If a line starts with "#", then it produces a _cookie_.
   * If a line starts with "/", then it produces a _path_.
   * Otherwise, this line and all the remaining lines are joined to produce
     _payload_.
