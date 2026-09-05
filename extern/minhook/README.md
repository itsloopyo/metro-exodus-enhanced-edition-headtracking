# MinHook (vendored)

Committed copy of MinHook, compiled into `MetroExodusHeadTracking.asi`.

## Snapshot

- Upstream: https://github.com/TsudaKageyu/minhook
- Commit: `c19241d4b90ced340df64bd43e655f62511518ab` (2025-03-28)
- `git describe`: `v1.3.4-1-gc19241d`
- Licence: BSD-2-Clause, `LICENSE.txt` beside this file, reproduced in
  `THIRD-PARTY-NOTICES.md` at the repo root.

Upstream ships no version macro and tags releases rather than stamping them
into a header, so the commit above is the record of exactly what is built. It
is a commit rather than the `v1.3.4` tag because upstream's own `v1.3.4` is one
commit behind it; the difference is a README typo fix and touches no source.

## Local modification

One file differs from upstream, `src/hook.c`, in one place: `MH_Initialize`
takes `GetProcessHeap()` instead of standing up a private heap with
`HeapCreate`, and `MH_Uninitialize` skips the matching `HeapDestroy`.

BSD-2-Clause permits the change and does not require it to be marked. It is
recorded here and in `THIRD-PARTY-NOTICES.md` so the attribution is not mistaken
for a claim of a pristine copy.

## Verifying

Only `src/hook.c` should differ, and only in the two places above:

```sh
git clone https://github.com/TsudaKageyu/minhook.git /tmp/minhook-up
git -C /tmp/minhook-up checkout c19241d4b90ced340df64bd43e655f62511518ab
diff -ru --strip-trailing-cr /tmp/minhook-up/src extern/minhook/src
diff -u --strip-trailing-cr /tmp/minhook-up/include/MinHook.h extern/minhook/include/MinHook.h
```

Only the files this mod builds are committed: `include/`, `src/`, `LICENSE.txt`
and `AUTHORS.txt`. Upstream's own build systems, CMake files and DLL resources
are not, so the diff above is scoped to `src/` and `include/` rather than run
over the whole tree.
