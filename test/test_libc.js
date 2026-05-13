#!/usr/bin/env qnode

// QNode Hello World Example

import * as std from "libc:std";
import * as os from "libc:os";

console.log("Hello, QNode!");

// Using std library
const { printf, puts, exit } = std;

printf("QNode version: %s\n", "1.0.0");
puts("A lightweight JavaScript runtime");

// Demonstrate ES6 features
const name = "QNode";
const version = 1.0;

console.log(`Welcome to ${name} version ${version}`);

// Using arrow functions
const greet = (who) => `Hello, ${who}!`;
console.log(greet("World"));

// Using async/await (simulated)
async function asyncGreet() {
    return Promise.resolve("Async greetings from QNode!");
}

asyncGreet().then(msg => console.log(msg));

// Using os module for file operations
try {
    const files = os.readdir(".");
    console.log("Files in current directory:");
    files.forEach(file => console.log("  " + file));
} catch (e) {
    console.log("Error listing files:", e);
}

exit(0);
