# Else exist specifically for clang
ifeq ($(CXX),g++)
    EXTRA_FLAGS = --no-gnu-unique
else
    EXTRA_FLAGS =
endif

CXXFLAGS ?= -O2
CXXFLAGS += -shared -fPIC -std=c++2b -Wno-narrowing
SRC = CachedPreview.cpp Config.cpp Dispatchers.cpp ExpoGesture.cpp GestureKeyword.cpp Hooks.cpp Internals.cpp Interaction.cpp LabelRenderer.cpp LivePreview.cpp Overview.cpp OverviewPassElement.cpp Plugin.cpp PreviewMode.cpp Render.cpp Swipe.cpp WorkspaceLayout.cpp

all:
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(EXTRA_FLAGS) $(SRC) -o hyprexpo.so `pkg-config --cflags --libs pixman-1 libdrm hyprland hyprgraphics gio-unix-2.0 pangocairo libinput libudev wayland-server xkbcommon`
clean:
	rm -f ./hyprexpo.so
