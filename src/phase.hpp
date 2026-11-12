/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Tristan Gauci
 *
 * When to prompt, equip, start the retail pad, and put the previous weapon back.
 * Memory and the pad hooks live in mod.cpp; this only decides the next phase.
 */
#pragma once

#include <cstdint>

namespace gadget {

constexpr int kTrespasser = 0x1A;
constexpr int kHydro = 0x16;
constexpr int kSwing = 0x0C;
constexpr int kSlotReady = 2;

constexpr int kEquipTimeout = 180;
constexpr int kArmTimeout = 45;
/* The game rechecks the line of sight once the Swingshot is out. This is how long that may take. */
constexpr int kSwingArmTimeout = 20;
constexpr int kRestoreTimeout = 180;

constexpr int kSwingNode = 758;
constexpr int kSwingNodeMoving = 803;

constexpr int kHeroGrappleEntry = 0x24;
constexpr int kHeroGrapplePull = 0x25;
constexpr int kHeroGrappleOrbit = 0x26;
constexpr int kHeroSwingEntry = 0x2C;
constexpr int kModeGrapple = 0xD;
constexpr int kModeGrappleSwing = 0xE;

enum class PadKind { None, Trespasser, Hydro, Swing };

enum class Phase { Idle, Equipping, Arm, Using, Restore };

inline int gadget_for(PadKind kind) {
    if (kind == PadKind::Trespasser) {
        return kTrespasser;
    }
    if (kind == PadKind::Hydro) {
        return kHydro;
    }
    if (kind == PadKind::Swing) {
        return kSwing;
    }
    return 0;
}

/* Absolute yaw and elevation errors, in radians, already wrapped to half a turn. */
struct SwingLook {
    int oclass = 0;
    int enable_word = 0;
    int moby_state = 0;
    int moby_flags = 0;
    int hero_state = 0;
    float distance = 0.f;
    float xy = 0.f;
    float hero_above = 0.f;
    float yaw_error = 0.f;
    float elevation_error = 0.f;
    float range_override = 0.f;
};

/* Range, cone, and enable-word filters from select_hero_grapple_target and
 * select_hero_grapple_swing_target, before the clip volume and the line of sight.
 * The quick-use asks those two functions, which apply this and the rest. */
inline bool swing_in_cone(const SwingLook& look) {
    if ((look.moby_flags & 1) != 0 || look.moby_state >= 0xFE) {
        return false;
    }
    float yaw_limit = 0.f;
    float elevation_limit = -1.f;
    float range = 0.f;
    if (look.oclass == kSwingNode) {
        if (look.enable_word != 0) {
            return false;
        }
        range = 30.f;
        yaw_limit = 0.5235988f;
    } else if (look.oclass == kSwingNodeMoving) {
        if (look.enable_word == 0) {
            return false;
        }
        range = 16.f;
        yaw_limit = 0.7853982f;
        elevation_limit = 0.7853982f;
        if (look.hero_state == 0x2D) {
            yaw_limit = 1.2217305f;
            elevation_limit = 1.134464f;
        }
    } else {
        return false;
    }
    if (look.range_override != 0.f) {
        range = look.range_override;
    }
    if (look.hero_state == 1 || look.hero_state == 0x1E) {
        yaw_limit = 0.08726646f;
        elevation_limit = 0.08726646f;
    }
    if (look.distance > range) {
        return false;
    }
    if (look.oclass == kSwingNode && look.xy < 4.f) {
        return false;
    }
    if (look.oclass == kSwingNodeMoving && look.hero_above > 3.f) {
        return false;
    }
    if (look.yaw_error > yaw_limit) {
        return false;
    }
    if (elevation_limit > 0.f && look.elevation_error > elevation_limit) {
        return false;
    }
    return true;
}

inline bool swing_busy(int hero_state, int control_mode) {
    return hero_state == kHeroGrappleEntry || hero_state == kHeroGrapplePull ||
           hero_state == kHeroGrappleOrbit || hero_state == kHeroSwingEntry ||
           control_mode == kModeGrapple || control_mode == kModeGrappleSwing;
}

/* State 4 of the held gadget still requires Circle when the hook arrives. These are past that. */
inline bool swing_committed(int hero_state, int gadget_state) {
    return hero_state == kHeroGrapplePull || hero_state == kHeroGrappleOrbit ||
           hero_state == kHeroSwingEntry || gadget_state == 5 || gadget_state == 9;
}

/* 0 and 1 are the held gadget idle. A negative read means the moby is gone. */
inline bool swing_reeled(int gadget_state) {
    return gadget_state < 0 || gadget_state == 0 || gadget_state == 1;
}

/* Trespasser state 1 is the wait. State 4 is already solved, including on spawn. */
inline bool trespasser_waiting(int state) { return state == 1; }

/* Hydro state 2 is the wait. The use is 3, 4 (camera) and 5 (timed fill). */
inline bool hydro_waiting(int state) { return state == 2; }

/* command bit 2 can fill an empty tank; bit 1 can empty a full one. */
inline bool hydro_can_use(bool tank_full, int command) {
    return tank_full ? (command & 1) != 0 : (command & 2) != 0;
}

inline bool use_started(PadKind kind, int state) {
    if (kind == PadKind::Trespasser) {
        return state == 2 || state == 3;
    }
    if (kind == PadKind::Hydro) {
        return state == 3 || state == 4 || state == 5;
    }
    return false;
}

/* Trespasser 1 is cancel, 4 is done. Hydro 2 is back to the wait, after 4 or 5. */
inline bool use_finished(PadKind kind, int state) {
    if (state < 0) {
        return false;
    }
    if (kind == PadKind::Trespasser) {
        return state == 1 || state == 4;
    }
    if (kind == PadKind::Hydro) {
        return state == 2;
    }
    return false;
}

/* What the player and the pad look like this tick, after the game has updated. */
struct View {
    bool playing = false;
    bool alive = false;
    bool loads_changed = false;
    bool at_prompt = false;
    bool left = false;
    PadKind prompt = PadKind::None;
    int target_state = -1;
    int hand_id = 0;
    int hand_state = 0;
    bool hand_moby = false;
    bool use_pressed = false;
    /* Swing only. target_state is the hero state for that kind, and this is the held moby. */
    int control_mode = 0;
    int gadget_state = -1;
    bool trigger_held = false;
};

/* Flags are for this tick only. Phase, the saved weapon and restore_sent stay. */
struct Session {
    Phase phase = Phase::Idle;
    PadKind kind = PadKind::None;
    int saved_id = 0;
    int gadget_id = 0;
    int frames = 0;
    bool restore_sent = false;
    /* Eligible to take Circle, and to show the game's interaction prompt. */
    bool show_prompt = false;
    bool swallow = false;
    bool inject = false;
    bool equip = false;
    bool restore = false;
    bool clear_pending = false;
};

inline Session stopped() { return {}; }

inline bool hand_is(const View& view, int item) {
    return view.hand_id == item && view.hand_state == kSlotReady && view.hand_moby;
}

/* The switch has left the saved weapon, or the slot is mid-holster. */
inline bool switch_started(const Session& session, const View& view) {
    return view.hand_id == session.gadget_id || view.hand_state != kSlotReady;
}

inline void go_idle(Session& session, bool clear_pending) {
    session = stopped();
    session.clear_pending = clear_pending;
}

/* Put the saved weapon back once the gadget is out. If it never left the hand, drop the request. */
inline void finish_attempt(Session& session, const View& view) {
    const bool started = switch_started(session, view);
    const bool already =
        session.saved_id == 0 || (view.hand_id == session.saved_id && view.hand_state == kSlotReady);
    if (!started || already) {
        go_idle(session, true);
        return;
    }
    session.phase = Phase::Restore;
    session.frames = 0;
    session.restore_sent = true;
    session.clear_pending = true;
    session.restore = true;
    // The gadget is still out for the holster. Swallow Circle so the pad does not start again.
    session.swallow = true;
}

inline Session step(Session session, const View& view) {
    session.show_prompt = false;
    session.swallow = false;
    session.inject = false;
    session.equip = false;
    session.restore = false;
    session.clear_pending = false;

    if (!view.alive || view.loads_changed) {
        return stopped();
    }

    switch (session.phase) {
    case Phase::Idle: {
        const int want = gadget_for(view.prompt);
        const bool holding = want != 0 && view.hand_id == want;
        const bool eligible = view.playing && view.at_prompt && want != 0;
        // The Swingshot has no pad light. Taking the button is the whole prompt.
        session.show_prompt = eligible && view.prompt != PadKind::Swing;
        session.swallow = eligible && !holding;
        // Circle is also fire. A hold that walked into range stays with that weapon.
        // A new press still starts the swing: the previous tick was already swallowing.
        if (view.prompt == PadKind::Swing && view.trigger_held && !view.use_pressed) {
            session.swallow = false;
        }
        if (session.swallow && view.use_pressed) {
            session.phase = Phase::Equipping;
            session.kind = view.prompt;
            session.gadget_id = want;
            session.saved_id = view.hand_id;
            session.frames = 0;
            session.restore_sent = false;
            session.equip = true;
            session.show_prompt = false;
            // Keep Circle off the weapon still in hand until the gadget is ready.
            session.swallow = true;
        }
        break;
    }
    case Phase::Equipping: {
        session.frames++;
        const bool ready = hand_is(view, session.gadget_id);
        const bool manual = view.hand_state == kSlotReady && view.hand_id != session.saved_id &&
                            view.hand_id != session.gadget_id;
        // A versatarget is not a pad. Turning away during the holster is the game's own recheck.
        const bool left = session.kind != PadKind::Swing && view.left;
        const bool give_up = !view.playing || left || session.frames >= kEquipTimeout;
        if (manual) {
            go_idle(session, true);
        } else if (ready && view.playing && !left) {
            session.phase = Phase::Arm;
            session.frames = 0;
            if (session.kind == PadKind::Hydro || session.kind == PadKind::Swing) {
                session.inject = true;
            }
        } else if (give_up) {
            finish_attempt(session, view);
        } else {
            session.swallow = true;
        }
        break;
    }
    case Phase::Arm: {
        session.frames++;
        const bool started = session.kind == PadKind::Swing
                                 ? swing_busy(view.target_state, view.control_mode)
                                 : use_started(session.kind, view.target_state);
        const int limit = session.kind == PadKind::Swing ? kSwingArmTimeout : kArmTimeout;
        const bool left = session.kind != PadKind::Swing && view.left;
        if (started) {
            session.phase = Phase::Using;
            session.frames = 0;
            if (session.kind == PadKind::Swing &&
                (!swing_committed(view.target_state, view.gadget_state) || view.trigger_held)) {
                session.inject = true;
            }
        } else if (!view.playing || left || session.frames >= limit) {
            finish_attempt(session, view);
        } else if (session.kind == PadKind::Swing) {
            // Hold Circle across the hook's flight. One press edge dies before state 4 reads it.
            session.inject = true;
        }
        break;
    }
    case Phase::Using: {
        const bool manual = view.hand_state == kSlotReady && view.hand_id != session.gadget_id &&
                            view.hand_id != session.saved_id;
        if (manual) {
            go_idle(session, false);
        } else if (session.kind == PadKind::Swing) {
            const bool busy = swing_busy(view.target_state, view.control_mode);
            if (!busy && swing_reeled(view.gadget_state)) {
                finish_attempt(session, view);
            } else {
                // Through the flight, Circle stays down. After the hook commits, only a held
                // button keeps it down, so a chain can start and a tap can let go.
                const bool flying = busy && !swing_committed(view.target_state, view.gadget_state);
                session.inject = flying || view.trigger_held;
            }
        } else if (view.target_state >= 0xFE || use_finished(session.kind, view.target_state)) {
            finish_attempt(session, view);
        }
        break;
    }
    case Phase::Restore: {
        session.frames++;
        const bool back = session.saved_id == 0 ||
                          (view.hand_id == session.saved_id && view.hand_state == kSlotReady);
        const bool manual = view.hand_state == kSlotReady && view.hand_id != session.gadget_id &&
                            view.hand_id != session.saved_id;
        if (back || manual || session.frames >= kRestoreTimeout) {
            go_idle(session, false);
        } else {
            session.swallow = true;
            if (!session.restore_sent) {
                session.restore_sent = true;
                session.clear_pending = true;
                session.restore = true;
            }
        }
        break;
    }
    }
    return session;
}

}  // namespace gadget
