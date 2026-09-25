# Changelog

## [Unreleased]

### Changed

- `MetroExodusHeadTracking.ini` has a new layout. The first time this version starts, it converts the file once into the new layout and keeps the file as it was beside it as `MetroExodusHeadTracking.ini.pre-canonical`. `MetroExodusHeadTracking.ini.pre-canonical.last`, when present, is the file as it was before the most recent conversion: the mod converts the file again when it finds the older layout later, for example after an older version of the mod rewrote it.
- Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:
  - A sensitivity, scale, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`. `[Hotkeys] Toggle`, `CycleMode` and `YawMode` and the `ChordToggle`, `ChordCycleMode` and `ChordYawMode` switches become `ToggleKey`, `CycleTrackingModeKey` and `YawModeKey`; a chord you had switched off is left out of its list.
- `[General] Port` is `[Network] UdpPort`, `[Position] Enabled` is the `[General] RotationEnabled` / `[Position] PositionEnabled` pair, and `[Position] LimitX`, `LimitY`, `LimitYDown`, `LimitZ` and `LimitZBack` are `PositionLimitX`, `PositionLimitY`, `PositionLimitYDown`, `PositionLimitZ` and `PositionLimitZBack`.
- The tracking mode (Page Up / Ctrl+Shift+G) and the yaw mode (Page Down / Ctrl+Shift+H) are saved to the file the moment they change, so the next launch starts where you left them. End still changes the current session only.
- A value the mod cannot use no longer stops it loading. The line is named in `HeadTracking.log` and the setting keeps its default: a `FieldOfView` that is neither 0 nor 60 to 120, for example. `UdpPort` takes 1 to 65535. An old file the previous version refused for its port or its field of view is left as it is, and the mod still does not start until that value is fixed.
- An older version of the mod may not read the new layout correctly. It reads a key that moved as its own default, and it can misread a hotkey or another value that is now written as a name. To go back to an older version, first copy `MetroExodusHeadTracking.ini.pre-canonical` back over `MetroExodusHeadTracking.ini`, which restores the old file.
- The Nexus ZIP no longer carries `MetroExodusHeadTracking.ini`, so extracting it over the game folder cannot replace your settings. The mod writes the file on its first start.
- Uninstalling keeps `MetroExodusHeadTracking.ini` and its `.pre-canonical` copies, so a reinstall starts on your settings.
- Head tracking carries on through the sights whatever `[View] AdsMode` held, and that key is no longer read (9554397).
- `[Hotkeys] AdsMode` and `ChordAdsMode` are no longer read, and neither Insert nor Ctrl+Shift+U cycles an ADS mode any more (9554397).

### Removed

- The sensitivity and axis inversion settings (`[Sensitivity] Yaw`, `Pitch`, `Roll`, `InvertYaw`, `InvertPitch`, `InvertRoll` and `[Position] SensitivityX`, `SensitivityY`, `SensitivityZ`). Set these in your tracker app instead.
- With these settings at their shipped defaults the camera moves as it did before.

## [0.0.0] - 2026-09-05

First release. Everything below is what 0.0.0 does, rather than a diff against
a version anybody ran.

### Added
- Head tracking reaches the view. The mod hooks the 4A function that builds
  the view matrix, the view-projection and the frustum planes out of the
  published camera, writes the head-tracked position and basis in, lets the
  engine derive everything from them, and puts the game's own camera back before
  returning. Culling follows the head, and the aim, weapons, raycasts and AI keep
  reading the camera the player is actually aiming with.
- 6DOF: yaw, pitch and roll about the world up-axis or the camera's own, and
  lean on all three axes, applied in the clean basis so a lean follows the body
  rather than the head.
- A reticle drawn where the rounds go, through the head-turned view, with the
  game's own crosshair hidden while it is up. Head lean is not compensated: the
  error is the lean divided by the range, so at the 30cm default limit the mark
  is about 23 degrees out at arm's length, 3 across a room, and under one past
  twenty metres.
- Head tracking switches itself off in the main menu and while a level loads.
- Windowed play now starts with the game window centred on the monitor it opened
  on. The mod waits for the game to finish placing the window, leaves a window
  the game already centred alone, and leaves fullscreen and borderless windows
  where they are.
- Build profile for the 2026-08-27 Steam build (TimeDateStamp 0x6A9046B0), now
  also carrying the address of the branch that pins the field of view in a
  level. The 2026-08-03 profile is unchanged and still routes for anyone on that
  build.

- `FieldOfView` moves the picture in a level. Writing the console variable is
  not enough on this engine: while a level is loaded the engine picks what it
  eases the field of view toward from a level-state byte, and picks a constant
  60 degrees, so the setting and the game's own slider both move nothing in
  gameplay. The override lifts that hold as well, which is six bytes of the
  running game's code replaced with no-ops after checking they are the
  instruction the build profile says they are, put back when the game exits
  along with the console variable's bounds and value, and not written at all
  when `FieldOfView=0`. The main menu draws at 60 either way.
- The field-of-view console variable is resolved from the frame loop and retried
  for a minute rather than once as the mod loads. The engine builds its console
  variable table while its statics are still being constructed, so a resolve at
  load time reads a half-built object about half the time.
- `Discovery` in the INI logs the tracker pose, the camera the game published
  and the camera the engine built the frame from, on one line per second.
