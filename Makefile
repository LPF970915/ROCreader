CXX ?= g++
PKG_CONFIG ?= pkg-config
REQUIRE_MUPDF ?= 0
H700_OPTIMIZE ?= 0

TARGET := build/rocreader_sdl
SRCS := src/main.cpp src/book_scanner.cpp src/storage_paths.cpp src/pdf_reader.cpp src/pdf_runtime.cpp src/epub_reader.cpp src/epub_comic_reader.cpp src/cover_resolver.cpp src/animation.cpp
OBJS := $(SRCS:.cpp=.o)

SDL_CFLAGS ?= $(shell $(PKG_CONFIG) --cflags sdl2 2>/dev/null)
SDL_LIBS ?= $(shell $(PKG_CONFIG) --libs sdl2 2>/dev/null)

ifeq ($(strip $(SDL_LIBS)),)
SDL_LIBS := -lSDL2
endif

CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wno-unused-parameter
ifeq ($(H700_OPTIMIZE),1)
CXXFLAGS := $(filter-out -O0 -O1 -O2 -O3 -Ofast,$(CXXFLAGS))
CXXFLAGS += -O3 -mcpu=cortex-a53 -mtune=cortex-a53 -ffast-math
endif
CXXFLAGS += $(SDL_CFLAGS) -I./src
LDFLAGS += $(SDL_LIBS)
CXXFLAGS += $(EXTRA_CXXFLAGS)
LDFLAGS += $(EXTRA_LDFLAGS)

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

ifeq ($(REQUIRE_MUPDF),1)
ifeq ($(strip $(MUPDF_LIBS)$(POPPLER_LIBS)),)
$(error REQUIRE_MUPDF=1 but no real PDF backend found. Install MuPDF/Fitz or poppler-cpp dev package.)
endif
endif

.PHONY: all clean run print-config

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

print-config:
	@echo "CXX=$(CXX)"
	@echo "PKG_CONFIG=$(PKG_CONFIG)"
	@echo "SDL_CFLAGS=$(SDL_CFLAGS)"
	@echo "SDL_LIBS=$(SDL_LIBS)"
	@echo "MUPDF_CFLAGS=$(MUPDF_CFLAGS)"
	@echo "MUPDF_LIBS=$(MUPDF_LIBS)"
	@echo "POPPLER_CFLAGS=$(POPPLER_CFLAGS)"
	@echo "POPPLER_LIBS=$(POPPLER_LIBS)"
	@echo "LIBZIP_CFLAGS=$(LIBZIP_CFLAGS)"
	@echo "LIBZIP_LIBS=$(LIBZIP_LIBS)"
	@echo "TTF_CFLAGS=$(TTF_CFLAGS)"
	@echo "TTF_LIBS=$(TTF_LIBS)"
	@echo "MIX_CFLAGS=$(MIX_CFLAGS)"
	@echo "MIX_LIBS=$(MIX_LIBS)"
	@echo "REQUIRE_MUPDF=$(REQUIRE_MUPDF)"

clean:
	rm -f src/*.o $(TARGET)
