CC=g++

all: dv_routing

dv_routing:
	$(CC) DV.cpp dv_routing.cpp -o dv_routing
	./start-router

clean:
	rm dv_routing routing-output*.txt
