# Metro Exodus Enhanced Edition Head Tracking

![Metro Exodus Enhanced Edition running with this mod](https://raw.githubusercontent.com/itsloopyo/metro-exodus-enhanced-edition-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Metro Exodus Enhanced Edition that moves the view with your head while your mouse or controller keeps aiming, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **Decoupled look and aim** - head tracking moves the view; your shots still go where the mouse or controller points.
- **6DOF positional tracking** - lean and peek with head position, not just rotation.
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- [Metro Exodus Enhanced Edition](https://store.steampowered.com/app/1449560/) on Steam.
- A head tracking source that sends the OpenTrack UDP protocol on port 4242, such as [OpenTrack](https://github.com/opentrack/opentrack).
- Windows 10 or 11, 64-bit.

## Installation

### Lopari

Download [Lopari](https://lopari.app), choose **Metro Exodus Enhanced Edition**, and click
**Play with head tracking**.

### Standalone Installer

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
   Leaving it out is fine too: the mod writes the file on its first start.

The Nexus ZIP is laid out the same way, without the config: extract it straight
into the folder holding `MetroExodus.exe` and the files land where they belong,
and the mod writes `MetroExodusHeadTracking.ini` on its first start.

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

Two equivalent binding sets by default, use whichever your keyboard has. Both are
lists in the `[Hotkeys]` section of the config, so you can rebind either or add
more (see [Configuration](#configuration)).

| Action              | Nav-cluster | Chord           |
|---------------------|-------------|-----------------|
| Toggle tracking     | `End`       | `Ctrl+Shift+Y`  |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G`  |
| Toggle yaw mode     | `Page Down` | `Ctrl+Shift+H`  |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode: rotation and position, then
rotation only, then position only, then back to rotation and position.

`Page Down` / `Ctrl+Shift+H` switches which up-axis head yaw turns about:

1. **World up-axis** (default) - yaw turns about the world's vertical, whatever
   the mouse has done to the camera. Look at the floor, turn your head, and you
   pan across it level with the horizon.
2. **Camera up-axis** - yaw turns about the camera's own vertical, so at steep
   pitches turning your head leans the view instead. Some players prefer it for
   climbing and vehicle sections.

The tracking mode and the yaw mode are saved to the config the moment you
change them (`RotationEnabled`, `PositionEnabled` and `WorldSpaceYaw`), so the
next launch starts where you left them. `End` / `Ctrl+Shift+Y` changes the
current session only; whether tracking is on at launch is `EnableOnStartup`.

### Aiming down sights

Head tracking stays on while you aim. The weapon stays where your mouse or
controller points it, so with your head turned it sits off to one side with its
sights still lined up, and your rounds land where those sights point. Head
movement is scaled to the zoom, so a scope does not magnify it.

Leaning is meant to ease out while the sights are up, because it moves your eye
off them. The mod cannot yet tell when your sights are up on either known build,
so for now the lean stays on while you aim.

### The reticle

While head tracking is running the mod draws its own reticle - a small white
cross - at the point your rounds are going, and hides the game's crosshair so
there is one mark on screen rather than two. Turn tracking off with `End`, or
open the main menu, and the game's own crosshair comes straight back. The mark
is drawn the same way while you aim down sights.

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

<!-- cameraunlock:config -->
The mod reads its settings from `MetroExodusHeadTracking.ini` in the game folder, and creates the file when it starts and finds none. Edit it with any text editor.

Earlier versions of the mod used an older layout for this file. The first time this version starts, it converts the file once into the layout below and keeps the file as it was beside it as `MetroExodusHeadTracking.ini.pre-canonical`. `MetroExodusHeadTracking.ini.pre-canonical.last`, when present, is the file as it was before the most recent conversion: the mod converts the file again when it finds the older layout later, for example after an older version of the mod rewrote it.

Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:

- Reticle settings, and a key that toggled the reticle.
- A sensitivity, scale, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
- The setting for a feature that earlier versions shipped switched off while it was untested. It now follows the mod's default.

An older version of the mod may not read the new layout correctly. It reads a key that moved as its own default, and it can misread a hotkey or another value that is now written as a name. To go back to an older version, first copy `MetroExodusHeadTracking.ini.pre-canonical` back over `MetroExodusHeadTracking.ini`, which restores the old file.

With every setting at its default, the file reads:

```ini
; Metro Exodus Enhanced Edition head tracking settings.
; Comments start with ; and go on their own line. Text after a value is part of the value.
; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; leave empty for none.

[CameraUnlock]
; Written by the mod. Leave this section in place.
ConfigFormat=1

[Network]
; UDP port the mod receives tracker data on (OpenTrack protocol).
UdpPort=4242

[General]
; true: head tracking is on when the game starts. ToggleKey turns it on and off.
EnableOnStartup=true
; true: yaw turns around the world's up axis. false: around the camera's own up axis.
WorldSpaceYaw=true
; true: turning your head turns the view.
; Tracking mode at startup, with PositionEnabled. The mode hotkey changes both.
RotationEnabled=true

[Smoothing]
; Smoothing when the tracker runs on this PC. 0 is the least, 1 the most.
LocalSmoothing=0.0
; Smoothing when the tracker is another device on the network, such as a phone.
; 0 is the least, 1 the most.
RemoteSmoothing=0.15

[Position]
; true: moving your head moves the view.
; Tracking mode at startup, with RotationEnabled. The mode hotkey changes both.
PositionEnabled=true
; How far, in metres, leaning left or right can move the view.
PositionLimitX=0.3
; How far, in metres, raising your head can move the view.
PositionLimitY=0.2
; How far, in metres, lowering your head can move the view.
PositionLimitYDown=0.2
; How far, in metres, leaning forward can move the view.
PositionLimitZ=0.4
; How far, in metres, leaning back can move the view.
PositionLimitZBack=0.1

[Hotkeys]
; Turns head tracking on and off.
ToggleKey=End, Ctrl+Shift+Y
; Changes the tracking mode: rotation and position, rotation only, position only.
CycleTrackingModeKey=PageUp, Ctrl+Shift+G
; Switches yaw between the world's up axis and the camera's own (WorldSpaceYaw).
YawModeKey=PageDown, Ctrl+Shift+H

[Light]
; true: a light you carry points where you look instead of where you aim.
LightFollowsHead=true
; How far the light turns for each degree your head turns.
; 1 matches the view, 0 keeps the light on your aim.
LightMultiplier=1.5

[Camera]
; Field of view in degrees, the number the game's own Field of View slider sets.
; 0 leaves that slider alone; otherwise 60 to 120. The game stops its slider at 75, and
; its engine holds the picture at 60 in a level whatever the slider says; a value here
; goes past both. Setting it writes six bytes into the running game's code to take that
; hold off, and widens the bounds the game's own setter enforces. The setting and its
; bounds are put back when the game exits. The main menu draws at 60 either way.
FieldOfView=0.0
; true: write the pose the tracker sent, the camera the game published and the camera
; the engine built the frame from to HeadTracking.log every frame. It writes megabytes
; an hour; leave it false unless you were asked to turn it on.
Discovery=false
```
<!-- /cameraunlock:config -->

The mod reads the file when the game starts, so an edit takes effect at the next
launch.

Changed from earlier versions, beyond what the conversion drops:

- `[Sensitivity]` (`Yaw`, `Pitch`, `Roll`, `InvertYaw`, `InvertPitch`,
  `InvertRoll`) and `[Position] SensitivityX`, `SensitivityY` and `SensitivityZ`
  are gone. Every one of them shipped at 1 or false, so the default view is
  unchanged; a value you changed is dropped and named in `HeadTracking.log`.
- `[General] Port` is `[Network] UdpPort`, `[Position] Enabled` is the
  `RotationEnabled` / `PositionEnabled` pair, and the limits are
  `PositionLimitX`, `PositionLimitY`, `PositionLimitYDown`, `PositionLimitZ` and
  `PositionLimitZBack`.
- `[Hotkeys] Toggle`, `CycleMode` and `YawMode` (virtual-key codes) and the
  `ChordToggle`, `ChordCycleMode` and `ChordYawMode` switches are one key list
  per action: `ToggleKey`, `CycleTrackingModeKey` and `YawModeKey`. A chord you
  had switched off is left out of its list.
- `[View] AdsMode` is no longer read. Head tracking carries on through the
  sights whatever it held.
- `[Hotkeys] AdsMode` and `ChordAdsMode` are no longer read, and neither
  `Insert` nor `Ctrl+Shift+U` cycles an ADS mode any more.
- A value the mod cannot use no longer stops it loading: the line is named in
  `HeadTracking.log` and the setting keeps its default. That covers a
  `FieldOfView` that is neither 0 nor 60 to 120, and `UdpPort` now takes 1 to
  65535. An old file that the previous version refused for its port or its
  field of view is left as it is, and the mod still does not start until you fix
  that value.

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

- Check that `winmm.dll` and `MetroExodusHeadTracking.asi` are both in the
  folder holding `MetroExodus.exe`.
- If `HeadTracking.log` is absent, the loader never engaged. Re-run
  `install.cmd` and let it pick the folder.
- If the log says the file could not be loaded because of its `Port` or
  `FieldOfView`, it is a file from an earlier version that that version also
  refused, and head tracking does not start until the line is fixed. `Port`
  takes 1024 to 65535 and `FieldOfView` takes 0 or 60 to 120. Fix the line, or
  delete the INI to get a clean default set back. In the new layout a value the
  mod cannot use is named in the log and the setting keeps its default.
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
  lean stays on while you aim.
- If the tracker stops sending, the view holds the last pose it was given rather
  than snapping back. It picks up again when packets resume.

**No tracking response**

- Confirm the tracker is running and its output is UDP to `127.0.0.1`, port
  `4242`.
- Check `UdpPort` in the INI matches the port the tracker sends to.
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
- If an axis moves the wrong way, invert it in the tracker.

**The weapon is off to one side when I aim down sights**

- Your head is turned: the weapon stays on your aim and you are looking past it.
  Turn back to it, or move your aim to where you are looking.

**The game window moved when I launched**

- By design, and only when you play windowed: once the game has finished placing
  its window, the mod centres it on the work area of the monitor it opened on. A
  window the game centred itself, and a fullscreen or borderless one that already
  fills the screen, are left where they are. There is no setting for this.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod's `.asi` and its logs, and leaves
`MetroExodusHeadTracking.ini` (and any `.pre-canonical` copies of it) in place so
a reinstall starts on your settings. Delete those by hand to remove the settings
too. The Ultimate ASI Loader is only removed if the installer put it there; use
`uninstall.cmd /force` to remove it anyway.

## Building from Source

Visual Studio 2022 with the C++ desktop workload, CMake 3.20 or newer, Git, and
Node.js on `PATH` (one of the tests runs the shared config lint with it). The
build needs no copy of the game.

```powershell
git clone --recurse-submodules https://github.com/itsloopyo/metro-exodus-enhanced-edition-headtracking
cd metro-exodus-enhanced-edition-headtracking
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```

## Community & Support

- Discord: [Loop's Head Tracking Hangout](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch for the released head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your iPhone or Android phone into the head tracker

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
