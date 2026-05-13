#!/usr/bin/env qnode

// QNode Module Example

import * as std from "libc:std";

// Import standard library
const { printf, puts, exit } = std;

// Simple module system
const modules = {
    math: {
        add: (a, b) => a + b,
        subtract: (a, b) => a - b,
        multiply: (a, b) => a * b,
        divide: (a, b) => b !== 0 ? a / b : undefined,
    },

    string: {
        capitalize: (str) => str.charAt(0).toUpperCase() + str.slice(1),
        reverse: (str) => str.split('').reverse().join(''),
        repeat: (str, times) => str.repeat(times),
    }
};

// Using the modules
puts("=== Math Module ===");
printf("5 + 3 = %d\n", modules.math.add(5, 3));
printf("10 - 4 = %d\n", modules.math.subtract(10, 4));
printf("6 * 7 = %d\n", modules.math.multiply(6, 7));
printf("15 / 3 = %d\n", modules.math.divide(15, 3));

puts("\n=== String Module ===");
const original = "hello world";
printf("Original: %s\n", original);
printf("Capitalized: %s\n", modules.string.capitalize(original));
printf("Reversed: %s\n", modules.string.reverse(original));
printf("Repeated: %s\n", modules.string.repeat("QNode ", 3));

// ES6 Modules support (with --module flag or .mjs extension)
// This demonstrates module-like structure

class Calculator {
    constructor() {
        this.result = 0;
    }

    add(n) {
        this.result += n;
        return this;
    }

    subtract(n) {
        this.result -= n;
        return this;
    }

    multiply(n) {
        this.result *= n;
        return this;
    }

    getResult() {
        return this.result;
    }
}

const calc = new Calculator();
printf("\nCalculator chain: %d\n", calc.add(10).multiply(2).subtract(5).getResult());

exit(0);
