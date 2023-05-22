CXX_FLAGS := -m32 -g -O2 -std=c++20 -Wall -static 
CXX_EXTRA_FLAGS := -Werror -Wa,--fatal-warnings
build-test:
	mkdir -p build
	g++ $(CXX_FLAGS) $(CXX_EXTRA_FLAGS) tests.cpp -o build/test
test: build-test
	./build/test
valgrind-test: build-test
	valgrind --tool=memcheck --gen-suppressions=all --leak-check=full --leak-resolution=med --track-origins=yes ./test

