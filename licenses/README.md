# Vendored licence texts

Nothing here is code. These are licence texts that belong in the binary's
third-party notices but that vcpkg's own `share/<port>/copyright` does not
carry, so `cmake/ThirdPartyLicenses.cmake` reads them from here instead.

A port lands in this directory only when its vcpkg copyright file is a
*pointer* rather than a *text*. That is the whole admission criterion, and the
build checks it: if the port's copyright stops being a pointer, or its version
moves away from the one the vendored text was taken from, the configure step
fails and says so rather than shipping a text that no longer corresponds to
what was linked.

| File | Port | Version | Why it is here |
|---|---|---|---|
| `pcre2-LICENCE.md` | pcre2 | 10.47 | vcpkg installs PCRE2's `COPYING`, which is four lines saying "please see the file LICENCE in the PCRE2 distribution". The licence is in `LICENCE.md`, taken here verbatim from the `pcre2-10.47` tag. |
