import { spawn } from 'node:child_process';
import process from 'node:process';

// Test 1: ls (may not exist on Windows)
var cp1 = spawn('ls');
cp1.on('error', function(e) { console.log('ls error: ' + e); });
cp1.stdout.on('data', function(d) { console.log('ls ok'); });
cp1.on('exit', function(code) {
    console.log('ls exit: ' + code);

    // Test 2: cmd.exe /c dir (always exists on Windows)
    var cp2 = spawn('cmd', ['/c', 'echo', 'hello_windows']);
    cp2.stdout.on('data', function(d) { console.log('cmd ok: ' + d.trim()); });
    cp2.stderr.on('data', function(d) { console.log('cmd err: ' + d); });
    cp2.on('exit', function(code2) {
        console.log('cmd exit: ' + code2);
        process.exit(0);
    });
});
