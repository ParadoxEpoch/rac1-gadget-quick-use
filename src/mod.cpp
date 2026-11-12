/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Tristan Gauci
 *
 * Rac2 quick-use for rac1's Trespasser and Hydrodisplacer pads, and the RaC3 Swingshot
 * versatarget. Owning the gadget is enough: the pad uses its own green light, or a node takes
 * the button only when the retail target check says it is grappleable. The gadget is equipped
 * through the hero's pending/saved/restore fields, the retail use runs, and the previous weapon
 * comes back.
 *
 * Circle is the raw report bit 0x2000, active-low, so report[1] bit 0x20.
 */
#include "layout.hpp"
#include "phase.hpp"

#include "openrac/games/rac1.hpp"
#include "openrac/mod.hpp"

#include <cstdint>
#include <string>

namespace {

constexpr std::uint8_t kCircleBit = 0x20;
constexpr std::uint8_t kTriangleBit = 0x10;
constexpr int kWrench = 8;

/* Which buttons start a quick-use. The gadget itself still fires on Circle. */
enum class Trigger { Circle, Triangle, Both };

struct App {
    openrac::Mod* mod = nullptr;
    gadget::Disc disc{};
    gadget::Session session;
    std::uint32_t target = 0;
    std::uint32_t loads = 0;
    /* last_weapon_id[0] from before this quick-use. Restored once the wrench is back, so Circle
     * still returns to the weapon that was equipped before Square. */
    std::int32_t circle_item = 0;
    bool circle_item_held = false;
    bool loads_init = false;
    bool prev_circle = false;
    bool prev_triangle = false;
    bool use_pressed = false;
    /* The quick-use button, from the report before this tick swallows or injects it. */
    bool trigger_down = false;
    /* The game's own try_set_help_message, resolved at load. */
    int (*try_set)(int, int) = nullptr;
    /* select_hero_grapple_swing_target and select_hero_grapple_target. */
    void (*select_swing)() = nullptr;
    int (*select_grapple)() = nullptr;
    gadget::PadKind prompt_kind = gadget::PadKind::None;
    /* While the prompt is up, one help entry points at our line instead of its own. */
    int prompt_level = -1;
    std::uint32_t prompt_entry = 0;
    std::uint32_t prompt_saved_text = 0;
    bool prompt_scratch_held = false;
    std::uint8_t prompt_scratch[0x50]{};
    bool prompt_gate_held = false;
    std::int32_t prompt_saved_gate = 0;
};

App& app() {
    static App state;
    return state;
}

Trigger trigger_mode() {
    if (app().mod == nullptr) {
        return Trigger::Circle;
    }
    const std::string value = app().mod->setting<std::string>("trigger");
    if (value == "triangle") {
        return Trigger::Triangle;
    }
    if (value == "both") {
        return Trigger::Both;
    }
    return Trigger::Circle;
}

bool trigger_uses_circle(Trigger mode) { return mode != Trigger::Triangle; }

bool trigger_uses_triangle(Trigger mode) { return mode != Trigger::Circle; }

/* Pad-icon glyphs: 0x11 Circle, 0x12 Triangle. Both shows Triangle. */
char prompt_glyph(Trigger mode) { return mode == Trigger::Circle ? '\x11' : '\x12'; }

struct Seen {
    gadget::View view;
    std::uint32_t candidate = 0;
};

/* Puts the held item back after a pad update that was shown the gadget, so the green light is
 * the game's own and the rest of the tick still sees what is really equipped. */
struct HeldPaint {
    openrac::Memory mem{nullptr};
    std::uint32_t item_at = 0;
    std::int32_t item = 0;
    std::uint32_t moby_at = 0;
    std::uint32_t moby = 0;
    std::uint32_t command_at = 0;
    std::uint8_t command = 0;
    bool item_on = false;
    bool moby_on = false;
    bool command_on = false;

    void undo() {
        if (command_on) {
            mem.write<std::uint8_t>(command_at, command);
        }
        if (moby_on) {
            mem.write<std::uint32_t>(moby_at, moby);
        }
        if (item_on) {
            mem.write<std::int32_t>(item_at, item);
        }
    }
};

template <class T>
bool read_at(const openrac::Memory& mem, std::uint32_t address, T& out) {
    if (!gadget::in_guest(address, sizeof(T))) {
        return false;
    }
    out = mem.read<T>(address);
    return true;
}

int moby_class(const openrac::Memory& mem, std::uint32_t moby) {
    std::int16_t oclass = -1;
    if (moby == 0 || !read_at(mem, moby + gadget::kMobyClass, oclass)) {
        return -1;
    }
    return oclass;
}

int moby_state(const openrac::Memory& mem, std::uint32_t moby) {
    std::uint8_t state = 0;
    if (moby == 0 || !read_at(mem, moby + gadget::kMobyState, state)) {
        return -1;
    }
    return state;
}

bool owned(const openrac::Memory& mem, int item) {
    std::uint8_t flag = 0;
    if (item < 0 || !read_at(mem, app().disc.owned + static_cast<std::uint32_t>(item), flag)) {
        return false;
    }
    return flag != 0;
}

bool playing_now() {
    return app().mod != nullptr && app().mod->play_status().phase == OPENRAC_PLAY_PLAYING;
}

bool base_holds(std::uint32_t base, std::uint32_t moby) {
    if (base < gadget::kGuestLo || moby < base) {
        return false;
    }
    const std::uint32_t delta = moby - base;
    return (delta & 0xFFu) == 0 && (delta >> 8) <= 8192u;
}

std::uint32_t read_pointer(const openrac::Memory& mem, std::uint32_t word) {
    std::uint32_t base = 0;
    if (word == 0 || !read_at(mem, word, base)) {
        return 0;
    }
    return base;
}

/* The array this moby actually sits in. Its slot in the live table gives the base directly.
 * A pointer word is only accepted when the moby falls on a slot of that array. A word that does
 * not is not a base: indexing it produced a command of 0 and the faucet was treated as unusable. */
std::uint32_t moby_base(const openrac::Memory& mem, std::uint32_t sample) {
    openrac::Mod* mod = app().mod;
    if (mod == nullptr) {
        return 0;
    }
    if (sample != 0) {
        const int slot = mod->moby_slot_of(sample);
        if (slot >= 0) {
            const std::uint32_t base = sample - static_cast<std::uint32_t>(slot) * gadget::kMobySize;
            if (base_holds(base, sample)) {
                return base;
            }
        }
    }
    const int level = mod->loaded_level();
    const std::uint32_t words[] = {
        level >= 0 ? mod->relocate_level(level, gadget::moby_table_word(app().disc.ntsc, level)) : 0u,
        mod->relocate(app().disc.moby_array),
        mod->relocate_level(0, gadget::moby_table_word(app().disc.ntsc, 0)),
    };
    for (const std::uint32_t word : words) {
        const std::uint32_t base = read_pointer(mem, word);
        if (sample != 0 && base_holds(base, sample)) {
            return base;
        }
    }
    return 0;
}

/* The faucet's first pvar is a slot in that table. That moby carries the fill and empty bits.
 * It is not the class 1251 dock: the dock is the update that watches this faucet. */
std::uint32_t linked_moby(const openrac::Memory& mem, std::uint32_t faucet) {
    std::uint32_t pvars = 0;
    std::int32_t index = -1;
    if (!read_at(mem, faucet + gadget::kMobyPvars, pvars) || pvars == 0 || !read_at(mem, pvars, index) ||
        index < 0 || index > 8192) {
        return 0;
    }
    const std::uint32_t base = moby_base(mem, faucet);
    if (base == 0) {
        return 0;
    }
    const std::uint64_t address =
        static_cast<std::uint64_t>(base) + static_cast<std::uint64_t>(index) * gadget::kMobySize;
    if (address > 0xffffffffu || !gadget::in_guest(static_cast<std::uint32_t>(address), gadget::kMobySize)) {
        return 0;
    }
    const auto moby = static_cast<std::uint32_t>(address);
    const int state = moby_state(mem, moby);
    if (state < 0 || state >= 0xFE) {
        return 0;
    }
    return moby;
}

/* Yes or no only when the linked moby's command was read. A missed link is not "the tank refuses":
 * that would paint a faucet red while the Hydrodisplacer is really equipped. */
enum class Tank { Unknown, No, Yes };

Tank tank_gate(const openrac::Memory& mem, std::uint32_t faucet) {
    const std::uint32_t linked = linked_moby(mem, faucet);
    std::uint8_t command = 0;
    std::uint8_t tank = 0;
    if (linked == 0 || !read_at(mem, linked + gadget::kMobyCommand, command) ||
        !read_at(mem, app().disc.hero + gadget::kHydroFull, tank)) {
        return Tank::Unknown;
    }
    return gadget::hydro_can_use(tank != 0, command) ? Tank::Yes : Tank::No;
}

struct Stance {
    bool playing = false;
    bool alive = false;
    int air = 1;
    int control = 0;
    int hero_state = 0;
    int hand_id = 0;
    std::uint32_t hand_moby = 0;
    std::uint32_t ground = 0;
};

Stance stance(const openrac::Memory& mem) {
    Stance out;
    out.playing = playing_now();
    const std::uint32_t hero = app().disc.hero;
    std::int32_t hp = 0;
    std::int16_t air = 1;
    std::int32_t control = 0;
    std::int32_t hero_state = 0;
    std::int32_t hand_id = 0;
    std::uint32_t hand_moby = 0;
    std::uint32_t ground = 0;
    out.alive = read_at(mem, hero + gadget::kHealth, hp) && hp > 0;
    if (read_at(mem, hero + gadget::kAirFrames, air)) {
        out.air = air;
    }
    if (read_at(mem, hero + gadget::kControlMode, control)) {
        out.control = control;
    }
    if (read_at(mem, hero + gadget::kHeroState, hero_state)) {
        out.hero_state = hero_state;
    }
    if (read_at(mem, hero + gadget::kHand + gadget::kHandItem, hand_id)) {
        out.hand_id = hand_id;
    }
    if (read_at(mem, hero + gadget::kHand + gadget::kHandMoby, hand_moby)) {
        out.hand_moby = hand_moby;
    }
    read_at(mem, hero + gadget::kGroundMoby, ground);
    out.ground = ground;
    return out;
}

bool on_foot(int control) { return static_cast<std::uint32_t>(control) < 2u; }

bool swing_setting_on() {
    return app().mod != nullptr && app().mod->setting<bool>("swingshot");
}

bool swing_control(int control, int hero_state) {
    return control == 0 || control == 1 || control == 2 || control == 4 || control == 5 || control == 0xF ||
           hero_state == 0x1E;
}

/* The two selectors write the chosen node even when the line test fails. Put those words back. */
struct SightMarks {
    const openrac::Memory* mem = nullptr;
    std::uint32_t hero = 0;
    bool held = false;
    std::uint32_t grapple_target = 0;
    std::int32_t grapple_sight = 0;
    std::int32_t grapple_swings = 0;
    float grapple_radius = 0.f;
    float swing_range = 0.f;
    std::uint32_t swing_target = 0;
    float swing_dist = 0.f;
    std::int16_t swing_sight = 0;

    bool capture(const openrac::Memory& memory, std::uint32_t hero_at) {
        mem = &memory;
        hero = hero_at;
        held = read_at(memory, hero + gadget::kGrappleTarget, grapple_target) &&
               read_at(memory, hero + gadget::kGrappleInSight, grapple_sight) &&
               read_at(memory, hero + gadget::kGrappleSwings, grapple_swings) &&
               read_at(memory, hero + gadget::kGrappleRadius, grapple_radius) &&
               read_at(memory, hero + gadget::kSwingMaxRange, swing_range) &&
               read_at(memory, hero + gadget::kSwingTarget, swing_target) &&
               read_at(memory, hero + gadget::kSwingTargetDist, swing_dist) &&
               read_at(memory, hero + gadget::kSwingInSight, swing_sight);
        return held;
    }

    ~SightMarks() {
        if (!held || mem == nullptr) {
            return;
        }
        mem->write<std::uint32_t>(hero + gadget::kGrappleTarget, grapple_target);
        mem->write<std::int32_t>(hero + gadget::kGrappleInSight, grapple_sight);
        mem->write<std::int32_t>(hero + gadget::kGrappleSwings, grapple_swings);
        mem->write<float>(hero + gadget::kGrappleRadius, grapple_radius);
        mem->write<float>(hero + gadget::kSwingMaxRange, swing_range);
        mem->write<std::uint32_t>(hero + gadget::kSwingTarget, swing_target);
        mem->write<float>(hero + gadget::kSwingTargetDist, swing_dist);
        mem->write<std::int16_t>(hero + gadget::kSwingInSight, swing_sight);
    }
};

/* True when select_hero_grapple_swing_target or select_hero_grapple_target would attach.
 * That is their range, cone, clip volume, and the line from the hero's chest to the node. */
bool swing_in_sight(const openrac::Memory& mem) {
    App& state = app();
    if (state.select_swing == nullptr || state.select_grapple == nullptr) {
        return false;
    }
    SightMarks marks;
    if (!marks.capture(mem, state.disc.hero)) {
        return false;
    }
    state.select_swing();
    const int grapple = state.select_grapple();
    std::int16_t swing_sight = 0;
    read_at(mem, state.disc.hero + gadget::kSwingInSight, swing_sight);
    return swing_sight != 0 || grapple != 0;
}

/* hero_set_state(0x38) and the fill that follows. control 0x13 is that fill. */
bool hydro_use(const Stance& now) {
    return now.control == 0x13 || (now.hero_state >= 0x38 && now.hero_state <= 0x3A);
}

/* The fill has the hero in its own states. Forcing the red palette then would flash the faucet. */
bool hydro_busy(const Stance& now) {
    if (hydro_use(now)) {
        return true;
    }
    const gadget::Session& session = app().session;
    return session.kind == gadget::PadKind::Hydro && session.phase != gadget::Phase::Idle;
}

/* Shows the update the item it checks for, without letting a Trespasser pad start on the weapon
 * that is really out. stand_in is only for an empty hand: the colour checks a null moby as "not
 * equipped", and it is not dereferenced except for the command byte, which is cleared. */
void paint_held(const openrac::Memory& mem, int want, bool zero_command, std::uint32_t stand_in, HeldPaint& paint) {
    const std::uint32_t hero = app().disc.hero;
    const std::uint32_t item_at = hero + gadget::kHand + gadget::kHandItem;
    const std::uint32_t moby_at = hero + gadget::kHand + gadget::kHandMoby;
    std::int32_t item = 0;
    std::uint32_t held = 0;
    if (!read_at(mem, item_at, item) || !read_at(mem, moby_at, held)) {
        return;
    }
    paint.mem = mem;
    if (item != want) {
        paint.item_at = item_at;
        paint.item = item;
        paint.item_on = true;
        mem.write<std::int32_t>(item_at, want);
    }
    if (held == 0 && stand_in != 0 && gadget::in_guest(stand_in, gadget::kMobySize)) {
        paint.moby_at = moby_at;
        paint.moby = held;
        paint.moby_on = true;
        mem.write<std::uint32_t>(moby_at, stand_in);
        held = stand_in;
    }
    if (zero_command && held != 0 && gadget::in_guest(held, gadget::kMobySize)) {
        std::uint8_t command = 0;
        if (read_at(mem, held + gadget::kMobyCommand, command)) {
            paint.command_at = held + gadget::kMobyCommand;
            paint.command = command;
            paint.command_on = true;
            mem.write<std::uint8_t>(paint.command_at, 0);
        }
    }
}

Seen see() {
    Seen seen;
    App& state = app();
    if (state.mod == nullptr) {
        return seen;
    }
    const openrac_play_status status = state.mod->play_status();
    seen.view.playing = status.phase == OPENRAC_PLAY_PLAYING;
    if (!state.loads_init) {
        state.loads = status.loads;
        state.loads_init = true;
    } else if (status.loads != state.loads) {
        seen.view.loads_changed = true;
        state.loads = status.loads;
    }
    const bool in_level = status.phase == OPENRAC_PLAY_PLAYING || status.phase == OPENRAC_PLAY_MENU ||
                          status.phase == OPENRAC_PLAY_SCENE;
    if (!in_level) {
        return seen;
    }

    const openrac::Memory mem = state.mod->memory();
    const Stance now = stance(mem);
    if (!now.alive) {
        return seen;
    }
    seen.view.alive = true;

    std::int32_t hand_state = 0;
    read_at(mem, state.disc.hero + gadget::kHand + gadget::kHandState, hand_state);
    seen.view.hand_id = now.hand_id;
    seen.view.hand_state = hand_state;
    seen.view.hand_moby = now.hand_moby != 0 && gadget::in_guest(now.hand_moby, gadget::kMobySize);
    seen.view.control_mode = now.control;
    seen.view.trigger_held = state.trigger_down;
    if (now.hand_id == gadget::kSwing && seen.view.hand_moby) {
        seen.view.gadget_state = moby_state(mem, now.hand_moby);
    }

    const int ground_class = now.ground != 0 ? moby_class(mem, now.ground) : -1;
    const int ground_state = now.ground != 0 ? moby_state(mem, now.ground) : -1;

    if (seen.view.playing && now.air == 0) {
        if (ground_class == gadget::kTrespasserPad && gadget::trespasser_waiting(ground_state) &&
            owned(mem, gadget::kTrespasser)) {
            seen.view.at_prompt = true;
            seen.view.prompt = gadget::PadKind::Trespasser;
            seen.candidate = now.ground;
        } else if (ground_class == gadget::kFaucet && on_foot(now.control) && owned(mem, gadget::kHydro) &&
                   tank_gate(mem, now.ground) != Tank::No) {
            // The dock does not care which weapon is out. It starts from the faucet underfoot, the
            // tank bits, and Circle. A link that cannot be read is not a refusal: the dock still
            // applies that test itself when Circle arrives. The dock that actually updates claims
            // the target, so a dock elsewhere in the level is not watched instead.
            seen.view.at_prompt = true;
            seen.view.prompt = gadget::PadKind::Hydro;
        }
    }

    if (seen.view.playing && !seen.view.at_prompt && swing_setting_on() && owned(mem, gadget::kSwing) &&
        now.hand_id != gadget::kSwing && swing_control(now.control, now.hero_state)) {
        std::uint8_t vehicle = 1;
        std::uint8_t locked = 1;
        std::int16_t lockout = 1;
        const bool gates = read_at(mem, state.disc.hero + gadget::kVehicleMode, vehicle) && vehicle == 0 &&
                           read_at(mem, state.disc.hero + gadget::kItemUseLocked, locked) && locked == 0 &&
                           read_at(mem, state.disc.hero + gadget::kGrappleLockout, lockout) && lockout == 0;
        if (gates && swing_in_sight(mem)) {
            seen.view.at_prompt = true;
            seen.view.prompt = gadget::PadKind::Swing;
        }
    }

    if (state.session.kind == gadget::PadKind::Swing && state.session.phase != gadget::Phase::Idle) {
        seen.view.target_state = now.hero_state;
        seen.view.left = false;
    } else if (state.target != 0 && state.session.phase != gadget::Phase::Idle) {
        if (state.session.kind == gadget::PadKind::Trespasser) {
            seen.view.left = now.air != 0 || now.ground != state.target;
        } else if (state.session.kind == gadget::PadKind::Hydro) {
            seen.view.left = now.air != 0 || ground_class != gadget::kFaucet;
        }
        seen.view.target_state = moby_state(mem, state.target);
    }
    return seen;
}

bool hand_ready(const openrac::Memory& mem, int item) {
    std::int32_t hand_id = 0;
    std::int32_t hand_state = 0;
    std::uint32_t hand_moby = 0;
    const std::uint32_t hero = app().disc.hero;
    if (!read_at(mem, hero + gadget::kHand + gadget::kHandItem, hand_id) ||
        !read_at(mem, hero + gadget::kHand + gadget::kHandState, hand_state) ||
        !read_at(mem, hero + gadget::kHand + gadget::kHandMoby, hand_moby)) {
        return false;
    }
    return hand_id == item && hand_state == gadget::kSlotReady && hand_moby != 0 &&
           gadget::in_guest(hand_moby, gadget::kMobySize);
}

void apply(const gadget::Session& previous, const gadget::Session& next, std::uint32_t candidate) {
    App& state = app();
    if (state.mod == nullptr) {
        return;
    }
    const openrac::Memory mem = state.mod->memory();
    const std::uint32_t hero = state.disc.hero;
    if (next.clear_pending && previous.gadget_id != 0) {
        std::int32_t pending = 0;
        if (read_at(mem, hero + gadget::kPendingItem, pending) && pending == previous.gadget_id) {
            mem.write<std::int32_t>(hero + gadget::kPendingItem, 0);
        }
    }
    if (next.equip && gadget::in_guest(hero + gadget::kSavedItem, sizeof(std::int32_t))) {
        const std::uint32_t last_weapon = state.disc.ntsc ? gadget::kLastWeaponNtsc : gadget::kLastWeaponPal;
        std::int32_t circle = 0;
        if (read_at(mem, last_weapon, circle)) {
            state.circle_item = circle;
            state.circle_item_held = true;
        }
        mem.write<std::int32_t>(hero + gadget::kSavedItem, next.saved_id);
        mem.write<std::int32_t>(hero + gadget::kPendingItem, next.gadget_id);
        state.target = candidate;
    }
    if (next.restore) {
        mem.write<std::int32_t>(hero + gadget::kRestoreItem, 1);
    }
    if (next.phase == gadget::Phase::Idle) {
        state.target = 0;
        // restore_item has already copied the wrench into last_weapon_id. Put the pre-Square
        // weapon back now, or Circle finds the wrench in both places and does nothing.
        if (state.circle_item_held && previous.phase == gadget::Phase::Restore) {
            const std::uint32_t last_weapon =
                state.disc.ntsc ? gadget::kLastWeaponNtsc : gadget::kLastWeaponPal;
            std::int32_t now = 0;
            // A manual switch during the holster already stored that weapon here. Leave it.
            if (read_at(mem, last_weapon, now) &&
                (now == previous.saved_id || now == previous.gadget_id || now == 0)) {
                mem.write<std::int32_t>(last_weapon, state.circle_item);
            }
        }
        state.circle_item_held = false;
    }
}

/* Not a caller the game uses. The ship is 2, vendors 1 and 8, jets 7. */
constexpr std::int32_t kPromptOwner = 3;
/* The ship's message. Its text pointer is swung at our line for as long as the prompt is up,
 * which is what makes the pill say this instead of "Enter ship". */
constexpr std::int32_t kShipMessage = 0x53E4;

void write_prompt_text(const openrac::Memory& mem, std::uint32_t at, std::uint32_t room, const char* text) {
    if (!gadget::in_guest(at, room)) {
        return;
    }
    std::uint32_t i = 0;
    for (; text[i] != '\0' && i + 1 < room; ++i) {
        mem.write<std::uint8_t>(at + i, static_cast<std::uint8_t>(text[i]));
    }
    mem.write<std::uint8_t>(at + i, 0);
}

/* The help entry for `id`, in the table the loaded level is actually using. */
std::uint32_t find_help_entry(const openrac::Memory& mem, std::int32_t id) {
    App& state = app();
    const gadget::HelpPrompt help = gadget::help_prompt_for(state.disc.ntsc);
    std::uint32_t table = 0;
    std::int32_t count = 0;
    if (!read_at(mem, state.mod->relocate(help.msgs), table) ||
        !read_at(mem, state.mod->relocate(help.state) + 0x2Cu, count) || count < 1 || count > 2048) {
        return 0;
    }
    if (!gadget::in_guest(table, static_cast<std::uint32_t>(count) * 16u)) {
        return 0;
    }
    for (std::int32_t i = 0; i < count; ++i) {
        const std::uint32_t entry = table + static_cast<std::uint32_t>(i) * 16u;
        std::int32_t found = 0;
        if (read_at(mem, entry + 4u, found) && found == id) {
            return entry;
        }
    }
    return 0;
}

/* Put the line where try_set_help_message will copy it from. The entry is restored on the way out. */
bool aim_help_text(const openrac::Memory& mem, std::uint32_t scratch) {
    App& state = app();
    if (state.prompt_entry != 0) {
        std::int32_t id = 0;
        if (read_at(mem, state.prompt_entry + 4u, id) && id == kShipMessage) {
            mem.write<std::uint32_t>(state.prompt_entry, scratch);
            return true;
        }
        state.prompt_entry = 0;
    }
    const std::uint32_t entry = find_help_entry(mem, kShipMessage);
    std::uint32_t text = 0;
    if (entry == 0 || !read_at(mem, entry, text)) {
        return false;
    }
    state.prompt_saved_text = text;
    state.prompt_entry = entry;
    mem.write<std::uint32_t>(entry, scratch);
    return true;
}

void release_prompt(const openrac::Memory& mem) {
    App& state = app();
    if (state.mod == nullptr || state.prompt_level < 0) {
        return;
    }
    const int level = state.mod->loaded_level();
    const bool same = level == state.prompt_level;
    if (same && state.prompt_entry != 0 && gadget::in_guest(state.prompt_entry, 8)) {
        mem.write<std::uint32_t>(state.prompt_entry, state.prompt_saved_text);
    }
    state.prompt_entry = 0;
    if (same && state.prompt_scratch_held) {
        const std::uint32_t scratch =
            state.mod->relocate(gadget::help_prompt_for(state.disc.ntsc).scratch);
        if (gadget::in_guest(scratch, 0x50)) {
            for (std::uint32_t i = 0; i < 0x50; ++i) {
                mem.write<std::uint8_t>(scratch + i, state.prompt_scratch[i]);
            }
        }
    }
    state.prompt_scratch_held = false;
    if (same && state.prompt_gate_held) {
        const std::uint32_t gate = state.mod->relocate_level(0, gadget::use_pill_for(state.disc.ntsc).gate);
        if (gadget::in_guest(gate, sizeof(std::int32_t))) {
            mem.write<std::int32_t>(gate, state.prompt_saved_gate);
        }
    }
    state.prompt_gate_held = false;
    if (same) {
        const gadget::HelpPrompt help = gadget::help_prompt_for(state.disc.ntsc);
        const std::uint32_t owner_at = state.mod->relocate(help.owner);
        std::int32_t owner = 0;
        if (read_at(mem, owner_at, owner) && owner == kPromptOwner) {
            mem.write<std::int32_t>(state.mod->relocate(help.keepalive), 0);
            mem.write<std::int32_t>(owner_at, 0);
            mem.write<std::int32_t>(state.mod->relocate(help.id), 0);
        }
        const gadget::UsePill pill = gadget::use_pill_for(state.disc.ntsc);
        const std::uint32_t visible_at = state.mod->relocate_level(0, pill.visible);
        const std::uint32_t frames_at = state.mod->relocate_level(0, pill.frames);
        if (gadget::in_guest(frames_at, sizeof(std::int32_t))) {
            mem.write<std::int32_t>(frames_at, 0);
        }
        if (gadget::in_guest(visible_at, sizeof(std::int32_t))) {
            mem.write<std::int32_t>(visible_at, 0);
        }
    }
    state.prompt_level = -1;
}

/* The green pill the ship, NPCs and taxis use. The first byte is the button glyph. */
void use_prompt_text(char* out, unsigned room, gadget::PadKind kind, Trigger mode) {
    const char* name = kind == gadget::PadKind::Trespasser ? " Use the Trespasser" : " Use the Hydrodisplacer";
    if (room < 2) {
        return;
    }
    out[0] = prompt_glyph(mode);
    unsigned i = 0;
    for (; name[i] != '\0' && i + 2 < room; ++i) {
        out[i + 1] = name[i];
    }
    out[i + 1] = '\0';
}

void show_use_prompt(const openrac::Memory& mem, gadget::PadKind kind) {
    App& state = app();
    if (state.mod == nullptr) {
        return;
    }
    const int level = state.mod->loaded_level();
    if (level < 0) {
        return;
    }
    if (state.prompt_level >= 0 && state.prompt_level != level) {
        state.prompt_entry = 0;
        state.prompt_scratch_held = false;
        state.prompt_gate_held = false;
        state.prompt_level = -1;
    }
    const gadget::HelpPrompt help = gadget::help_prompt_for(state.disc.ntsc);
    const gadget::UsePill pill = gadget::use_pill_for(state.disc.ntsc);
    const std::uint32_t owner_at = state.mod->relocate(help.owner);
    const std::uint32_t visible_at = state.mod->relocate_level(0, pill.visible);
    std::int32_t owner = 0;
    read_at(mem, owner_at, owner);
    // Someone else's talk, ship or taxi prompt owns the slot. Hand the message text back.
    if (owner != 0 && owner != kPromptOwner) {
        if (state.prompt_level == level) {
            release_prompt(mem);
        }
        return;
    }
    char text[48];
    use_prompt_text(text, sizeof text, kind, trigger_mode());
    const std::uint32_t scratch = state.mod->relocate(help.scratch);
    if (!state.prompt_scratch_held && gadget::in_guest(scratch, 0x50)) {
        for (std::uint32_t i = 0; i < 0x50; ++i) {
            state.prompt_scratch[i] = mem.read<std::uint8_t>(scratch + i);
        }
        state.prompt_scratch_held = true;
    }
    write_prompt_text(mem, scratch, 0x50, text);
    const bool aimed = state.prompt_scratch_held && aim_help_text(mem, scratch);
    if (aimed) {
        if (state.try_set != nullptr) {
            state.try_set(kPromptOwner, kShipMessage);
        } else {
            mem.write<std::int32_t>(owner_at, kPromptOwner);
            mem.write<std::int32_t>(state.mod->relocate(help.keepalive), 2);
            mem.write<std::int32_t>(state.mod->relocate(help.id), kShipMessage);
        }
        write_prompt_text(mem, state.mod->relocate(help.text), 0x50, text);
    }
    write_prompt_text(mem, state.mod->relocate_level(0, pill.text), 0x80, text);
    const std::uint32_t frames_at = state.mod->relocate_level(0, pill.frames);
    if (gadget::in_guest(frames_at, sizeof(std::int32_t))) {
        mem.write<std::int32_t>(frames_at, 0);
    }
    if (gadget::in_guest(visible_at, sizeof(std::int32_t))) {
        mem.write<std::int32_t>(visible_at, kPromptOwner);
    }
    const std::uint32_t gate_at = state.mod->relocate_level(0, pill.gate);
    std::int32_t gate = 0;
    if (read_at(mem, gate_at, gate) && gate == 0) {
        if (!state.prompt_gate_held) {
            state.prompt_saved_gate = gate;
            state.prompt_gate_held = true;
        }
        mem.write<std::int32_t>(gate_at, 1);
    }
    state.prompt_level = level;
}

void hide_use_prompt(const openrac::Memory& mem) { release_prompt(mem); }

/* The dock requires Circle to have been pressed within the last few frames and still held.
 * Writing the pad words here is the sample that update reads; on_pad alone can miss it. */
struct PadPress {
    openrac::Memory mem{nullptr};
    std::uint32_t held_at = 0;
    std::uint32_t pressed_at = 0;
    std::uint32_t count_at = 0;
    std::uint32_t hist_at = 0;
    std::int32_t held = 0;
    std::int32_t pressed = 0;
    std::int32_t count = 0;
    std::uint32_t hist = 0;
    bool held_on = false;
    bool pressed_on = false;
    bool count_on = false;
    bool hist_on = false;

    void undo() {
        if (hist_on) {
            mem.write<std::uint32_t>(hist_at, hist);
        }
        if (count_on) {
            mem.write<std::int32_t>(count_at, count);
        }
        if (pressed_on) {
            mem.write<std::int32_t>(pressed_at, pressed);
        }
        if (held_on) {
            mem.write<std::int32_t>(held_at, held);
        }
    }
};

void press_use(const openrac::Memory& mem, PadPress& saved) {
    const std::uint32_t pad = app().disc.pad;
    std::int32_t mask = 0;
    read_at(mem, app().disc.hero + gadget::kHand + gadget::kHandButton, mask);
    if (mask == 0) {
        mask = 0x20;
    }
    std::int32_t held = 0;
    std::int32_t pressed = 0;
    std::int16_t index = 0;
    std::int32_t count = 0;
    if (!read_at(mem, pad + 0x1A0u, held) || !read_at(mem, pad + 0x1A4u, pressed) ||
        !read_at(mem, pad + 0x18Eu, index) || !read_at(mem, pad + 0x190u, count)) {
        return;
    }
    saved.mem = mem;
    saved.held_at = pad + 0x1A0u;
    saved.held = held;
    saved.held_on = true;
    mem.write<std::int32_t>(saved.held_at, held | mask);
    saved.pressed_at = pad + 0x1A4u;
    saved.pressed = pressed;
    saved.pressed_on = true;
    mem.write<std::int32_t>(saved.pressed_at, pressed | mask);
    if (count < 1) {
        saved.count_at = pad + 0x190u;
        saved.count = count;
        saved.count_on = true;
        mem.write<std::int32_t>(saved.count_at, 1);
        count = 1;
    }
    const int slot = (static_cast<int>(index) - 1 + 30) % 30;
    const std::uint32_t hist_at = pad + 0x1E0u + static_cast<std::uint32_t>(slot) * 4u;
    std::uint32_t hist = 0;
    if (!read_at(mem, hist_at, hist)) {
        return;
    }
    saved.hist_at = hist_at;
    saved.hist = hist;
    saved.hist_on = true;
    mem.write<std::uint32_t>(hist_at, hist | static_cast<std::uint32_t>(mask));
}

/* Class 1251 is the held Hydrodisplacer. State 1 waits for its own anim_flags bit 2, which is the
 * equip clip. The faucet is class 341. Putting this moby in state 2 lets the same update test
 * Circle and call hero_set_state(0x38) while that clip is still playing. */
void release_equip_wait(const openrac::Memory& mem, std::uint32_t moby) {
    App& state = app();
    const gadget::Phase phase = state.session.phase;
    if (state.session.kind != gadget::PadKind::Hydro ||
        (phase != gadget::Phase::Equipping && phase != gadget::Phase::Arm)) {
        return;
    }
    std::uint32_t held = 0;
    if (!read_at(mem, state.disc.hero + gadget::kHand + gadget::kHandMoby, held) || held != moby) {
        return;
    }
    const int gadget_state = moby_state(mem, moby);
    if (gadget_state == 0 || gadget_state == 1) {
        mem.write<std::uint8_t>(moby + gadget::kMobyState, 2);
    }
}

/* Same moment as arm_trespasser. Circle is visible to state 2, so this update's own
 * hero_set_state(0x38) is the lock. Square during the later camera pan is the wrench. */
bool arm_hydro(std::uint32_t moby, PadPress& saved) {
    App& state = app();
    const gadget::Phase phase = state.session.phase;
    if (state.mod == nullptr || state.session.kind != gadget::PadKind::Hydro ||
        (phase != gadget::Phase::Equipping && phase != gadget::Phase::Arm)) {
        return false;
    }
    const openrac::Memory mem = state.mod->memory();
    if (state.target != 0 && state.target != moby) {
        return false;
    }
    if (moby_state(mem, moby) != 2 || !hand_ready(mem, gadget::kHydro)) {
        return false;
    }
    const Stance now = stance(mem);
    if (now.air != 0 || moby_class(mem, now.ground) != gadget::kFaucet) {
        return false;
    }
    press_use(mem, saved);
    return true;
}

/* The faucet's pvars are three ints. [0] is the tank. [1] and [2] are camera nodes, and both
 * being set is what starts the pan. Hiding them for this update sends the attach down the timer
 * path, which still flips the tank. Restored as soon as the update returns. */
struct CameraNodes {
    openrac::Memory mem{nullptr};
    std::uint32_t at = 0;
    std::int32_t first = -1;
    std::int32_t second = -1;
    bool on = false;

    void undo() {
        if (!on) {
            return;
        }
        mem.write<std::int32_t>(at, first);
        mem.write<std::int32_t>(at + 4u, second);
        on = false;
    }
};

void hide_camera_nodes(const openrac::Memory& mem, std::uint32_t moby, CameraNodes& saved) {
    if (app().mod == nullptr || !app().mod->setting<bool>("skip_camera") || moby_state(mem, moby) != 3) {
        return;
    }
    std::int32_t flags = 0;
    if (!read_at(mem, app().disc.hero + gadget::kAnimFlags, flags) || (flags & 2) == 0) {
        return;
    }
    const Stance now = stance(mem);
    if (now.ground == 0 || moby_class(mem, now.ground) != gadget::kFaucet) {
        return;
    }
    std::uint32_t pvars = 0;
    if (!read_at(mem, now.ground + gadget::kMobyPvars, pvars) || !gadget::in_guest(pvars, 12)) {
        return;
    }
    std::int32_t first = -1;
    std::int32_t second = -1;
    if (!read_at(mem, pvars + 4u, first) || !read_at(mem, pvars + 8u, second) || first == -1 ||
        second == -1) {
        return;
    }
    saved.mem = mem;
    saved.at = pvars + 4u;
    saved.first = first;
    saved.second = second;
    saved.on = true;
    mem.write<std::int32_t>(saved.at, -1);
    mem.write<std::int32_t>(saved.at + 4u, -1);
}

void arm_trespasser(std::uint32_t moby) {
    App& state = app();
    if (state.mod == nullptr || state.session.phase != gadget::Phase::Arm ||
        state.session.kind != gadget::PadKind::Trespasser || moby != state.target) {
        return;
    }
    const openrac::Memory mem = state.mod->memory();
    if (!hand_ready(mem, gadget::kTrespasser)) {
        return;
    }
    std::uint32_t held = 0;
    if (read_at(mem, state.disc.hero + gadget::kHand + gadget::kHandMoby, held) &&
        gadget::in_guest(held, gadget::kMobySize)) {
        mem.write<std::uint8_t>(held + gadget::kMobyCommand, 1);
    }
}

void paint_trespasser(std::uint32_t moby, HeldPaint& paint) {
    App& state = app();
    if (state.mod == nullptr) {
        return;
    }
    const openrac::Memory mem = state.mod->memory();
    const Stance now = stance(mem);
    const bool ready = now.playing && now.alive && now.air == 0 && now.ground == moby &&
                       gadget::trespasser_waiting(moby_state(mem, moby)) && owned(mem, gadget::kTrespasser);
    if (ready && now.hand_id != gadget::kTrespasser) {
        paint_held(mem, gadget::kTrespasser, true, moby, paint);
        return;
    }
    arm_trespasser(moby);
}

void paint_faucet(std::uint32_t moby, HeldPaint& paint) {
    App& state = app();
    if (state.mod == nullptr) {
        return;
    }
    const openrac::Memory mem = state.mod->memory();
    const Stance now = stance(mem);
    // update_hydrodisplacer_faucet_pulse_color paints green only when ground is this faucet and
    // the held item is 0x16. Air, control, the dock's state and the tank bits are not that test.
    // The tank still forces the red palette when its command byte was actually read and refuses.
    if (!now.playing || !now.alive || now.ground != moby || !owned(mem, gadget::kHydro)) {
        return;
    }
    const Tank tank = tank_gate(mem, moby);
    if (tank == Tank::No) {
        if (now.hand_id == gadget::kHydro && !hydro_busy(now)) {
            paint_held(mem, kWrench, false, 0, paint);
        }
        return;
    }
    if (now.hand_id != gadget::kHydro) {
        paint_held(mem, gadget::kHydro, false, moby, paint);
    }
}

void load(openrac::Mod& mod) {
    if (mod.game() != "rac1") {
        throw openrac::Error("Gadget quick-use is for Ratchet & Clank");
    }
    const bool ntsc = mod.version() == "ntsc";
    if (!ntsc && mod.version() != "pal") {
        throw openrac::Error("Gadget quick-use is for the PAL and NTSC-U discs");
    }

    app() = App{};
    App& state = app();
    state.mod = &mod;
    state.disc = gadget::disc_for(ntsc);
    using TrySet = int (*)(int, int);
    state.try_set = mod.function<TrySet>("try_set_help_message");
    state.select_swing = mod.function<void (*)()>(ntsc ? "FUN_L00_00221ee0" : "func_L00_00222668");
    state.select_grapple = mod.function<int (*)()>(ntsc ? "FUN_L00_00222158" : "func_L00_002228E0");

    mod.hook<openrac::rac1::Moby_TresspasserPad_Update>([](auto original, std::uint32_t moby) {
        // The colour and the puzzle both read the held item in this call. Command stays 0 unless
        // the trespasser is really out and armed, so the green light does not start the puzzle.
        HeldPaint paint{app().mod->memory()};
        paint_trespasser(moby, paint);
        original(moby);
        paint.undo();
    });
    mod.hook<openrac::rac1::Moby_Hydrodisplacer_Update>([](auto original, std::uint32_t moby) {
        const openrac::Memory mem = app().mod->memory();
        release_equip_wait(mem, moby);
        CameraNodes nodes{mem};
        hide_camera_nodes(mem, moby, nodes);
        PadPress press{mem};
        const bool armed = arm_hydro(moby, press);
        original(moby);
        nodes.undo();
        if (armed) {
            const int dock = moby_state(mem, moby);
            if (dock >= 3 && dock <= 5) {
                app().target = moby;
            }
        }
        press.undo();
    });
    mod.hook<openrac::rac1::Moby_HydrodisplacerFaucet_Update>([](auto original, std::uint32_t moby) {
        // The green palette is the held-item check inside this update. Standing in for item 0x16
        // removes that check. A tank that was read and cannot be used keeps the red palette.
        HeldPaint paint{app().mod->memory()};
        paint_faucet(moby, paint);
        original(moby);
        paint.undo();
    });
    mod.on_pad([](int port, std::uint8_t* report, int size) {
        App& state = app();
        if (state.mod != nullptr) {
            const openrac::Memory mem = state.mod->memory();
            // One pad is enough: this runs again for every other port.
            if (port == 0) {
                // Before the tick, so the pill is armed by the time the frame decides whether to draw it.
                if (state.session.show_prompt && (state.prompt_kind == gadget::PadKind::Trespasser ||
                                                  state.prompt_kind == gadget::PadKind::Hydro)) {
                    show_use_prompt(mem, state.prompt_kind);
                } else {
                    hide_use_prompt(mem);
                }
            }
        }
        if (port != 0 || report == nullptr || size < 2) {
            return;
        }
        const Trigger mode = trigger_mode();
        const bool circle_held = (report[1] & kCircleBit) == 0;
        const bool triangle_held = (report[1] & kTriangleBit) == 0;
        // The launcher button is for the pads. A versatarget always starts, and chains, on Circle.
        const bool swing = state.session.kind == gadget::PadKind::Swing ||
                           state.prompt_kind == gadget::PadKind::Swing;
        state.trigger_down = swing ? circle_held
                                   : (trigger_uses_circle(mode) && circle_held) ||
                                         (trigger_uses_triangle(mode) && triangle_held);
        const bool circle_edge = circle_held && !state.prev_circle;
        const bool triangle_edge = triangle_held && !state.prev_triangle;
        const gadget::Phase phase = state.session.phase;
        // Equipping and restoring still hide Circle, so the gadget in hand cannot start the
        // faucet or the puzzle a second time. Triangle-only leaves Circle alone at a pad.
        const bool in_sequence =
            phase == gadget::Phase::Equipping || phase == gadget::Phase::Restore;
        const bool take_circle =
            state.session.swallow && (swing || trigger_uses_circle(mode) || in_sequence);
        const bool take_triangle = state.session.swallow && !swing && trigger_uses_triangle(mode);
        if (state.session.inject) {
            // Every poll until the next frame. One poll would release on the next sample, and the
            // pad requires the press to still be held. The gadget fires on Circle either way.
            report[1] = static_cast<std::uint8_t>(report[1] & ~kCircleBit);
            state.prev_circle = true;
            state.prev_triangle = triangle_held;
        } else {
            if ((take_circle && (swing || trigger_uses_circle(mode)) && circle_edge) ||
                (take_triangle && triangle_edge)) {
                state.use_pressed = true;
            }
            // A clear bit is held. Set it so the button does not also reach the game.
            if (take_circle) {
                report[1] = static_cast<std::uint8_t>(report[1] | kCircleBit);
            }
            if (take_triangle) {
                report[1] = static_cast<std::uint8_t>(report[1] | kTriangleBit);
            }
            state.prev_circle = circle_held;
            state.prev_triangle = triangle_held;
        }
    });
    mod.on_frame([] {
        App& state = app();
        const Seen seen = see();
        gadget::View view = seen.view;
        view.use_pressed = state.use_pressed;
        state.use_pressed = false;
        const gadget::Session previous = state.session;
        state.session = gadget::step(previous, view);
        apply(previous, state.session, seen.candidate);
        if (state.mod != nullptr) {
            const openrac::Memory mem = state.mod->memory();
            if (state.session.show_prompt &&
                (view.prompt == gadget::PadKind::Trespasser || view.prompt == gadget::PadKind::Hydro)) {
                state.prompt_kind = view.prompt;
                show_use_prompt(mem, view.prompt);
            } else {
                // No pill for a node. The kind is remembered so the next pad poll takes Circle only.
                state.prompt_kind = view.prompt == gadget::PadKind::Swing ||
                                            state.session.kind == gadget::PadKind::Swing
                                        ? gadget::PadKind::Swing
                                        : gadget::PadKind::None;
                hide_use_prompt(mem);
            }
        }
    });
    mod.info("loaded");
}

}  // namespace

OPENRAC_MOD_CPP("gadget-quick-use", "0.1.0", load)
