# libvterm for Mika

Version 0.4.0 of this fork starts from the Neovim-maintained libvterm 0.3.3 source snapshot
and prepares its terminal core for use in the Mika kernel and user-space
terminals. Original libvterm: https://www.leonerd.org.uk/code/libvterm/.
Neovim now maintains its copy in
https://github.com/neovim/neovim/tree/master/src/nvim/vterm.
The original MIT license is retained in `LICENSE`.

## Internal utilities

`src/lib/utils.h` and `src/lib/utils.c` provide `vt_memcpy`, `vt_memmove`,
`vt_memset`, `vt_strncpy`, `vt_strncmp`, `vt_abs`, `vt_snprintf` and
`vt_vsnprintf`. The terminal core explicitly calls these private functions;
they do not override the host's libc symbols. Memory and string functions
follow their C library contracts, including overlap support for `vt_memmove`,
NUL padding for `vt_strncpy`, and unsigned-byte comparison for `vt_strncmp`.
Only the sign of the `vt_strncmp` result is part of the comparison contract.
As with standard `abs`, `vt_abs(INT_MIN)` is outside the defined domain.

Formatting implements the byte-oriented C-locale subset used by libvterm:

- Conversions: `d`, `i`, `u`, `o`, `x`, `X`, `c`, `s`, `%%`.
- Integer lengths: `hh`, `h`, `l`, `ll`, `j`, `z`, `t` (ILP32/LP64/LLP64).
- Applicable flags: `-`, `+`, space, `#`, `0`; literal or `*` width/precision.
- The return value counts the bytes that would have been written, excluding
  the terminating NUL. Nonempty buffers are NUL-terminated even on truncation;
  size zero permits a NULL output pointer. Embedded NUL characters count.
- Negative dynamic width means left alignment; negative dynamic precision
  means omitted precision. The caller's `va_list` is not consumed.
- Unsupported/invalid formats or results exceeding `INT_MAX` return `-1`
  without using `errno`; a nonempty buffer still contains a terminated prefix.

Floating point, wide characters, `%p`, `%n`, positional arguments, and locale
extensions are intentionally unsupported. There is no allocation or floating
point arithmetic in the utilities. A truncated terminal response is discarded
instead of reading beyond its formatting buffer or sending an incomplete reply.

## Freestanding API (breaking revision 1)

The entire core uses compiler-provided C headers and injected host services.
There is no default allocator, logger, or fatal handler, in either hosted or
freestanding builds. `vterm_new` and `vterm_new_with_allocator` are removed.
All clients must rebuild against this header; the shared-library ABI is bumped.

```c
VTermAllocatorFunctions allocator = { my_alloc, my_free };
struct VTermBuilder builder = {
    .ver = VTERM_API_VERSION,
    .rows = 25, .cols = 80,
    .allocator = &allocator,
    .allocdata = my_heap,
    .fatal = my_fatal,       /* required, must not return */
    .log = my_log,           /* optional */
    .userdata = my_context,
};
VTerm *vt = NULL;
VTermStatus status = vterm_build(&builder, &vt);
```

The allocator function table is copied; its context must outlive the instance.
Allocation must return suitably aligned storage or NULL. The core zeroes every
allocation itself. Free must not fail; the core filters NULL before calling it.
An arena with no-op free remains usable, but reclaiming arena capacity after a
failed operation is the host's responsibility. `outbuffer_len` must be at least
`tmpbuffer_len`; either zero field requests its 4096-byte default.

Diagnostics use a bounded 256-byte local buffer and never allocate or reuse
the decoder's working buffer. Log callbacks receive an actual byte count and
may receive truncated messages. Fatal callbacks receive the formatted reason;
if a fatal callback returns, the core executes a compiler trap. Routine OOM,
invalid arguments, capacity limits and version mismatches return `VTermStatus`
and never call fatal. Unknown terminal controls remain ordinary unhandled input.

Creation and state/screen acquisition return status plus an output pointer.
Operations that can allocate, invoke host callbacks or produce output now
return status directly; there are no legacy status-discarding wrappers.
Non-failing getters and destruction retain their simple signatures.
Result-bearing callbacks return `VTermCallbackResult`, separating `status` from
`handled` (or row availability for `sb_popline`). Text callbacks return status
and write their accepted byte count through `consumed`. Callback/user pointers
are borrowed, synchronous, and must not reenter the same instance. Serialize
access to each instance in the host; different instances have separate state.

### Errors, progress and recovery

- Construction failure clears the output pointer and releases partial objects.
  Failed lazy screen creation preserves pre-existing parser/state callbacks.
- Allocation/validation failures during resize leave dimensions and cells
  unchanged. All required internal allocations are prepared before scrollback
  or display callbacks. Once host callbacks begin, their external side effects
  cannot be rolled back: a callback error may be reported after the resize has
  committed a valid new size. Query the size rather than assuming rollback for
  host errors. Custom state-resize callbacks must leave old buffers/fields
  untouched when rejecting a resize.
- Combining-buffer growth restores the decoder on OOM, so unaccepted bytes can
  be retried. Selection-buffer replacement preserves the old configuration on
  allocation failure. Borrowed selection buffers are never freed.
- `vterm_input_write(vt, bytes, length, &consumed)` reports the accepted prefix
  on both success and failure. This includes bytes saved in incomplete UTF-8
  or escape sequences. A host rendering error can occur after model updates;
  already consumed input must not be replayed. A failed damage notification is
  retained and can be retried with `vterm_screen_flush_damage`.
- Output callbacks must accept a whole offered byte slice or reject it without
  consuming any bytes. Rejected bytes stay queued, including replies generated
  from already consumed input. Retry `vterm_output_flush`, or explicitly drain
  the queue with `vterm_output_read`; do not replay the originating key/input.
  Input stops before accepting another event if the queue lacks room for a
  bounded reply. A formatting buffer too small for a protocol sequence reports
  `VTERM_ERR_NO_SPACE`; increase the configured capacity when constructing it.
- Log/fatal messages and callback slices are borrowed only for the callback's
  duration. Log handling must not affect terminal output or allocate via the
  failing terminal allocator.

Hosted adapters live in `support/host.h` for tests and command-line examples,
not in the library. A kernel can supply compiler-emitted memory primitives
using `vt_memcpy`/`vt_memmove`/`vt_memset`; `t/freestanding.c` demonstrates this
compiler ABI glue separately from the core. It links without libc or startup
objects. Linux x86_64 CI executes this smoke test using a fixture-only exit
syscall; this does not constitute Mika kernel/QEMU runtime validation.

Build a static core archive with an appropriate target compiler and flags:

```sh
make freestanding CC='zig cc -target x86_64-freestanding-none' AR='zig ar' \
  FREESTANDING_CFLAGS='-O2 -ffreestanding -fno-stack-protector -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -msoft-float -mcmodel=kernel -fno-pic -fno-pie'
```

## Build and verify

With GNU libtool installed (`glibtool` on macOS):

```sh
make all test
```

Without GNU libtool, run the same 43 terminal test files plus utility,
output-buffer, allocation-failure and fatal regression tests with the host C
compiler and Perl:

```sh
make test-native
```

To force a sanitizer rebuild:

```sh
make -B test-native CFLAGS='-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer' LDFLAGS='-fsanitize=address,undefined'
```

Utility tests compare bytes, return values and comparison signs with host libc
across buffer sizes, flags, widths, precisions and integer boundaries. The
`INT_MIN` dynamic-precision case uses the C-specified result directly because
Darwin libc mishandles that boundary. See C11 draft N1570, section 7.21.6.1:
https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf.
GitHub Actions runs shared-library builds and the suite with GCC/Clang, then
runs the same suite through `test-native` with ASan/UBSan to keep sanitizer
linkage within one compiler toolchain. It also checks that the freestanding
utilities object has no undefined symbols.
Integration tests also verify selection-buffer ownership: internally allocated
buffers are released on replacement/destruction, borrowed buffers are retained,
and allocation failure preserves the previous configuration.
The error suite injects failure at each constructor/resize allocation, checks
partial-input recovery, output retry, log-buffer isolation, parser limits, and
uses child processes to verify fatal delivery and the returning-fatal trap.
