/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Tristan Gauci
 *
 * The quick-use phases, away from the game: prompt, equip, arm, the retail exit, restore.
 */
#include "phase.hpp"

#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "fail: %s\n", what);
        ++failures;
    }
}

gadget::View standing(gadget::PadKind pad) {
    gadget::View view;
    view.playing = true;
    view.alive = true;
    view.at_prompt = true;
    view.prompt = pad;
    view.hand_id = 8;
    view.hand_state = gadget::kSlotReady;
    view.hand_moby = true;
    view.target_state = pad == gadget::PadKind::Trespasser ? 1 : 2;
    return view;
}

}  // namespace

int main() {
    using namespace gadget;

    expect(!trespasser_waiting(4), "a solved trespasser pad is not waiting");
    expect(trespasser_waiting(1), "trespasser state 1 waits");
    expect(hydro_waiting(2), "hydro state 2 waits");
    expect(!hydro_waiting(4), "the hydro camera is not the wait");
    expect(hydro_can_use(false, 0x02), "an empty tank needs command bit 2");
    expect(!hydro_can_use(false, 0x01), "bit 1 does not fill an empty tank");
    expect(hydro_can_use(true, 0x01), "a full tank needs command bit 1");
    expect(!hydro_can_use(true, 0x02), "bit 2 does not empty a full tank");

    View idle_view;
    idle_view.playing = true;
    idle_view.alive = true;
    idle_view.hand_id = 8;
    idle_view.hand_state = kSlotReady;
    idle_view.hand_moby = true;
    Session session = step(Session{}, idle_view);
    expect(session.phase == Phase::Idle, "off a pad stays idle");
    expect(!session.show_prompt, "off a pad shows nothing");
    expect(!session.swallow, "off a pad does not take Circle");

    View pad = standing(PadKind::Trespasser);
    session = step(Session{}, pad);
    expect(session.show_prompt && session.swallow, "an owned trespasser pad prompts and takes Circle");

    pad.hand_id = kTrespasser;
    session = step(Session{}, pad);
    expect(session.show_prompt && !session.swallow, "Circle passes through when the trespasser is already out");
    pad.use_pressed = true;
    session = step(Session{}, pad);
    expect(session.phase == Phase::Idle && !session.equip, "a press while it is already out does not equip");

    pad = standing(PadKind::Trespasser);
    pad.at_prompt = false;
    session = step(Session{}, pad);
    expect(!session.show_prompt, "not owned, or not waiting, shows nothing");

    pad = standing(PadKind::Hydro);
    pad.playing = false;
    session = step(Session{}, pad);
    expect(!session.show_prompt, "a menu hides the prompt");

    pad = standing(PadKind::Trespasser);
    pad.use_pressed = true;
    session = step(Session{}, pad);
    expect(session.phase == Phase::Equipping && session.equip, "Circle starts the equip");
    expect(session.saved_id == 8 && session.gadget_id == kTrespasser, "the wrench is saved and the trespasser requested");
    expect(!session.show_prompt && session.swallow, "the prompt goes away and Circle stays off the old weapon");

    pad.use_pressed = false;
    pad.hand_id = kTrespasser;
    session = step(session, pad);
    expect(session.phase == Phase::Arm && !session.inject, "a ready trespasser arms without injecting Circle");

    pad.target_state = 2;
    session = step(session, pad);
    expect(session.phase == Phase::Using, "trespasser state 2 is the puzzle");
    session = step(session, pad);
    expect(session.phase == Phase::Using, "the puzzle stays up");
    pad.target_state = 3;
    session = step(session, pad);
    expect(session.phase == Phase::Using, "committing the puzzle is still the use");
    pad.target_state = 4;
    session = step(session, pad);
    expect(session.phase == Phase::Restore && session.restore, "a solved puzzle puts the wrench back");

    pad = standing(PadKind::Trespasser);
    pad.use_pressed = true;
    session = step(Session{}, pad);
    pad.use_pressed = false;
    pad.hand_id = kTrespasser;
    session = step(session, pad);
    pad.target_state = 2;
    session = step(session, pad);
    pad.target_state = 1;
    session = step(session, pad);
    expect(session.phase == Phase::Restore && session.restore, "cancelling the puzzle puts the wrench back");

    pad = standing(PadKind::Hydro);
    pad.use_pressed = true;
    session = step(Session{}, pad);
    expect(session.gadget_id == kHydro, "hydro is the requested gadget");
    pad.use_pressed = false;
    pad.hand_id = kHydro;
    pad.target_state = 2;
    session = step(session, pad);
    expect(session.phase == Phase::Arm && session.inject, "a ready hydrodisplacer injects one Circle");
    session = step(session, pad);
    expect(session.phase == Phase::Arm && !session.inject, "Circle is injected once");
    pad.target_state = 4;
    session = step(session, pad);
    expect(session.phase == Phase::Using, "the hydro camera is still the use");
    pad.target_state = 5;
    session = step(session, pad);
    expect(session.phase == Phase::Using, "the timed fill is still the use");
    pad.target_state = 2;
    session = step(session, pad);
    expect(session.phase == Phase::Restore, "hydro returning to the wait restores");

    pad = standing(PadKind::Trespasser);
    pad.use_pressed = true;
    session = step(Session{}, pad);
    pad.use_pressed = false;
    pad.left = true;
    session = step(session, pad);
    expect(session.phase == Phase::Idle && session.clear_pending, "leaving before the switch drops the request");
    expect(!session.restore, "leaving before the switch does not restore");

    pad = standing(PadKind::Trespasser);
    pad.use_pressed = true;
    session = step(Session{}, pad);
    pad.use_pressed = false;
    pad.hand_state = 3;
    pad.left = true;
    session = step(session, pad);
    expect(session.phase == Phase::Restore && session.restore, "leaving mid-holster still restores");

    pad = standing(PadKind::Trespasser);
    pad.use_pressed = true;
    session = step(Session{}, pad);
    pad.use_pressed = false;
    pad.hand_id = 15;
    session = step(session, pad);
    expect(session.phase == Phase::Idle && session.clear_pending, "a manual switch during the wait is kept");

    pad = standing(PadKind::Trespasser);
    pad.use_pressed = true;
    session = step(Session{}, pad);
    pad.use_pressed = false;
    for (int i = 0; i < kEquipTimeout - 1; ++i) {
        session = step(session, pad);
    }
    expect(session.phase == Phase::Equipping, "the equip waits out the holster");
    session = step(session, pad);
    expect(session.phase == Phase::Idle && session.clear_pending, "an equip that never starts is dropped");

    pad = standing(PadKind::Trespasser);
    pad.use_pressed = true;
    session = step(Session{}, pad);
    pad.use_pressed = false;
    pad.hand_id = kTrespasser;
    session = step(session, pad);
    expect(session.phase == Phase::Arm, "armed");
    for (int i = 0; i < kArmTimeout - 1; ++i) {
        session = step(session, pad);
    }
    expect(session.phase == Phase::Arm, "arm waits for the pad to take the use");
    session = step(session, pad);
    expect(session.phase == Phase::Restore && session.restore, "a use that never starts restores");

    pad = standing(PadKind::Trespasser);
    pad.use_pressed = true;
    session = step(Session{}, pad);
    pad.use_pressed = false;
    pad.hand_id = kTrespasser;
    session = step(session, pad);
    pad.target_state = 2;
    session = step(session, pad);
    pad.hand_id = 15;
    session = step(session, pad);
    expect(session.phase == Phase::Idle && !session.restore, "a manual switch during the puzzle is kept");

    pad = standing(PadKind::Hydro);
    pad.use_pressed = true;
    session = step(Session{}, pad);
    pad.alive = false;
    session = step(session, pad);
    expect(session.phase == Phase::Idle && !session.restore && !session.equip, "death clears the phase without writing");

    pad = standing(PadKind::Trespasser);
    pad.use_pressed = true;
    session = step(Session{}, pad);
    pad.use_pressed = false;
    pad.hand_id = kTrespasser;
    session = step(session, pad);
    pad.target_state = 2;
    session = step(session, pad);
    pad.loads_changed = true;
    session = step(session, pad);
    expect(session.phase == Phase::Idle && !session.restore, "a level load clears the phase without writing");

    pad = standing(PadKind::Trespasser);
    pad.use_pressed = true;
    session = step(Session{}, pad);
    pad.use_pressed = false;
    pad.hand_id = kTrespasser;
    session = step(session, pad);
    pad.target_state = 2;
    session = step(session, pad);
    pad.target_state = -1;
    session = step(session, pad);
    expect(session.phase == Phase::Using, "a missed read does not end the puzzle");

    SwingLook node;
    node.oclass = kSwingNode;
    node.distance = 10.f;
    node.xy = 10.f;
    node.yaw_error = 0.2f;
    expect(swing_in_cone(node), "a static node inside the cone is eligible");
    node.xy = 3.f;
    expect(!swing_in_cone(node), "a static node closer than 4 in the plane is refused");
    node.xy = 10.f;
    node.enable_word = 1;
    expect(!swing_in_cone(node), "a static node with a nonzero enable word is off");
    node.enable_word = 0;
    node.distance = 31.f;
    expect(!swing_in_cone(node), "a static node past 30 is out of range");
    node.distance = 20.f;
    node.range_override = 12.f;
    expect(!swing_in_cone(node), "a node range override replaces the default");
    node.range_override = 0.f;
    node.yaw_error = 0.6f;
    expect(!swing_in_cone(node), "a static node outside the yaw cone is refused");
    node.yaw_error = 0.05f;
    node.hero_state = 1;
    node.elevation_error = 0.2f;
    expect(!swing_in_cone(node), "look mode uses the tight cone for yaw and elevation");
    node.elevation_error = 0.02f;
    expect(swing_in_cone(node), "look mode accepts a node the camera is on");
    node.hero_state = 0;
    node.moby_flags = 1;
    expect(!swing_in_cone(node), "a hidden node is refused");
    node.moby_flags = 0;
    node.moby_state = 0xFE;
    expect(!swing_in_cone(node), "a dead node is refused");

    node = SwingLook{};
    node.oclass = kSwingNodeMoving;
    node.enable_word = 1;
    node.distance = 10.f;
    node.xy = 10.f;
    node.yaw_error = 0.4f;
    node.elevation_error = 0.4f;
    expect(swing_in_cone(node), "a swinging node inside both cones is eligible");
    node.enable_word = 0;
    expect(!swing_in_cone(node), "a swinging node with a zero enable word is off");
    node.enable_word = 1;
    node.hero_above = 3.5f;
    expect(!swing_in_cone(node), "a swinging node more than 3 below the hero is refused");
    node.hero_above = 0.f;
    node.yaw_error = 1.0f;
    expect(!swing_in_cone(node), "a swinging node outside the yaw cone is refused");
    node.hero_state = 0x2D;
    expect(swing_in_cone(node), "the item-release fall widens the swinging cone");

    View swing = standing(PadKind::Swing);
    swing.target_state = 0;
    session = step(Session{}, swing);
    expect(!session.show_prompt && session.swallow, "a node takes the button and draws no pill");
    swing.trigger_held = true;
    session = step(Session{}, swing);
    expect(!session.swallow && session.phase == Phase::Idle, "a fire button already held is left on the weapon");
    swing.use_pressed = true;
    session = step(Session{}, swing);
    expect(session.phase == Phase::Equipping && session.swallow, "a press while held still equips the swingshot");
    swing = standing(PadKind::Swing);
    swing.target_state = 0;
    session = step(Session{}, swing);
    swing.hand_id = kSwing;
    session = step(Session{}, swing);
    expect(!session.swallow && !session.equip, "the swingshot already out is left to the game");

    swing = standing(PadKind::Swing);
    swing.target_state = 0;
    swing.use_pressed = true;
    session = step(Session{}, swing);
    expect(session.phase == Phase::Equipping && session.gadget_id == kSwing, "the button equips the swingshot");
    expect(session.saved_id == 8 && session.swallow && !session.show_prompt, "the wrench is saved and the pill stays down");
    swing.use_pressed = false;
    swing.left = true;
    session = step(session, swing);
    expect(session.phase == Phase::Equipping, "turning away during the holster does not cancel");
    swing.left = false;
    swing.hand_id = kSwing;
    swing.gadget_state = 0;
    session = step(session, swing);
    expect(session.phase == Phase::Arm && session.inject, "a ready swingshot holds Circle");
    session = step(session, swing);
    expect(session.phase == Phase::Arm && session.inject, "Circle stays held until the swing starts");
    swing.target_state = kHeroGrappleEntry;
    swing.control_mode = kModeGrapple;
    session = step(session, swing);
    expect(session.phase == Phase::Using && session.inject, "the grapple entry is the use and Circle is still held");
    swing.gadget_state = 5;
    swing.trigger_held = false;
    session = step(session, swing);
    expect(session.phase == Phase::Using && !session.inject, "once the hook commits, a released button is released");
    swing.trigger_held = true;
    session = step(session, swing);
    expect(session.phase == Phase::Using && session.inject, "holding the button keeps Circle for a chain");
    swing.trigger_held = false;
    swing.target_state = 0;
    swing.control_mode = 0;
    swing.gadget_state = 7;
    session = step(session, swing);
    expect(session.phase == Phase::Using && !session.restore && !session.inject, "the reel keeps the swingshot out");
    swing.gadget_state = 1;
    session = step(session, swing);
    expect(session.phase == Phase::Restore && session.restore, "an idle hook puts the wrench back");

    swing = standing(PadKind::Swing);
    swing.target_state = 0;
    swing.use_pressed = true;
    session = step(Session{}, swing);
    swing.use_pressed = false;
    swing.hand_id = kSwing;
    session = step(session, swing);
    expect(session.phase == Phase::Arm, "armed at a blocked node");
    for (int i = 0; i < kSwingArmTimeout - 1; ++i) {
        session = step(session, swing);
    }
    expect(session.phase == Phase::Arm, "a refused node waits out the short arm");
    session = step(session, swing);
    expect(session.phase == Phase::Restore && session.restore, "a node the game refuses restores");

    swing = standing(PadKind::Swing);
    swing.target_state = 0;
    swing.use_pressed = true;
    session = step(Session{}, swing);
    swing.alive = false;
    session = step(session, swing);
    expect(session.phase == Phase::Idle && !session.restore, "death during a swing clears without restoring");

    swing = standing(PadKind::Swing);
    swing.target_state = 0;
    swing.use_pressed = true;
    session = step(Session{}, swing);
    swing.use_pressed = false;
    swing.hand_id = kSwing;
    session = step(session, swing);
    swing.target_state = kHeroSwingEntry;
    swing.control_mode = kModeGrappleSwing;
    session = step(session, swing);
    expect(session.phase == Phase::Using, "a swinging node starts the use");
    swing.hand_id = 15;
    session = step(session, swing);
    expect(session.phase == Phase::Idle && !session.restore, "a manual switch during the swing is kept");

    pad = standing(PadKind::Trespasser);
    pad.use_pressed = true;
    session = step(Session{}, pad);
    pad.use_pressed = false;
    pad.hand_id = kTrespasser;
    session = step(session, pad);
    pad.target_state = 2;
    session = step(session, pad);
    pad.target_state = 4;
    session = step(session, pad);
    expect(session.phase == Phase::Restore && session.restore, "restore queued");
    pad.hand_id = 8;
    pad.hand_state = kSlotReady;
    session = step(session, pad);
    expect(session.phase == Phase::Idle && !session.restore, "the saved weapon coming back goes idle");

    if (failures != 0) {
        std::fprintf(stderr, "%d checks failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("gadget quick use: ok\n");
    return EXIT_SUCCESS;
}
