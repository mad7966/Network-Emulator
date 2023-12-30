CC     = g++
INCLUDES = ether.h ip.h
CFLAGS = --std=gnu++11
MODE = -pthread


all: bridge station 

bridge: bridge.cpp ether.h
	$(CC) $(CFLAGS) bridge.cpp -o bridge $(MODE)

station: station.cpp ether.h ip.h 
	$(CC) $(CFLAGS) station.cpp -o station $(MODE)

clean : 
	rm -f bridge station *.o .*.addr .*.port 
