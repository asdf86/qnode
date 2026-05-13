# QNode

A Node.js-like runtime based on QuickJS JavaScript engine.

## Overview

QNode is a lightweight JavaScript runtime that provides a Node.js-like experience using the QuickJS engine. It aims to be fast, lightweight, and easy to embed.

## Features

- **Fast & Lightweight**: Built on QuickJS, one of the fastest JavaScript engines
- **Node.js Compatibility**: Supports common Node.js modules and APIs
- **ES6+ Support**: Full support for modern JavaScript features
- **Worker Threads**: Built-in support for concurrent JavaScript execution
- **Cross-Platform**: Works on Windows, Linux, and macOS

## Building QNode

### Prerequisites

- CMake 3.15 or higher
- C compiler (GCC, Clang, or MSVC)
- Make or Ninja build system

### Build Instructions

```bash
# Create build directory
mkdir build
cd build

# Configure with CMake
cmake ..

# Build
cmake --build .

# Install (optional)
cmake --install .
```

### Build Options

You can customize the build with CMake options:

```bash
# Enable bignum support
cmake -DCONFIG_BIGNUM=ON ..

# Enable debug output
cmake -DENABLE_DEBUG=ON ..

# Enable link-time optimization
cmake -DENABLE_LTO=ON ..

# Disable Worker API
cmake -DUSE_WORKER=OFF ..
```

## Usage

### Running JavaScript Files

```bash
qnode script.js
```

### Interactive Mode

```bash
qnode -i
```

### Evaluate Expressions

```bash
qnode -e "console.log('Hello, QNode!')"
```

### ES6 Modules

```bash
qnode module.mjs
```

### Standard Library

QNode includes standard libraries similar to Node.js:

- `std` - Standard utilities
- `os` - Operating system interface

```bash
qnode --std script.js
```

## Example

```javascript
// hello.js
console.log("Hello, QNode!");

// Using std library
const { printf, puts, exit } = std;

printf("Welcome to QNode v%s\n", "1.0.0");
puts("JavaScript runtime");

// Using OS module
const { readdir, open, close, read } = os;

exit(0);
```

## Project Structure

```
qnode/
├── include/          # Public headers
├── src/
│   ├── core/        # QuickJS core library
│   ├── deps/        # Dependencies (libregexp, libunicode, etc.)
│   ├── lib/         # Standard library
│   └── qnode.c      # Main executable
└── CMakeLists.txt   # Build configuration
```

## Development

QNode is based on QuickJS by Fabrice Bellard. For information about the underlying engine, visit:

- QuickJS: https://bellard.org/quickjs/
- QuickJS GitHub: https://github.com/bellard/quickjs

## License

This project is licensed under the MIT License. See LICENSE file for details.

## Contributing

Contributions are welcome! Please feel free to submit issues and pull requests.

## Roadmap

- [ ] Enhanced Node.js module compatibility
- [ ] Built-in package manager
- [ ] Better error messages
- [ ] Performance optimizations
- [ ] Additional built-in modules (fs, http, crypto, etc.)
- [ ] Debugging support
