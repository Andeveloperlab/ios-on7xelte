/*
 * gl_test: the first OpenGL ES drawing on the J7 Prime's Mali-T830, through Mesa's panfrost (tools/mesa,
 * libOSMesa.8.dylib in /usr/local/lib) and the kernel's HakiosMali (xnu patch 0193). After the a50's
 * core/userland/src/gl_test.c, cut down to what a first boot has to show, and like tools/mali_test it says
 * everything in the kernel log ("USB> on7wifid: gl: ...", kern.on7xelte_wifi "log TEXT", root only, 80-byte
 * pieces), because under launchd its output goes nowhere. Mesa's own messages (stderr) go there too ("gl!").
 *
 *   1  an OpenGL ES 2 context (OSMesa, OSMESA_ES_PROFILE); GL_RENDERER says which driver took it
 *      (panfrost: the GPU; softpipe: the processor -- HakiosMali missing, or OSMESA_SOFTWARE=1)
 *   2  a 64 x 64 texture as the framebuffer, cleared red, read back
 *   3  one triangle (red, green, blue corners) over a dark blue clear, read back: a few pixels
 *   4  ROUNDS more clear + triangle + glFinish (default 100) for the time a frame takes
 *   5  full-screen fill timing (the a50's "gl_test fill"): a WIDTH x HEIGHT (default the panel, 1080 x 1920)
 *      texture as the framebuffer; a clear alone, one small rectangle, a clear + 1/2/4/8 full-screen
 *      rectangles, the same textured + blended; 16 frames of each (glFinish each), shortest/longest/mean
 *
 *   gl_test [ROUNDS [WIDTH HEIGHT]]   (WIDTH 0: no step 5; tools/mali_test runs it with no arguments)
 */
#include <errno.h>
#include <fcntl.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>

/* GL/osmesa.h and GLES2/gl2.h, as far as they are used here */
typedef struct osmesa_context *OSMesaContext;
extern OSMesaContext OSMesaCreateContextAttribs(const int *attributes, OSMesaContext share);
extern unsigned char OSMesaMakeCurrent(OSMesaContext context, void *buffer, unsigned type, int width, int height);
extern void OSMesaDestroyContext(OSMesaContext context);
extern const unsigned char *glGetString(unsigned name);
extern unsigned glGetError(void);
extern void glGenTextures(int n, unsigned *names);
extern void glBindTexture(unsigned target, unsigned texture);
extern void glTexImage2D(unsigned target, int level, int internal, int width, int height, int border, unsigned format,
    unsigned type, const void *pixels);
extern void glTexParameteri(unsigned target, unsigned name, int value);
extern void glGenFramebuffers(int n, unsigned *names);
extern void glBindFramebuffer(unsigned target, unsigned framebuffer);
extern void glFramebufferTexture2D(unsigned target, unsigned attachment, unsigned textarget, unsigned texture, int level);
extern unsigned glCheckFramebufferStatus(unsigned target);
extern void glViewport(int x, int y, int width, int height);
extern void glClearColor(float red, float green, float blue, float alpha);
extern void glClear(unsigned mask);
extern unsigned glCreateShader(unsigned type);
extern void glShaderSource(unsigned shader, int count, const char *const *strings, const int *lengths);
extern void glCompileShader(unsigned shader);
extern void glGetShaderiv(unsigned shader, unsigned name, int *value);
extern void glGetShaderInfoLog(unsigned shader, int size, int *length, char *log);
extern unsigned glCreateProgram(void);
extern void glAttachShader(unsigned program, unsigned shader);
extern void glBindAttribLocation(unsigned program, unsigned index, const char *name);
extern void glLinkProgram(unsigned program);
extern void glGetProgramiv(unsigned program, unsigned name, int *value);
extern void glUseProgram(unsigned program);
extern void glVertexAttribPointer(unsigned index, int size, unsigned type, unsigned char normalized, int stride,
    const void *pointer);
extern void glEnableVertexAttribArray(unsigned index);
extern void glDisableVertexAttribArray(unsigned index);
extern void glDrawArrays(unsigned mode, int first, int count);
extern void glFinish(void);
extern void glReadPixels(int x, int y, int width, int height, unsigned format, unsigned type, void *pixels);
extern void glEnable(unsigned cap);
extern void glDisable(unsigned cap);
extern void glBlendFunc(unsigned sfactor, unsigned dfactor);
extern int glGetUniformLocation(unsigned program, const char *name);
extern void glUniform1i(int location, int value);
extern void glUniform4f(int location, float x, float y, float z, float w);

#define OSMESA_RGBA                     0x1908
#define OSMESA_FORMAT                   0x22
#define OSMESA_PROFILE                  0x33
#define OSMESA_CONTEXT_MAJOR_VERSION    0x36
#define OSMESA_ES_PROFILE               0x38
#define GL_ONE                          1
#define GL_TRIANGLES                    0x0004
#define GL_TRIANGLE_STRIP               0x0005
#define GL_ONE_MINUS_SRC_ALPHA          0x0303
#define GL_BLEND                        0x0be2
#define GL_TEXTURE_2D                   0x0de1
#define GL_UNSIGNED_BYTE                0x1401
#define GL_FLOAT                        0x1406
#define GL_RGBA                         0x1908
#define GL_VENDOR                       0x1f00
#define GL_RENDERER                     0x1f01
#define GL_VERSION                      0x1f02
#define GL_NEAREST                      0x2600
#define GL_LINEAR                       0x2601
#define GL_TEXTURE_MAG_FILTER           0x2800
#define GL_TEXTURE_MIN_FILTER           0x2801
#define GL_COLOR_BUFFER_BIT             0x4000
#define GL_FRAGMENT_SHADER              0x8b30
#define GL_VERTEX_SHADER                0x8b31
#define GL_COMPILE_STATUS               0x8b81
#define GL_LINK_STATUS                  0x8b82
#define GL_FRAMEBUFFER_COMPLETE         0x8cd5
#define GL_COLOR_ATTACHMENT0            0x8ce0
#define GL_FRAMEBUFFER                  0x8d40

#define SIZE 64

static void
say_as(const char *tag, const char *line)
{
	char msg[96];

	printf("gl_test: %s\n", line);
	fflush(stdout);
	/* the kernel takes at most 95 bytes per write (on7xelte_wifi.c buf[96]): 80-byte pieces, "+" continues */
	for (size_t at = 0, len = strlen(line); at < len || at == 0; at += 80) {
		snprintf(msg, sizeof(msg), "log %s%s %.80s", tag, at ? "+" : ":", line + at);
		(void)sysctlbyname("kern.on7xelte_wifi", NULL, NULL, msg, strlen(msg) + 1);
		if (len == 0) {
			break;
		}
	}
}

static void
say(const char *fmt, ...)
{
	char line[240];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	say_as("gl", line);
}

/* Mesa's messages (stderr): each line into the kernel log as "gl!" */
static int err_pipe[2] = { -1, -1 };

static void *
forward_stderr(void *unused)
{
	char buf[512], line[240];
	size_t n = 0;
	ssize_t got;

	(void)unused;
	while ((got = read(err_pipe[0], buf, sizeof(buf))) > 0) {
		for (ssize_t i = 0; i < got; i++) {
			if (buf[i] == '\n' || n == sizeof(line) - 1) {
				line[n] = 0;
				if (n != 0) {
					say_as("gl!", line);
				}
				n = 0;
				if (buf[i] == '\n') {
					continue;
				}
			}
			line[n++] = buf[i];
		}
	}
	return NULL;
}

static void
capture_stderr(void)
{
	pthread_t t;

	if (pipe(err_pipe) == 0 && dup2(err_pipe[1], 2) >= 0) {
		setvbuf(stderr, NULL, _IONBF, 0);
		pthread_create(&t, NULL, forward_stderr, NULL);
		pthread_detach(t);
	}
}

static void
flush_stderr(void)
{
	/* give the forwarding thread a moment to drain what Mesa wrote */
	usleep(200000);
}

static void
on_alarm(int sig)
{
	(void)sig;
	say("TIMEOUT: no answer in time (GPU hang?) -> FAILED");
	flush_stderr();
	_exit(3);
}

static double
ms_since(uint64_t then)
{
	static mach_timebase_info_data_t tb;

	if (tb.denom == 0) {
		mach_timebase_info(&tb);
	}
	return (double)(mach_absolute_time() - then) * tb.numer / tb.denom / 1e6;
}

static unsigned
shader(unsigned type, const char *text)
{
	unsigned name = glCreateShader(type);
	int good = 0;
	char log[160] = "";

	glShaderSource(name, 1, &text, NULL);
	glCompileShader(name);
	glGetShaderiv(name, GL_COMPILE_STATUS, &good);
	if (!good) {
		glGetShaderInfoLog(name, sizeof(log), NULL, log);
		say("%s shader does not compile: %s", type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
	}
	return name;
}

static uint32_t
pixel(const uint8_t *p, int x, int y)
{
	const uint8_t *q = p + 4 * (y * SIZE + x);

	return (uint32_t)q[0] << 24 | (uint32_t)q[1] << 16 | (uint32_t)q[2] << 8 | q[3];
}

/* 5: full-screen fill timing, after the a50's fill(); 0 = it went */
static int
fill(int width, int height)
{
	static const float all[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
	static const float some[] = { -0.1f, -0.1f, 0.1f, -0.1f, -0.1f, 0.1f, 0.1f, 0.1f };
	static const struct {
		int clear, layers, textured, small;
		const char *what;
	} kinds[] = {
		{ 1, 0, 0, 0, "clear only" },
		{ 0, 1, 0, 1, "one small rectangle, no clear" },
		{ 1, 1, 0, 0, "clear + 1 rectangle" },
		{ 1, 2, 0, 0, "clear + 2 rectangles" },
		{ 1, 4, 0, 0, "clear + 4 rectangles" },
		{ 1, 8, 0, 0, "clear + 8 rectangles" },
		{ 1, 1, 1, 0, "clear + 1 textured blended" },
		{ 1, 2, 1, 0, "clear + 2 textured blended" },
		{ 1, 4, 1, 0, "clear + 4 textured blended" },
		{ 1, 8, 1, 0, "clear + 8 textured blended" },
	};
	unsigned picture = 0, target = 0, framebuffer = 0, program, error;
	uint32_t *texels = malloc((size_t)width * height * 4), mid = 0;
	int good = 0, textured, colour, failed = 0;
	uint64_t t0;

	if (texels == NULL) {
		say("fill: no memory for %d x %d -> FAILED", width, height);
		return 1;
	}
	for (size_t i = 0; i < (size_t)width * height; i++) {
		texels[i] = 0x80404040u + ((uint32_t)i * 2654435761u >> 28);
	}
	t0 = mach_absolute_time();
	glGenTextures(1, &picture);
	glBindTexture(GL_TEXTURE_2D, picture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
	free(texels);
	glGenTextures(1, &target);
	glBindTexture(GL_TEXTURE_2D, target);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		say("fill: %d x %d framebuffer incomplete -> FAILED", width, height);
		return 1;
	}
	glViewport(0, 0, width, height);
	glFinish();
	say("fill %d x %d: textures made (one uploaded, %.1f MB) in %.1f ms, error %#x", width, height,
	    width * height * 4 / 1e6, ms_since(t0), glGetError());

	program = glCreateProgram();
	glAttachShader(program, shader(GL_VERTEX_SHADER,
	    "attribute vec2 where; varying vec2 at;\n"
	    "void main() { gl_Position = vec4(where, 0.0, 1.0); at = where * 0.5 + 0.5; }\n"));
	glAttachShader(program, shader(GL_FRAGMENT_SHADER,
	    "precision mediump float; varying vec2 at; uniform sampler2D picture; uniform int textured;"
	    " uniform vec4 colour;\n"
	    "void main() { gl_FragColor = textured != 0 ? texture2D(picture, at) * colour : colour; }\n"));
	glBindAttribLocation(program, 0, "where");
	glLinkProgram(program);
	glGetProgramiv(program, GL_LINK_STATUS, &good);
	if (!good) {
		say("fill: program does not link -> FAILED");
		return 1;
	}
	glUseProgram(program);
	textured = glGetUniformLocation(program, "textured");
	colour = glGetUniformLocation(program, "colour");
	glUniform1i(glGetUniformLocation(program, "picture"), 0);
	glBindTexture(GL_TEXTURE_2D, picture);
	glEnableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
	glClearColor(0.1f, 0.1f, 0.1f, 1);

	for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); k++) {
		double least = 1e9, most = 0, sum = 0;

		glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, kinds[k].small ? some : all);
		glUniform1i(textured, kinds[k].textured);
		if (kinds[k].textured) {
			glEnable(GL_BLEND);
			glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		} else {
			glDisable(GL_BLEND);
		}
		for (int frame = 0; frame < 16; frame++) {
			double ms;

			t0 = mach_absolute_time();
			if (kinds[k].clear) {
				glClear(GL_COLOR_BUFFER_BIT);
			}
			for (int layer = 0; layer < kinds[k].layers; layer++) {
				glUniform4f(colour, 0.2f + 0.1f * layer, 0.5f, 0.3f, 0.5f);
				glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
			}
			glFinish();
			ms = ms_since(t0);
			sum += ms;
			least = ms < least ? ms : least;
			most = ms > most ? ms : most;
		}
		error = glGetError();
		say("  %-29s %7.2f ms shortest %7.2f longest %7.2f mean, error %#x", kinds[k].what, least, most,
		    sum / 16, error);
		if (error != 0) {
			failed = 1;
		}
	}
	glDisable(GL_BLEND);
	/* the last frame: 8 textured layers over the clear; the pixel in the middle, read back */
	glReadPixels(width / 2, height / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, &mid);
	say("fill: middle pixel bytes %08x (r g b a, little-endian word), error %#x", mid, glGetError());
	if (mid == 0 || mid == 0xff1a1a1au) {
		failed = 1;     /* nothing drawn over the clear colour */
	}
	return failed;
}

int
main(int argc, char **argv)
{
	static const int attributes[] = { OSMESA_FORMAT, OSMESA_RGBA, OSMESA_PROFILE, OSMESA_ES_PROFILE,
		                          OSMESA_CONTEXT_MAJOR_VERSION, 2, 0 };
	static const float corners[] = {
		/* where, and what colour */
		-0.8f, -0.8f, 1, 0, 0,
		0.8f, -0.8f, 0, 1, 0,
		0.0f, 0.8f, 0, 0, 1,
	};
	static uint8_t window[SIZE * SIZE * 4], drawn[SIZE * SIZE * 4];
	int rounds = argc > 1 ? atoi(argv[1]) : 100, good = 0, ok = 1;
	int width = argc > 3 ? atoi(argv[2]) : 1080, height = argc > 3 ? atoi(argv[3]) : 1920;
	unsigned texture = 0, framebuffer = 0, program;
	OSMesaContext context;
	uint64_t t0;
	double first;

	capture_stderr();
	signal(SIGALRM, on_alarm);
	alarm(60);
	say("start (pid %d, %d rounds, fill %d x %d)", getpid(), rounds, width, height);

	t0 = mach_absolute_time();
	context = OSMesaCreateContextAttribs(attributes, NULL);
	if (context == NULL || !OSMesaMakeCurrent(context, window, GL_UNSIGNED_BYTE, SIZE, SIZE)) {
		say("no OpenGL ES 2 context (%p) -> FAILED", (void *)context);
		flush_stderr();
		return 1;
	}
	say("context in %.1f ms: %s | %s | %s", ms_since(t0), (const char *)glGetString(GL_VENDOR),
	    (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));

	/* 2: a clear */
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SIZE, SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		say("framebuffer incomplete -> FAILED");
		flush_stderr();
		return 1;
	}
	glViewport(0, 0, SIZE, SIZE);
	glClearColor(1, 0, 0, 1);
	t0 = mach_absolute_time();
	glClear(GL_COLOR_BUFFER_BIT);
	glFinish();
	first = ms_since(t0);
	memset(drawn, 0x5a, sizeof(drawn));
	glReadPixels(0, 0, SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, drawn);
	say("clear red: %.2f ms, pixels (0,0) %08x (32,32) %08x (63,63) %08x, error %#x", first, pixel(drawn, 0, 0),
	    pixel(drawn, 32, 32), pixel(drawn, 63, 63), glGetError());
	if (pixel(drawn, 0, 0) != 0xff0000ff || pixel(drawn, 63, 63) != 0xff0000ff) {
		ok = 0;
	}

	/* 3: a triangle */
	program = glCreateProgram();
	glAttachShader(program, shader(GL_VERTEX_SHADER,
	    "attribute vec2 where; attribute vec3 colour; varying vec3 c;\n"
	    "void main() { gl_Position = vec4(where, 0.0, 1.0); c = colour; }\n"));
	glAttachShader(program, shader(GL_FRAGMENT_SHADER,
	    "precision mediump float; varying vec3 c;\n"
	    "void main() { gl_FragColor = vec4(c, 1.0); }\n"));
	glBindAttribLocation(program, 0, "where");
	glBindAttribLocation(program, 1, "colour");
	glLinkProgram(program);
	glGetProgramiv(program, GL_LINK_STATUS, &good);
	if (!good) {
		say("program does not link -> FAILED");
		flush_stderr();
		return 1;
	}
	glUseProgram(program);
	glVertexAttribPointer(0, 2, GL_FLOAT, 0, 5 * sizeof(float), corners);
	glVertexAttribPointer(1, 3, GL_FLOAT, 0, 5 * sizeof(float), corners + 2);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glClearColor(0.0f, 0.0f, 0.25f, 1);
	t0 = mach_absolute_time();
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glFinish();
	first = ms_since(t0);
	memset(drawn, 0x5a, sizeof(drawn));
	glReadPixels(0, 0, SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, drawn);
	say("triangle: %.2f ms (with the shader compile), error %#x", first, glGetError());
	say("  corner (1,62) %08x  centre (32,28) %08x  near red (10,10) %08x", pixel(drawn, 1, 62), pixel(drawn, 32, 28),
	    pixel(drawn, 10, 10));
	say("  near green (53,10) %08x  near blue (32,52) %08x", pixel(drawn, 53, 10), pixel(drawn, 32, 52));
	/* corner: the clear colour; inside: opaque and not the clear colour; the red corner mostly red */
	{
		uint32_t corner = pixel(drawn, 1, 62), centre = pixel(drawn, 32, 28), red = pixel(drawn, 10, 10);

		if ((corner >> 8) != 0x000040 && (corner >> 8) != 0x00003f) {
			ok = 0;
		}
		if (centre == corner || (centre & 0xff) != 0xff) {
			ok = 0;
		}
		if ((red >> 24) < 0x80 || ((red >> 16) & 0xff) > 0x60) {
			ok = 0;
		}
	}
	/* a picture of it, 16 x 16 (every 4th pixel), one letter per pixel: R G B (mostly), . (clear), ? */
	for (int y = 15; y >= 0; y--) {
		char row[17];

		for (int x = 0; x < 16; x++) {
			uint32_t p = pixel(drawn, x * 4 + 2, y * 4 + 2);
			unsigned r = p >> 24, g = (p >> 16) & 0xff, b = (p >> 8) & 0xff;

			row[x] = (r < 8 && g < 8 && b >= 0x38 && b <= 0x48) ? '.' :
			    (r >= g && r >= b) ? 'R' : (g >= r && g >= b) ? 'G' : (b > r && b > g) ? 'B' : '?';
		}
		row[16] = 0;
		say("  |%s|", row);
	}

	/* 4: frames */
	t0 = mach_absolute_time();
	for (int i = 0; i < rounds; i++) {
		glClear(GL_COLOR_BUFFER_BIT);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glFinish();
	}
	if (rounds > 0) {
		say("%d frames (clear + triangle + glFinish): %.3f ms each, error %#x", rounds, ms_since(t0) / rounds,
		    glGetError());
	}

	/* 5: full-screen fill timing (a watchdog of its own: 160 big frames take a while) */
	if (width > 0 && height > 0) {
		alarm(120);
		if (fill(width, height) != 0) {
			ok = 0;
		}
	}

	OSMesaDestroyContext(context);
	alarm(0);
	say("%s", ok ? "ALL OK" : "FAILED (pixels not as expected)");
	flush_stderr();
	return ok ? 0 : 2;
}
