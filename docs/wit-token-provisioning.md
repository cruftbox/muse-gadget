# Wit.ai token provisioning design

## Status

Implemented for the `lcd154` firmware. The build no longer accepts or embeds a
Wit.ai client token. The owner provisions the token at runtime from a phone.

## Goal

Let the owner configure and replace their own Wit.ai client token from a phone,
without connecting the gadget to a computer, rebuilding firmware, or flashing
the device.

The owner still obtains the client token from their Wit.ai account. This design
only changes how that token reaches the gadget.

## Existing mechanisms

The implementation has four relevant pieces:

- `main/wit_token_setup.c` owns the temporary HTTP server, token validation,
  and storage in the `homehub` NVS namespace.
- `muse_chat_session.cpp` obtains a bounded runtime copy when it sends a Wit.ai
  synthesis request, then clears its token and Authorization buffers.
- `main/config_store.c` persists account credentials in the `homehub` NVS
  namespace, preserves owner-provided service tokens during automatic
  recovery, and verifies that transfer-reset erasures succeeded.
- The Sound settings page starts and stops the local setup server and reports
  its state without exposing the token.

The runtime token belongs in `config_store`, not `muse_settings`. It is an
owner-provided credential that the explicit transfer reset must erase, while
automatic pairing recovery preserves it. The `muse` namespace is primarily
retained user preferences such as volume and brightness.

## Selected design

The gadget serves a temporary setup page over the owner's existing local Wi-Fi
connection. Wi-Fi setup remains in the Muse app.

The flow is:

1. The owner creates or opens a Wit.ai app and copies its client access token.
2. The owner finishes the gadget's normal Muse and Wi-Fi setup.
3. On the gadget, the owner opens **Settings > Sound > Wit.ai token** and taps
   **Configure**.
4. The gadget starts a temporary HTTP server and displays its numeric local URL,
   for example `http://192.168.1.42`.
5. A phone on the same Wi-Fi network opens that URL. The page accepts one token
   and does not display a stored token.
6. The gadget makes a short Wit.ai synthesis request with the submitted token.
7. A successful response causes the gadget to save the token in NVS and report
   **Wit.ai ready**. A failed response is not stored and the page reports the
   failure without echoing the token.
8. The HTTP server stops after success, explicit cancellation, leaving the
   settings page, or five minutes of inactivity.

The initial implementation does not create a SoftAP or captive portal. By this
point the Muse setup flow has already connected the gadget to Wi-Fi. Reusing
that network avoids AP/STA channel switching, captive-portal differences among
phones, and a second Wi-Fi setup path.

## Storage and component boundary

The bounded `wit_token` value is stored in the `homehub` NVS namespace. The
main component owns reading, writing, and erasing the credential through
`config_store` and `wit_token_setup`.

`muse_link_ops_t` has operations that let the Muse component query
whether a Wit token is configured and submit or clear one. This follows the
existing SDK-token boundary: `components/muse` owns the UI and TTS behavior,
while `main` owns account credentials and reset behavior. The interface must
not return the complete stored token to the UI.

`muse_chat_session.cpp` obtains a bounded copy of the token immediately before
building the Authorization header. It must clear temporary buffers after the
request. If no token is configured, the reply remains visible as text and the
UI reports **Wit.ai not configured**.

There is no CMake requirement for `WIT_CLIENT_TOKEN` or `.wit-token`. A build
with `CONFIG_MUSE_WIT_TTS` succeeds without a token.

## HTTP interface

The temporary server has a deliberately small interface:

- `GET /` returns a self-contained HTML form.
- `POST /token` accepts `application/x-www-form-urlencoded` or a small JSON
  object containing one `token` field.
- `POST /cancel` stops the server without changing stored configuration.

Requests have fixed size and time limits. The server rejects chunked bodies,
oversized headers, oversized bodies, missing tokens, control characters, and
additional fields. Neither responses nor logs include the submitted or stored
token. The page sends `Cache-Control: no-store` and does not load remote scripts,
fonts, images, or analytics.

The submitted token is first held in a temporary buffer. It is committed only
after Wit.ai returns a successful audio response. Replacing a working token is
atomic from the user's perspective: failure leaves the existing token intact.

## Security limits

The setup page uses HTTP on the local network. The Wit token is protected by the
Wi-Fi network while in transit, but there is no browser-authenticated TLS
session. A self-signed HTTPS certificate would add a browser warning and would
not authenticate the device usefully for a nontechnical owner.

The current board has Secure Boot, Flash Encryption, and NVS encryption
disabled. The runtime token is therefore recoverable by someone with physical
access to the flash, just as the current embedded token is. The UI and logs must
never display it, but this design does not claim protection against physical
extraction.

Starting the server requires a local touch action. It listens only while the
configuration screen is active and stops after five minutes. Only one client
and one request body are processed at a time.

## Transfer reset

The shared pairing and setup recovery paths preserve `sdk_token` and
`wit_token`. The explicit **Settings > Muse > Reset pairing** action calls the
separate owner-token erasure before clearing the remaining setup. Both erasures
use the existing erase-and-readback verification. The transfer reset is
unsuccessful if either owner token cannot be verified absent.

## User interface

The Sound settings page shows one of these states:

- **Not configured**
- **Ready**
- **Testing...**
- **Invalid token**
- **Wit.ai unavailable**

It never shows a token prefix or suffix. The configuration page provides
**Configure**, **Replace**, **Remove**, and **Cancel** actions. Removing the
token requires the existing two-step confirmation pattern used for destructive
settings.

## Failure behavior

- No token: show the complete text reply and do not attempt Wit.ai TTS.
- Invalid token: reject it, retain the previous token, and report the HTTP
  status without response content.
- Wit.ai or Internet unavailable: retain the previous token and report that
  validation could not complete.
- Phone disconnects or submission is incomplete: discard the temporary value.
- Device restarts during submission: the previous committed value remains.
- HTTP server cannot start: report **Setup page unavailable** and leave TTS
  configuration unchanged.

## Verification

Host tests must cover:

- NVS read, write, replace, remove, and maximum-length handling;
- automatic recovery preserving `wit_token` and the explicit transfer reset
  erasing and verifying it;
- a build with Wit TTS enabled and no build-time token;
- missing, malformed, oversized, and extra HTTP form fields;
- failed validation preserving the prior token;
- successful validation committing the submitted token;
- logs and HTTP responses containing no submitted token;
- server timeout and cancellation.

Hardware acceptance requires:

1. Configure Wi-Fi and Muse without USB data connected.
2. Start the setup page from the touch UI and open it from an iPhone on the
   same network.
3. Confirm that a bad token is rejected and not retained.
4. Store a working token, restart the gadget, and receive a spoken reply.
5. Replace the token and confirm that the replacement is used.
6. Remove the token and confirm that replies remain visible but silent.
7. Use **Settings > Muse > Reset pairing** and verify that Wi-Fi, Muse pairing,
   SDK token, and Wit token are all absent.

## Deferred work

A SoftAP captive portal is not part of this design. It can be considered later
if Wit configuration must occur before the Muse app supplies Wi-Fi. It requires
separate AP lifecycle, discovery, DNS interception, phone captive-portal, and
recovery testing.
