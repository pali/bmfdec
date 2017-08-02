BINS := bmfdec bmfparse bmf2mof

all: $(BINS)

clean:
	$(RM) $(BINS)

%: %.c
	$(CC) -o $@ $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $^
