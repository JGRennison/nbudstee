nbudstee: nbudstee.cpp
	g++ nbudstee.cpp -Wall --std=gnu++0x -O3 -g -o nbudstee

.PHONY: install

install: nbudstee
	cp nbudstee /usr/local/bin/
