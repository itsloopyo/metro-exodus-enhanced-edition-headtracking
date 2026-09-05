#include "build_profile.h"

namespace metroex {

// Metro Exodus Enhanced Edition, Steam, x64. TimeDateStamp 0x6A70B771 is
// 2026-08-03; the linker leaves CheckSum at zero on this EXE, so the
// fingerprint routes on the timestamp and the image size alone.
//
// The field-of-view addresses are derived; the game-state, sights and camera
// addresses are not, and are zero rather than guessed. Per build_profile.h that
// costs the features that read them and nothing else.
//
// base_fov_pin_rva is 0 here on purpose. The pin the profile below carries was
// confirmed on THAT build's bytes, and this build is not installed anywhere the
// mod can read it back from, so copying the address across would be a guess
// written into the game's own code. Head tracking does not work on this build
// either - it has no camera addresses - so the cost is one already-dormant
// feature staying dormant.
//
// How the field-of-view addresses were derived, so a patch can be re-derived
// the same way rather than re-discovered:
//
//   base_fov_cvar_rva  The `r_base_fov_option` string has exactly two code
//                      references. One is the console-variable constructor
//                      call, whose `this` is this object; the constructor
//                      stores the name, the value pointer and the bounds at
//                      +0x08, +0x18, +0x20 and +0x24, and the matching setter
//                      refuses anything outside those bounds. The value it
//                      guards is the base field of view in degrees, shipped
//                      bounded to 60 and 75.
//
//   camera_fov_rva     Two functions write it, both as
//                      clamp(per-camera coefficient * base field of view,
//                      0.01, 179). The base field of view they read is a
//                      second global the engine eases toward the console
//                      variable at 5.5 per second, which is why writing the
//                      variable is enough to move the picture.
//
//   camera_aspect_rva  Written beside the field of view as the engine's
//                      pixel-aspect term times the viewport ratio. The
//                      engine's own screen-to-world unprojection multiplies
//                      the horizontal half-field tangent by exactly this, which
//                      is what pins the field of view above as VERTICAL and the
//                      engine as Hor+.
extern const BuildProfile kSteamProfile_20260803 = {
    "steam-win64-20260803",

    0x6A70B771,  // TimeDateStamp
    0x03233000,  // SizeOfImage
    0x00000000,  // CheckSum

    0,  // level_state_root_rva - not derived
    0,  // level_state_object_offset
    0,  // level_state_flag_offset
    0,  // level_state_vtable_rva
    0,  // pause_flag_offset
    0,  // ads_flag_rva - not derived

    0x01703884,  // camera_fov_rva
    0x01703888,  // camera_aspect_rva

    0,  // camera_block_rva - not derived
    0,  // view_builder_rva - not derived

    0x02FE6120,  // base_fov_cvar_rva
    0,           // base_fov_pin_rva - not derived
    0,           // crosshair_cvar_rva - not derived

    0,  // torch_root_rva - not derived
    0,  // torch_player_adjust
    0,  // torch_subsystem_offset
    0,  // torch_component_offset
    0,  // torch_component_guard
    0,  // torch_entity_adjust
    0,  // torch_narrow_rva
    0,  // torch_matrix_offset
    0,  // torch_right_offset
    0,  // torch_up_offset
    0,  // torch_dir_offset
    0,  // torch_pos_offset
};

// Metro Exodus Enhanced Edition, Steam, x64, TimeDateStamp 0x6A9046B0. The
// patch that produced this build left every address in the published camera
// block where it was and moved the console-variable table, so only
// base_fov_cvar_rva differs from the profile above. Both were re-checked in a
// running game rather than carried over: the block by reading the basis back
// and confirming right == cross(up, forward), the variable by reading its name
// through the pointer at +0x08.
//
// The camera addresses this build adds:
//
//   camera_block_rva   The block the two publishers write and the renderer
//                      consumes. Found by scanning the game's .data for three
//                      consecutive unit vectors that are mutually orthogonal
//                      while a level was loaded; exactly one such triple sits
//                      beside a position, a view matrix whose columns are that
//                      basis, and a projection matrix carrying the live field
//                      of view.
//
//   base_fov_pin_rva   The `jbe` in the function that eases the live base field
//                      of view, at the point where it chooses between the
//                      console variable and a constant 60 degrees on the
//                      level-state byte. Found by disassembling the one writer
//                      of the live base field of view - a displacement scan of
//                      .text finds exactly one - and confirmed in a running
//                      level: with the console variable at 90 the live base sat
//                      at 60.000, replacing these six bytes with NOPs moved it
//                      to 90.000 within half a second, and putting them back
//                      snapped it to 60.
//
//   view_builder_rva   The function both publishers tail-call once the block's
//                      position, basis and projection are written. It builds
//                      the double-precision view matrix at +0x100, multiplies
//                      it into the view-projection at +0x180, narrows both to
//                      float at +0x40 and +0xC0, and extracts the six frustum
//                      planes at +0x200. Taking the block in RCX and nothing
//                      else, it is the last point in the frame where changing
//                      the camera still changes what the engine culls.
extern const BuildProfile kSteamProfile_20260827 = {
    "steam-win64-20260827",

    0x6A9046B0,  // TimeDateStamp
    0x03234000,  // SizeOfImage
    0x00000000,  // CheckSum

    0x01696CA0,  // level_state_root_rva
    0x08,        // level_state_object_offset
    0x02B,       // level_state_flag_offset
    0x0138BE30,  // level_state_vtable_rva
    0x152,       // pause_flag_offset
    0,           // ads_flag_rva - not derived

    0x01703884,  // camera_fov_rva
    0x01703888,  // camera_aspect_rva

    0x017033D0,  // camera_block_rva
    0x005C8CB0,  // view_builder_rva

    0x02FE67E8,  // base_fov_cvar_rva
    0x00581F1A,  // base_fov_pin_rva
    0x02FED3E0,  // crosshair_cvar_rva

    0x01696CE0,  // torch_root_rva
    0x0E0,       // torch_player_adjust
    0x10F0,      // torch_subsystem_offset
    0x190,       // torch_component_offset
    0x282,       // torch_component_guard
    0x620,       // torch_entity_adjust
    0x081710,    // torch_narrow_rva
    0x798,       // torch_matrix_offset
    0x030,       // torch_right_offset
    0x040,       // torch_up_offset
    0x050,       // torch_dir_offset
    0x060,       // torch_pos_offset
};

namespace {

// Newest first: the top entry is the diagnostic primary the "this build is not
// one I know" line compares against.
const BuildProfile kProfiles[] = {
    kSteamProfile_20260827,
    kSteamProfile_20260803,
};

}  // namespace

const BuildProfile* const kKnownProfiles = kProfiles;
const int kKnownProfileCount = static_cast<int>(sizeof(kProfiles) / sizeof(kProfiles[0]));

}  // namespace metroex
