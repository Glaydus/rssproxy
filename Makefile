PROG    = rssproxy
OUT    ?= -o bin/$(PROG)
SERVICE = $(PROG).service

SOURCES = main.c blacklist.c source.c

-include config.mk

PREFIX     ?= /usr/local
SYSCONFDIR ?= $(PREFIX)/etc
CONF_DIR   ?= $(SYSCONFDIR)/rssproxy
SYSTEMD_UNIT_DIR ?= /usr/lib/systemd/system

WARNINGS = -Wall -Wextra -Wpedantic -std=c11

CFLAGS       += $(WARNINGS) -O2 -s -DNDEBUG -DCONF_DIR='"$(CONF_DIR)"' $(PKG_CFLAGS)
CFLAGS_DEBUG += $(WARNINGS) -O0 -g -DDEBUG  -DCONF_DIR='"$(CONF_DIR)"' $(PKG_CFLAGS)

LIBS = $(if $(PKG_LIBS),$(PKG_LIBS),-lcurl -lmicrohttpd) -lpthread

.PHONY: all build debug install clean

all: build

bin:
	mkdir -p bin

build: bin $(SOURCES)
	$(CC) $(SOURCES) $(CFLAGS) $(LIBS) $(OUT)
	@if [ ! -f bin/rssproxy.conf ]; then \
		cp rssproxy.conf bin/rssproxy.conf; \
		echo "Local configuration template copied to bin/rssproxy.conf"; \
	fi

debug: bin $(SOURCES)
	$(CC) $(SOURCES) $(CFLAGS_DEBUG) $(LIBS) $(OUT)
	@if [ ! -f bin/rssproxy.conf ]; then \
		cp rssproxy.conf bin/rssproxy.conf; \
	fi

install:
	# 1. Check if binary exists and install it
	@if [ ! -f bin/$(PROG) ]; then \
		echo "Error: Binary bin/$(PROG) not found."; \
		echo "Please run 'make' or 'make debug' first before installing."; \
		exit 1; \
	fi

	# 2. Install binary
	@mkdir -p $(DESTDIR)$(PREFIX)/bin
	@if [ -n "$$SUDO_UID" ] && [ -n "$$SUDO_GID" ]; then \
		chown $$SUDO_UID:$$SUDO_GID $(DESTDIR)$(PREFIX)/bin; \
	fi
	@cp bin/$(PROG) $(DESTDIR)$(PREFIX)/bin/$(PROG)
	@chmod 755 $(DESTDIR)$(PREFIX)/bin/$(PROG)
	@if [ -n "$$SUDO_UID" ] && [ -n "$$SUDO_GID" ]; then \
		chown $$SUDO_UID:$$SUDO_GID $(DESTDIR)$(PREFIX)/bin/$(PROG); \
	fi
	@echo "Installed binary to $(PREFIX)/bin/$(PROG)"

	# 3. Install global configuration file
	@mkdir -p $(DESTDIR)$(CONF_DIR)
	@if [ -n "$$SUDO_UID" ] && [ -n "$$SUDO_GID" ]; then \
		chown $$SUDO_UID:$$SUDO_GID $(DESTDIR)$(CONF_DIR); \
	fi
	@if [ ! -f $(DESTDIR)$(CONF_DIR)/rssproxy.conf ]; then \
		cp bin/rssproxy.conf $(DESTDIR)$(CONF_DIR)/rssproxy.conf; \
		chmod 644 $(DESTDIR)$(CONF_DIR)/rssproxy.conf; \
		if [ -n "$$SUDO_UID" ] && [ -n "$$SUDO_GID" ]; then \
			chown $$SUDO_UID:$$SUDO_GID $(DESTDIR)$(CONF_DIR)/rssproxy.conf; \
		fi; \
		echo "Installed configuration to $(CONF_DIR)/rssproxy.conf"; \
	else \
		cp bin/rssproxy.conf $(DESTDIR)$(CONF_DIR)/rssproxy.conf.new; \
		chmod 644 $(DESTDIR)$(CONF_DIR)/rssproxy.conf.new; \
		if [ -n "$$SUDO_UID" ] && [ -n "$$SUDO_GID" ]; then \
			chown $$SUDO_UID:$$SUDO_GID $(DESTDIR)$(CONF_DIR)/rssproxy.conf.new; \
		fi; \
		echo "Warning: $(CONF_DIR)/rssproxy.conf already exists."; \
		echo "         New default configuration installed as rssproxy.conf.new"; \
	fi

	# 4. Install rssproxy.service
	@if [ -f $(SERVICE) ]; then \
		mkdir -p $(DESTDIR)$(SYSTEMD_UNIT_DIR); \
		cp $(SERVICE) $(DESTDIR)$(SYSTEMD_UNIT_DIR)/$(SERVICE); \
		chmod 644 $(DESTDIR)$(SYSTEMD_UNIT_DIR)/$(SERVICE); \
		echo "Installed systemd unit to $(SYSTEMD_UNIT_DIR)/$(SERVICE)"; \
	fi

clean:
	rm -rf bin config.mk $(SERVICE)

deploy:
	sudo systemctl daemon-reload
	sudo systemctl restart $(SERVICE)
	sudo systemctl status $(SERVICE) --no-pager -l
