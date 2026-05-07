// C++ Hello World for orizon-os
// Compile with: aarch64-none-elf-g++ -nostdlib -ffreestanding hello.cpp -o
// hello

extern "C" void puts(const char *str);

extern "C" int main() {
  puts("Hello from C++!");
  puts("This is a C++ program running on Orizon OS");
  return 0;
}
