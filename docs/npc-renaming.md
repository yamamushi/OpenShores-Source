# Manual NPC renaming

Open an NPC's resume and choose **Rename NPC…**. Enter a valid game name; the client submits the request and closes the snapshot resume. The normal thought/log channel reports success or an ownership failure. Reopen the resume after the scene update to see the authoritative name.

This first implementation supports NPCs of your empire aboard a spacecraft in your chain of command. It rejects player avatars, foreign NPCs, dying NPCs, and NPCs outside the current solar system. Land-based NPC renaming is not implemented.

The existing `PE_Rename` packet gains subtype 6 with payload `(quint8 subtype, QString name, AuId targetNpcId)`. Existing subtypes are unchanged. `AuApp::NpcRename` is the reusable client entry point; single-user mode calls the same authoritative rename method directly. Multiplayer requires this server change to be deployed; an older server rejects subtype 6. No server deployment is performed here.

The authoritative method resolves the target from the requesting avatar's solar system, verifies it is a DaNpc, validates the name, checks empire and ship command permissions, sets the name and modified flag, and updates cached roster occupant names and the roster change timestamp. Existing NPC name storage and scene updates carry the result; no save schema change is needed.

## Manual integration checks

- Rename your crew member from its resume; confirm the thought/log result, scene label, reopened resume and ship roster show the new name.
- Save/reload local play or restart the test server and verify the name persists.
- Repeat for an NPC officer, troop or passenger aboard a controlled spacecraft.
- Confirm requests targeting a player, another empire's NPC, an uncontrolled ship, a missing NPC, a dying NPC, or a land-based NPC cannot change the name.
- Confirm blank, oversized and forbidden-character names are rejected.
- Confirm an ordinary avatar or building rename still works with the unchanged payloads.
- Confirm a second client sees the renamed NPC after a scene update.

Live gameplay, multiplayer propagation and save/reload checks require a suitable game/test server and remain to be exercised.

## Build validation

The native macOS Debug `AuSceneClient` target built and linked successfully. The `AuSceneServer` `dtchatchannel.cpp` object also compiled successfully. `git diff --check` passed. This verifies compilation, not the live integration checks above; no server was deployed.

Manual rename requests now also create a client-side history marker before submission. The marker protects the NPC from Twitch assignment even when the server rejects the request. A later Twitch preview observing the requested scene name promotes the marker from submitted to confirmed. Earlier/other-client renames can be protected explicitly from the Twitch assignment preview. See twitch-integration.md for namespace and synchronization limits.
