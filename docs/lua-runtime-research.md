# Lua runtime research

Which Lua virtual machines exist (November 2026), what they cost in speed, and what each would mean
for EAWEngine's script host (`src/script`, [compatibility-research.md](compatibility-research.md#lua-host-protocol)).
The host currently embeds Lua 5.0.3, the version the original's scripts are written for.

## The candidates

| Runtime | Language | Relative speed | Fit for the script host |
| --- | --- | --- | --- |
| PUC Lua 5.0.3 (current) | Lua 5.0, exactly what the scripts target | the oldest official line; no recent benchmark measures it | exact semantics; count hooks fire everywhere, so the instruction budget always holds |
| PUC Lua 5.4.x / 5.5 | Lua 5.4 / 5.5 (5.5 released December 2025) | 5.4.7 is about 1.8x faster than 5.3.0 and about 7x slower than LuaJIT on the benchmark below | needs a 5.0 compatibility layer |
| LuaJIT 2.1 | Lua 5.1 | the fastest; about 7x faster than Lua 5.4.7 | debug hooks do not fire in JIT-compiled code |
| Luau (Roblox) | derived from Lua 5.1, with gradual types | interpreter somewhat faster than Lua 5.4 and on par with LuaJIT's interpreter on some workloads; optional native code generation | interrupt callback for budgets; built-in sandboxing |

Ports to other runtimes (MoonSharp and NLua for .NET, GopherLua for Go, Fengari for JavaScript)
are slower than the C implementations and do not fit a C++ engine; they are not considered.

## Measurements and how far they can be trusted

- **The Green Side of the Lua** (Brandão, Matos, Guimarães, Cunha, Saraiva; arXiv:2601.16670,
  January 2026) is the only rigorous recent comparison found. It measured 17 official interpreters
  (5.3.0 through 5.4.8 and 5.5.0) and the LuaJIT releases 2.0.0 through 2.1.1 on seven programs of
  the LuaAOT suite (from the Computer Language Benchmarks Game: binary-trees, fannkuch-redux,
  fasta, k-nucleotide, mandelbrot, n-body, spectral-norm), ten runs each, on one laptop (Intel
  i7-8550U, Ubuntu 24.04). Runtime relative to LuaJIT 2.0.4 (Table I): LuaJIT 2.1.1 1.006, Lua
  5.4.7 6.978, Lua 5.4.8 7.083, Lua 5.4.4 7.807, Lua 5.3.4 10.842, Lua 5.3.0 12.531. Energy follows
  runtime. **Lua 5.0, 5.1, and 5.2 are not in the study**; no published measurement of 5.0 against
  current versions was found.
- **Luau** figures come from Luau's own performance page (the interpreter is faster than Lua 5.x,
  including 5.4, and can match LuaJIT's interpreter on some workloads) and from community reports
  (one informal comment puts Luau with native code about 1.6x slower than LuaJIT 2.1 with its JIT).
  Neither is an independent benchmark.
- **Lua 5.5**: its release notes and press coverage stress memory and garbage collection (compact
  arrays, reported about 60% smaller for large tables; a generational mode; incremental major
  collections) and global declarations, not raw speed. No independent speed benchmark was found.

## What the script host needs from a runtime

1. **Speed is not the constraint at the original's scale.** `EAWEngine lua-survey` ran all 370
   stock scripts for 300 ticks each in 2.2 s (Release build, loading included): at most about
   20 microseconds per script per tick, on Lua 5.0 with recording stubs. The framework services
   scripts five times a second (`ServiceRate = 0.2`). Even hundreds of active scripts would cost a
   few percent of one core; real gameplay time will go into the engine functions the scripts call,
   not the VM. This is an estimate from stub runs; per-resume timing in the survey would measure it.
2. **Compatibility is the cost of switching.** The stock library is Lua 5.0: it uses the implicit
   `arg` table for varargs and `unpack` as a global, alongside `table.getn` and `string.gfind`. Lua
   5.1 keeps `arg`, `table.getn`, and `string.gfind` only behind compatibility switches; 5.2 and
   later remove `setfenv`, `loadstring`, and global `unpack` (among others); LuaJIT and Luau follow
   5.1 and drop or change parts of it. Any other runtime needs a shim layer, and the survey must
   show the 370 scripts behave the same on it.
3. **The sandbox relies on an instruction budget.** LuaJIT does not call debug hooks from
   compiled code (confirmed by its author in 2011; Tarantool's tests and the Garry's Mod
   documentation say the same), so a runaway loop escapes a count-hook budget unless the JIT is
   turned off, which gives up most of its speed. Luau has an interrupt callback for exactly this;
   PUC Lua's hooks fire everywhere.
4. **Error handling must fit the no-exceptions rule.** PUC Lua built as C uses `setjmp`/`longjmp`.
   Luau is C++ and raises errors with C++ exceptions by default; it has a `longjmp` build option,
   which would be required here (not yet verified against current Luau sources).
5. **Bytecode.** Lua 5.0 does not verify bytecode, so the host refuses compiled chunks. Any runtime
   that loads bytecode from mods needs the same rule or a verifier.

## Conclusion

Keep Lua 5.0.3 for the original's and mods' scripts: it runs them exactly as they were written,
its hooks make the instruction budget reliable, and the measured cost is small. If performance
becomes a measured problem, or for scripts written for EAWEngine itself, the candidates are Luau
(the strongest sandbox and good speed, if its `longjmp` build holds up) and Lua 5.4/5.5 (modern and
simple to embed), as an opt-in runtime chosen per script beside the 5.0 path, in the spirit of the
legacy and enhanced fidelity profiles ([architecture.md](architecture.md#content-contract-versus-legacy-implementation)),
not as a replacement. LuaJIT's speed comes with a sandbox gap that rules it out for untrusted mod
scripts.

Open: measure the host's per-resume cost in the survey, and check Luau's `longjmp` build and its
interrupt callback against current sources before any prototype.

## Sources

- [The Green Side of the Lua (arXiv 2601.16670)](https://arxiv.org/html/2601.16670v2)
- [How we make Luau fast](https://luau.org/performance/)
- [Luau native code generation](https://create.roblox.com/docs/luau/native-code-gen.md)
- [Luau performance benchmarks against LuaJIT (BigGo)](https://biggo.com/news/202510261935_luau-performance-benchmarks-luajit-comparison)
- [Luau's performance (Hacker News discussion)](https://nextjs-hn-feed.vercel.app/post/45682560)
- [Lua 5.5 released (Phoronix)](https://www.phoronix.com/news/Lua-5.5-Released)
- [Lua 5.5 compact arrays and garbage collection (Linuxiac)](https://linuxiac.com/lua-5-5-released-with-incremental-garbage-collection-and-compact-arrays/)
- [Fedora change proposal: Lua 5.5](https://fedoraproject.org/wiki/Changes/Lua5.5)
- [Lua in 2026: three dialects (blog)](https://www.birjob.com/blog/lua-5-5-luajit-luau-dialect-split-2026)
- [lua-l: LuaJIT debug hooks are not called from compiled code (2011)](https://lua-users.org/lists/lua-l/2011-02/msg01106.html)
- [Garry's Mod wiki: debug.sethook under LuaJIT](https://wiki.facepunch.com/gmod/debug.sethook)
- [Tarantool: count-hook test disables the JIT](https://lists.tarantool.org/pipermail/tarantool-patches/2021-March/023025.html)
- [Luau interrupt callback (rs-luau bindings)](https://docs.rs/rs-luau/latest/rs_luau/ffi/luau/struct.LuaCallbacks.html)
- [pluau: interrupt-based timeouts](https://github.com/gluau/pluau)
