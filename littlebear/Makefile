CXX = g++
CXXFLAGS = -std=c++23 -Wall -Wextra $(NIX_CFLAGS_COMPILE)
LDFLAGS = -luring

SRC = src/core.cpp
OUT = core

$(OUT): $(SRC)
	$(CXX) $(CXXFLAGS) -DNDEBUG -o $(OUT) $(SRC) $(LDFLAGS)

debug: $(SRC)
	$(CXX) $(CXXFLAGS) -g -fsanitize=address,undefined -o $(OUT) $(SRC) $(LDFLAGS)

clean:
	rm -f $(OUT)
