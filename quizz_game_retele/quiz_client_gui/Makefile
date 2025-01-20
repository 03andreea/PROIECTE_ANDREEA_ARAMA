CC = gcc
CFLAGS = `pkg-config --cflags gtk+-3.0` -Wall
LIBS = `pkg-config --libs gtk+-3.0`

quiz_client: client_gui.c
	$(CC) -o quiz_client client_gui.c $(CFLAGS) $(LIBS)

clean:
	rm -f quiz_client
