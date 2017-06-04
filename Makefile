all: bmfdec bmfparse

%: %.c
	$(CC) -o $@ $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $^
