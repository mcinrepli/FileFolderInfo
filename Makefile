CXX      = x86_64-w64-mingw32-g++
CXXFLAGS = -O2 -std=c++17 -Wall -Wextra -municode -DUNICODE -D_UNICODE -Iinc -finput-charset=UTF-8
LDFLAGS  = -shared -static -static-libgcc -static-libstdc++ -Wl,--subsystem,windows
LIBS     = -lshlwapi -lole32 -loleaut32 -luuid -luser32 -lgdi32 -ladvapi32 -lshell32 -lpropsys -lwindowscodecs -lmsi

SRC = src/dllmain.cpp src/register.cpp src/shellext.cpp src/util.cpp src/commands.cpp src/metadata.cpp src/peinfo.cpp src/rotate.cpp
OBJ = $(patsubst src/%.cpp,build/%.o,$(SRC))

TARGET = dist/FileFolderInfo.dll
PROBE  = dist/ultralight_probe.exe

all: $(TARGET)

build/%.o: src/%.cpp inc/*.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): $(OBJ)
	@mkdir -p dist
	$(CXX) $(OBJ) $(LDFLAGS) $(LIBS) -o $@
	x86_64-w64-mingw32-strip --strip-unneeded $@

# Офф-скрин проба Ultralight (карта Leaflet). Собирается отдельно, т.к. не
# линкуется с Ultralight.lib (только LoadLibrary/GetProcAddress на DLL).
$(PROBE): test/ultralight_probe.cpp
	@mkdir -p dist
	$(CXX) $(CXXFLAGS) -Iinc/ultralight $< -o $@ \
	    -luser32 -lgdi32 -static -static-libgcc -static-libstdc++ -Wl,--subsystem,windows

probe: $(PROBE)

# Готовый к запуску на Windows архив: exe + 4 DLL + resources/ + leaflet_page/.
# Всё в одной папке (Ultralight грузит DLL и ресурсы относительно exe).
package: $(PROBE)
	rm -rf build/ultralight_test dist/ultralight_test.zip dist/src.zip
	mkdir -p build/ultralight_test/leaflet_page
	cp $(PROBE) build/ultralight_test/
	cp inc/ultralight/bin/*.dll build/ultralight_test/
	cp -r inc/ultralight/resources build/ultralight_test/
	cp -r inc/ultralight/license build/ultralight_test/
	cp -r test/leaflet_probe_page/. build/ultralight_test/leaflet_page/
	cd build && zip -qr ../dist/ultralight_test.zip ultralight_test
	zip -qr dist/src.zip src inc Makefile test/ultralight_probe.cpp \
	    test/leaflet_probe_page dist/README.md dist/install.bat dist/uninstall.bat \
	    -x 'inc/ultralight/bin/*' -x 'inc/ultralight/resources/*'
	@echo "Готово: dist/ultralight_test.zip и dist/src.zip"

clean:
	rm -rf build
	rm -f dist/FileFolderInfo.dll $(PROBE)
