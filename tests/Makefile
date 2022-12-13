libs := ../3rd/hiredis/libhiredis.a -lfmt

test_srcs := $(wildcard *.cpp)

test_targets := $(patsubst %.cpp, %, $(test_srcs))


CXX_FLAGS := -std=c++20 -I ../3rd/asio/asio/include -I ../3rd/hiredis -I ../src ../3rd/hiredis/libhiredis.a -pthread -O2 -pipe -s

ifeq ($(shell uname -s), Linux)
        CXX=g++-10
        CXX_FLAGS += -fcoroutines
endif

.PHONY: all clean build_lib

all :$(test_targets)

$(test_targets) : %: %.cpp $(libs)
	$(CXX) $(CXX_FLAGS) $< $(libs) -o $@ -s

clean:
	rm -fr $(test_targets)
