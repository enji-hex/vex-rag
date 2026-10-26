CC       ?= gcc
CXX      ?= g++

CFLAGS   := -O2 -I./src -I./src/lib/sqlite_vec
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Werror -Wpedantic \
            -I./src -I./src/lib/sqlite_vec \
            -I./src/lib/llama.cpp/include -I./src/lib/llama.cpp/ggml/include

LDFLAGS  := -fopenmp -Wl,--start-group  \
            ./src/lib/llama.cpp/build/src/libllama.a \
            ./src/lib/llama.cpp/build/ggml/src/libggml.a \
            ./src/lib/llama.cpp/build/ggml/src/libggml-base.a \
            ./src/lib/llama.cpp/build/ggml/src/libggml-cpu.a \
            -Wl,--end-group \
            -lsqlite3 -lpthread -ldl

OBJS := src/main.o src/chunker.o src/ranker.o src/llm.o src/lib/sqlite_vec/sqlite-vec.o

all: vex-rag

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

src/lib/sqlite_vec/sqlite-vec.o: src/lib/sqlite_vec/sqlite-vec.c
	$(CC) $(CFLAGS) -c $< -o $@

vex-rag: $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $@ $(LDFLAGS)
	strip --strip-all ./vex-rag
    chmod +x ./vex-rag

clean:
	rm -f src/*.o src/lib/sqlite_vec/*.o vex-rag

.PHONY: all clean