CXX = g++
CXXFLAGS += -std=c++11 -I ./ -O3

SOURCES=./C++/MyBot.cpp
OBJECTS=$(SOURCES:%.cpp=%.o)
ENGINE=halite

SEED=*
MAP=50x50
ROUND=1

TARGET=MyBot

build: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $@

.c.o:
	$(CXX) $(CXXFLAGS) -fPIC $< -o $@

run: $(TARGET)
	./$(TARGET)

check:$(TARGET) $(ENGINE) run.py
	python3 ./run.py --cmd "./$(TARGET)" --round $(ROUND)

replay:vis.py
	python3 vis.py firefox replays/*x*-*-$(SEED)*.hlt

halite:
	$(MAKE) -C environment
	mv ./environment/$(ENGINE) .

clean:
	rm -f *.log 
	rm -f $(TARGET) ./C++/*.o 
	rm -f ./replays/*.hlt ./visualizer/*-*
	rm -f $(ENGINE) ./environment/*.o

.SILENT: run
