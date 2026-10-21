CC ?= cc
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -Werror
CPPFLAGS += -Isrc
HEADERS = $(wildcard src/*.h src/*.inc) src/third_party/micromod/micromod.h
CORE = src/audio.c src/music.c src/third_party/micromod/micromod.c src/math.c src/terrain.c src/universe.c src/galaxy.c src/population.c src/solar.c src/game.c src/destruction.c src/weapons.c src/ground.c src/career.c src/save.c src/ui.c src/services.c src/intro.c src/settings.c src/launcher.c src/performance.c src/xfloat.c src/planet_view.c src/planet_field.c src/planet_contours.c src/classic_raster.c src/render.c
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
build/classic-tests: src/classic_raster.c src/classic_raster.h tests/test_classic.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) src/classic_raster.c tests/test_classic.c -o $@ -lm
build/renderers-tests: $(CORE) $(HEADERS) tests/test_renderers.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tests/test_renderers.c -o $@
build/planet-tests: $(CORE) $(HEADERS) tests/test_planet.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tests/test_planet.c -lm -o $@
build/music-tests: $(CORE) $(HEADERS) tests/test_music.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tests/test_music.c -o $@
test: build/classic-tests build/renderers-tests build/planet-tests build/settings-tests build/galaxy-tests build/tests build/renderer-tests build/presentation-tests build/music-tests build/career-tests build/ui-tests build/parking-tests
	./build/classic-tests
	./build/planet-tests
	./build/settings-tests
	./build/galaxy-tests
	./build/music-tests
	./build/ui-tests
	./build/career-tests
	./build/parking-tests
	./build/presentation-tests
	./build/renderer-tests
	./build/renderers-tests
	./build/tests
sanitize: | build
	$(CC) $(CPPFLAGS) -std=c99 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined src/classic_raster.c tests/test_classic.c -o build/classic-tests-sanitize
	./build/classic-tests-sanitize
	$(CC) $(CPPFLAGS) -std=c99 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined $(CORE) tests/test_renderers.c -o build/renderers-tests-sanitize
	./build/renderers-tests-sanitize
	$(CC) $(CPPFLAGS) -std=c99 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined $(CORE) tests/test_planet.c -lm -o build/planet-tests-sanitize
	./build/planet-tests-sanitize
	$(CC) $(CPPFLAGS) -std=c99 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined $(CORE) tests/test_settings.c -o build/settings-tests-sanitize
	./build/settings-tests-sanitize
	$(CC) $(CPPFLAGS) -std=c99 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined $(CORE) tests/test_galaxy.c -o build/galaxy-tests-sanitize
	./build/galaxy-tests-sanitize
	$(CC) $(CPPFLAGS) -std=c99 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined $(CORE) tests/test_music.c -o build/music-tests-sanitize
	./build/music-tests-sanitize
	$(CC) $(CPPFLAGS) -std=c99 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined $(CORE) tests/test_ui.c -o build/ui-tests-sanitize
	./build/ui-tests-sanitize
	$(CC) $(CPPFLAGS) -std=c99 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined $(CORE) tests/test_careers.c -o build/career-tests-sanitize
	./build/career-tests-sanitize
	$(CC) $(CPPFLAGS) -std=c99 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined $(CORE) tests/test_parking.c -o build/parking-tests-sanitize
	./build/parking-tests-sanitize
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
	rm -f build/classic-tests build/classic-tests-sanitize build/renderers-tests build/renderers-tests-sanitize build/planet-tests build/planet-tests-sanitize build/planet-bench build/settings-tests build/settings-tests-sanitize build/galaxy-tests build/galaxy-tests-sanitize build/galaxy-preview build/music-tests build/music-tests-sanitize
	rm -f build/port-preview build/ground-preview build/weapons-preview build/destruction-preview build/ui-tests build/ui-tests-sanitize build/astralbound build/tests build/tests-sanitize build/navigation-tests build/navigation-tests-sanitize build/presentation-tests build/presentation-tests-sanitize build/career-tests build/career-tests-sanitize build/parking-tests build/parking-tests-sanitize build/benchmark build/renderer-tests build/renderer-tests-sanitize build/fpu-tests build/fpu-renderer-tests build/*.ppm

build/intro-preview: $(CORE) $(HEADERS) tools/intro_preview.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tools/intro_preview.c -o $@
intro-preview: build/intro-preview
	./build/intro-preview --sheet 3 build/intro-sheet.ppm
.PHONY: benchmark package package-test intro-preview
build/benchmark: $(CORE) $(HEADERS) tools/benchmark.c src/astralbound.h src/trig.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tools/benchmark.c -o $@
benchmark: build/benchmark
	./build/benchmark
package:
	./package.sh
package-test:
	python3 tests/test_package.py

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
	mkdir -p build/evidence
	./build/terrain-detail-preview

.PHONY: surface-preview
build/surface-preview: $(CORE) $(HEADERS) tools/surface_preview.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tools/surface_preview.c -o $@
surface-preview: build/surface-preview
	mkdir -p build/evidence
	./build/surface-preview

build/career-tests: $(CORE) $(HEADERS) tests/test_careers.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tests/test_careers.c -o $@

build/parking-tests: $(CORE) $(HEADERS) tests/test_parking.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tests/test_parking.c -o $@

.PHONY: career-preview
build/career-preview: $(CORE) $(HEADERS) tools/career_preview.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tools/career_preview.c -o $@
career-preview: build/career-preview
	mkdir -p build/evidence
	./build/career-preview

build/ground-preview: $(CORE) $(HEADERS) tools/ground_preview.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tools/ground_preview.c -o $@
.PHONY: ground-preview
ground-preview: build/ground-preview
	mkdir -p build/evidence
	./build/ground-preview

build/weapons-preview: $(CORE) $(HEADERS) tools/weapons_preview.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tools/weapons_preview.c -o $@
.PHONY: weapons-preview
weapons-preview: build/weapons-preview
	mkdir -p build/evidence
	./build/weapons-preview

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
galaxy-test: build/settings-tests build/galaxy-tests
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

.PHONY: port-preview
build/port-preview: $(CORE) $(HEADERS) tools/port_preview.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tools/port_preview.c -o $@
port-preview: build/port-preview
	mkdir -p build/evidence
	./build/port-preview

build/settings-tests: $(CORE) $(HEADERS) tests/test_settings.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tests/test_settings.c -o $@
.PHONY: settings-test
settings-test: build/settings-tests
	./build/settings-tests

.PHONY: planet-bench planet-evidence
build/planet-bench: $(CORE) $(HEADERS) tools/planet_bench.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(CORE) tools/planet_bench.c -o $@
planet-bench: build/planet-bench
	./build/planet-bench
planet-evidence: build/planet-bench
	mkdir -p build/evidence/planets
	./build/planet-bench --no-timing --images build/evidence/planets
