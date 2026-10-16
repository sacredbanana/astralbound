CC ?= cc
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -Werror
CPPFLAGS += -Isrc
HEADERS = $(wildcard src/*.h src/*.inc) src/third_party/micromod/micromod.h
CORE = src/audio.c src/music.c src/third_party/micromod/micromod.c src/math.c src/terrain.c src/universe.c src/galaxy.c src/population.c src/solar.c src/game.c src/destruction.c src/career.c src/save.c src/ui.c src/render.c
.PHONY: all test sanitize preview clean amiga
all: build/astralbound
build:
	mkdir -p build
build/astralbound: $(CORE) $(HEADERS) src/platform/host.c src/flight_input.h src/astralbound.h src/trig.h src/native_video.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $$(sdl2-config --cflags) $(CORE) src/platform/host.c $$(sdl2-config --libs) -o $@
build/tests: $(CORE) $(HEADERS) tests/test_astralbound.c src/flight_input.h src/astralbound.h src/trig.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tests/test_astralbound.c -o $@
build/renderer-tests: $(CORE) $(HEADERS) tests/test_renderer.c src/astralbound.h src/trig.h src/native_video.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter-out src/render.c,$(CORE)) tests/test_renderer.c -o $@
build/presentation-tests: $(CORE) $(HEADERS) tests/test_presentation.c src/controller.h src/audio.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tests/test_presentation.c -o $@
build/music-tests: $(CORE) $(HEADERS) tests/test_music.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tests/test_music.c -o $@
test: build/galaxy-tests build/tests build/renderer-tests build/presentation-tests build/music-tests build/career-tests build/ui-tests
	./build/galaxy-tests
	./build/music-tests
	./build/ui-tests
	./build/career-tests
	./build/presentation-tests
	./build/renderer-tests
	./build/tests
sanitize: | build
	$(CC) $(CPPFLAGS) -std=c99 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined $(CORE) tests/test_galaxy.c -o build/galaxy-tests-sanitize
	./build/galaxy-tests-sanitize
	$(CC) $(CPPFLAGS) -std=c99 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined $(CORE) tests/test_music.c -o build/music-tests-sanitize
	./build/music-tests-sanitize
	$(CC) $(CPPFLAGS) -std=c99 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined $(CORE) tests/test_ui.c -o build/ui-tests-sanitize
	./build/ui-tests-sanitize
	$(CC) $(CPPFLAGS) -std=c99 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined $(CORE) tests/test_careers.c -o build/career-tests-sanitize
	./build/career-tests-sanitize
	$(CC) $(CPPFLAGS) -std=c99 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined $(CORE) tests/test_presentation.c -o build/presentation-tests-sanitize
	./build/presentation-tests-sanitize
	$(CC) $(CPPFLAGS) -std=c99 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined $(CORE) tests/test_astralbound.c -o build/tests-sanitize
	./build/tests-sanitize
	$(CC) $(CPPFLAGS) -std=c99 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined $(filter-out src/render.c,$(CORE)) tests/test_renderer.c -o build/renderer-tests-sanitize
	./build/renderer-tests-sanitize
	$(CC) $(CPPFLAGS) -std=c99 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined $(CORE) tests/test_navigation.c -o build/navigation-tests-sanitize
	./build/navigation-tests-sanitize
preview: build/astralbound
	./build/astralbound --render build/surface.ppm --surface
	./build/astralbound --render build/orbit.ppm --orbit
amiga:
	$(MAKE) -f Makefile.Amiga
clean:
	rm -f build/galaxy-tests build/galaxy-tests-sanitize build/galaxy-preview build/music-tests build/music-tests-sanitize
	rm -f build/destruction-preview build/ui-tests build/ui-tests-sanitize build/astralbound build/tests build/tests-sanitize build/navigation-tests build/navigation-tests-sanitize build/presentation-tests build/presentation-tests-sanitize build/career-tests build/career-tests-sanitize build/benchmark build/renderer-tests build/renderer-tests-sanitize build/fpu-tests build/fpu-renderer-tests build/*.ppm

.PHONY: benchmark package
build/benchmark: $(CORE) $(HEADERS) tools/benchmark.c src/astralbound.h src/trig.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tools/benchmark.c -o $@
benchmark: build/benchmark
	./build/benchmark
package:
	./package.sh

# Host run of the FPU code paths used by Astralbound.020fpu/.040/.060.
.PHONY: fpu-test
build/fpu-tests: $(CORE) $(HEADERS) tests/test_astralbound.c | build
	$(CC) $(CPPFLAGS) -DSPACE_FPU $(CFLAGS) $(CORE) tests/test_astralbound.c -o $@
build/fpu-renderer-tests: $(CORE) $(HEADERS) tests/test_renderer.c | build
	$(CC) $(CPPFLAGS) -DSPACE_FPU $(CFLAGS) $(filter-out src/render.c,$(CORE)) tests/test_renderer.c -o $@
fpu-test: build/fpu-tests build/fpu-renderer-tests
	./build/fpu-renderer-tests
	./build/fpu-tests

.PHONY: navigation-test
build/navigation-tests: $(CORE) $(HEADERS) tests/test_navigation.c src/astralbound.h src/trig.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tests/test_navigation.c -o $@
navigation-test: build/navigation-tests
	./build/navigation-tests

.PHONY: environment-preview
build/environment-preview: $(CORE) $(HEADERS) tools/environment_preview.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tools/environment_preview.c -o $@
environment-preview: build/environment-preview
	./build/environment-preview

.PHONY: terrain-detail-preview
build/terrain-detail-preview: $(CORE) $(HEADERS) tools/terrain_detail_preview.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tools/terrain_detail_preview.c -o $@
terrain-detail-preview: build/terrain-detail-preview
	./build/terrain-detail-preview

.PHONY: surface-preview
build/surface-preview: $(CORE) $(HEADERS) tools/surface_preview.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tools/surface_preview.c -o $@
surface-preview: build/surface-preview
	mkdir -p build/evidence
	./build/surface-preview

build/career-tests: $(CORE) $(HEADERS) tests/test_careers.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tests/test_careers.c -o $@

.PHONY: career-preview
build/career-preview: $(CORE) $(HEADERS) tools/career_preview.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tools/career_preview.c -o $@
career-preview: build/career-preview
	mkdir -p build/evidence
	./build/career-preview

.PHONY: npc-preview
build/npc-preview: $(CORE) $(HEADERS) tools/npc_preview.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tools/npc_preview.c -o $@
npc-preview: build/npc-preview
	mkdir -p build/evidence
	./build/npc-preview

build/ui-tests: $(CORE) $(HEADERS) tests/test_ui.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tests/test_ui.c -o $@

.PHONY: landform-preview
build/landform-preview: $(CORE) $(HEADERS) tools/landform_preview.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tools/landform_preview.c -o $@
landform-preview: build/landform-preview
	mkdir -p build/evidence
	./build/landform-preview

build/galaxy-tests: $(CORE) $(HEADERS) src/star_catalog.inc src/solar_catalog.inc tests/test_galaxy.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tests/test_galaxy.c -o $@
.PHONY: galaxy-test
galaxy-test: build/galaxy-tests
	./build/galaxy-tests

build/galaxy-preview: $(CORE) $(HEADERS) tools/galaxy_preview.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tools/galaxy_preview.c -o $@
.PHONY: galaxy-preview
galaxy-preview: build/galaxy-preview
	mkdir -p build/evidence
	./build/galaxy-preview

build/destruction-preview: $(CORE) $(HEADERS) tools/destruction_preview.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tools/destruction_preview.c -o $@
.PHONY: destruction-preview
destruction-preview: build/destruction-preview
	mkdir -p build/evidence
	./build/destruction-preview
