# jumpseat's H.264 encoder: OpenH264 v2.6.0's encoder, trimmed and changed
# (README.md). Builds build/libjumpseat_h264.a for x86-64 and arm64.
#   make -j8          (x86-64 needs nasm)
ARCH ?= $(shell uname -m)
CXX ?= g++
OPT ?= -O3 -g
INCLUDES := -Iapi/wels -Icommon/inc -Iencoder/core/inc -Iencoder/plus/inc -Iprocessing/interface \
	$(addprefix -I,$(wildcard processing/src/*))
CXXFLAGS := $(OPT) -DNDEBUG -DGENERATED_VERSION_HEADER -Wall -fno-strict-aliasing -fPIC -MMD -MP $(INCLUDES)
SRCS := $(wildcard common/src/*.cpp encoder/core/src/*.cpp encoder/plus/src/*.cpp processing/src/*/*.cpp)
ifeq ($(ARCH),x86_64)
CXXFLAGS += -DX86_ASM -DHAVE_AVX2
ASM := $(filter-out common/x86/asm_inc.asm,$(wildcard common/x86/*.asm encoder/core/x86/*.asm processing/src/x86/*.asm))
else ifneq ($(filter aarch64 arm64,$(ARCH)),)
CXXFLAGS += -DHAVE_NEON_AARCH64 -march=armv8-a
ASM := $(filter-out common/arm64/arm_arch64_common_macro.S,$(wildcard common/arm64/*.S encoder/core/arm64/*.S processing/src/arm64/*.S))
endif
OBJS := $(patsubst %,build/%.o,$(SRCS) $(ASM))

build/libjumpseat_h264.a: $(OBJS)
	rm -f $@ && ar crs $@ $^

build/%.cpp.o: %.cpp Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build/%.asm.o: %.asm Makefile
	@mkdir -p $(dir $@)
	nasm -f elf64 -DUNIX64 -DHAVE_AVX2 -Icommon/x86/ -o $@ $<

build/%.S.o: %.S Makefile
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Icommon/arm64/ -c -o $@ $<

clean:
	rm -rf build
.PHONY: clean
-include $(OBJS:.o=.d)
