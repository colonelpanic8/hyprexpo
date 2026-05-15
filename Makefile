# Else exist specifically for clang
ifeq ($(CXX),g++)
    EXTRA_FLAGS = --no-gnu-unique
else
    EXTRA_FLAGS =
endif

CXXFLAGS ?= -O2
CXXFLAGS += -shared -fPIC -std=c++2b -Wno-narrowing
SRC = Config.cpp Dispatchers.cpp ExpoGesture.cpp GestureKeyword.cpp Hooks.cpp LabelRenderer.cpp Overview.cpp OverviewPassElement.cpp Plugin.cpp WorkspaceLayout.cpp

all:
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(EXTRA_FLAGS) $(SRC) -o hyprexpo.so `pkg-config --cflags pixman-1 libdrm hyprland pangocairo libinput libudev wayland-server xkbcommon`
clean:
	rm -f ./hyprexpo.so
