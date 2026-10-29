# libvterm for Mika

This fork starts from the Neovim-maintained libvterm 0.3.3 source snapshot
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

This is **not yet a fully freestanding libvterm**: the core still has the
default `malloc/free` adapter, stdio diagnostics, and `abort/exit` paths.
The utilities can already be compiled independently without libc dependencies.

## Build and verify

With GNU libtool installed (`glibtool` on macOS):

```sh
make all test
```

Without GNU libtool, run the same 43 terminal test files plus utility and
output-buffer regression tests with the host C compiler and Perl:

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
GitHub Actions runs the suite with GCC/Clang, with and without ASan/UBSan,
and checks that the freestanding utilities object has no undefined symbols.
Integration tests also verify selection-buffer ownership: internally allocated
buffers are released on replacement/destruction, borrowed buffers are retained,
and allocation failure preserves the previous configuration.
