ALL_TARGETS += $(o)tsnic-flash $(o)tsnic-flash-static
CLEAN_TARGETS += clean-tsnic-flash
INSTALL_TARGETS += install-tsnic-flash

tsnic-flash_SOURCES := $(wildcard *.c)
tsnic-flash_LIBS := -lpciaccess
tsnic-flash_OBJECTS := $(addprefix $(o),$(tsnic-flash_SOURCES:.c=.o))

tsnic-flash-static_LIBS := -lpciaccess
tsnic-flash-static_LDFLAGS := -static

$(o)%.o: %.c
	$(call compile_tgt,tsnic-flash)

$(o)tsnic-flash: $(tsnic-flash_OBJECTS)
	$(call link_tgt,tsnic-flash)

$(o)tsnic-flash-static: $(tsnic-flash_OBJECTS)
	$(call link_tgt,tsnic-flash-static)

clean-tsnic-flash:
	rm -f $(tsnic-flash_OBJECTS) $(o)tsnic-flash $(o)tsnic-flash-static

install-tsnic-flash: $(o)tsnic-flash
	$(INSTALL) -d -m 0755 $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 0755 $(o)tsnic-flash $(DESTDIR)$(BINDIR)/
