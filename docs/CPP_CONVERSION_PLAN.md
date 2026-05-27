<!--
Pisces Moon OS
Copyright (C) 2026 Eric Becker / Fluid Fortune
SPDX-License-Identifier: AGPL-3.0-or-later
Contributions: see CLA.md

fluidfortune.com
-->

# C-to-C++ Conversion Plan

**Status:** Outline only. Not actionable until decided.

**Decision context:** Pisces Moon P4 was written in pure C for the
P4-side firmware (with one C++ translation unit for the RadioLib SX1262
wrapper). This was a deliberate choice over the C++/Arduino style used
on the S3 line. Going back to C++ is technically straightforward but
not free, and only worth doing under specific circumstances. This doc
exists so we know what we'd be signing up for if those circumstances
arise.

---

## When this becomes relevant

The bring-up on the CrowPanel might fail in ways that aren't fixable
without major library churn — for example, if a critical sensor driver
exists only as a C++/Arduino library and porting it to ESP-IDF C is
prohibitively expensive. The Kode Dot port, when it happens, might
favor C++/Arduino because their published examples are all in that
style. Or PlatformIO publication might become a priority and the C/
ESP-IDF combination is too friction-heavy for the broader audience.

If none of those happen, the C version stays. It's working, it's
clean, and the C++ conversion has real costs.

---

## What conversion would entail

### Mechanical changes (scriptable, ~1-2 hours of automated work)

1. **Rename 137 `.c` files to `.cpp`**

   A shell script handles this:
   ```bash
   for f in $(find . -name '*.c' -not -path '*/build/*'); do
       git mv "$f" "${f%.c}.cpp"
   done
   ```

2. **Update every CMakeLists.txt SRCS list**

   Same mechanical rewrite. Find every `"foo.c"` reference, change to
   `"foo.cpp"`. Verify build still parses.

3. **Wrap every public header in `extern "C"`**

   Most of our headers already have `#ifdef __cplusplus extern "C" { ...`
   patterns from the start — we added them defensively. A few don't.
   Sweep for headers that lack the guard and add it.

4. **Replace `static inline` patterns that C++ handles differently**

   C `static inline` in headers can conflict with C++ ODR rules. Most
   of ours are fine; a handful might need to become anonymous-namespace
   declarations or just lose the `static`.

### What stays the same

- **`pm_app_t` struct of function pointers.** C++ handles function-pointer
  structs perfectly fine. No need to convert to virtual classes.
- **`PM_SPI_TAKE(name) { ... } PM_SPI_GIVE()` macro pattern.** Works
  identically in C++.
- **C99 designated initializers** (`.id = "snake", .category = PM_CAT_GAMES`).
  C++20 accepts these natively. Earlier C++ standards require a flag
  or partial rewrite; if ESP-IDF defaults to C++17, we'd need to either
  upgrade or convert to constructor-style init for a few structs.
- **All the FreeRTOS / ESP-IDF API calls.** C ABI. Unchanged.
- **The RadioLib `.cpp` translation unit (`pm_lora.cpp`).** Already C++.
  It loses its `extern "C"` exit boundary if the whole codebase is C++,
  which slightly simplifies things.

### What we don't gain

- **It is NOT "Arduino with setup/loop."** That's specifically the
  Arduino framework. ESP-IDF in C++ still uses `app_main()` as entry
  and the same task-based runtime. Going C++ doesn't make the code
  look like an Arduino sketch.
- **Code doesn't get shorter.** Maybe 5-10% across the codebase if you
  use STL containers in a few places. The bulk of the work is
  hardware/protocol/state-machine code that's the same length either
  way.
- **We don't gain capabilities the C version doesn't have.** The C
  codebase is already doing everything you'd want C++ to do —
  components, vtable-style app dispatch, mutex-protected blocks,
  modular drivers.

### What we do gain

- **Arduino-style contributor onboarding.** People who only know
  Arduino can read C++ code more easily than C with ESP-IDF macros.
  This is a real consideration if the project starts attracting hobbyist
  contributors.
- **Kode Dot compatibility (or any future board where official examples
  are C++/Arduino).** Their hardware abstraction is in Arduino-style
  C++; sharing code between Pisces Moon firmware and a Kode Dot port is
  much easier if both are C++.
- **PlatformIO friendliness.** PlatformIO has weaker support for
  pure-ESP-IDF projects than for Arduino-style. Publishing as
  "PlatformIO-compatible" is a stronger marketing position if we're
  C++/Arduino-shaped.
- **A small handful of community drivers** (some sensor libraries, some
  protocol parsers) that exist only in Arduino/C++ form become directly
  usable instead of requiring a C-IDF port.

---

## Honest read

The C decision was **the right call for the P4 in isolation**. Tight
control over memory layout, predictable stack frames, smaller binaries,
faster builds, fewer surprises. The SPI Treaty discipline is naturally
expressed in C macros, which read more clearly than RAII-wrapper
classes would.

But the C decision creates **friction with the broader ESP32 ecosystem**.
Most hobbyist work happens in Arduino/C++. Most published examples for
expansion modules are Arduino sketches. Most of the "I bought this
board, how do I do X" answers on forums assume Arduino.

If Pisces Moon stays a single-board project, C is correct. If Pisces
Moon expands to multiple boards (Kode Dot, future ELECROW variants,
generic ESP32-S3/P4 dev boards), C++ becomes worth its cost.

The conversion is reversible. We could go C→C++ and back if we needed
to. It's not a one-way door.

---

## Estimated effort

- **Mechanical conversion:** 4-8 hours (scripted renames + manual
  fixups for the ~10% of files that don't auto-convert cleanly).
- **Build validation:** 2-4 hours (getting it to compile clean, fixing
  the inevitable ODR violations, function-pointer template
  instantiations, etc.).
- **Testing:** 4-8 hours (verifying nothing regressed; the LVGL
  callbacks, the FreeRTOS task entries, the cJSON callbacks — every
  place we pass a function pointer to a C API needs to be re-verified).
- **Total:** One focused day of work, roughly.

Not bad. Not free. Worth doing if and only if we have a concrete reason
to (Kode Dot port, PlatformIO publication, contributor friction).

---

## Recommendation

**Do NOT convert proactively.** Push the C alpha to GitHub. Bring up
the hardware. If it works cleanly, ship the C version, write articles
about it, build community around the modular framing. If it doesn't
work cleanly or if a Kode Dot port becomes a priority, revisit this
plan and execute the conversion in one focused session.

The C codebase is not a dead end. Even if we eventually go C++ for
Pisces Moon proper, the C reference implementation is still valuable
as a teaching artifact (how to build an ESP-IDF OS from first
principles) and as a fall-back for resource-constrained targets where
C++'s overhead is unwelcome.

---

## Open questions for if we do convert

1. **C++17 or C++20?** C++20 keeps our C99 designated initializers
   working. C++17 requires reworking ~30-50 designated-init structs into
   constructor-style. Lean toward C++20.

2. **Arduino framework on top of ESP-IDF, or stay pure ESP-IDF in C++?**
   The Arduino-ESP32 layer is an ESP-IDF wrapper that adds `setup()`,
   `loop()`, `Serial`, etc. Going pure ESP-IDF in C++ keeps everything
   we have; layering Arduino on top adds those familiar APIs at the
   cost of a 1-2 MB framework overhead.

3. **What's the migration path for `pm_app_t` and the SPI Treaty
   macros?** They both work in C++ unchanged. Tempting to leave them
   alone vs. modernizing to class-based / RAII patterns. Recommendation:
   leave alone. They work. The conversion is mechanical; don't make it
   a redesign.

4. **`extern "C"` granularity?** Wrap entire headers, or just public
   functions? Wrapping entire headers is simpler and has no downside
   we've identified. Recommendation: wrap entire headers.

5. **Build size impact?** Unknown without testing. C++ STL pulls in
   significant code; if we avoid STL containers and stick with the
   C-style structs we already have, the binary should be within 5-10%
   of the C version. If we go heavy on templates and `<string>` and
   `<vector>`, expect 30-50% bigger.

---

*Last reviewed: Phase 15 session, 2026-05-10.*

*Pisces Moon OS · Fluid Fortune · fluidfortune.com · AGPL-3.0-or-later*
