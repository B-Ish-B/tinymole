CXXFLAGS_RELEASE = -std=c++17 -O2 -march=native
CXXFLAGS_RELEASE += -DQUILL_COMPILE_ACTIVE_LOG_LEVEL=QUILL_COMPILE_ACTIVE_LOG_LEVEL_INFO

CXXFLAGS_DEBUG = -std=c++17 -O0 -g -fsanitize=address,undefined

CXXFLAGS_TSAN = -std=c++17 -O1 -g -fsanitize=thread

LDFLAGS = -lquill -lssl -lcrypto -lpthread

.PHONY: all debug tsan test bench clean

all:

debug:

tsan:

test:

bench:

clean:
