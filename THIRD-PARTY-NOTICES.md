# Third-Party Notices

MetroExodusHeadTracking bundles, statically links, or credits the third-party components
listed below. Each remains the property of its authors and is used under its own
licence. Where a licence requires the copyright notice, the conditions and the
disclaimer to accompany a binary distribution, the full text is reproduced here
verbatim, and this file ships at the root of every release ZIP we publish.

This repository redistributes no part of Metro Exodus Enhanced Edition: no game
code, no assets, no proprietary DLLs. What analysis of the game did leave in the
source, and what it did not, is set out in the closing section.

| Component | Version | Licence | How it ships |
|-----------|---------|---------|--------------|
| Ultimate ASI Loader | v9.7.4 | MIT | Bundled verbatim in the installer ZIP |
| ModuleList | tree `065a7ac` (inside Ultimate ASI Loader v9.7.4) | MIT, under Ultimate ASI Loader's own licence | Compiled into the vendored dinput8.dll |
| FunctionHookMinHook | `3a384e8` (inside Ultimate ASI Loader v9.7.4) | MIT | Compiled into the vendored dinput8.dll |
| injector | `3a384e8` (inside Ultimate ASI Loader v9.7.4) | zlib | Not compiled in; credited as the repository the two entries above and the loader's MinHook come from |
| miniz | 3.0.0 (inside Ultimate ASI Loader v9.7.4) | MIT | Compiled into the vendored dinput8.dll |
| MinHook | `c19241d4` (v1.3.4-1-gc19241d) in the .asi, `d94c64d3` (v1.3.4-14-gd94c64d) in the loader | BSD-2-Clause | Compiled into `MetroExodusHeadTracking.asi` and into the vendored dinput8.dll |
| cameraunlock-core | 26b4f175a568b985eb56d3538d8cffee13aee506 | MIT | Compiled into `MetroExodusHeadTracking.asi` |
| OpenTrack | n/a | ISC | Not bundled; UDP protocol interoperability only |

---

## Ultimate ASI Loader

- **Version:** `v9.7.4` (commit `6b440669144c4a0bef5718ab155df160d231cd42`)
- **License:** `MIT`
- **Upstream:** https://github.com/ThirteenAG/Ultimate-ASI-Loader
- **Usage:** Vendored at `vendor/ultimate-asi-loader/` and used as the install-time source; `install.cmd` copies `dinput8.dll` into the game exe directory as `winmm.dll` so the mod's `.asi` is loaded.
- **Bundled:** yes.

Taken from the upstream release asset untouched; the upstream licence file
ships beside it at `vendor/ultimate-asi-loader/LICENSE`. `install.cmd` never
reaches the network for the loader, so the vendored copy is the only
install-time source.

- Asset: `Ultimate-ASI-Loader_x64.zip`
- `dinput8.dll` SHA-256: `fa266e3513d02c08a1b808f28c10538a489eaffaa4b0707f7cc1066e71b5afd7`

```
MIT License

Copyright (c) 2023 ThirteenAG

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

That `dinput8.dll` is a static binary and is not one component. The
`Ultimate-ASI-Loader-x64` target in `premake5.lua` at v9.7.4 compiles
`external/injector/minhook/src/**.c`,
`external/injector/utility/FunctionHookMinHook.cpp` and `external/miniz/miniz.c`
alongside the loader's own sources, and `source/dllmain.h` includes
`external/ModuleList/ModuleList.hpp` outside any 32-bit guard, so redistributing
the binary redistributes MinHook, the FunctionHookMinHook wrapper, miniz and
ModuleList as well.

The first three have their own sections below. ModuleList does not, and the
reason is worth stating rather than leaving as a gap: it is a plain file
committed into the Ultimate ASI Loader repository rather than a submodule, and
it carries no copyright header and no licence file of its own, so the MIT
licence reproduced above is the only one that covers it and the notice is
already discharged.

Nothing under the injector repository's own zlib licence reaches this binary.
The x64 target takes exactly two things out of that repository - the MinHook
submodule and the `utility/FunctionHookMinHook.cpp` wrapper - and both are
separately licensed, BSD-2-Clause and MIT respectively, each with its own
section below. `injector.hpp`, the zlib-licensed work itself, is never included
by the loader's sources.

MemoryModule and d3d8to9 sit behind `#if !X64` in `source/dllmain.h` and
`source/dllmain.cpp`, and the minidx9 DirectX headers are referenced by the
32-bit project alone, so none of the three is in this binary. Neither are
safetyhook, zydis, bddisasm or kananlib, which the injector repository carries
and the `Ultimate-ASI-Loader-x64` target does not compile.

The MinHook section covers the copy inside the loader as well as the one linked
into the mod itself. They are different commits - the loader pins
`d94c64d32ea37bc4f5ee47d580709f70c6fb6080`, the mod builds
`c19241d4b90ced340df64bd43e655f62511518ab` - and `LICENSE.txt` and `AUTHORS.txt`
are byte-identical at both, so one reproduction of the text serves both.

---

## injector

- **Version:** commit `3a384e8d1b575c09383b0fab8bd92e34cb654949`, the submodule
  Ultimate ASI Loader v9.7.4 pins at `external/injector/`
- **License:** zlib
- **Upstream:** https://github.com/ThirteenAG/injector
- **Usage:** The repository the loader takes two separately licensed pieces
  from: the `FunctionHookMinHook` wrapper at `utility/`, and the MinHook
  submodule at `minhook/`. Both have their own sections in this file. The
  `Ultimate-ASI-Loader-x64` target compiles nothing else out of it, and the
  loader's sources never include `injector.hpp`.
- **Bundled:** no. Credited as the origin of the two pieces above.

The licence is reproduced whole regardless. Nothing under it is shipped or
altered, so neither the acknowledgment condition nor the "altered source
versions" condition arises; it is here so the chain from the vendored binary
back to every repository that fed it is complete.

```
Copyright (C) 2012-2014 LINK/2012 <dma_2012@hotmail.com>

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

   3. This notice may not be removed or altered from any source
   distribution.
```

---

## FunctionHookMinHook (injector `utility/`)

- **Version:** the `utility/` directory of injector commit
  `3a384e8d1b575c09383b0fab8bd92e34cb654949`, the submodule Ultimate ASI Loader
  v9.7.4 pins at `external/injector/`
- **License:** `MIT`
- **Upstream:** https://github.com/ThirteenAG/injector, `utility/`
- **Usage:** A small RAII wrapper over MinHook. The `Ultimate-ASI-Loader-x64`
  target compiles `external/injector/utility/FunctionHookMinHook.cpp` into the
  loader. Nothing in this repository calls or links it; it ships only inside
  that binary.
- **Bundled:** yes. Compiled into the shipped `dinput8.dll`.

That directory carries its own `LICENSE.txt`, under a different licence and a
different copyright holder from the injector repository hosting it. The zlib
notice above therefore does not cover it, and MIT wants this one to travel with
the binary in its own right.

```
MIT License

Copyright (c) 2019 praydog

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## miniz

- **Version:** 3.0.0, as vendored at `external/miniz/` in Ultimate ASI Loader
  v9.7.4
- **License:** MIT
- **Upstream:** https://github.com/richgel999/miniz
- **Usage:** Zip reading for the loader's `LoadVirtualFilesFromZip` path, which
  the `Ultimate-ASI-Loader-x64` target compiles from `external/miniz/miniz.c`.
  Nothing in this repository calls or links it; it ships only inside that
  binary.
- **Bundled:** yes. Compiled into the shipped `dinput8.dll`.

```
Copyright 2013-2014 RAD Game Tools and Valve Software
Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC

All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

---

## MinHook

- **Version:** commit `c19241d4b90ced340df64bd43e655f62511518ab` (2025-03-28),
  which `git describe --tags` names `v1.3.4-1-gc19241d`, for the copy at
  `extern/minhook/` that is compiled into `MetroExodusHeadTracking.asi`. The
  second copy, inside the vendored `dinput8.dll`, is commit
  `d94c64d32ea37bc4f5ee47d580709f70c6fb6080` (`v1.3.4-14-gd94c64d`), which
  Ultimate ASI Loader v9.7.4 pins through `external/injector/`. Upstream ships
  no version macro and tags releases rather than stamping them into a header, so
  the commits are the record of what is built.
- **License:** `BSD-2-Clause`
- **Upstream:** https://github.com/TsudaKageyu/minhook
- **Usage:** Function hooking library, compiled into `MetroExodusHeadTracking.asi` and, at the second commit above, into the vendored `dinput8.dll`.
- **Bundled:** yes, as compiled-in source at `extern/minhook/`, and again inside the vendored `dinput8.dll`.

MinHook carries two copyright holders: Tsuda Kageyu for MinHook itself, and
Vyacheslav Patkov for the Hacker Disassembler Engine that `src/hde/` is built
from. Both notices appear below exactly as upstream ships them, and both are
retained at the head of every file they cover. `LICENSE.txt` and `AUTHORS.txt`
are byte-identical at the two commits above, so the text below covers the copy
in the loader as well as the one in the `.asi`.

The copy at `extern/minhook/` is modified in exactly one place, and the one in
the loader not at all. In `src/hook.c`, `MH_Initialize`
takes `GetProcessHeap()` rather than standing up a private heap with
`HeapCreate`, and `MH_Uninitialize` skips the matching `HeapDestroy`. Every
other committed file is byte-identical to `c19241d4`.
BSD-2-Clause permits the change and does not require it to be marked; it is
recorded here so the attribution is not mistaken for a claim of a pristine copy.
`extern/minhook/README.md` carries the commands that check the claim.

```
MinHook - The Minimalistic API Hooking Library for x64/x86
Copyright (C) 2009-2017 Tsuda Kageyu.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

================================================================================
Portions of this software are Copyright (c) 2008-2009, Vyacheslav Patkov.
================================================================================
Hacker Disassembler Engine 32 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

-------------------------------------------------------------------------------
Hacker Disassembler Engine 64 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

---

## cameraunlock-core

- **Version:** `26b4f175a568b985eb56d3538d8cffee13aee506`
- **License:** `MIT`
- **Upstream:** https://github.com/itsloopyo/cameraunlock-core
- **Usage:** Shared CameraUnlock tracking library (protocol, pose processing, camera maths), compiled into `MetroExodusHeadTracking.asi`.
- **Bundled:** yes, as a compiled-in git submodule at `cameraunlock-core/`.

Our own code, MIT licensed, reproduced here so the notices are complete.

```
MIT License

Copyright (c) 2026 itsloopyo

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## OpenTrack

- **Version:** n/a. The wire format is consumed, no release is pinned.
- **License:** `ISC`
- **Upstream:** https://github.com/opentrack/opentrack
- **Usage:** This mod implements the OpenTrack UDP pose datagram layout so that OpenTrack and compatible trackers can drive it.
- **Bundled:** no. Not bundled and not linked.

No OpenTrack code, headers or binaries are copied, linked or redistributed, so
its licence triggers no notice obligation here. It is credited because the wire
format is its work.

---

## Metro Exodus Enhanced Edition

Metro Exodus Enhanced Edition and all related names, logos, characters and
marks are trademarks of their respective owners. They are used here only to
identify the game this mod applies to, which is nominative use and not a claim
of any right in them. This project is an unofficial, fan-made modification. It
is not affiliated with, endorsed by, or sponsored by the game's developers, its
publishers, its engine vendor, or any other rights holder. It redistributes no
game code, no game assets and no proprietary DLLs, and it requires a
legitimately purchased copy of the game. Any engine structure offsets,
function addresses or byte patterns referenced in the source were derived by
the authors through independent analysis of a legitimately owned copy, and are
recorded as numbers.

No game source, no decompiler output and no disassembly excerpt is stored here.
What analysis of the game left behind is numbers and prose: the per-build
addresses in `src/steam_offsets.cpp`, the structure offsets each reader uses,
and comments describing what the engine does at them, so a patch can be
re-derived rather than taken on trust. The two byte constants in `src/fov.cpp`
that the field-of-view override checks before it writes anything are x86 opcode
encodings, which the instruction set defines and the game does not.
