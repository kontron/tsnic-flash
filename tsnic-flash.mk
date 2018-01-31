ALL_TARGETS += $(o)tsnic-flash
CLEAN_TARGETS += clean-tsnic-flash
INSTALL_TARGETS += install-tsnic-flash

tsnic-flash_SOURCES := $(wildcard *.c)
tsnic-flash_LIBS := -lpciaccess
tsnic-flash_OBJECTS := $(addprefix $(o),$(tsnic-flash_SOURCES:.c=.o))

$(o)%.o: %.c
	$(call compile_tgt,tsnic-flash)

$(o)tsnic-flash: $(tsnic-flash_OBJECTS)
	$(call link_tgt,tsnic-flash)

clean-tsnic-flash:
	rm -f $(tsnic-flash_OBJECTS) $(o)tsnic-flash

install-tsnic-flash: $(o)tsnic-flash
	$(INSTALL) -d -m 0755 $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 0755 $(o)tsnic-flash $(DESTDIR)$(BINDIR)/
