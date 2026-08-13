# Battery Optimization Playbook

A reusable guide for battery-powered embedded projects (written from ESP-IDF +
LVGL experience on the ESP32-S3, but the principles generalize to any MCU
with a "mostly idle, occasionally active" duty cycle — clocks, watches,
sensors, remotes, badges).

**Origin**: this project (`pocket-watch`) went from ~30 min to 3+ day battery
life. The list of *what* to change ([BATTERY_OPTIMIZATIONS.md](BATTERY_OPTIMIZATIONS.md))
took an afternoon to write. Actually implementing it took days, because
almost every item had a non-obvious failure mode that only showed up on real
hardware (silent hangs, blank panels on wake, timers that quietly stop
running exactly when you need them). This doc exists so the *next* project
starts from the hard-won list of gotchas instead of rediscovering them.

## How to use this doc

1. Copy this file into a new project on day one, before feature work starts.
2. Work through the [priority checklist](#priority-ordered-checklist) in
   order — each item is a prerequisite for the next one mattering.
3. Start a project-local `BATTERY_OPTIMIZATIONS.md` tracking table (template
   at the bottom) as soon as you have a working prototype, and update it as
   you measure and fix things.
4. Build the [diagnostics harness](#build-a-measurement-harness-on-day-one)
   before you start optimizing, not after — you cannot tell if any of this
   worked without it.

## Mental model

For a device that's idle almost all the time, battery life is dominated by
**one number: how deep is idle sleep, and how long does it run uninterrupted
between wakes.** Everything else is a rounding error by comparison until
that number is right.

That gives a strict order of operations:

1. First, make deep idle sleep *possible at all* — remove whatever is
   silently preventing it or capping it to milliseconds.
2. Then, make sure the device *actually wakes up* when it needs to (a real
   interrupt-driven wake source), not just "sleeps and hopes a polling loop
   catches up later."
3. Only then do peripheral power-gating, CPU frequency scaling, dimming
   tiers, and log/build hygiene start to matter — they're all fighting over
   a sleep window that doesn't exist yet if 1–2 aren't done.

Fixing step 3 before steps 1–2 is the classic trap: it feels like progress
(current draw looks a little better) but the actual multi-day win is still
locked behind an unfixed tick timer or a missing GPIO wake source.

## Priority-ordered checklist

Do these roughly in this order for a new project:

1. **Find and eliminate periodic timers that fire during idle.** Any
   framework/library that runs its own periodic hardware timer (UI
   frameworks, RTOS tick, animation engines) caps your sleep window to that
   timer's period, no matter what else you fix. This is almost always the
   single biggest lever and the easiest thing to miss, because the timer is
   usually buried in a vendored/managed dependency, not your own code.
2. **Register real wake sources**, not just polling. GPIO/RTC wake needs to
   be explicitly armed (and re-armed every sleep cycle, in some driver
   designs) — it is not automatic just because a pin has an interrupt
   handler for normal operation.
3. **Power down or fully release every peripheral that isn't actively
   needed** — display panel, backlight, audio codec/I2S, radios, sensors —
   instead of initializing everything at boot and leaving it live for the
   process lifetime "for convenience." Open/close around actual use, or
   reference-count shared resources multiple subsystems might need.
4. **Make sure logic that must keep running through the idle state runs on
   a mechanism that isn't itself suspended in that state.** A common bug
   class: pausing your UI framework's worker to save power *also* silently
   pauses every timer that framework owns, including ones your business
   logic depends on (alarms, countdowns, anything that must fire while the
   screen is off).
5. **Only after 1–4 are solid**, tune CPU frequency-scaling range, add
   intermediate "dim" tiers before full-off, slow down cosmetic polling, and
   strip dev-only build overhead (verbose logging, debug stats/profiling,
   non-release compiler optimization).

## Deep dives on the tricky parts

Each of these was a multi-hour-to-multi-day investigation in practice. The
"why" is the reusable part — the exact API names will differ per platform.

### 1. A framework's own tick timer silently caps sleep duration

UI/animation frameworks often need a periodic "tick" to know how much time
has passed (for animations, timeouts, debouncing). The naive implementation
is a periodic hardware timer that fires every N ms forever, including while
the device is otherwise idle. If the OS/power layer separately supports
automatic light sleep, that sleep can only ever last up to N ms before the
tick timer wakes it — you get thousands of N-ms micro-naps per second
instead of one long sleep. This looks like "sleep is enabled and working"
in logs/config while delivering close to zero of the actual benefit.

**Fix pattern**: switch the framework to an on-demand tick — have it read a
monotonic clock (e.g. a free-running microsecond timer) only when it's
actually about to do work, instead of being driven by its own periodic
timer. If the framework is a vendored/managed dependency without this
option, this may mean patching or forking it — worth doing, since it's
usually the highest-leverage change available.

**How to find it**: don't trust "light sleep enabled" in config. Check
whether anything still fires on a fixed period while the device should be
idle (framework tick, watchdog, telemetry). If sleep windows in a profiler
or current trace look like a fixed short interval no matter what else
changes, this is almost always the cause.

### 2. Wake sources have to be explicitly armed — and re-armed every cycle

Enabling automatic light/deep sleep does not automatically make any given
pin or peripheral a wake source. Each wake source (GPIO edge, RTC pin,
timer) typically needs an explicit "arm as wake source" call before the
sleep transition, and depending on the driver, that arming may need to
happen *every time* you enter the idle state (some drivers manage this for
you as part of a "power save" mode; others need it done by hand at the call
site that turns the screen/peripheral off).

**Gotcha**: a pin already has an interrupt handler for normal operation
doesn't mean it's wired up as a *wake* source — those are frequently
separate APIs. If a driver never installed its own interrupt handler on a
pin (e.g. it's polled instead of interrupt-driven during normal operation),
it's usually safe to repurpose that pin's interrupt configuration for wake
duty specifically during the idle state, since nothing else is contending
for it — but confirm that by reading the driver, not by assuming.

### 3. "Off" isn't just backlight/output = 0

Turning a visible output's brightness/duty to zero often leaves the
underlying controller chip fully powered and clocked — display GRAM,
driving circuitry, codec, etc. If the chip has a documented low-power/sleep
command, use it. If it doesn't (some low-cost display controllers only
support "display off," not true panel sleep), that's still worth doing —
it's better than nothing — but note in your tracking doc that it's a
partial win, not the ceiling.

### 4. CPU frequency scaling + deep sleep margins can cause silent hangs

Aggressive power settings (running RAM at reduced speed to save power,
powering down CPU/cache domains during light sleep) can interact badly with
tight timing margins elsewhere in the system — e.g. external RAM run at its
maximum rated speed has the least timing margin, and that margin gets spent
exactly during the internal cache-suspend/resume that a sleep transition
does. The failure mode is not a crash or a watchdog reset — it's a **silent
full hang**, including timers on independent hardware channels, which makes
it look like a hardware fault rather than a configuration issue.

**Diagnosis approach that worked**: bisect one power setting at a time
(does it hang with light-sleep-enable off? with CPU-domain power-down off?
with external RAM downclocked?) rather than assuming the newest change is
the cause — the actual cause here was memory clock speed margin, not the
CPU power-down setting that was the initial suspect. Keep a written trail
of what was ruled out; this kind of bug takes multiple sessions to nail
down and you will forget what you already tried.

### 5. Async/DMA work doesn't stop the instant you ask it to

"Pause" or "stop" on a rendering/transfer pipeline usually only guarantees
no *new* operation starts — an operation already in flight (DMA-driven,
completed later via a callback/interrupt) can still be running when the
pause call returns. If you let a low-power transition clock-gate or power
down the bus underneath that in-flight transfer, the peripheral can come
back in a corrupted or permanently wedged state after wake — and it may
manifest as "works fine after the first few sleep cycles, breaks on some
later one," which reads like a heisenbug if you don't know to look for
this.

**Fix pattern**: add an explicit "wait until truly idle" step after pause,
before touching power state, with a bounded timeout — and treat a timeout
here as "abort this idle transition and retry later," not as something to
push through.

### 6. Peripheral state does not survive light sleep — rebuild, don't assume

Chips with automatic light sleep frequently have no retention hardware for
DMA-driven peripheral state (display controllers, buses) across a real sleep
cycle, even though everything looks fine across a *simulated*/short sleep in
testing. Leaving such a peripheral "live" across a real sleep window can
leave it in a broken state on wake (blank output, or worse, wedging every
future attempt to pause/use it). The robust pattern is to fully detach/free
the peripheral before entering the sleep-eligible idle state, and rebuild it
fresh from scratch on wake, the same way it was built at boot — treat
"survived sleep" as never guaranteed rather than something to special-case
only after it breaks.

This adds real complexity (teardown/rebuild code, ordering constraints with
whatever framework owns the resource) — budget time for it; it was the
single most involved item in this project's list.

### 7. Background logic needs a timer that isn't paused with the UI

If you pause a UI framework's worker/render loop to save power (very
common and correct for saving power), any timer *owned by that framework*
typically stops too, because framework timers only run from inside the
framework's own pump/handler function. Business logic that must keep
running through the idle state — an alarm, a countdown, a scheduled task —
needs to live on a timer mechanism that's independent of the paused
component (e.g. the OS's own timer service), not the UI framework's timer
API, even though the UI framework's timer API is the more obvious/idiomatic
choice when you first write the feature. This is easy to miss because it
works perfectly in every manual test where the screen happens to stay on.

### 8. Reference-count shared "on when needed" resources

Peripherals used intermittently by multiple call sites (a radio used for
both a settings sync and a background check-in; an audio codec used by
alarms, timers, and UI sounds) should be opened/started on first use and
closed/stopped when the last user is done, via a simple acquire/release
reference count — not opened once at boot and left running for the process
lifetime "since something might need it eventually." Boot-time
initialization of such a resource is one of the easiest wins available and
one of the easiest to overlook, since it doesn't require touching any
sleep/power API at all — it's just architecture.

### 9. Build a measurement harness on day one

None of the above is verifiable by reading code or by how it "feels" in
casual use — you need an actual current/time trace, and a way to correlate
it with what state the device was in. Two details from this project were
worth the small extra effort:

- **Log independent of whatever you're trying to measure.** A diagnostic
  timer that samples current/state should run on its own OS timer, not on
  the framework you've paused to save power — otherwise you can't observe
  behavior *during* the very state you're trying to measure, and a hang in
  the thing you're testing takes your instrumentation down with it (which
  is itself useful signal — "even the independent diagnostic timer stopped
  ticking" narrows a hang to below the OS, not just stuck application code).
- **Log somewhere you can retrieve fully disconnected from USB/debugger**,
  e.g. to an SD card or persisted flash region, not only to a serial
  console. Any debug/power connection can itself power the board or mask
  the exact current draw you're trying to measure, so a real measurement
  run needs the device to be genuinely standalone on battery.
- Avoid adding expensive diagnostic calls (anything that does blocking I/O,
  or that touches shared locks the very subsystem you're debugging also
  needs) to a periodic diagnostic hot path — it can itself become a source
  of contention or deadlock with the thing you're trying to observe.

## Day-1 setup checklist for a new project

Work through this before writing feature code, if battery life matters for
the product:

- [ ] Identify every periodic hardware timer that will run during idle
      (framework/library ticks, health checks, telemetry) and confirm each
      one either doesn't need to exist, can be made on-demand, or has a
      period you've deliberately chosen knowing it caps your sleep window.
- [ ] List every wake source the device needs (buttons, touch, alarms,
      external interrupts) and confirm each has an explicit "arm as wake
      source" call, not just a normal-operation interrupt handler.
- [ ] List every peripheral (display, audio, radios, sensors, storage) and
      decide, per peripheral: always-on (justify why), open/close around
      use, or ref-counted shared access. Boot-time "just in case" init is a
      smell — call it out explicitly if you choose it anyway.
- [ ] Confirm any timer-driven business logic that must survive the idle
      state runs on a mechanism independent of whatever gets paused/stopped
      to save power.
- [ ] Set up the measurement harness (independent periodic logger, written
      somewhere retrievable without USB) before starting to optimize.
- [ ] Add a real release/production build profile (log level, debug
      stats/profiling flags, compiler optimization) separate from the dev
      build, so measurements aren't contaminated by dev-only overhead.
- [ ] Start the project's own `BATTERY_OPTIMIZATIONS.md` tracking table
      (below) once you have a working prototype to measure.

## `BATTERY_OPTIMIZATIONS.md` tracking table template

```markdown
# Battery-Life Optimization Findings

Status column: `Not started` / `In progress` / `Done` / `Wontfix`.

| # | Priority | Issue | Battery impact | Difficulty | Affected files | Recommended change | Side effects | Status |
|---|---|---|---|---|---|---|---|---|
| 1 | | | | | | | | Not started |
```

Keep the "Side effects" column honest — most of the fixes above have a real
tradeoff (rebuild latency on wake, a brief visual glitch, slightly less
"live" telemetry while idle) and it's worth writing those down at the time
you make the tradeoff, not reconstructing them later from a diff.
