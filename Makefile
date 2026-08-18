CXX      = x86_64-w64-mingw32-g++
CXXFLAGS = -O2 -std=c++17 -Wall -Wextra -municode -DUNICODE -D_UNICODE -Iinc -finput-charset=UTF-8
LDFLAGS  = -shared -static -static-libgcc -static-libstdc++ -Wl,--subsystem,windows
LIBS     = -lshlwapi -lole32 -loleaut32 -luuid -luser32 -lgdi32 -ladvapi32 -lshell32 -lpropsys -lwindowscodecs

SRC = src/dllmain.cpp src/register.cpp src/shellext.cpp src/util.cpp src/commands.cpp src/metadata.cpp src/peinfo.cpp src/rotate.cpp
OBJ = $(patsubst src/%.cpp,build/%.o,$(SRC))

TARGET = dist/FileFolderInfo.dll

all: $(TARGET)

build/%.o: src/%.cpp inc/*.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): $(OBJ)
	@mkdir -p dist
	$(CXX) $(OBJ) $(LDFLAGS) $(LIBS) -o $@
	x86_64-w64-mingw32-strip --strip-unneeded $@

clean:
	rm -rf build
	rm -f dist/FileFolderInfo.dll
