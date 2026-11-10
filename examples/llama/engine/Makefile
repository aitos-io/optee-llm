# The engine, twice: libengine-tee.a for the TA (tee-libc headers), and the Linux program ref on
# the Linux build of the same llama.cpp (build-llama.sh), for comparing outputs.
include ../../../tee-libc/flags.mk
BUILD ?= $(abspath $(or $(OPTEE_LLM_BUILD),../../../build))
LLAMA_SRC := $(wildcard $(BUILD)/llama.cpp-*)
INC := -I$(LLAMA_SRC)/include -I$(LLAMA_SRC)/ggml/include
LINUX_LIBS := -Wl,--start-group $(shell find $(BUILD)/llama-linux -name '*.a') -Wl,--end-group \
	-lpthread -lm

all: libengine-tee.a ref

libengine-tee.a: engine.cpp engine.h
	g++ -std=c++17 -O2 $(TEE_LIBC_CXXFLAGS) $(INC) -c engine.cpp -o engine-tee.o
	ar rcs $@ engine-tee.o

ref: ref.c engine.cpp engine.h
	g++ -std=c++17 -O2 $(INC) -c engine.cpp -o engine-ref.o
	gcc -O2 -c ref.c -o ref.o
	g++ -o $@ ref.o engine-ref.o $(LINUX_LIBS)

clean:
	rm -f *.o libengine-tee.a ref
