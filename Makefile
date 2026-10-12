CC ?= cc
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -Werror
CPPFLAGS += -Isrc
HEADERS = $(wildcard src/*.h)
CORE = src/audio.c src/math.c src/terrain.c src/universe.c src/game.c src/save.c src/render.c
.PHONY: all test sanitize preview clean amiga
all: build/space
build:
	mkdir -p build
build/space: $(CORE) $(HEADERS) src/platform/host.c src/flight_input.h src/space.h src/trig.h src/native_video.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $$(sdl2-config --cflags) $(CORE) src/platform/host.c $$(sdl2-config --libs) -o $@
build/tests: $(CORE) $(HEADERS) tests/test_space.c src/flight_input.h src/space.h src/trig.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tests/test_space.c -o $@
build/renderer-tests: $(CORE) $(HEADERS) tests/test_renderer.c src/space.h src/trig.h src/native_video.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter-out src/render.c,$(CORE)) tests/test_renderer.c -o $@
build/presentation-tests: $(CORE) $(HEADERS) tests/test_presentation.c src/controller.h src/audio.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tests/test_presentation.c -o $@
test: build/tests build/renderer-tests build/presentation-tests
	./build/presentation-tests
	./build/renderer-tests
	./build/tests
sanitize: | build
	$(CC) $(CPPFLAGS) -std=c99 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined $(CORE) tests/test_presentation.c -o build/presentation-tests-sanitize
	./build/presentation-tests-sanitize
	$(CC) $(CPPFLAGS) -std=c99 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined $(CORE) tests/test_space.c -o build/tests-sanitize
	./build/tests-sanitize
	$(CC) $(CPPFLAGS) -std=c99 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined $(filter-out src/render.c,$(CORE)) tests/test_renderer.c -o build/renderer-tests-sanitize
	./build/renderer-tests-sanitize
preview: build/space
	./build/space --render build/surface.ppm --surface
	./build/space --render build/orbit.ppm --orbit
amiga:
	$(MAKE) -f Makefile.Amiga
clean:
	rm -f build/space build/tests build/tests-sanitize build/navigation-tests build/presentation-tests build/presentation-tests-sanitize build/benchmark build/renderer-tests build/renderer-tests-sanitize build/*.ppm

.PHONY: benchmark package
build/benchmark: $(CORE) $(HEADERS) tools/benchmark.c src/space.h src/trig.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tools/benchmark.c -o $@
benchmark: build/benchmark
	./build/benchmark
package:
	./package.sh

.PHONY: navigation-test
build/navigation-tests: $(CORE) $(HEADERS) tests/test_navigation.c src/space.h src/trig.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tests/test_navigation.c -o $@
navigation-test: build/navigation-tests
	./build/navigation-tests
