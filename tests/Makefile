# CXX has a built-in default (c++), so `?=` alone would never pick clang++.
# Default to clang++ unless CXX came from the command line or environment.
ifeq ($(origin CXX),default)
CXX := clang++
endif
ARDUINOJSON_DIR ?= $(HOME)/Documents/Arduino/libraries/ArduinoJson/src
CXXFLAGS := -std=c++17 -O1 -g -Wall -Wextra -DHOST_TEST -I../flight_radar/src -I$(ARDUINOJSON_DIR)
SRC := $(filter-out ../flight_radar/src/opensky_client.cpp ../flight_radar/src/net_task.cpp ../flight_radar/src/knob_input.cpp ../flight_radar/src/radar_view.cpp ../flight_radar/src/aircraft_layer.cpp ../flight_radar/src/detail_card.cpp ../flight_radar/src/settings_screen.cpp ../flight_radar/src/status_bar.cpp ../flight_radar/src/ui.cpp, $(wildcard ../flight_radar/src/*.cpp))
TESTS := $(wildcard test_*.cpp)
BINS := $(patsubst %.cpp,build/%,$(TESTS))
all: $(BINS)
	@for b in $(BINS); do echo "== $$b"; ./$$b || exit 1; done
build/%: %.cpp $(SRC) test.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $< $(SRC) -o $@
clean:
	rm -rf build
.PHONY: all clean
