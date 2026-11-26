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

# Один самодостаточный тестовый архив: DLL + BAT + исходники + map-probe.
PACKAGE = dist/FileFolderInfo-Win10-x64-test.zip
PACKAGE_DIR = build/FileFolderInfo-Win10-x64-test
package: $(TARGET) $(PROBE)
	rm -rf $(PACKAGE_DIR) $(PACKAGE)
	mkdir -p $(PACKAGE_DIR)/source $(PACKAGE_DIR)/map-probe/leaflet_page
	cp $(TARGET) dist/install.bat dist/uninstall.bat dist/TEST_PACKAGE_README.txt $(PACKAGE_DIR)/
	cp -a src inc Makefile $(PACKAGE_DIR)/source/
	cp $(PROBE) test/run_probe.bat test/run_probe_noanim.bat $(PACKAGE_DIR)/map-probe/
	cp -a inc/ultralight/bin/*.dll $(PACKAGE_DIR)/map-probe/
	cp -a inc/ultralight/resources inc/ultralight/license $(PACKAGE_DIR)/map-probe/
	cp -a test/leaflet_probe_page/. $(PACKAGE_DIR)/map-probe/leaflet_page/
	cd build && zip -qr ../$(PACKAGE) $(notdir $(PACKAGE_DIR))
	@echo "Готово: $(PACKAGE)"

clean:
	rm -rf build
	rm -f dist/FileFolderInfo.dll $(PROBE) $(PACKAGE)
