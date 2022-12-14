libs := ../3rd/hiredis/libhiredis.a -lfmt

srcs := $(wildcard *.cpp)

depends := $(patsubst %.cpp, %.d, $(srcs))

targets := $(patsubst %.cpp, %, $(srcs))

CXXFLAGS := -Wall -Wextra -Wno-unused-variable -Wno-unused-parameter -Wno-unused-lambda-capture -Wno-deprecated-declarations -std=c++20 -I ../3rd/asio/asio/include -I ../src -I /usr/local/include/hiredis -pthread -O0 -g

ifeq ($(shell uname -s), Linux)
        CXX=g++-10
        CXXFLAGS += -fcoroutines
endif

.PHONY: all clean

all :$(targets)

$(targets) : %: %.cpp
	$(CXX) $(CXXFLAGS) $< $(libs) -o $@

clean:
	rm -fr $(targets) $(depends)


$(depends): %.d: %.cpp
	@rm -fr $@
	@echo build dep $<
	@$(CXX) $(CXXFLAGS) -MM $< -MT $(<:.cpp=.o) -MF $@

ifneq ($(MAKECMDGOALS), clean)
-include $(depends)
endif
