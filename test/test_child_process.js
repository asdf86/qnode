// Test node:child_process module — comprehensive
import process from 'node:process';
import { execSync, exec, spawnSync, execFileSync, spawn, execFile, fork } from 'node:child_process';
import child_process from 'node:child_process';

let pass = 0, fail = 0;
function assert(cond, msg) {
    if (cond) { pass++; }
    else { fail++; console.log('FAIL: ' + msg); }
}

// 1. Module exports
{
    assert(typeof execSync === 'function', 'execSync is function');
    assert(typeof exec === 'function', 'exec is function');
    assert(typeof spawnSync === 'function', 'spawnSync is function');
    assert(typeof execFileSync === 'function', 'execFileSync is function');
    assert(typeof spawn === 'function', 'spawn is function');
    assert(typeof execFile === 'function', 'execFile is function');
    assert(typeof fork === 'function', 'fork is function');
    assert(typeof child_process === 'function', 'default export is execSync');
    console.log('1. exports: OK');
}

// 2. execSync basic
{
    var out = execSync('echo hello world');
    assert(out.indexOf('hello world') >= 0, 'execSync captures output');
    console.log('2. execSync basic: OK');
}

// 3. execSync returns string by default
{
    var out2 = execSync('echo test');
    assert(typeof out2 === 'string', 'returns string');
    console.log('3. returns string: OK');
}

// 4. execSync with encoding buffer
{
    var buf = execSync('echo buf', { encoding: 'buffer' });
    assert(buf instanceof ArrayBuffer, 'returns ArrayBuffer with encoding buffer');
    console.log('4. encoding buffer: OK');
}

// 5. execSync error on failure
{
    try {
        execSync('exit 42');
        assert(false, 'should have thrown');
    } catch (e) {
        assert(e.status === 42, 'error.status = 42');
        assert(typeof e.cmd === 'string', 'error.cmd is string');
        console.log('5. error handling: OK');
    }
}

// 6. execSync with cwd
{
    var out3 = execSync('pwd', { cwd: '/' });
    var trimmed = out3.trim();
    assert(trimmed === '/' || trimmed === '\\' || trimmed.length <= 4,
           'cwd option works (got: ' + trimmed + ')');
    console.log('6. cwd option: OK');
}

// 7. exec async
{
    exec('echo async test', function(err, stdout, stderr) {
        assert(err === null, 'exec no error');
        assert(stdout.indexOf('async test') >= 0, 'exec captures output');
        console.log('7. exec async: OK');
    });
}

// 8. exec async error
{
    exec('exit 1', function(err, stdout, stderr) {
        assert(err !== null, 'exec error is not null');
        assert(err.status === 1, 'exec error.status = 1');
        console.log('8. exec async error: OK');
    });
}

// 9. exec with options as second arg
{
    exec('echo opts_test', { encoding: 'utf8' }, function(err, stdout, stderr) {
        assert(stdout.indexOf('opts_test') >= 0, 'exec with options');
        console.log('9. exec with options: OK');
    });
}

// 10. spawnSync basic
{
    var r = spawnSync('echo', ['spawn', 'sync']);
    assert(r.status === 0, 'spawnSync status 0');
    assert(typeof r.stdout === 'string', 'spawnSync stdout is string');
    assert(r.stdout.indexOf('spawn sync') >= 0, 'spawnSync captures output');
    console.log('10. spawnSync basic: OK');
}

// 11. spawnSync with shell
{
    var r2 = spawnSync('echo shell_test', undefined, { shell: true });
    assert(r2.status === 0, 'spawnSync shell status 0');
    assert(r2.stdout.indexOf('shell_test') >= 0, 'spawnSync shell output');
    console.log('11. spawnSync shell: OK');
}

// 12. spawnSync error
{
    var r3 = spawnSync('exit', ['5'], { shell: true });
    assert(r3.status === 5, 'spawnSync error status = 5');
    console.log('12. spawnSync error: OK');
}

// 13. spawnSync buffer encoding
{
    var r4 = spawnSync('echo', ['buf_test'], { encoding: 'buffer' });
    assert(r4.stdout instanceof ArrayBuffer, 'spawnSync buffer encoding');
    console.log('13. spawnSync buffer: OK');
}

// 14. execFileSync basic
{
    var ef = execFileSync('echo', ['execfile', 'sync']);
    assert(typeof ef === 'string', 'execFileSync returns string');
    assert(ef.indexOf('execfile sync') >= 0, 'execFileSync output');
    console.log('14. execFileSync basic: OK');
}

// 15. execFileSync error
{
    try {
        execFileSync('nonexistent_cmd_xyz_123');
        assert(false, 'execFileSync should throw');
    } catch (e) {
        assert(e.status !== undefined, 'execFileSync error has status');
        console.log('15. execFileSync error: OK');
    }
}

// 16. spawn async with events
{
    var cp = spawn('echo', ['hello', 'spawn']);
    assert(typeof cp.pid === 'number', 'spawn returns pid');
    assert(cp.pid > 0, 'pid > 0');
    assert(cp.stdout !== null, 'stdout is not null');
    assert(cp.stderr !== null, 'stderr is not null');
    assert(cp.stdin !== null, 'stdin is not null');

    var gotData = false;
    var gotExit = false;
    cp.on('data', function(chunk, src) {
        gotData = true;
    });
    cp.on('exit', function(code, signal) {
        gotExit = true;
        assert(code === 0, 'spawn exit code 0');
        assert(signal === null, 'spawn signal null');
        console.log('16. spawn async: OK (data=' + gotData + ' exit=' + gotExit + ')');
    });
}

// 17. spawn with shell
{
    var cp2 = spawn('echo shell_spawn', undefined, { shell: true });
    var exited = false;
    cp2.on('exit', function(code) {
        assert(code === 0, 'spawn shell exit 0');
        exited = true;
        console.log('17. spawn shell: OK');
    });
}

// 18. execFile async
{
    execFile('echo', ['execfile', 'async'], function(err, stdout, stderr) {
        assert(err === null, 'execFile no error');
        assert(stdout.indexOf('execfile async') >= 0, 'execFile output');
        console.log('18. execFile async: OK');
    });
}

// 19. spawn .kill()
{
    var cp3 = spawn('sleep', ['10']);
    var result = cp3.kill();
    assert(typeof result === 'boolean', 'kill returns boolean');
    assert(cp3.killed === true, 'killed flag set');
    console.log('19. spawn kill: OK');
}

// Final check
setTimeout(function() {
    console.log('\nResults: ' + pass + ' passed, ' + fail + ' failed');
    if (fail > 0) process.exit(1);
    console.log('All child_process tests passed!');
    process.exit(0);
}, 2000);
