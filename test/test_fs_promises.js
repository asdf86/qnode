// Test fs.promises API
import fs from 'node:fs';
import { readFile, writeFile, mkdir, readdir, stat, unlink, rename, rm, copyFile, realpath, access } from 'node:fs/promises';

const testDir = './_fs_test_tmp2';

// Clean up
try { fs.rmSync(testDir, { recursive: true, force: true }); } catch(e) {}

async function run() {
    // 1. mkdir (promise)
    await mkdir(testDir);
    console.log('1. mkdir: OK');

    // 2. writeFile (promise)
    await writeFile(testDir + '/test.txt', 'Hello Promise', 'utf8');
    console.log('2. writeFile: OK');

    // 3. readFile (promise, string)
    const data = await readFile(testDir + '/test.txt', 'utf8');
    console.log('3. readFile:', data);

    // 4. readFile (promise, ArrayBuffer)
    const buf = await readFile(testDir + '/test.txt');
    console.log('4. readFile ArrayBuffer byteLength:', buf.byteLength);

    // 5. stat (promise)
    const s = await stat(testDir + '/test.txt');
    console.log('5. stat: isFile=' + s.isFile() + ', size=' + s.size);

    // 6. readdir (promise)
    const entries = await readdir(testDir);
    console.log('6. readdir:', entries);

    // 7. mkdir recursive (promise)
    await mkdir(testDir + '/a/b/c', { recursive: true });
    console.log('7. mkdir recursive:', fs.existsSync(testDir + '/a/b/c'));

    // 8. copyFile (promise)
    await copyFile(testDir + '/test.txt', testDir + '/copy.txt');
    console.log('8. copyFile:', fs.existsSync(testDir + '/copy.txt'));

    // 9. rename (promise)
    await rename(testDir + '/copy.txt', testDir + '/renamed.txt');
    console.log('9. rename:', fs.existsSync(testDir + '/renamed.txt'));

    // 10. realpath (promise)
    const rp = await realpath(testDir);
    console.log('10. realpath:', rp);

    // 11. access (promise)
    await access(testDir);
    console.log('11. access: OK');

    // 12. unlink (promise)
    await unlink(testDir + '/test.txt');
    await unlink(testDir + '/renamed.txt');
    console.log('12. unlink: OK');

    // 13. rm recursive (promise)
    await rm(testDir, { recursive: true, force: true });
    console.log('13. rm recursive:', !fs.existsSync(testDir));

    // 14. error handling - readFile on non-existent
    try {
        await readFile('/nonexistent_xyz.txt');
        console.log('14. error: should have thrown');
    } catch (e) {
        console.log('14. error caught:', e.code);
    }

    // 15. fs.promises on main fs object
    const data2 = await fs.promises.readFile('./test/test_fs_promises.js', 'utf8');
    console.log('15. fs.promises.readFile: first 30 chars:', data2.substring(0, 30));

    // 16. named import from node:fs/promises already used above

    console.log('\nAll fs.promises tests passed!');
}

run().catch(e => { console.error('Test failed:', e); process.exit(1); });
