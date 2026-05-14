// Test Buffer module
import { Buffer } from 'node:buffer';
//import process from 'node:process';
let pass = 0, fail = 0;
function assert(cond, msg) {
    if (cond) { pass++; }
    else { fail++; console.log('FAIL: ' + msg); }
}

// 1. Buffer.alloc
{
    const b = Buffer.alloc(5);
    assert(b instanceof Uint8Array, 'alloc instanceof Uint8Array');
    assert(b.length === 5, 'alloc length');
    assert(b[0] === 0 && b[4] === 0, 'alloc zeroed');
    console.log('1. alloc: OK');
}

// 2. Buffer.alloc with fill
{
    const b = Buffer.alloc(4, 0xAA);
    assert(b[0] === 0xAA && b[3] === 0xAA, 'alloc fill number');
    console.log('2. alloc(fill): OK');
}

// 3. Buffer.from string
{
    const b = Buffer.from('hello');
    assert(b.length === 5, 'from string length');
    assert(b[0] === 0x68, 'from string byte 0');
    assert(b.toString() === 'hello', 'from string toString');
    console.log('3. from(string): OK');
}

// 4. Buffer.from array
{
    const b = Buffer.from([1, 2, 3, 4]);
    assert(b.length === 4, 'from array length');
    assert(b[0] === 1 && b[3] === 4, 'from array values');
    console.log('4. from(array): OK');
}

// 5. Buffer.from hex
{
    const b = Buffer.from('48656c6c6f', 'hex');
    assert(b.toString() === 'Hello', 'from hex');
    console.log('5. from(hex): OK');
}

// 6. Buffer.from base64
{
    const b = Buffer.from('SGVsbG8=', 'base64');
    assert(b.toString() === 'Hello', 'from base64');
    console.log('6. from(base64): OK');
}

// 7. Buffer.from ArrayBuffer
{
    const ab = new ArrayBuffer(4);
    const u8 = new Uint8Array(ab);
    u8[0] = 10; u8[1] = 20; u8[2] = 30; u8[3] = 40;
    const b = Buffer.from(ab, 1, 2);
    assert(b.length === 2, 'from ArrayBuffer length');
    assert(b[0] === 20 && b[1] === 30, 'from ArrayBuffer values');
    console.log('7. from(ArrayBuffer): OK');
}

// 8. Buffer.isBuffer
{
    const b = Buffer.alloc(4);
    assert(Buffer.isBuffer(b) === true, 'isBuffer true');
    assert(Buffer.isBuffer(new Uint8Array(4)) === false, 'isBuffer false');
    console.log('8. isBuffer: OK');
}

// 9. toString with encoding
{
    const b = Buffer.from([0xDE, 0xAD, 0xBE, 0xEF]);
    assert(b.toString('hex') === 'deadbeef', 'toString hex');
    console.log('9. toString(hex): OK');
}

// 10. toString base64
{
    const b = Buffer.from('Hello');
    assert(b.toString('base64') === 'SGVsbG8=', 'toString base64');
    console.log('10. toString(base64): OK');
}

// 11. slice
{
    const b = Buffer.from('Hello World');
    const s = b.slice(0, 5);
    assert(s.toString() === 'Hello', 'slice');
    assert(Buffer.isBuffer(s), 'slice is Buffer');
    console.log('11. slice: OK');
}

// 12. write
{
    const b = Buffer.alloc(10);
    const n = b.write('abc', 0);
    assert(n === 3, 'write returns bytes');
    assert(b.toString('utf8', 0, 3) === 'abc', 'write content');
    console.log('12. write: OK');
}

// 13. fill
{
    const b = Buffer.alloc(6);
    b.fill(0xFF, 2, 5);
    assert(b[0] === 0 && b[1] === 0, 'fill before');
    assert(b[2] === 0xFF && b[4] === 0xFF, 'fill range');
    assert(b[5] === 0, 'fill after');
    console.log('13. fill: OK');
}

// 14. copy
{
    const src = Buffer.from([1, 2, 3, 4, 5]);
    const dst = Buffer.alloc(5);
    src.copy(dst, 0, 1, 4);
    assert(dst[0] === 2 && dst[1] === 3 && dst[2] === 4, 'copy');
    console.log('14. copy: OK');
}

// 15. equals
{
    const a = Buffer.from('test');
    const b = Buffer.from('test');
    const c = Buffer.from('TEST');
    assert(a.equals(b) === true, 'equals true');
    assert(a.equals(c) === false, 'equals false');
    console.log('15. equals: OK');
}

// 16. compare
{
    const a = Buffer.from([1, 2]);
    const b = Buffer.from([1, 3]);
    assert(a.compare(b) < 0, 'compare instance');
    assert(Buffer.compare(a, b) < 0, 'compare static');
    console.log('16. compare: OK');
}

// 17. indexOf
{
    const b = Buffer.from('Hello World');
    assert(b.indexOf('World') === 6, 'indexOf string');
    assert(b.indexOf(0x6F) === 4, 'indexOf byte');
    assert(b.indexOf('xyz') === -1, 'indexOf not found');
    console.log('17. indexOf: OK');
}

// 18. includes
{
    const b = Buffer.from('Hello World');
    assert(b.includes('World') === true, 'includes true');
    assert(b.includes('xyz') === false, 'includes false');
    console.log('18. includes: OK');
}

// 19. concat
{
    const a = Buffer.from('Hello ');
    const b = Buffer.from('World');
    const c = Buffer.concat([a, b]);
    assert(c.toString() === 'Hello World', 'concat');
    console.log('19. concat: OK');
}

// 20. byteLength
{
    assert(Buffer.byteLength('hello') === 5, 'byteLength string');
    assert(Buffer.byteLength('deadbeef', 'hex') === 4, 'byteLength hex');
    console.log('20. byteLength: OK');
}

// 21. toJSON
{
    const b = Buffer.from([1, 2, 3]);
    const json = b.toJSON();
    assert(json.type === 'Buffer', 'toJSON type');
    assert(json.data[0] === 1 && json.data[2] === 3, 'toJSON data');
    console.log('21. toJSON: OK');
}

// 22. readUInt8/16BE/16LE/32BE/32LE
{
    const b = Buffer.from([0x12, 0x34, 0x56, 0x78]);
    assert(b.readUInt8(0) === 0x12, 'readUInt8');
    assert(b.readUInt16BE(0) === 0x1234, 'readUInt16BE');
    assert(b.readUInt16LE(0) === 0x3412, 'readUInt16LE');
    assert(b.readUInt32BE(0) === 0x12345678, 'readUInt32BE');
    assert(b.readUInt32LE(0) === 0x78563412, 'readUInt32LE');
    console.log('22. readUInt variants: OK');
}

// 23. writeUInt variants
{
    const b = Buffer.alloc(4);
    b.writeUInt16BE(0xABCD, 0);
    assert(b.readUInt16BE(0) === 0xABCD, 'writeUInt16BE roundtrip');
    b.writeUInt32LE(0x12345678, 0);
    assert(b.readUInt32LE(0) === 0x12345678, 'writeUInt32LE roundtrip');
    console.log('23. writeUInt variants: OK');
}

// 24. swap16/32
{
    const b = Buffer.from([0x01, 0x02, 0x03, 0x04]);
    b.swap16();
    assert(b[0] === 0x02 && b[1] === 0x01 && b[2] === 0x04 && b[3] === 0x03, 'swap16');
    b.swap32();
    assert(b[0] === 0x03 && b[1] === 0x04 && b[2] === 0x01 && b[3] === 0x02, 'swap32');
    console.log('24. swap: OK');
}

// 25. subarray (shares memory)
{
    const b = Buffer.from([1, 2, 3, 4, 5]);
    const s = b.subarray(1, 4);
    assert(s.length === 3, 'subarray length');
    assert(s[0] === 2, 'subarray value');
    s[0] = 99;
    assert(b[1] === 99, 'subarray shares memory');
    console.log('25. subarray: OK');
}

// 26. Buffer.allocUnsafe
{
    const b = Buffer.allocUnsafe(10);
    assert(b.length === 10, 'allocUnsafe length');
    assert(Buffer.isBuffer(b), 'allocUnsafe isBuffer');
    console.log('26. allocUnsafe: OK');
}

// 27. global Buffer
{
    assert(typeof globalThis.Buffer === 'function', 'global Buffer exists');
    const b = globalThis.Buffer.alloc(3);
    assert(Buffer.isBuffer(b), 'globalThis.Buffer.alloc works');
    console.log('27. global Buffer: OK');
}

// 28. default import
{
    const { default: Buf } = await import('node:buffer');
    assert(typeof Buf === 'function', 'default export');
    assert(typeof Buf.alloc === 'function', 'default export has static methods');
    console.log('28. default import: OK');
}

// 29. readInt32/writeInt32 (signed)
{
    const b = Buffer.alloc(4);
    b.writeInt32BE(-1, 0);
    assert(b.readInt32BE(0) === -1, 'writeInt32BE roundtrip -1');
    b.writeInt32LE(-256, 0);
    assert(b.readInt32LE(0) === -256, 'writeInt32LE roundtrip -256');
    console.log('29. readInt32/writeInt32: OK');
}

console.log('\nResults: ' + pass + ' passed, ' + fail + ' failed');
if (fail > 0) process.exit(1);
console.log('All Buffer tests passed!');
