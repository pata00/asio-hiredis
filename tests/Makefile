libs := ../3rd/hiredis/libhiredis.a -lfmt

srcs := $(wildcard *.cpp)

depends := $(patsubst %.cpp, %.d, $(srcs))

targets := $(patsubst %.cpp, %, $(srcs))


CXXFLAGS := -std=c++20 -I ../3rd/asio/asio/include -I ../3rd/hiredis -I ../src -pthread -O2 -g

ifeq ($(shell uname -s), Linux)
        CXX=g++-10
        CXXFLAGS += -fcoroutines
endif

.PHONY: all clean build_lib

all :$(targets)

$(targets) : %: %.cpp $(libs)
	$(CXX) $(CXXFLAGS) $< $(libs) -o $@ -s

clean:
	rm -fr $(targets) $(depends)


$(depends): %.d: %.cpp
	@rm -fr $@
	@echo build dep $<
	@$(CXX) $(CXXFLAGS) -MM $< -MT $(<:.cpp=.o) -MF $@

ifneq ($(MAKECMDGOALS), clean)
-include $(depends)
endif
