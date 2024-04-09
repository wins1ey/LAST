BIN := LAST

INC := `pkg-config --cflags gtk+-3.0 x11 jansson lua`
CFLAGS := -std=gnu99 -O2 -pthread -Wall -Wno-unused-parameter
LDFLAGS := `pkg-config --libs gtk+-3.0 x11 jansson lua`

SRC_DIR := ./src
OBJ_DIR := ./obj

# Obtain list of source files and create list of object files
SOURCES := $(wildcard $(SRC_DIR)/*.c)
COMPONENTS := $(wildcard $(SRC_DIR)/components/*.c)
OBJECTS := $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SOURCES)) \
          $(patsubst $(SRC_DIR)/components/%.c, $(OBJ_DIR)/%.o, $(COMPONENTS))

DESTDIR :=
PREFIX := /usr/local
APP := last.desktop
ICON := last
SCHEMA := last.gschema.xml

ifdef DESTDIR
	update_icon_cache :=
	compile_schemas :=
else
	update_icon_cache := gtk-update-icon-cache -f -t $(PREFIX)/share/icons/hicolor
	compile_schemas := glib-compile-schemas $(PREFIX)/share/glib-2.0/schemas
endif

all: last-gtk.h $(BIN)

# Rule to link object files to create executable
$(BIN): $(OBJECTS)
	gcc $(CFLAGS) $^ $(LDFLAGS) -o $@

# Rule to compile C source files to object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	gcc $(INC) $(CFLAGS) -c -o $@ $<

# Rule to compile C component source files to object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/components/%.c | $(OBJ_DIR)
	gcc $(INC) $(CFLAGS) -c -o $@ $<

# Rule to create the object directory
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

last-gtk.h: $(SRC_DIR)/last-gtk.css
	xxd --include $(SRC_DIR)/last-gtk.css > $(SRC_DIR)/last-gtk.h || (rm $(SRC_DIR)/last-gtk.h; false)

install: all
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)
	install -Dm644 $(APP) $(DESTDIR)$(PREFIX)/share/applications/$(APP)
	for size in 16 22 24 32 36 48 64 72 96 128 256 512; do \
		mkdir -p $(DESTDIR)$(PREFIX)/share/icons/hicolor/"$$size"x"$$size"/apps ; \
		rsvg-convert -w "$$size" -h "$$size" -f png -o $(DESTDIR)$(PREFIX)/share/icons/hicolor/"$$size"x"$$size"/apps/$(ICON).png $(ICON).svg ; \
	done
	$(update_icon_cache)
	install -Dm644 $(SRC_DIR)/$(SCHEMA) $(DESTDIR)$(PREFIX)/share/glib-2.0/schemas/$(SCHEMA)
	$(compile_schemas)
	install -Dm644 resources/themes/standard/standard.css $(DESTDIR)$(PREFIX)/share/LAST/themes/standard/standard.css
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)
	rm -f $(DESTDIR)$(PREFIX)/share/applications/$(APP)
	rm -rf $(DESTDIR)$(PREFIX)/share/LAST
	for size in 16 22 24 32 36 48 64 72 96 128 256 512; do \
		rm -f $(DESTDIR)$(PREFIX)/share/icons/hicolor/"$$size"x"$$size"/apps/$(ICON).png ; \
	done

remove-schema:
	rm $(DESTDIR)$(PREFIX)/share/glib-2.0/schemas/$(SCHEMA)
	$(compile_schemas)

# Clean target to remove object files and LAS executable
clean:
	rm -rf $(BIN) $(OBJ_DIR) $(SRC_DIR)/last-gtk.h

.PHONY: all last-gtk.h install uninstall remove-schema clean
