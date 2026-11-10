# The probe's C++ code, built as a static library against tee-libc (not the TA dev kit's headers),
# and bench_ref, the allocation benchmark on Linux for comparison.
include ../../../tee-libc/flags.mk

all: libprobe.a bench_ref

libprobe.a: probe.o bench.o
	ar rcs $@ $^

probe.o: probe.cpp ../ta/include/cxxprobe_ta.h
	g++ -std=c++17 -O2 $(TEE_LIBC_CXXFLAGS) -I../ta/include -c $< -o $@

bench.o: bench.cpp
	g++ -std=c++17 -O2 $(TEE_LIBC_CXXFLAGS) -c $< -o $@

bench_ref: bench.cpp bench_ref.cpp
	g++ -std=c++17 -O2 -o $@ bench.cpp bench_ref.cpp

clean:
	rm -f *.o libprobe.a bench_ref
