const { spawn } = require('node:child_process');
const { userError } = require('./messages');

function runTool(executable, args, options = {}) {
    return new Promise((resolve, reject) => {
        const child = spawn(executable, args, {
            cwd: options.cwd, signal: options.signal, shell: false, windowsHide: true,
            stdio: [options.input === undefined ? 'ignore' : 'pipe', 'pipe', 'pipe']
        });
        let stdout = '', stderr = '', finished = false;
        const timer = setTimeout(() => {
            child.kill(); finish(new Error('LiarsoftTool timed out'));
        }, 60000);
        function finish(error, result) {
            if (finished) return;
            finished = true; clearTimeout(timer);
            if (error) reject(error); else resolve(result);
        }
        function collect(name, value) {
            if (name === 'stdout') stdout += value; else stderr += value;
            if (Buffer.byteLength(stdout) + Buffer.byteLength(stderr) > (options.maxOutput || 8 * 1024 * 1024)) {
                child.kill(); finish(new Error('LiarsoftTool output is too large'));
            }
        }
        child.stdout.setEncoding('utf8'); child.stderr.setEncoding('utf8');
        child.stdout.on('data', value => collect('stdout', value));
        child.stderr.on('data', value => collect('stderr', value));
        child.once('error', error => finish(error));
        if (options.input !== undefined) {
            child.stdin.on('error', error => { if (error.code !== 'EPIPE') finish(error); });
            child.stdin.end(options.input, 'utf8');
        }
        child.once('close', (code, signal) => signal
            ? finish(userError('LiarsoftTool stopped ({0})', signal))
            : finish(null, { code, stdout, stderr }));
    });
}
module.exports = { runTool };
