# CMake generated Testfile for 
# Source directory: C:/Users/LordN.NASHOU/Desktop/code/maritime_traffic/sea-builder-desktop
# Build directory: C:/Users/LordN.NASHOU/Desktop/code/maritime_traffic/sea-builder-desktop/build-debug
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(logic_tests "C:/Users/LordN.NASHOU/Desktop/code/maritime_traffic/sea-builder-desktop/build-debug/logic_tests.exe")
set_tests_properties(logic_tests PROPERTIES  ENVIRONMENT "SEA_BUILDER_DATA=C:/Users/LordN.NASHOU/Desktop/code/maritime_traffic/sea-builder-desktop/data" WORKING_DIRECTORY "C:/Users/LordN.NASHOU/Desktop/code/maritime_traffic/sea-builder-desktop" _BACKTRACE_TRIPLES "C:/Users/LordN.NASHOU/Desktop/code/maritime_traffic/sea-builder-desktop/CMakeLists.txt;97;add_test;C:/Users/LordN.NASHOU/Desktop/code/maritime_traffic/sea-builder-desktop/CMakeLists.txt;0;")
subdirs("_deps/raylib-build")
