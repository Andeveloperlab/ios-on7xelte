# libon7gl.dylib: CoreAnimation on the Mali-T830 (EAGLContext on Mesa)

CoreAnimation's render server (backboardd) draws with Metal, else OpenGL ES (EAGLContext), else
its own CPU rasterizer. The J7 has no Metal device and no Apple GLES driver, so today it draws on
the CPU. This library, inserted into backboardd, replaces the EAGLContext methods CoreAnimation
uses and hands its OpenGL ES calls to Mesa 25.0.7 panfrost (`tools/mesa`, `/usr/local/bin/
libOSMesa.8.dylib`), i.e. to the GPU through HakiosMali.

How it gets in (xnu patch 0196, only with boot-arg `on7gl=1`): the kernel starts backboardd with
`DYLD_INSERT_LIBRARIES=/usr/local/bin/libon7gl.dylib` and answers backboardd's dyld policy
request (AMFI 0x5a) with "allow all". Its lines go to the kernel log through `kern.on7gl_log`
(`ON7 GL> ...` in the USB log). Built by `.github/workflows/mesa.yml` (artifact `gpu-userland`,
`bin/libon7gl.dylib`).

The J7's HakiosMali has no IOSurface import (request 0x7e) yet, so the IOSurfaces must be
copied: boot with `glcopy=1` (textures are filled from the surfaces and read back into them with
OpenGL ES calls). Zero-copy needs 0x7e in the kernel.

Taken from the Samsung Galaxy A50 port (HakiOS, `ios-a50-exynos9610/core/userland`, MIT License,
Copyright (c) 2025-2026 the HakiOS authors):

- `eagl.c` -- changed: every `write(2, ...)` goes to `on7_write()` (kern.on7gl_log) instead
- `gl_osmesa.c`, `hakios.gl.h`, `surface_checksum.h`, `eagl_slots.txt` (the OpenGLES function
  table of iOS 16.5 20F66; [G] assumed the same on this root), `mkeagl.py` -- unchanged

Ours: `surface_checksum.c` (a plain multiply-xor hash; the A50 uses XXH3, not carried over).

The MIT License of the A50 port's files:

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
