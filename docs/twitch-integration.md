# Twitch integration exploration

Status: settings UI, session authorization, and participant preview implemented; runtime NPC naming remains proposed. Branch: twitch-integration, based on local main.

## Proposed experience

Add a Twitch settings page with an enable toggle, application client ID, Connect/Disconnect authorization, configured channels, connection/live status, and a naming scope (crew by default; optionally officers, troops, passengers, workers). Include excluded accounts and an option to name eligible existing NPCs when a channel goes live. Start with newly created crew only to avoid unexpectedly renaming established characters.

Use authorized chat participants as the name pool. Twitch chat presence is not the same as watching a stream, and the API does not provide a complete named viewer list. Each channel needs suitable broadcaster/moderator authorization to retrieve chatters. Public channel names plus a client ID alone are insufficient.

## Code entry points verified on main

- `src/lib/global/core/ausettings.{h,cpp}`: QSettings wrapper suitable for non-secret preferences.
- `src/lib/designer/settings/ausettingsdialog.{h,cpp}` and neighboring `auset*.{h,cpp}`: existing settings UI pattern; verify which game client contexts expose the shared dialog before choosing its final placement.
- `src/lib/atom/entities/vehicles/davessel.cpp`: direct random-name construction for officers (~2666), crew (~2710, ~6476), troops, and passengers. Centralize eligible naming decisions rather than replacing every random-name generator globally.
- `src/lib/atom/entities/celestial/daworldll.cpp`: worker (~2408) and general NPC (~6771) creation paths, relevant to expanded role scope.
- `src/lib/atom/entities/actors/danpc.cpp`: AssignedName is persisted in existing save/query paths; Name/CrewName add rank formatting (~839). Preserve this formatting and investigate dirty-state/update propagation before renaming existing NPCs.
- Existing QNetworkAccessManager use provides an HTTP precedent; build/link dependencies for a simulation service still need checking.

Line numbers are approximate and refer to the inspected main snapshot.

## Architecture

1. An asynchronous Twitch service resolves channel logins to stable broadcaster IDs, checks live status, retrieves paginated chatters, and maintains bounded cached pools. Start with periodic Helix Get Streams polling and refresh chatters only for live channels. Poll intervals should respect rate-limit headers and use backoff. EventSub stream.online/offline can follow later; reconcile live state on startup regardless of transport.
2. A simulation-owned name allocator selects from the cache without performing network I/O during NPC creation. When disabled, offline, unauthorized, rate-limited, or out of names, retain ordinary random naming.
3. Bind each channel to an authorized player/empire or explicitly selected fleet. Server-side ownership checks prevent renaming other players' NPCs. Define deterministic channel priority or round-robin selection where several channels share one pool.
4. Persist NPC-to-Twitch-user-ID assignments separately from display names if avoiding duplicate assignments across reloads is required. Specify uniqueness per streamer/fleet, account release on death, and account rename behavior. Do not rename existing assigned NPCs when a stream ends or a chatter leaves.
5. Validate names against game length/encoding rules, filter excluded/bot accounts, and fall back safely. Stable user IDs handle changed Twitch display names.

For multiplayer, credentials and network access belong to the authoritative service; the client edits preferences through an authenticated configuration path. For local play, host the same service locally. Confirm existing ownership and settings synchronization mechanisms before implementation.

## Authentication

Use Twitch OAuth rather than requiring pasted long-lived tokens. Get Chatters requires moderator:read:chatters and the authenticated identity must satisfy broadcaster/moderator requirements. Choose and verify the desktop OAuth flow and token renewal design before coding. A distributed desktop application cannot keep an embedded client secret confidential. Store credentials in OS credential storage or a protected server secret store, never ordinary QSettings, saves, logs, or source control. Handle expiry, revocation, disconnect, and per-channel authorization failures visibly.

## Suggested implementation slices

1. Settings schema/UI and credential abstraction, disabled by default; define player/channel ownership.
2. HTTP service with live checks, pagination, authorization status, bounded caching and backoff.
3. Crew creation allocator, fallback behavior, and persistent assignment identity.
4. Optional existing-crew assignment and additional role switches after save/network propagation is verified.

Validate with fake HTTP responses for offline/live transitions, pagination, expired credentials, 429s, empty pools, duplicate users across channels and restart behavior. Verify multiplayer ownership isolation and that ordinary names still work offline. Exercise authorization and live-channel refresh manually using a test channel. No application build was run for this documentation-only exploration.

## Official references

- https://dev.twitch.tv/docs/api/reference/#get-chatters
- https://dev.twitch.tv/docs/api/reference/#get-streams
- https://dev.twitch.tv/docs/authentication/
- https://dev.twitch.tv/docs/eventsub/eventsub-subscription-types/#streamonline

## Product decisions remaining

Confirm whether chat participants are the intended audience; whether existing crew should be renamed; which player's fleet each channel controls; and whether streamer-owned authorization or a moderator service account is preferred. Recommended initial scope: one authorized channel per player, newly created crew only, live-only allocation, persistent assigned names.

## Settings implementation

The shared Settings dialog now includes a Twitch tab. Preferences are saved immediately using the existing AuSettings store: `Twitch/Enabled` (false by default), `Twitch/ClientId`, `Twitch/Channel` (lowercase login), and `Twitch/NpcScope` (`crew`, `crew-officers`, or `all`; default `crew`). Disabling preserves the other preferences. Client ID and channel inputs restrict characters; no credentials are collected. This slice configures local preferences only and displays that account connection and NPC naming are pending.

## Account connection and participant preview

The Twitch tab uses Twitch's Device Code Grant for a **Public** registered application. Enter its client ID and a channel login, enable integration, and click **Connect Twitch account**. Finish consent in the browser as the broadcaster or a moderator of the channel, then click **Fetch chat participants**. This preview can be tested while offline on Twitch; live-only allocation belongs to the later naming service.

The only requested scope is `moderator:read:chatters`. Tokens are validated after authorization and hourly; invalid tokens end the local session. Credentials are session-only and never saved in QSettings. Closing/destroying the settings window or disabling integration requires reconnecting. Persistent credential storage and refresh are deferred. Disconnect clears the local session; it does not revoke the app grant in Twitch account settings.

Requests have a 30-second timeout. Cancelling or changing client ID invalidates pending callbacks. Participant requests paginate (up to 100 pages), deduplicate by user ID, and show a sorted name preview. Permission failures, expired authorization, rate limits, empty channels and partial pagination are reported. NPC creation is not connected to this preview yet.

### Testing

`tests/twitch/twitch.pro` is a standalone Qt 5 test target. Build it out of tree with qmake, make, then run `twitch-connection-test`. Mocked-network cases cover device authorization/validation, participant pagination and deduplication, cancellation, missing scope, authorization rejection, and channel permission failure. Seven QtTest results passed (including initialization and cleanup). The native AuSceneClient target also builds successfully. Live OAuth requires the user's registered application and browser consent.

References: [device flow](https://dev.twitch.tv/docs/authentication/getting-tokens-oauth/#device-code-grant-flow), [token validation](https://dev.twitch.tv/docs/authentication/validate-tokens/), [chatters](https://dev.twitch.tv/docs/api/reference/#get-chatters).

## Manual assignment and client rename history

`Twitch/AssignmentMode` stores `manual` (default) or `auto`. The settings UI offers **Manual name assignment** and **Auto name assignment**. Automatic mode is deliberately inactive pending a background coordinator; its status text explains that it sends no requests. Switching modes cancels outstanding manual queue entries.

In Manual mode, fetch participants, select **Preview manual name assignments**, review proposed pairs and check the NPCs to rename, then **Submit selected assignments**. The current role scope, empire and ship command rules filter the preview. It shows up to 50 eligible NPCs, avoids occupied/reserved names and case-insensitive duplicates, and uses the existing NpcRename API. Selected requests are spaced half a second apart. NPC eligibility and current names are checked again immediately before submission. The server remains authoritative; its outcome is reported through the game log. No new server change is required beyond the NPC rename implementation.

A persistent local history marker under `NpcNaming/Submitted/<server>/<local-or-online>/<npc-id>` stores the requested name, source, state and timestamps. Both manual NPC renames and Twitch assignments record submissions. State is `submitted` until a subsequent preview observes the requested name in the authoritative scene, then becomes `confirmed`. Either state protects the NPC from future Twitch previews. Rejected requests stay reserved conservatively; sending is never reported as confirmation. These records are client preferences, not part of a game save, and do not synchronize between machines. Local saves share a conservative namespace, so reused NPC IDs can cause extra protection rather than unintended reassignment.

To protect names that existed before this history feature, select the appropriate preview rows and choose **Protect selected NPCs as already renamed**. This writes state `protected` and sends no rename requests. You can preview/protect NPCs even before fetching participant names. The client cannot infer historical renames or renames on another machine; review current names before selection. There is no server-side compare-and-set guard against a rename that races with submission. Automatic background assignment remains unavailable until that behavior and cross-client expectations are resolved.

Tests cover name-pool deduplication/occupied-name exclusion and local history persistence, submitted versus confirmed state, protected state, and replacing a prior history record. Live game requests and server-result observation still require gameplay verification.

Each preview row now has a Twitch participant dropdown, including **Do not assign**. Selecting a name used by another row swaps the two pairings, so participants stay unique while you adjust the proposal. Row checkboxes control which pairs are submitted. Submission checks for valid, distinct names before queuing any requests; selecting no usable pair keeps the preview open with guidance.
