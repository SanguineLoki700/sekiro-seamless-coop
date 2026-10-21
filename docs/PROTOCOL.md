# Portable protocol core — version 2

`skcoop_core` is a dependency-free C++20 static library. Public headers are
`skcoop/core/core.hpp` and `skcoop/core/wire.hpp`, namespace `skcoop::core`.
The core implements protocol rules and an in-memory state model. It contains no
Steam implementation, sockets, game hooks, actor factory, save-file adapter,
combat implementation, interpolation or playable multiplayer.

## Identity, admission and reconnection

- The host and up to three guests are identified by nonzero 64-bit `SteamId`
  values. These are declared identifiers, **not authentication claims**. A Steam
  transport must bind the authenticated sender to the `sender` argument before
  invoking any state-changing core API. The core never authenticates an account.
- `Handshake` requires protocol version 2 and exact 32-byte equality for both the
  game executable SHA-256 and compatibility-profile SHA-256. The adapter computes
  those hashes over the actual files. Either digest being all zero means unresolved
  identity and is rejected by the host constructor and handshake encoder/decoder.
  The core does not implement SHA-256.
- `CampaignCheckpoint` carries a nonzero 16-byte campaign ID, a 64-bit durable
  revision, and the adapter's SHA-256 over canonical shared checkpoint bytes.
  Those bytes must include the durable receipts needed for deduplication; personal
  character state is stored separately. A digest in a packet is not proof of its
  contents or authenticity. Verify the actual save bytes before restoring them.
- A new guest learns the campaign ID through authenticated session discovery,
  sends `has_checkpoint=false`, revision zero and a zero state digest, then needs
  a full snapshot. It is not marked synchronized until it acknowledges the host's
  exact checkpoint. This applies even at host revision zero.
- A returning guest with an older revision also needs a snapshot. A different
  campaign, a peer revision ahead of the host, or a same-revision digest conflict
  is rejected. Reconnection never silently rolls a newer peer back.
- Disconnected guests reserve their slots and reconnect by the same identifier.
  The host can explicitly release a disconnected slot. Repeated admission of an
  already-connected identity does not allocate a second slot. Host loss and host
  migration belong to the session adapter; `disconnect(host)` is rejected.
- Advancing the host checkpoint clears guest synchronization until the new
  checkpoint is acknowledged. Only connected, synchronized members may enter
  gameplay; the adapter enforces that barrier.

## Session envelopes and lifetime

Every wire message, including handshakes, snapshots, durable events and checkpoint
transfers, carries a nonzero 16-byte `SessionId`. `encode(message, session)` requires
it explicitly. `decode(packet, expected_session)` requires the caller's active
incarnation and rejects a different ID before returning any payload. There is no
public context-free decoder. Successful results retain their session stamp;
`result.matches_session(active_session)` rechecks queued results before dispatch.
Raw state APIs such as `SnapshotStore::apply` and `DurableLedger::apply` are trusted
internal APIs, not network decoders. Do not pass unvalidated network data to them.

The future session adapter must create a fresh, unique incarnation when starting
a host session or loading a checkpoint that replaces the running world, and
announce it through authenticated session discovery. Ordinary peer reconnects
to the same running host retain that host's current incarnation. Rotate the ID
before restarting counters/stores, discard old queued work, and serialize rotation
with decode/apply. Never derive the expected ID from the packet being validated.
The core validates nonzero values and exact equality; it does not generate IDs,
guarantee their uniqueness, implement discovery or authenticate Steam accounts.
Deterministic IDs in tests are fixtures, not production credentials.

Campaign IDs, revisions and durable event IDs remain persistent across host
incarnations. A verified save may restore those receipts explicitly, then transfer
them in the current session's envelope. A packet from the previous incarnation
is rejected; deliberately retransmitting a previously confirmed event in a new
envelope still returns `duplicate` from the restored ledger. Do not reset durable
event IDs on session rotation. A session ID is not a secret or a MAC; this binding
rejects old envelopes, not forged/tampered traffic from an unauthenticated source.

## Entity authority and snapshots

`EntityId` is `(map:u32, generation:u32, object:u64)`, never a memory address.
Generation is a session-wide monotonically increasing loading epoch. `begin_area`
must use a strictly newer generation, clears previous entities, and resets the
snapshot sequence. No generation/sequence wraparound is accepted; start a new
session before exhausting an integer range.

`can_simulate` grants enemy simulation to the host and player simulation to the
owning player. It is a simulation lease, not permission to decide shared damage,
posture, phases, deathblows or rewards. Those decisions remain host-authoritative
in the future game adapter. The host relays canonical snapshots, including player
states; `SnapshotStore` accepts only host-originated batches.

A snapshot contains at most 128 entities. The entire batch is checked before
replacement: matching map/generation, strictly increasing sequence, unique
nonzero object IDs, known entity kinds, nonzero owners, and finite coordinates
and yaw. Enemy owners must be the host. The game adapter additionally validates
player owners against the authenticated roster and resolves IDs to live actors
on the game thread. Older generations, duplicate sequences and malformed batches
cannot partially update state. Entity IDs from a previous loading epoch cannot
resolve to a newly allocated actor.

## Durable events and rewards

`DurableLedger` accepts only host-issued events from the authenticated host.
Revisions are contiguous from 1. Event identity is `(host SteamId, sequence)`;
the host assigns a unique nonzero sequence to each new event. Replaying an exact
event returns `duplicate`; changing its payload under the same ID returns
`conflicting_id`. Neither changes revision, flags or reward balances.

The current event vocabulary is deliberately small: `set_flag` stores a boolean
under a stable flag ID; `grant_reward` adds an unsigned scalar amount for a
recipient ID. It does not encode Sekiro items, currencies or quest definitions.
The future adapter maps these operations to verified game behavior and validates
reward recipients against the roster.

Apply engine effects **only** for `EventResult::applied`. A production save adapter
must make the canonical game effect and its receipt durable together, or restore
both from one consistent checkpoint; the in-memory ledger alone cannot provide
crash-safe exactly-once effects in an external game save.

At most 4,096 receipts are retained. When full, new events fail closed with
`journal_full`; old receipts are never evicted to fake continued idempotence.
This is an explicit prototype capacity, not a claim of complete campaign support.
Compaction/migration requires a new verified persistence design before release.
`restore` replays a complete bounded journal atomically, checks the campaign and
final revision, rejects gaps/duplicate receipts, and refuses revision regression.
An existing confirmed history prefix must match every field of every receipt,
even for an equal-revision restore or a file with a separately verified digest.
Identical restores and prefix-preserving extensions are allowed; a new checkpoint
cannot replace previously confirmed event IDs or payloads.
The save adapter verifies the checkpoint digest separately before calling it.

## Binary wire format

All integers use **little-endian** order. Floats use explicit IEEE-754 binary32
bits in little-endian order. There is no native C++ struct layout, padding,
pointer, platform-size integer or raw enum representation on the wire.

Each packet has a 28-byte header. `SKC1` remains the format-family magic;
the explicit protocol field is now 2, and version-1 packets are rejected:

| Offset | Field |
|---|---|
| 0 | ASCII `SKC1`, 4 bytes |
| 4 | protocol version, u16; must equal 2 |
| 6 | message kind, u8: handshake 1, snapshot 2, event 3, checkpoint 4 |
| 7 | reserved u8; must equal zero |
| 8 | payload length, u32; must equal packet size minus 28 |
| 12 | session incarnation, 16 bytes; must be nonzero and match the active session |

| Payload | Ordered fields | Bytes |
|---|---|---:|
| Checkpoint | campaign[16], revision:u64, state SHA-256[32] | 56 |
| Handshake | player:u64, game SHA-256[32], profile SHA-256[32], checkpoint, has-checkpoint:u8 (0 or 1) | 129 |
| Snapshot | map:u32, generation:u32, sequence:u64, count:u16, entities | 18 + 53 × count |
| Entity | map:u32, generation:u32, object:u64, kind:u8, owner:u64, xyz:3×f32, yaw:f32, HP:u32, posture:u32, animation:u32 | 53 |
| Durable event | campaign[16], revision:u64, issuer:u64, event sequence:u64, kind:u8, subject:u64, value:u32 | 53 |

`EntityKind` values are enemy 1 and player 2. `EventKind` values are set-flag 1 and
grant-reward 2; flags allow only values 0/1, rewards require a nonzero recipient
and amount. Maximum total packet size is 16 KiB. Counts are checked before
allocation. Decoder failures include truncated input, trailing bytes, inconsistent
lengths, unknown version/type, zero/mismatched session IDs, nonzero reserved fields, invalid values, duplicate
entities, nonfinite floats and exceeded bounds. The transport supplies exactly one
complete packet per decode call; fragmentation, delivery reliability, channel
selection, encryption, rate limiting and authentication are outside this codec.
Changing this wire contract requires a protocol version bump and explicit
compatibility decisions. This protocol is not compatible with LukeYui's mods.

## Evidence and tests

With `BUILD_TESTING=ON`, the child CMake file builds `skcoop_core_tests` and
registers it with CTest. Tests cover exact identity checks, four-member limits,
reconnection barriers, host/player authority, stale loading epochs, atomic batch
rejection, reward replay/conflict, bounded journals, atomic restore, a fixed golden
little-endian packet, truncated/corrupted inputs and a four-peer in-memory scenario.

The anti-replay regression was added and run against the original version-1
implementation before changing the wire format. A snapshot from the previous
session used host 1001, map 100, generation 1 and sequence 900 with enemy HP 1.
A new store on the same host/map restarted at generation 1, sequence 1 and HP 100.
The old packet decoded and replaced the new state; the test failed with
`old-session snapshot was accepted after counters restarted`
(`CORE_RESULT FAIL cases=13 failed=1`). Version 2 rejects that envelope with
`wrong_session` before application. Tests cover the same boundary for all four
message kinds, queued-result stamps, zero IDs, the final byte of the session ID,
version-1 refusal and campaign receipt restoration across incarnations.

The four-peer scenario exercises the actual codec and core state rules across four
independent stores/ledgers. It does **not** start four game processes, authenticate
four Steam accounts, test invitations/relay or demonstrate a playable mod.
Those remain separate acceptance gates with game and Steam evidence.
