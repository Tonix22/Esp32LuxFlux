# RGB synchronization service

The server uses a SoftAP and listens on `0.0.0.0:3333`. The client uses
Station mode and the DHCP gateway, with a separate address override for lab
testing. Socket ownership and timeouts are local to this component. Parsing,
validation and transfer state live in `sync_core`; output effects live in
`effects`. After a complete transfer, this service saves the active sequence
through `nvm`. On startup it loads and validates that record before activating
playback, even while Wi-Fi is still connecting. The two-frame server sequence
is a bootstrap demonstration used when no matching saved sequence exists.
Selection of a role in Kconfig does not choose an LED
backend. Credentials in `sdkconfig` are local and ignored by Git.

For a step-by-step map from the Python server through this service to the
LEDs, see [How a light sequence reaches the LEDs](../../docs/SEQUENCE_CODE_WALKTHROUGH.md).
