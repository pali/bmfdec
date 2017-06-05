all: bmfdec bmfparse bmf2mof

%: %.c
	$(CC) -o $@ $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $^
