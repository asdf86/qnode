//Process Module Test for QNode
import process from 'node:process';
import { arch } from 'node:process';
import { argv } from 'node:process';
console.log(`This processor architecture is ${arch}`);
//print process.argv
console.log('Command line arguments:');
argv.forEach((val, index) => {
  console.log(`${index}: ${val}`);
});

console.log(`This processor architecture is ${arch}`);
console.log(`This processor argv0 is ${process.argv[0]}`);

console.log('Setting up event handlers...');

// process.on('beforeExit', (code) => {
//   console.log('Process beforeExit event with code: ', code);
// });

// process.on('exit', (code) => {
//   console.log('Process exit event with code: ', code);
// });

// process.on('SIGINT', () => {
//   console.log('Received SIGINT. Press Control-D to exit.');
// });

// console.log('This message is displayed first.');