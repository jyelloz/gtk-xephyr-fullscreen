PACKAGES = gtk+-3.0 gtk+-x11-3.0 gdk-3.0 gdk-x11-3.0 glib-2.0

CPPFLAGS += $(shell pkg-config --cflags $(PACKAGES))
LDFLAGS += $(shell pkg-config --libs $(PACKAGES))

CFLAGS += -pedantic -Wall -std=gnu99
CXXFLAGS += -pedantic -Wall -std=gnu++98

CC := clang
CXX := clang++

TARGETS = gtk-xephyr-fullscreen

all: $(TARGETS)

clean:
	rm -f *.o $(TARGETS)
