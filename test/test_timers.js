// Test node:timers module and global timer functions
import process from 'node:process';
import timers from 'node:timers';
import { setTimeout as setTimeout2, clearTimeout as clearTimeout2,
         setInterval as setInterval2, clearInterval as clearInterval2,
         setImmediate as setImmediate2, clearImmediate as clearImmediate2 }
    from 'node:timers';

let pass = 0, fail = 0;
function assert(cond, msg) {
    if (cond) { pass++; }
    else { fail++; console.log('FAIL: ' + msg); }
}

// 1. Global types
{
    assert(typeof setTimeout === 'function', 'global setTimeout');
    assert(typeof clearTimeout === 'function', 'global clearTimeout');
    assert(typeof setInterval === 'function', 'global setInterval');
    assert(typeof clearInterval === 'function', 'global clearInterval');
    assert(typeof setImmediate === 'function', 'global setImmediate');
    assert(typeof clearImmediate === 'function', 'global clearImmediate');
    console.log('1. globals: OK');
}

// 2. node:timers module exports
{
    assert(typeof timers === 'function', 'timers default is setTimeout');
    assert(typeof setTimeout2 === 'function', 'named setTimeout');
    assert(typeof clearTimeout2 === 'function', 'named clearTimeout');
    console.log('2. module exports: OK');
}

// 3. setTimeout basic
{
    setTimeout(function() {
        console.log('3. setTimeout basic: OK');
    }, 10);
}

// 4. setTimeout with args
{
    setTimeout(function(a, b) {
        assert(a === 42 && b === 'hi', 'setTimeout args');
        console.log('4. setTimeout args: OK');
    }, 20, 42, 'hi');
}

// 5. clearTimeout
{
    var called = false;
    var id = setTimeout(function() { called = true; }, 50);
    clearTimeout(id);
    setTimeout(function() {
        assert(!called, 'clearTimeout prevented call');
        console.log('5. clearTimeout: OK');
    }, 100);
}

// 6. setImmediate
{
    var immCalled = false;
    setImmediate(function(x) {
        immCalled = true;
        assert(x === 'now', 'setImmediate arg');
        console.log('6. setImmediate: OK');
    }, 'now');
}

// 7. clearImmediate
{
    var imm2Called = false;
    var imm2 = setImmediate(function() { imm2Called = true; });
    clearImmediate(imm2);
    setTimeout(function() {
        assert(!imm2Called, 'clearImmediate prevented call');
        console.log('7. clearImmediate: OK');
    }, 100);
}

// 8. setInterval + clearInterval
{
    var count = 0;
    var id = setInterval(function(n) {
        count++;
        assert(n === 99, 'setInterval arg');
        if (count >= 3) {
            clearInterval(id);
            console.log('8. setInterval: OK (3 ticks)');
        }
    }, 30, 99);
}

// 9. TypeError on non-function
{
    var gotError = false;
    try { setTimeout('not a function', 100); }
    catch (e) { gotError = e instanceof TypeError; }
    assert(gotError, 'setTimeout TypeError on non-function');
    console.log('9. TypeError: OK');
}

// 10. Final check after all timers
{
    setTimeout(function() {
        console.log('\nResults: ' + pass + ' passed, ' + fail + ' failed');
        if (fail > 0) process.exit(1);
        console.log('All timers tests passed!');
        process.exit(0);
    }, 500);
}
