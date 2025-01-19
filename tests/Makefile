libs := -lhiredis -lfmt

srcs := $(wildcard *.cpp)

depends := $(patsubst %.cpp, %.d, $(srcs))

targets := $(patsubst %.cpp, %, $(srcs))

CXXFLAGS := -Wall -Wextra -Wno-unused-variable -Wno-unused-parameter -Wno-unused-lambda-capture -Wno-deprecated-declarations -std=c++20 -I /usr/include/asio -I ../src -I /usr/include/hiredis -pthread -O2 -g

ifeq ($(shell uname -s), Linux)
        CXX=g++
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
