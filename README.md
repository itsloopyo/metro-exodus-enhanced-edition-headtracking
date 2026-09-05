# Metro Exodus Enhanced Edition Head Tracking

![Metro Exodus Enhanced Edition running with this mod](https://raw.githubusercontent.com/itsloopyo/metro-exodus-enhanced-edition-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Metro Exodus Enhanced Edition that moves the view with your head while your mouse or controller keeps aiming, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **Decoupled look and aim** - head tracking moves the view; your shots still go where the mouse or controller points.
- **6DOF positional tracking** - lean and peek with head position, not just rotation.
- **Works with any OpenTrack-compatible source** - webcam, phone app, or anything else that sends the OpenTrack UDP protocol

## Requirements

- [Metro Exodus Enhanced Edition](https://store.steampowered.com/app/1449560/) on Steam.
- A head tracking source that sends the OpenTrack UDP protocol on port 4242, such as [OpenTrack](https://github.com/opentrack/opentrack).
- Windows 10 or 11, 64-bit.

## Installation

1. Download the installer ZIP from the [Releases](https://github.com/itsloopyo/metro-exodus-enhanced-edition-headtracking/releases) page.
2. Extract it anywhere.
3. Double-click `install.cmd`.
4. Configure OpenTrack to output UDP to `127.0.0.1:4242`.
5. Launch the game.

If the installer cannot find your game, point it at the folder yourself. Either
pass the path as an argument:

```powershell
install.cmd "D:\Games\Metro Exodus Enhanced Edition"
```

or set the override environment variable before running it:

```powershell
$env:METRO_EXODUS_ENHANCED_EDITION_PATH = "D:\Games\Metro Exodus Enhanced Edition"
```

The folder to point at is the one holding `MetroExodus.exe`.

### Manual Installation

Placing the files by hand takes three copies into the folder holding
`MetroExodus.exe`:

1. `vendor\ultimate-asi-loader\dinput8.dll` from the installer ZIP, renamed to
   `winmm.dll`. A proxy DLL is only loaded if the game imports that name, and
   `MetroExodus.exe`'s import table has `winmm.dll` and `dinput8.dll` in it and
   nothing else you would use for this. `winmm.dll` is what the installer picks;
   leaving the file as `dinput8.dll` works too.
2. `plugins\MetroExodusHeadTracking.asi`.
3. `plugins\MetroExodusHeadTracking.ini`, only if you do not already have one.
   The installer never overwrites an existing config and neither should you.

The Nexus ZIP is already laid out this way: extract it straight into the folder
holding `MetroExodus.exe` and the files land where they belong.

## Setting Up OpenTrack

The mod listens on UDP port 4242 for the OpenTrack protocol and applies whatever
pose arrives, at 1:1 scale. In OpenTrack, set **Output** to `UDP over network`
and point it at `127.0.0.1`, port `4242`. Pick whichever **Input** matches your
hardware.

Sensitivity, deadzones, response curves and axis inversion are the tracker's
job, not the mod's, so one profile behaves the same way in every game. Centring
is the tracker's job too: sit the way you play, then use OpenTrack's Center
bind, the CENTER button in a phone app, or SteamVR's reset.

### VR Headset Setup

1. Connect the headset to the PC over Air Link, Virtual Desktop or a link cable.
2. Start SteamVR.
3. In OpenTrack, set **Input** to the SteamVR tracker.
4. Leave **Output** on UDP `127.0.0.1`, port `4242`.

### Webcam Setup

In OpenTrack, set **Input** to `neuralnet tracker`. It tracks your face from a
plain webcam, with no markers and no IR hardware. Leave **Output** on UDP
`127.0.0.1`, port `4242`.

### Phone App Setup

The mod accepts one thing: the OpenTrack UDP protocol on port `4242`. A phone
app is usable here if it sends that protocol itself, or ships a PC-side
companion that does. Check your app against that before anything else.

For an app that does send it, what decides the wiring is how much filtering the
app does before the packet leaves the phone. An app that filters on-device can
point straight at this PC's LAN address on port `4242`. A raw or lightly
filtered feed sent direct can jitter, because the mod's smoothing is sized to
take the edge off a clean signal rather than to rescue a noisy one; that app
should send to OpenTrack instead, so OpenTrack's filters and curves can clean
the feed up before it reaches the game. The test is quick: try direct, hold your
head still, and if the view drifts or shakes, route it through OpenTrack.

I made [Headcam](https://headcam.app) so decent tracking was free for anybody
with a phone already in their pocket, and it filters on-device, so it can send
direct. It is one app that qualifies, not the one you have to use: any app that
filters enough noise works the same way here.

Smoothing is picked per connection from the address the packets come from.
Loopback (`127.0.0.1`) gets `LocalSmoothing`, which defaults to 0; everything
else gets `RemoteSmoothing`, which defaults to 0.15 because phones on WiFi
jitter. Note that "everything else" includes this machine's own LAN address: if
you run OpenTrack here but send to `192.168.x.x` rather than `127.0.0.1`, you
get the remote value. The classifier sees a transport, not a machine.

## Controls

Two equivalent binding sets, use whichever your keyboard has:

| Action              | Nav-cluster | Chord           |
|---------------------|-------------|-----------------|
| Toggle tracking     | `End`       | `Ctrl+Shift+Y`  |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G`  |
| Toggle yaw mode     | `Page Down` | `Ctrl+Shift+H`  |
| Cycle ADS mode      | `Insert`    | `Ctrl+Shift+U`  |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode: rotation and position, then
rotation only, then position only, then back to rotation and position.

`Page Down` / `Ctrl+Shift+H` switches which up-axis head yaw turns about:

1. **World up-axis** (default) - yaw turns about the world's vertical, whatever
   the mouse has done to the camera. Look at the floor, turn your head, and you
   pan across it level with the horizon.
2. **Camera up-axis** - yaw turns about the camera's own vertical, so at steep
   pitches turning your head leans the view instead. Some players prefer it for
   climbing and vehicle sections.

The switch applies immediately and is not written back to the INI, so the next
launch starts on whatever `WorldSpaceYaw` says.

`Insert` / `Ctrl+Shift+U` cycles what happens when you aim down sights.

**None of it runs yet.** The mod cannot tell when your sights are up on either
known build - the address has not been derived - so head tracking stays on
through an aim in all three positions. The key still cycles the setting and
saves it. What each position will do once the sights are detected, all three
starting the same way by swinging the view onto the point the reticle was
marking:

1. **Tracking paused** (default) - the game keeps the camera for as long as the
   sights are up. The sight picture is exactly the game's, and turning or leaning
   your head does nothing until you lower the weapon. Tilting it still rolls the
   view, in this mode and the other two: a tilt does not move your eye off the
   barrel or the aim off the middle of the screen, so there is nothing to hand
   back to the gun.
2. **Tracking on, with an aim marker** - head tracking carries on from the
   snapped position, and a small white crosshair is drawn wherever your rounds
   will actually land. This white marker is authoritative, including with scoped
   weapons. A scope's built-in reticle is only accurate while your eye is
   exactly aligned with the optic, so the two reticles separate when head
   tracking moves your view off that sight line.
3. **Tracking on, no aim marker** - the same as 2 without the marker, for a
   cleaner screen when you are happy reading the sights themselves.

The choice is saved to `MetroExodusHeadTracking.ini`, so it survives a restart.
Pressing the key writes the mode it switched to into `HeadTracking.log`.

### The reticle

While head tracking is running the mod draws its own reticle - a small white
cross - at the point your rounds are going, and hides the game's crosshair so
there is one mark on screen rather than two. Turn tracking off with `End`, or
open the main menu, and the game's own crosshair comes straight back.

Once the sights are detected, the ADS mode will decide it instead: only
**tracking on, with an aim marker** draws the mark. The other two hand the crosshair back to the game
for as long as the sights are up, so **tracking paused** gives you the sight
picture the game would have drawn on its own and **tracking on, no aim marker**
leaves you reading the sights themselves.

Two things follow from that, and both are worth knowing before you play:

- Metro's crosshair spreads its rays with your weapon's accuracy. The mod's mark
  does not carry that, so you lose the dispersion read while tracking is on.
- The mark follows your head's **rotation** exactly. It does not correct for
  head **lean**, and that error is the angle your lean subtends at whatever you
  are shooting: with your head at the 30cm default limit the mark sits about 23
  degrees off a target at arm's length, about 8 degrees off one at two metres,
  about 3 across a room, and under one past twenty. Leaning hard while shooting
  something close is where the mark and the round disagree by enough to matter;
  centre your head for close work.

## Configuration

`MetroExodusHeadTracking.ini` is written next to `MetroExodus.exe` on first
launch. Edit it with the game closed and restart to apply. Delete it to reset to
defaults. Every key it holds is below, at its default, with the file's own
comments shortened.

```ini
[General]
EnableOnStartup=1
; UDP port to listen on, 1024 to 65535. A value outside that range stops the mod
; loading rather than being ignored.
Port=4242
; Which up-axis head yaw turns about.
;   true  - the world up-axis. Look at the floor and turn your head and you
;           still pan across it, level with the horizon.
;   false - the camera's own up-axis, which leans the view once the camera is
;           pitched steeply.
; Page Down switches between the two while you play; this key is only what the
; mod starts on.
WorldSpaceYaw=1

[Sensitivity]
Yaw=1
Pitch=1
Roll=1
InvertYaw=0
InvertPitch=0
InvertRoll=0

[Smoothing]
; Smoothing applied when the tracker runs on this machine (loopback).
; 0 = no smoothing, 1 = heavy.
LocalSmoothing=0
; Smoothing applied when the tracker is a remote device on the network.
RemoteSmoothing=0.15

[Position]
; Positional (6DOF) head tracking: leaning and moving your head.
Enabled=1
SensitivityX=1
SensitivityY=1
SensitivityZ=1
; Travel limits in metres.
LimitX=0.3
LimitY=0.2
LimitYDown=0.2
LimitZ=0.4
LimitZBack=0.1

[Hotkeys]
; Virtual-key codes. Defaults: End (toggle tracking), Page Up (cycle mode:
; rotation and position, rotation only, position only).
Toggle=0x23
CycleMode=0x21
YawMode=0x22
AdsMode=0x2D
; Chord alternatives: Ctrl+Shift+Y (toggle tracking), Ctrl+Shift+G (cycle
; mode), Ctrl+Shift+H (yaw mode), Ctrl+Shift+U (cycle ADS mode).
ChordToggle=1
ChordCycleMode=1
ChordYawMode=1
ChordAdsMode=1

[View]
; What head tracking does while you are aiming down sights.
;   paused  - the game keeps the camera until you lower the weapon.
;   marker  - tracking carries on, and a marker is drawn where your rounds
;             will land.
;   tracked - tracking carries on, nothing drawn.
; Insert cycles the same three in that order and saves the choice back here.
; Anything else in this key reads as paused.
AdsMode=paused

[Camera]
; Field of view in degrees, the same number the game's own Field of View slider
; sets. 0 leaves that slider alone. The game stops its slider at 75, and its
; engine holds the drawn picture at 60 in a level whatever the slider says; a
; value here goes past both, up to 120. Setting it also takes the engine's hold
; off, which is six bytes written into the running game's code, and it widens
; the bounds the game's own setter enforces. The setting and its bounds are put
; back when the game exits; the six bytes live only in memory and go with the
; process. Accepted: 0, or 60 to 120. Anything else stops the mod loading.
FieldOfView=0
; Per-frame camera logging: the pose the tracker sent, the camera the game
; published, and the camera the engine built the frame from. It is how a game
; patch that moved the camera gets re-derived. It writes megabytes an hour to
; the log; leave it off.
Discovery=0
```

Four things to know about `FieldOfView`:

- **The game's own slider does not move the picture in a level on this build.**
  Measured: with the setting at 90 degrees, the field of view the game drew a
  level with sat at exactly 60.000 for the whole level. The engine picks what it
  eases the field of view toward from a level-state byte, and while a level is
  loaded it picks a constant 60 rather than the setting. That is what this
  option is for; without lifting that hold there would be nothing here worth
  setting.
- **Nothing on disk is touched to lift that hold.** The mod patches the running
  game's code in memory, having checked the instruction against what the build
  profile says is there. That patch lives in the game's own memory and goes with
  the process when it exits; the console variable's value and bounds, which the
  game may write to its settings, are put back explicitly on the way out. With
  `FieldOfView=0` nothing is written at all.
- **The main menu still draws at 60.** Measured with the hold lifted: the menu
  holds the field of view at 60 whatever the setting says, so judge the setting
  in a level rather than on the menu.
- **It is written into the game's own setting, not layered on top of it.** The
  override widens the bounds the game's own slider enforces and writes the value
  they guard, which is what makes the whole engine follow rather than only the
  picture. The mod puts all three back when the game exits, so a setting the game
  saves on the way out is the one it started with. What the game does if it saves
  its settings mid-session, while the override is in place, has not been tested.
  If you would rather not find out, set `FieldOfView=0` and use the game's own
  slider.

## Troubleshooting

Start with the log: `HeadTracking.log`, next to `MetroExodus.exe`. It records
whether the loader engaged, which build profile matched, the hotkeys it
registered and whether tracker packets are arriving. Each launch starts a fresh
file and the launch before it is kept as `HeadTracking.prev.log`, so if the game
crashed and you relaunched before fetching the log, the crashed session is in
the `.prev` one.

**Mod not loading**

- Check that `winmm.dll`, `MetroExodusHeadTracking.asi` and
  `MetroExodusHeadTracking.ini` are all in the folder holding `MetroExodus.exe`.
- If `HeadTracking.log` is absent, the loader never engaged. Re-run
  `install.cmd` and let it pick the folder.
- If the log names a `Port` or `FieldOfView` out of range, that stops the whole
  mod rather than just the key: nothing else in the file is applied and head
  tracking does not start. `Port` takes 1024 to 65535 and `FieldOfView` takes 0
  or 60 to 120. Fix the line, or delete the INI to get a clean default set
  back.
- If the log says no build profile matched, the game has been patched since this
  release. The mod stays dormant and the game runs stock; check the Releases page
  for an update.
- If the log names a build profile but says its camera addresses have not been
  derived, the mod recognises your build and cannot move the view on it yet.
  Check the Releases page for an update.

**Known limitations**

- Head tracking stays on behind the pause menu, the journal and the death
  screen. The engine's level-state byte does not move for any of them and nothing
  else that does has been found. It costs you a view that drifts under a menu you
  are reading; it costs nothing you can aim or walk into, because the camera the
  game reads is put back clean every frame either way.
- Detecting that the sights are up is not derived on either known build, so the
  `Insert` cycle has nothing to act on yet.
- If the tracker stops sending, the view holds the last pose it was given rather
  than snapping back. It picks up again when packets resume.

**No tracking response**

- Confirm the tracker is running and its output is UDP to `127.0.0.1`, port
  `4242`.
- Check `Port` in the INI matches the port the tracker sends to.
- Press `End` or `Ctrl+Shift+Y` in case tracking is toggled off, and check
  `EnableOnStartup` in the INI.
- The log records whether packets are arriving, which separates a tracker
  problem from a mod problem.

**Jittery or unstable tracking**

- Raise `RemoteSmoothing` if the tracker is a phone or another machine on the
  network, or `LocalSmoothing` if it runs on this PC.
- A phone app sending a raw feed direct is the usual cause; route it through
  OpenTrack so its filters can clean the feed up.

**Wrong rotation axis, or the view sits off to one side**

- Centre in your tracker app, seated the way you play: OpenTrack's Center bind,
  the CENTER button in a phone app, or SteamVR's reset.
- If yaw feels wrong when you are looking a long way up or down, try the other
  yaw mode with `Page Down` or `Ctrl+Shift+H`.
- If an axis moves the wrong way, invert it in the tracker, or set `InvertYaw`,
  `InvertPitch` or `InvertRoll` in the INI.

**The game window moved when I launched**

- By design, and only when you play windowed: once the game has finished placing
  its window, the mod centres it on the work area of the monitor it opened on. A
  window the game centred itself, and a fullscreen or borderless one that already
  fills the screen, are left where they are. There is no setting for this.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod's `.asi`, its INI and its logs. The
Ultimate ASI Loader is only removed if the installer put it there; use
`uninstall.cmd /force` to remove it anyway.

## Building from Source

Visual Studio 2022 with the C++ desktop workload, CMake 3.20 or newer, and Git.
The build needs no copy of the game.

```powershell
git clone --recurse-submodules https://github.com/itsloopyo/metro-exodus-enhanced-edition-headtracking
cd metro-exodus-enhanced-edition-headtracking
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```

## Community & Support

- [Discord](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch of head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your phone into a head tracker

## License

MIT License - see [LICENSE](LICENSE) for details.

Third-party components and their licences are listed in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Credits

- Metro Exodus is made by 4A Games and published by Deep Silver.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG loads the mod into the game.
- [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu provides the function hooking.
- [OpenTrack](https://github.com/opentrack/opentrack) defines the tracking protocol the mod listens for.

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by 4A Games or Deep
Silver. Use at your own risk. It requires a legitimately purchased copy of the
game.
