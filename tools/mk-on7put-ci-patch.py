#!/usr/bin/env python3
import glob
import os
import subprocess
import sys
import tempfile


def main():
    if len(sys.argv) != 4:
        sys.exit("usage: mk-on7put-ci-patch.py GPU_USERLAND_DIR TAG OUT.patch")
    gpu, tag, out = sys.argv[1:]
    here = os.path.dirname(os.path.abspath(__file__))
    libs = sorted(glob.glob(os.path.join(gpu, "lib", "*.dylib")))
    on7gl = os.path.join(gpu, "bin", "libon7gl.dylib")
    if not os.path.isfile(on7gl):
        sys.exit("missing " + on7gl)
    if not any(os.path.basename(p) == "libOSMesa.8.dylib" for p in libs):
        sys.exit("missing libOSMesa.8.dylib in " + os.path.join(gpu, "lib"))
    specs = ["/usr/local/bin/libon7gl.dylib=%s,0755,z" % on7gl]
    for p in libs:
        specs.append("/usr/local/bin/%s=%s,0755,z" % (os.path.basename(p), p))
    with tempfile.TemporaryDirectory() as tmp:
        hdr = os.path.join(tmp, "on7put_payload.h")
        subprocess.check_call([sys.executable, os.path.join(here, "on7put-payload.py"), hdr, tag] + specs)
        with open(hdr, newline="") as f:
            body = f.read().split("\n")
    if body and body[-1] == "":
        body.pop()
    with open(out, "w", newline="\n") as f:
        f.write("diff --git a/bsd/kern/on7put_payload.h b/bsd/kern/on7put_payload.h\n")
        f.write("new file mode 100644\n")
        f.write("--- /dev/null\n")
        f.write("+++ b/bsd/kern/on7put_payload.h\n")
        f.write("@@ -0,0 +1,%d @@\n" % len(body))
        for line in body:
            f.write("+" + line + "\n")
    print("%s: %d header lines, %d bytes" % (out, len(body), os.path.getsize(out)))


if __name__ == "__main__":
    main()
