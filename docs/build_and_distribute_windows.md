# Building, Running, and Distributing on Windows

This covers three different things, in increasing order of effort:

1. **Building and running** the browser for development/testing (what this
   project has been using all along).
2. **Rebuilding after a code change.**
3. **Producing a standalone installer `.exe`** that can be copied to and
   installed on a different Windows machine.

## 1. Build and run (development)

From `src/brave/` (this checkout is at `chromium/src/brave` relative to the
repo root):

```
pnpm run build Component
```

This runs branding/resource generation, `gn gen`, and `autoninja` for the
`Component` configuration under `chromium/src/out/Component/`. It's an
incremental build - after the first full build (which takes hours), each
rebuild only recompiles what changed.

Run it:

```
chromium/src/out/Component/brave.exe
```

### Notes specific to this checkout

- **depot_tools**: needed on `PATH` for `autoninja`/`gn` to work.
  `chromium/src/brave/vendor/depot_tools` is the bootstrapped copy in this
  checkout (the one at `chromium/vendor/depot_tools` is not initialized).
- **Package manager**: this project's `package.json` requires `pnpm`, not
  `npm` - `npm run build` fails with an `EBADDEVENGINES` error.
- **Close the browser before rebuilding.** The linker will fail with
  `permission denied` on `.dll`/`.exe` outputs if a previous build's
  `brave.exe` (and its child processes) are still running and holding those
  files open:
  ```
  taskkill /F /IM brave.exe /T
  ```
- **`Component` is a dev configuration, not a distributable build.** It's a
  component (DLL-based) build with DCHECKs enabled (Chromium enables DCHECKs
  by default unless `is_official_build=true` - see `dcheck_always_on` below),
  which means it crashes fatally on any internal invariant violation instead
  of just logging it. This is expected during development but is one reason
  not to hand a `Component` build to another machine - use a `Release`-style
  config instead (next section).

## 2. Building a distributable configuration (`Release`)

Create a separate output directory so `Component` (used for day-to-day
development) is untouched:

```
gn gen out/Release --args="
  import(\"//brave/build/args/brave_defaults.gni\")
  target_os=\"win\"
  target_cpu=\"x64\"
  is_component_build=false
  is_debug=false
  dcheck_always_on=false
  skip_signing=true
  is_ai_automation_browser_branded=true
"
autoninja -C out/Release brave
```

Key differences from the dev `Component` config:
- `is_component_build=false` - everything statically links into `brave.exe`
  instead of shipping a directory full of `.dll`s.
- `dcheck_always_on=false` - the browser degrades gracefully instead of
  crashing on the class of internal-assertion bugs that don't affect
  correctness for an end user (see the thumbnail-capture DCHECK crash
  encountered during this project's testing, for example).
- `skip_signing=true` - see the signing/licensing note below; without this
  the build tries to invoke a real code-signing step and fails.

The resulting `out/Release/brave.exe` is a self-contained, portable build -
copying that one file (plus its `locales/`, `resources.pak`, etc. - i.e. the
whole `out/Release/` output directory minus intermediate build artifacts)
to another Windows machine and running it works without installing anything.

## 3. Building an actual installer (`mini_installer.exe`)

For a proper Windows installer (Start Menu shortcut, uninstaller entry,
updates) rather than a portable folder, build the `create_dist` target from
`src/brave/`:

```
pnpm run create_dist
```

This is Brave's own scripted equivalent of building
`//brave/build/win:create_signed_installer`, which in turn builds
`chrome/installer/mini_installer:mini_installer`. The output is
`out/Release/mini_installer.exe` - a single self-extracting installer, which
is what you'd hand to another Windows system.

### About "a valid license" for the installer

There's no license *file* gating whether the installer runs - Chromium/Brave
are open source (BSD-3-Clause + Mozilla Public License 2.0; this fork's own
additions are under the same MPL-2.0 header already in every new file). Any
Windows machine can run `mini_installer.exe` as-is.

What "valid" more likely means in practice is **code signing**:

- Without a real Authenticode certificate (`skip_signing=true`, as set
  above), `mini_installer.exe` is **unsigned**. It will still install and
  run correctly, but Windows SmartScreen/Defender will show an "Unknown
  publisher" warning on other machines - the user has to click
  "More info" → "Run anyway" once.
- To get rid of that warning, you'd need to buy a real code-signing
  certificate (an EV or standard Authenticode cert from a CA like
  DigiCert/Sectigo, roughly $100-500/year, and for EV certs, verified
  business registration), then set `skip_signing=false` and point
  `brave/script/sign_binaries.py` (via `sign.gni`) at that certificate.
  This isn't something that can be set up without those actual purchased
  credentials, so it's a deliberate follow-up step, not something this
  build does automatically.

For internal/personal distribution (installing on your own other machines,
or a small team), the unsigned installer is normally fine - "Run anyway"
once per machine is the only friction.
