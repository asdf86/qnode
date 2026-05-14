import fs from 'node:fs';
import { readFileSync, writeFileSync, existsSync } from 'node:fs';

const testDir = './_fs_test_tmp';

// Clean up any previous test
try { fs.rmSync(testDir, { recursive: true, force: true }); } catch(e) {}

// 1. mkdirSync
fs.mkdirSync(testDir);
console.log('1. mkdirSync: OK');

// 2. existsSync
console.log('2. existsSync:', existsSync(testDir));

// 3. writeFileSync (string)
writeFileSync(testDir + '/test.txt', 'Hello QNode', 'utf8');
console.log('3. writeFileSync: OK');

// 4. readFileSync (string)
const data = readFileSync(testDir + '/test.txt', 'utf8');
console.log('4. readFileSync:', data);

// 5. readFileSync (ArrayBuffer)
const buf = readFileSync(testDir + '/test.txt');
console.log('5. readFileSync ArrayBuffer byteLength:', buf.byteLength);

// 6. appendFileSync
fs.appendFileSync(testDir + '/test.txt', ' World', 'utf8');
const data2 = readFileSync(testDir + '/test.txt', 'utf8');
console.log('6. appendFileSync:', data2);

// 7. statSync
const stat = fs.statSync(testDir + '/test.txt');
console.log('7. statSync: isFile=' + stat.isFile() + ', isDirectory=' + stat.isDirectory() + ', size=' + stat.size);

// 8. readdirSync
const entries = fs.readdirSync(testDir);
console.log('8. readdirSync:', entries);

// 9. mkdirSync recursive
fs.mkdirSync(testDir + '/a/b/c', { recursive: true });
console.log('9. mkdirSync recursive:', existsSync(testDir + '/a/b/c'));

// 10. copyFileSync
fs.copyFileSync(testDir + '/test.txt', testDir + '/copy.txt');
console.log('10. copyFileSync:', existsSync(testDir + '/copy.txt'));

// 11. renameSync
fs.renameSync(testDir + '/copy.txt', testDir + '/renamed.txt');
console.log('11. renameSync:', existsSync(testDir + '/renamed.txt'));

// 12. realpathSync
const rp = fs.realpathSync(testDir);
console.log('12. realpathSync:', rp);

// 13. accessSync
fs.accessSync(testDir);
console.log('13. accessSync: OK');

// 14. unlinkSync
fs.unlinkSync(testDir + '/test.txt');
fs.unlinkSync(testDir + '/renamed.txt');
console.log('14. unlinkSync: OK');

// 15. rmSync recursive
fs.rmSync(testDir, { recursive: true, force: true });
console.log('15. rmSync recursive:', !existsSync(testDir));

// 16. constants
console.log('16. constants F_OK:', fs.constants.F_OK, 'R_OK:', fs.constants.R_OK, 'O_RDONLY:', fs.constants.O_RDONLY);

// 17. existsSync on non-existent path
console.log('17. existsSync non-existent:', !existsSync('/nonexistent_path_xyz'));

// 18. lstatSync on directory
fs.mkdirSync(testDir);
const lstat = fs.lstatSync(testDir);
console.log('18. lstatSync: isDirectory=' + lstat.isDirectory());
fs.rmdirSync(testDir);

console.log('\nAll fs tests passed!');
