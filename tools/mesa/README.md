# Mesa for the J7 Prime (panfrost on HakiosMali)

`mk-mesa-ios.sh` builds Mesa 25.0.7's OSMesa (OpenGL ES 2, off-screen) for iOS arm64 with the
gallium drivers panfrost (Midgard v5: the Mali-T830, `MODEL(0x830, ...)` in pan_props.c) and
softpipe. CI: `.github/workflows/mesa.yml` (artifact `gpu-userland`: `lib/libOSMesa.8.dylib`,
`lib/libglapi.0.dylib`, `bin/gl_test`, `bin/mali_test`; install names `/usr/local/lib/...`).

panfrost talks to the kernel's `HakiosMali` (xnu patch 0193) through `hakios-drm/`, a stand-in
for libdrm: a DRM ioctl's low byte is the IOKit selector, buffers are client memory of type =
handle, SUBMIT answers the job number in `jc`, 0x7d waits for a number.

Taken from the Samsung Galaxy A50 port (HakiOS, `ios-a50-exynos9610/core/tools/mesa`, MIT
License, Copyright (c) 2025-2026 the HakiOS authors), unchanged:

- `hakios-drm/hakios.drm.c`, `hakios-drm/xf86drm.h`
- `freedreno-darwin.patch` (needed: it adds the Darwin libdrm stand-in and the GPU path in
  osmesa_target.c that `panfrost-darwin.patch` extends; freedreno itself is not built)
- `panfrost-darwin.patch`, `panfrost-pack-pointer.patch`, `osmesa-es-profile.patch`

Ours (J7): `panfrost-tiler-heap.patch` -- the 128 MB tiler heap is wired in full on Darwin
(no growable buffers), so it is 32 MB here (`PAN_TILER_HEAP_MB`, 4..128).

Mesa itself is MIT licensed (its own notices); it is downloaded at build time, nothing of it is
in this repository.

The MIT License of the A50 port's files:

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
