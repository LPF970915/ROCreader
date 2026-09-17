CXX ?= g++
PKG_CONFIG ?= pkg-config
REQUIRE_MUPDF ?= 0
H700_OPTIMIZE ?= 0

TARGET ?= build/rocreader_sdl
WN04_FETCH_TARGET ?= bin/wn04_fetch
RGDS_DUALSCREEN_PROBE_TARGET ?= build/rgds_sdl_dualscreen_probe
RGDS_PLUS_REGRESSION_TARGET ?= build/rgds_plus_regression
RGDS_PLUS_UPDATE_REGRESSION_TARGET ?= build/rgds_plus_update_regression
SHELF_ANIMATION_REGRESSION_TARGET ?= build/shelf_animation_regression
IMAGE_FLOW_REGRESSION_TARGET ?= build/image_flow_regression
APP_SRCS := \
  src/main.cpp \
  src/app_loop.cpp \
  src/app_bootstrap.cpp \
  src/app_composition.cpp \
  src/app_runtime.cpp \
  src/app_context.cpp \
  src/app_services.cpp \
  src/app_shell.cpp \
  src/app_layout.cpp \
  src/app_config_bridge.cpp \
  src/scene_manager.cpp \
  src/app_language.cpp \
  src/app_stores.cpp \
  src/rgds_runtime.cpp \
  src/rgds_interaction.cpp \
  src/rgds_reader_layout.cpp \
  src/rgds_render.cpp

CORE_SRCS := \
  src/storage_paths.cpp \
  src/path_adapter.cpp \
  src/sdl_utils.cpp \
  src/image_decode.cpp \
  src/animation.cpp \
  src/input_manager.cpp \
  src/screen_profile.cpp \
  src/runtime_log.cpp

BOOT_SRCS := \
  src/boot_runtime.cpp \
  src/boot_scene.cpp

SHELF_SRCS := \
  src/book_scanner.cpp \
  src/book_library_service.cpp \
  src/cover_resolver.cpp \
  src/cover_service.cpp \
  src/cover_cache_runtime.cpp \
  src/shelf_runtime.cpp \
  src/shelf_scene.cpp

READER_SRCS := \
  src/reader_core.cpp \
  src/reader_module.cpp \
  src/reader_manager.cpp \
  src/reader_launch_service.cpp \
  src/chapter_detection.cpp \
  src/chapter_sidebar.cpp \
  src/reader_input_router.cpp \
  src/reader_progress_controller.cpp \
  src/reader_scene.cpp \
  src/reader_runtime_common.cpp \
  src/reader_render_controller.cpp \
  src/reader_render_runtime.cpp \
  src/async_image_render_queue.cpp \
  src/progress_store.cpp \
  src/reader_session_ops.cpp \
  src/pdf_reader.cpp \
  src/pdf_runtime.cpp \
  src/pdf_reader_module.cpp \
  src/epub_reader.cpp \
  src/epub_common.cpp \
  src/epub_classifier.cpp \
  src/epub_comic_reader.cpp \
  src/epub_comic_runtime.cpp \
  src/epub_flow_reader.cpp \
  src/epub_runtime.cpp \
  src/epub_cover_cache.cpp \
  src/epub_reader_module.cpp \
  src/epub_comic_reader_module.cpp \
  src/epub_flow_reader_module.cpp \
  src/txt_reader_runtime.cpp \
  src/txt_reader_session.cpp \
  src/txt_reader_module.cpp \
  src/txt_session_facade.cpp \
  src/txt_text_service.cpp \
  src/txt_transcode_service.cpp \
  src/zip_image_reader.cpp \
  src/zip_image_runtime.cpp \
  src/zip_image_cover_cache.cpp \
  src/zip_image_reader_module.cpp

MENU_SRCS := \
  src/settings_runtime.cpp \
  src/menu_scene.cpp \
  src/menu_runtime.cpp \
  src/settings_panel_router.cpp \
  src/system_controls_panel.cpp \
  src/txt_settings_panel.cpp \
  src/avatar_panel.cpp \
  src/history_panel.cpp \
  src/cache_panel.cpp \
  src/online_source_transport.cpp \
  src/online_opds_provider.cpp \
  src/online_manual_web_provider.cpp \
  src/online_source_runtime.cpp \
  src/online_session_service.cpp \
  src/online_shelf_bridge.cpp \
  src/online_shelf_controller.cpp \
  src/online_source_panel.cpp \
  src/update_panel.cpp \
  src/contact_panel.cpp \
  src/exit_panel.cpp \
  src/key_guide_panel.cpp \
  src/key_calibration_panel.cpp \
  src/key_calibration_runtime.cpp \
  src/system_settings_runtime.cpp \
  src/txt_settings_runtime.cpp \
  src/version_update_runtime.cpp \
  src/contributor_avatar_runtime.cpp

UI_SRCS := \
  src/ui_assets.cpp \
  src/ui_assets_loader.cpp \
  src/ui_text_cache.cpp \
  src/texture_registry.cpp \
  src/avatar_badge_runtime.cpp

STATUS_SRCS := \
  src/system_status.cpp \
  src/status_bar_runtime.cpp \
  src/volume_overlay.cpp \
  src/audio_runtime.cpp \
  src/system_controls.cpp \
  src/lid_power_control.cpp

SRCS := \
  $(APP_SRCS) \
  $(CORE_SRCS) \
  $(BOOT_SRCS) \
  $(SHELF_SRCS) \
  $(READER_SRCS) \
  $(MENU_SRCS) \
  $(UI_SRCS) \
  $(STATUS_SRCS)
OBJS := $(SRCS:.cpp=.o)

SDL_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags sdl2 2>/dev/null)
SDL_LIBS ?= $(shell $(PKG_CONFIG) --libs sdl2 2>/dev/null)
ALSA_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags alsa 2>/dev/null)
ALSA_LIBS ?= $(shell $(PKG_CONFIG) --libs alsa 2>/dev/null)

ifeq ($(strip $(SDL_LIBS)),)
SDL_LIBS := -lSDL2
endif

CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wno-unused-parameter
ifeq ($(H700_OPTIMIZE),1)
CXXFLAGS := $(filter-out -O0 -O1 -O2 -O3 -Ofast,$(CXXFLAGS))
CXXFLAGS += -O3 -mcpu=cortex-a53 -mtune=cortex-a53 -ffast-math
endif
CXXFLAGS += $(SDL_CFLAGS) -I./src
CXXFLAGS += -pthread
LDFLAGS += -pthread $(SDL_LIBS)
CXXFLAGS += $(EXTRA_CXXFLAGS)
LDFLAGS += $(EXTRA_LDFLAGS)
LDFLAGS += $(FS_LIBS)

ifneq ($(strip $(ALSA_LIBS)),)
CXXFLAGS += -DHAVE_ALSA $(ALSA_CFLAGS)
LDFLAGS += $(ALSA_LIBS)
endif

IMG_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags SDL2_image 2>/dev/null)
IMG_LIBS ?= $(shell $(PKG_CONFIG) --libs SDL2_image 2>/dev/null)
ifneq ($(strip $(IMG_LIBS)),)
CXXFLAGS += -DHAVE_SDL2_IMAGE $(IMG_CFLAGS)
LDFLAGS += $(IMG_LIBS)
endif

TTF_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags SDL2_ttf 2>/dev/null)
TTF_LIBS ?= $(shell $(PKG_CONFIG) --libs SDL2_ttf 2>/dev/null)
ifneq ($(strip $(TTF_LIBS)),)
CXXFLAGS += -DHAVE_SDL2_TTF $(TTF_CFLAGS)
LDFLAGS += $(TTF_LIBS)
endif

MIX_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags SDL2_mixer 2>/dev/null)
MIX_LIBS ?= $(shell $(PKG_CONFIG) --libs SDL2_mixer 2>/dev/null)
ifneq ($(strip $(MIX_LIBS)),)
CXXFLAGS += -DHAVE_SDL2_MIXER $(MIX_CFLAGS)
LDFLAGS += $(MIX_LIBS)
endif

MUPDF_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags mupdf 2>/dev/null || $(PKG_CONFIG) --cflags fitz 2>/dev/null)
MUPDF_LIBS ?= $(shell $(PKG_CONFIG) --libs mupdf 2>/dev/null || $(PKG_CONFIG) --libs fitz 2>/dev/null)
ifneq ($(strip $(MUPDF_LIBS)),)
CXXFLAGS += -DHAVE_MUPDF $(MUPDF_CFLAGS)
LDFLAGS += $(MUPDF_LIBS)
endif

POPPLER_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags poppler-cpp 2>/dev/null)
POPPLER_LIBS ?= $(shell $(PKG_CONFIG) --libs poppler-cpp 2>/dev/null)
ifneq ($(strip $(POPPLER_LIBS)),)
CXXFLAGS += -DHAVE_POPPLER $(POPPLER_CFLAGS)
LDFLAGS += $(POPPLER_LIBS)
endif

LIBZIP_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags libzip 2>/dev/null)
LIBZIP_LIBS ?= $(shell $(PKG_CONFIG) --libs libzip 2>/dev/null)
ifneq ($(strip $(LIBZIP_LIBS)),)
CXXFLAGS += -DHAVE_LIBZIP $(LIBZIP_CFLAGS)
LDFLAGS += $(LIBZIP_LIBS)
endif

CURL_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags libcurl 2>/dev/null)
CURL_LIBS ?= $(shell $(PKG_CONFIG) --libs libcurl 2>/dev/null)
ifneq ($(strip $(CURL_LIBS)),)
CXXFLAGS += -DHAVE_LIBCURL $(CURL_CFLAGS)
LDFLAGS += $(CURL_LIBS)
endif

WEBP_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags libwebp 2>/dev/null)
WEBP_LIBS ?= $(shell $(PKG_CONFIG) --libs libwebp 2>/dev/null)
ifneq ($(strip $(WEBP_LIBS)),)
CXXFLAGS += -DHAVE_WEBP $(WEBP_CFLAGS)
LDFLAGS += $(WEBP_LIBS)
endif
ifeq ($(shell uname -s 2>/dev/null),Linux)
LDFLAGS += -ldl
endif

JPEG_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags libjpeg 2>/dev/null)
JPEG_LIBS ?= $(shell $(PKG_CONFIG) --libs libjpeg 2>/dev/null)
ifneq ($(strip $(JPEG_LIBS)),)
CXXFLAGS += -DHAVE_JPEG $(JPEG_CFLAGS)
LDFLAGS += $(JPEG_LIBS)
endif

ifeq ($(REQUIRE_MUPDF),1)
ifeq ($(strip $(MUPDF_LIBS)$(POPPLER_LIBS)),)
$(error REQUIRE_MUPDF=1 but no real PDF backend found. Install MuPDF/Fitz or poppler-cpp dev package.)
endif
endif

.PHONY: all clean run print-config smoke-windows wn04-fetch rgds-dualscreen-probe rgds-plus-regression rgds-plus-update-regression shelf-animation-regression image-flow-regression

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

wn04-fetch: $(WN04_FETCH_TARGET)

$(WN04_FETCH_TARGET): tools/wn04_fetch.cpp
	@mkdir -p $(dir $@)
	$(CXX) -O2 -std=c++17 -Wall -Wextra -I./src $< -o $@ $(FS_LIBS)

rgds-dualscreen-probe: $(RGDS_DUALSCREEN_PROBE_TARGET)

rgds-plus-regression: $(RGDS_PLUS_REGRESSION_TARGET)
	./$(RGDS_PLUS_REGRESSION_TARGET)

$(RGDS_PLUS_REGRESSION_TARGET): tools/rgds_plus_regression.cpp $(filter-out src/main.o,$(OBJS))
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

rgds-plus-update-regression: $(RGDS_PLUS_UPDATE_REGRESSION_TARGET)
	./$(RGDS_PLUS_UPDATE_REGRESSION_TARGET)

shelf-animation-regression: $(SHELF_ANIMATION_REGRESSION_TARGET)
	./$(SHELF_ANIMATION_REGRESSION_TARGET)

image-flow-regression: $(IMAGE_FLOW_REGRESSION_TARGET)
	./$(IMAGE_FLOW_REGRESSION_TARGET) scale
	./$(IMAGE_FLOW_REGRESSION_TARGET)

src/zip_image_reader.o src/epub_comic_reader.o src/zip_image_runtime.o src/epub_comic_runtime.o src/pdf_runtime.o: src/image_flow_layout.h

$(IMAGE_FLOW_REGRESSION_TARGET): tools/image_flow_regression.cpp $(filter-out src/main.o,$(OBJS))
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(SHELF_ANIMATION_REGRESSION_TARGET): tools/shelf_animation_regression.cpp $(filter-out src/main.o,$(OBJS))
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(RGDS_PLUS_UPDATE_REGRESSION_TARGET): tools/rgds_plus_update_regression.cpp src/version_update_runtime.cpp $(filter-out src/main.o src/version_update_runtime.o,$(OBJS))
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(filter-out src/version_update_runtime.cpp,$^) -o $@ $(LDFLAGS)

$(RGDS_DUALSCREEN_PROBE_TARGET): tools/rgds_sdl_dualscreen_probe.cpp
	@mkdir -p $(dir $@)
	$(CXX) -O2 -std=c++17 -Wall -Wextra $(SDL_CFLAGS) $< -o $@ $(SDL_LIBS)

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

smoke-windows:
	powershell -ExecutionPolicy Bypass -File scripts/smoke_windows.ps1

print-config:
	@echo "CXX=$(CXX)"
	@echo "PKG_CONFIG=$(PKG_CONFIG)"
	@echo "SDL_CFLAGS=$(SDL_CFLAGS)"
	@echo "SDL_LIBS=$(SDL_LIBS)"
	@echo "ALSA_CFLAGS=$(ALSA_CFLAGS)"
	@echo "ALSA_LIBS=$(ALSA_LIBS)"
	@echo "MUPDF_CFLAGS=$(MUPDF_CFLAGS)"
	@echo "MUPDF_LIBS=$(MUPDF_LIBS)"
	@echo "POPPLER_CFLAGS=$(POPPLER_CFLAGS)"
	@echo "POPPLER_LIBS=$(POPPLER_LIBS)"
	@echo "LIBZIP_CFLAGS=$(LIBZIP_CFLAGS)"
	@echo "LIBZIP_LIBS=$(LIBZIP_LIBS)"
	@echo "CURL_CFLAGS=$(CURL_CFLAGS)"
	@echo "CURL_LIBS=$(CURL_LIBS)"
	@echo "WEBP_CFLAGS=$(WEBP_CFLAGS)"
	@echo "WEBP_LIBS=$(WEBP_LIBS)"
	@echo "TTF_CFLAGS=$(TTF_CFLAGS)"
	@echo "TTF_LIBS=$(TTF_LIBS)"
	@echo "MIX_CFLAGS=$(MIX_CFLAGS)"
	@echo "MIX_LIBS=$(MIX_LIBS)"
	@echo "FS_LIBS=$(FS_LIBS)"
	@echo "REQUIRE_MUPDF=$(REQUIRE_MUPDF)"

clean:
	rm -f src/*.o $(TARGET) $(WN04_FETCH_TARGET) $(RGDS_DUALSCREEN_PROBE_TARGET) $(RGDS_PLUS_REGRESSION_TARGET) $(RGDS_PLUS_UPDATE_REGRESSION_TARGET) $(SHELF_ANIMATION_REGRESSION_TARGET) $(IMAGE_FLOW_REGRESSION_TARGET)
