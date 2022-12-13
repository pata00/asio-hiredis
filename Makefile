#cheat not:
# $@ target file
# $^ all depends file
# $< first depends file
################################# CONFIG AREA #############################

target := libasio_hiredis.a libasio_hiredis.so
INCDIR := -I ./3rd/asio/asio/include -I ./3rd/hiredis -I ./src

CXXFLAGS := -Wall -Wextra -Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable -Wno-unused-parameter -pipe -fPIC -std=c++20 -g -O3 -pthread #-DENABLE_ASIO_HIREDIS_CLIENT_DEBUG
#CXXFLAGS := -Wall -Wextra -Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable -Wno-unused-parameter -pipe -std=c++20 -g -O0 -pthread # -DASIO_ENABLE_HANDLER_TRACKING

LDFLAGS := -lfmt -pthread

ifeq ($(shell uname -s), Linux)
	CXX=g++-10
	CXXFLAGS += -fcoroutines
endif

CXXFLAGS += $(INCDIR)

libs := ./3rd/hiredis/libhiredis.a

srcs := $(wildcard src/*.cpp)
srcs += $(wildcard src/asio_hiredis/*.cpp)

GIT_COMMIT=$(shell git show -s --pretty=format:%H)
GIT_TAGVER=$(shell git describe --tags)

###########################################################################
.PHONY: clean


objects := $(patsubst %.cpp, %.o, $(srcs))
depends := $(patsubst %.cpp, %.d, $(srcs))



.PHONY: clean all

all: $(target)

libasio_hiredis.a: $(objects) $(libs)
	ar rcs $@ $(objects)
	#$(CXX) $(objects) $(libs) $(LDFLAGS) -o $@

libasio_hiredis.so: $(objects) $(libs)
	$(CXX) $(objects) $(libs) $(LDFLAGS) -shared -o $@

src/main.o:
	$(CXX) $(CXXFLAGS)   -c -o $@ src/main.cpp \
	-DGIT_TAGVER="\"$(GIT_TAGVER)\"" \
	-DGIT_COMMIT="\"$(GIT_COMMIT)\""

./3rd/hiredis/libhiredis.a:
	make -C 3rd/hiredis

clean:
	rm -fr $(depends) $(objects) $(target)

$(depends): %.d: %.cpp
	@rm -fr $@
	@echo build dep $<
	@$(CXX) $(CXXFLAGS) -MM $< -MT $(<:.cpp=.o) -MF $@

ifneq ($(MAKECMDGOALS), clean)
-include $(depends)
endif
