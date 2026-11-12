/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Tristan Gauci
 *
 * Guest layout for both rac1 discs. Hero field offsets are the same on PAL and NTSC
 * (games/rac1/ntsc-decomp/include/rnc/hero/hero.h, moby.h). The bases are the ones
 * weapon-leveling already checked against the port: NTSC is not a flat 0x100 below PAL.
 */
#pragma once

#include <cstdint>

namespace gadget {

constexpr std::uint32_t kGuestLo = 0x00100000u;
constexpr std::uint32_t kGuestEnd = 0x02000000u;

constexpr std::uint32_t kHand = 0x1090u;
constexpr std::uint32_t kHandMoby = 0x00u;
constexpr std::uint32_t kHandButton = 0x10u;
constexpr std::uint32_t kHandState = 0x24u;
constexpr std::uint32_t kHandItem = 0x28u;
constexpr std::uint32_t kHeroPos = 0x80u;
constexpr std::uint32_t kHeroYaw = 0x98u;
constexpr std::uint32_t kGrindYaw = 0x57Cu;
constexpr std::uint32_t kGrappleLockout = 0x1E6u;
/* Written by select_hero_grapple_target and select_hero_grapple_swing_target. */
constexpr std::uint32_t kGrappleTarget = 0x964u;
constexpr std::uint32_t kGrappleInSight = 0x968u;
constexpr std::uint32_t kGrappleSwings = 0x978u;
constexpr std::uint32_t kGrappleRadius = 0x980u;
constexpr std::uint32_t kSwingMaxRange = 0x984u;
constexpr std::uint32_t kSwingTarget = 0x990u;
constexpr std::uint32_t kSwingTargetDist = 0x998u;
constexpr std::uint32_t kSwingInSight = 0x99Eu;
constexpr std::uint32_t kHeroMoby = 0x2080u;
constexpr std::uint32_t kHeroState = 0x2084u;
constexpr std::uint32_t kControlMode = 0x208Cu;
constexpr std::uint32_t kVehicleMode = 0x20A4u;
constexpr std::uint32_t kItemUseLocked = 0x20ACu;
/* Bit 2 is set when the current hero animation has finished. */
constexpr std::uint32_t kAnimFlags = 0xA98u;
constexpr std::uint32_t kHydroFull = 0x20B0u;
constexpr std::uint32_t kPendingItem = 0x20B8u;
constexpr std::uint32_t kSavedItem = 0x20F0u;
constexpr std::uint32_t kRestoreItem = 0x210Cu;
/* last_weapon_id[0]. Circle, while the wrench is out, selects this item. Consuming
 * pending_item[0] replaces it, and restore_item writes saved_item back over it. */
constexpr std::uint32_t kLastWeaponNtsc = 0x00141660u;
constexpr std::uint32_t kLastWeaponPal = 0x00141760u;
constexpr std::uint32_t kGroundMoby = 0x2FCu;
constexpr std::uint32_t kAirFrames = 0x30Eu;
constexpr std::uint32_t kHealth = 0x22A8u;

constexpr std::uint32_t kMobyPos = 0x10u;
constexpr std::uint32_t kMobyState = 0x20u;
constexpr std::uint32_t kMobyFlags = 0x34u;
constexpr std::uint32_t kMobyPvars = 0x78u;
constexpr std::uint32_t kMobyClass = 0xA6u;
constexpr std::uint32_t kMobyCommand = 0xBCu;
constexpr std::uint32_t kMobySize = 0x100u;

constexpr int kTrespasserPad = 615;
constexpr int kFaucet = 341;
constexpr int kHydroPad = 1251;

struct Disc {
    std::uint32_t hero;
    std::uint32_t owned;
    /* The executable's moby-array pointer (D_00160018 / D_0015FF18). */
    std::uint32_t moby_array;
    /* controller_state. held is +0x1A0, pressed +0x1A4, history +0x1E0. */
    std::uint32_t pad;
    bool ntsc;
};

/* The word the faucet and the activation test read. It is not the same address on every level:
 * PAL Pokitaru (11), Umbris (7), Hoven (12) and Veldin II (18) use D_Lxx_00160058, while level 5
 * uses D_L05_00160098. NTSC names that word D_Lxx_0015FFD8 on each of those levels. */
inline std::uint32_t moby_table_word(bool ntsc, int level) {
    if (ntsc) {
        return 0x0015FFD8u;
    }
    switch (level) {
    case 7:
    case 11:
    case 12:
    case 18:
        return 0x00160058u;
    default:
        return 0x00160098u;
    }
}

inline Disc disc_for(bool ntsc) {
    if (ntsc) {
        return {0x0013F350u, 0x0013D4C0u, 0x0015FF18u, 0x0013C940u, true};
    }
    return {0x0013F450u, 0x0013D5C8u, 0x00160018u, 0x0013CA40u, false};
}

/* try_set_help_message (PAL func_00215F80, NTSC FUN_00215130). A loaded level keeps
 * its own copy of these, so every use goes through relocate. text is the 0x50-byte
 * line, msgs the help-table pointer, state the help box (entry count at +0x2C),
 * scratch a spare 0x50-byte line the framed banner only reads while it is up. */
struct HelpPrompt {
    std::uint32_t keepalive;
    std::uint32_t owner;
    std::uint32_t id;
    std::uint32_t text;
    std::uint32_t msgs;
    std::uint32_t state;
    std::uint32_t scratch;
};

inline HelpPrompt help_prompt_for(bool ntsc) {
    if (ntsc) {
        return {0x0015F5D0u, 0x0015F5D4u, 0x00161288u, 0x0019A440u,
                0x0015F6A0u, 0x001996D0u, 0x00199428u};
    }
    return {0x0015F6B0u, 0x0015F6B4u, 0x00161388u, 0x0019A540u,
            0x0015F780u, 0x001997D0u, 0x00199528u};
}

/* The green pill at the top of the screen (draw_hud_text_panel): "Enter ship",
 * talk, taxis. Level 0's addresses; relocate_level from level 0. frames counts
 * down and clears visible, visible arms the pill, gate must be nonzero, and
 * text is the 0x80-byte line the pill prints. */
struct UsePill {
    std::uint32_t frames;
    std::uint32_t visible;
    std::uint32_t gate;
    std::uint32_t text;
};

inline UsePill use_pill_for(bool ntsc) {
    if (ntsc) {
        return {0x0015F590u, 0x0015F594u, 0x00161E08u, 0x0017E530u};
    }
    return {0x0015F670u, 0x0015F674u, 0x00161F08u, 0x0017E630u};
}

inline bool in_guest(std::uint32_t address, std::uint32_t size) {
    return size > 0 && address >= kGuestLo && address <= kGuestEnd - size;
}

}  // namespace gadget
