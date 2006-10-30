all:
	$(CC) -Wall -Os -s $(CFLAGS) -I . main.c toys/*.c lib/*.c -o toybox

clean:
	rm toybox
