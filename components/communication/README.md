# Communication

Synchronous TCP client/server and UDP socket wrappers own and close their file
descriptors. Calls have configurable socket timeouts and bounded byte buffers.
The server's `accept` hands the caller a file descriptor; the caller owns and
must close it. There are no background tasks or application wire formats.
Network interfaces must be configured by the Wi-Fi layer before use.
