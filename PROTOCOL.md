# The k3 Protocol Specification

This document defines the syntax of messages between hosts within a network. The
ways how network connections between hosts are established, and the semantics of
individual messages, are implementation-defined and vary between applications.

## Terms and Definitions

1. A _host_ is a process in a network that is capable of sending and receiving
   data.
2. A _connection_ is a binary relationship between two hosts for direct exchange
   of data. A connection may be established from a host to itself.
3. A _message_ is a piece of data that can be sent from one host to another via
   a connection.
4. A _handshake_ is a special piece of data that a host sends via a connection
   for authentication.
5. The _client peer_ of a connection is the host that initiates connections and
   sends handshakes.
6. The _server peer_ of a connection is the host that listens for incoming
   connections and receives handshakes.
7. A _peer_ is either a client peer or a server peer.

## Connection Management and Message Delivery

1. A message from a client peer may be delivered as an HTTP GET request, an HTTP
   HEAD request, a WebSocket text message, or a WebSocket binary message. The
   representation, limitation and requirements about how a message is delivered
   have no effect on the semantics of the message.
2. A client peer that sends an HTTP request shall include authentication
   information within its URI query. The URI fragment and HTTP request body have
   no meaning and should be empty.
3. A client peer that sends a WebSocket message shall include authentication
   information within the URI query of the WebSocket handshake of the connection
   where the message is being delivered.
4. A peer of a WebSocket connection which wishes to shut a connection down
   should send a WebSocket Close frame with an appropriate status code and error
   message. It can then close the connection and release associated resources
   without having to await a response from the other peer.
5. A peer of a WebSocket connection which has received a WebSocket Close frame
   shall close the connection and release associated resources. The closure is
   normal if and only if a Close frame with status 1000 has been received.
6. A server peer that has received an HTTP GET or HEAD request shall ignore the
   URI fragment and HTTP request body, and translate the message as follows: The
   HTTP request URI becomes `message-uri`; the HTTP request URI query is decoded
   to produce `message-fields`, with their order of (possibly duplicate)
   occurrences preserved.

## Message Syntax

