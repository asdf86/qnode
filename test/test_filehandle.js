// Test FileHandle API (fs.promises.open / filehandle.read/write/close/sync)
import fs from 'node:fs';
import { open } from 'node:fs/promises';

const testFile = './_fh_test.dat';

function ab2str(buf, len) {
    const u8 = new Uint8Array(buf);
    let s = '';
    for (let i = 0; i < len; i++) s += String.fromCharCode(u8[i]);
    return s;
}

function str2ab(s) {
    const ab = new ArrayBuffer(s.length);
    const u8 = new Uint8Array(ab);
    for (let i = 0; i < s.length; i++) u8[i] = s.charCodeAt(i);
    return ab;
}

async function run() {
    // 1. open with 'w' flag
    let fh = await open(testFile, 'w');
    console.log('1. open(w): fd=' + fh.fd);

    // 2. write string
    {
        const r = await fh.write('Hello ');
        console.log('2. write(string): bytesWritten=' + r.bytesWritten);
    }

    // 3. write string with position
    {
        const r = await fh.write('World', 6);
        console.log('3. write(string, position): bytesWritten=' + r.bytesWritten);
    }

    // 4. sync
    await fh.sync();
    console.log('4. sync: OK');

    // 5. close
    await fh.close();
    console.log('5. close: OK');

    // 6. open with 'r' flag
    fh = await open(testFile, 'r');
    console.log('6. open(r): fd=' + fh.fd);

    // 7. read with buffer (positional args)
    {
        const buf = new ArrayBuffer(16);
        const r = await fh.read(buf, 0, 16, 0);
        console.log('7. read(buf,0,16,0): bytesRead=' + r.bytesRead +
                     ', content="' + ab2str(r.buffer, r.bytesRead) + '"');
    }

    // 8. read with options object
    {
        const buf = new ArrayBuffer(16);
        const r = await fh.read({ buffer: buf, offset: 0, length: 5, position: 0 });
        console.log('8. read({options}): bytesRead=' + r.bytesRead +
                     ', content="' + ab2str(r.buffer, r.bytesRead) + '"');
    }

    // 9. read with default buffer
    {
        const r = await fh.read();
        console.log('9. read(): bytesRead=' + r.bytesRead + ', bufferLen=' + r.buffer.byteLength);
    }
    await fh.close();

    // 10. write ArrayBuffer
    fh = await open(testFile, 'w');
    {
        const data = str2ab('ArrayBuf');
        const r = await fh.write(data);
        console.log('10. write(ArrayBuffer): bytesWritten=' + r.bytesWritten);
    }
    await fh.close();

    // 11. write ArrayBuffer with offset/length/position
    fh = await open(testFile, 'r+');
    {
        const data = str2ab('XX-Hello-YY');
        const r = await fh.write(data, 3, 5, 0);
        console.log('11. write(buf,offset,len,pos): bytesWritten=' + r.bytesWritten);
    }
    await fh.close();

    // 12. verify
    fh = await open(testFile, 'r');
    {
        const buf = new ArrayBuffer(32);
        const r = await fh.read(buf, 0, 32, 0);
        console.log('12. verify: content="' + ab2str(r.buffer, r.bytesRead) + '"');
    }
    await fh.close();

    // 13. double close should error
    fh = await open(testFile, 'r');
    await fh.close();
    try {
        await fh.read();
        console.log('13. after close: should have thrown');
    } catch (e) {
        console.log('13. after close: caught "' + e.message + '"');
    }

    // 14. open non-existent
    try {
        await open('./nonexistent_fh.dat', 'r');
        console.log('14. open(nonexistent): should have thrown');
    } catch (e) {
        console.log('14. open(nonexistent): code=' + e.code);
    }

    // 15. fs.promises.open
    fh = await fs.promises.open(testFile, 'r');
    {
        const buf = new ArrayBuffer(32);
        const r = await fh.read(buf, 0, 32, 0);
        console.log('15. fs.promises.open: bytesRead=' + r.bytesRead);
    }
    await fh.close();

    // cleanup
    fs.unlinkSync(testFile);

    console.log('\nAll FileHandle tests passed!');
}

run().catch(e => { console.error('Test failed:', e); process.exit(1); });
