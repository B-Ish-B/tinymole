CXXFLAGS_RELEASE = -std=c++17 -O2 -march=native
CXXFLAGS_RELEASE += -DQUILL_COMPILE_ACTIVE_LOG_LEVEL=QUILL_COMPILE_ACTIVE_LOG_LEVEL_INFO

CXXFLAGS_DEBUG = -std=c++17 -O0 -g -fsanitize=address,undefined

CXXFLAGS_TSAN = -std=c++17 -O1 -g -fsanitize=thread

LDFLAGS = -lquill -lssl -lcrypto -lpthread

.PHONY: all debug tsan test bench clean

all:

debug:

tsan:

test: build/test_tiny_ptr build/test_hash_table
	./build/test_tiny_ptr
	./build/test_hash_table

build/test_tiny_ptr: tests/test_tiny_ptr.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS_DEBUG) -I. $< -o $@ -lgtest -lgtest_main -lpthread

build/test_hash_table: tests/test_hash_table.cpp src/cpp/hash_table.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS_DEBUG) -I. $^ -o $@ -lgtest -lgtest_main -lpthread -lssl -lcrypto

bench:

clean:
	rm -rf build
