/**
 * QNode Type Definitions
 *
 * Type declarations for QNode built-in modules:
 *   - node:process
 *   - node:fs / node:fs/promises
 */

// ============================================================
// node:process
// ============================================================

declare module "node:process" {

  interface ProcessErrno {
    readonly EINVAL: number;
    readonly EIO: number;
    readonly EACCES: number;
    readonly EEXIST: number;
    readonly ENOSPC: number;
    readonly ENOSYS: number;
    readonly EBUSY: number;
    readonly ENOENT: number;
    readonly EPERM: number;
    readonly EPIPE: number;
    readonly EBADF: number;
  }

  interface Process {
    /** CPU architecture: "x64" | "ia32" | "arm64" | "arm" | "unknown" */
    readonly arch: string;

    /** Operating system platform: "win32" | "darwin" | "linux" | "freebsd" | "unknown" */
    readonly platform: string;

    /** Command-line arguments (argv[0] = execPath, argv[1..] = script args) */
    readonly argv: string[];

    /** The original value of argv[0] when the process was launched */
    readonly argv0: string | undefined;

    /** Absolute path to the qnode executable */
    readonly execPath: string;

    /** Process ID */
    readonly pid: number;

    /** Exit code — can be set before process exits; number | undefined */
    exitCode: number | undefined;

    /** Environment variables */
    readonly env: Record<string, string>;

    /** QNode-specific execution flags (flags before the script name) */
    readonly execArgv: string[];

    /** Errno constants */
    readonly errno: ProcessErrno;

    /** Exit the process with an optional exit code (default 0) */
    exit(code?: number): never;

    /** Get the current working directory */
    cwd(): string;

    /** Change the current working directory */
    chdir(directory: string): void;

    /**
     * Get the group identity of the process.
     * Not supported on Windows — throws TypeError.
     */
    getgid(): number;

    /** High-resolution timestamp (milliseconds) */
    now(): number;
  }

  const process: Process;
  export default process;

  export const arch: string;
  export const platform: string;
  export const argv: string[];
  export const argv0: string | undefined;
  export const execPath: string;
  export const pid: number;
}

// ============================================================
// node:fs
// ============================================================

declare module "node:fs" {

  // ---- Options ----

  interface ReadFileOptions {
    encoding?: BufferEncoding;
    flag?: string;
  }

  interface WriteFileOptions {
    encoding?: BufferEncoding;
    flag?: string;
    mode?: number;
  }

  interface MkdirOptions {
    recursive?: boolean;
    mode?: number;
  }

  interface RmOptions {
    recursive?: boolean;
    force?: boolean;
  }

  // ---- Stats ----

  interface Stats {
    readonly dev: number;
    readonly ino: number;
    readonly mode: number;
    readonly nlink: number;
    readonly uid: number;
    readonly gid: number;
    readonly rdev: number;
    readonly size: number;
    readonly blksize: number;
    readonly blocks: number;
    readonly atimeMs: number;
    readonly mtimeMs: number;
    readonly ctimeMs: number;
    readonly birthtimeMs: number;
    readonly atime: number;
    readonly mtime: number;
    readonly ctime: number;
    readonly birthtime: number;

    isFile(): boolean;
    isDirectory(): boolean;
    isSymbolicLink(): boolean;
  }

  // ---- Constants ----

  interface FSConstants {
    // access
    readonly F_OK: number;
    readonly R_OK: number;
    readonly W_OK: number;
    readonly X_OK: number;
    // open flags
    readonly O_RDONLY: number;
    readonly O_WRONLY: number;
    readonly O_RDWR: number;
    readonly O_CREAT: number;
    readonly O_EXCL: number;
    readonly O_TRUNC: number;
    readonly O_APPEND: number;
    readonly O_SYNC: number | undefined;
    // file types
    readonly S_IFMT: number;
    readonly S_IFREG: number;
    readonly S_IFDIR: number;
    readonly S_IFCHR: number;
    readonly S_IFBLK: number | undefined;
    readonly S_IFIFO: number | undefined;
    readonly S_IFLNK: number | undefined;
    readonly S_IFSOCK: number | undefined;
    // permissions (Unix)
    readonly S_IRWXU: number | undefined;
    readonly S_IRUSR: number | undefined;
    readonly S_IWUSR: number | undefined;
    readonly S_IXUSR: number | undefined;
    readonly S_IRWXG: number | undefined;
    readonly S_IRGRP: number | undefined;
    readonly S_IWGRP: number | undefined;
    readonly S_IXGRP: number | undefined;
    readonly S_IRWXO: number | undefined;
    readonly S_IROTH: number | undefined;
    readonly S_IWOTH: number | undefined;
    readonly S_IXOTH: number | undefined;
    // copyFile
    readonly COPYFILE_EXCL: number;
    readonly COPYFILE_FICLONE: number;
  }

  // ---- Promise-based API (fs.promises) ----

  // ---- FileHandle ----

  interface FileHandleReadOptions {
    buffer?: ArrayBuffer;
    offset?: number;
    length?: number;
    position?: number | null;
  }

  interface FileHandleWriteBufferOptions {
    offset?: number;
    length?: number;
    position?: number | null;
  }

  interface FileHandleReadResult {
    bytesRead: number;
    buffer: ArrayBuffer;
  }

  interface FileHandleWriteResult {
    bytesWritten: number;
    buffer: ArrayBuffer | string;
  }

  interface FileHandle {
    /** The numeric file descriptor */
    readonly fd: number;

    /** Close the file handle */
    close(): Promise<void>;

    /**
     * Read data from file.
     * - read() — uses default 16KB buffer, reads from current position
     * - read(buffer, offset, length, position) — read into provided buffer
     * - read(options) — read with options object
     */
    read(): Promise<FileHandleReadResult>;
    read(buffer: ArrayBuffer, offset?: number, length?: number, position?: number | null): Promise<FileHandleReadResult>;
    read(options: FileHandleReadOptions): Promise<FileHandleReadResult>;

    /**
     * Write data to file.
     * - write(buffer, offset, length?, position?) — write ArrayBuffer
     * - write(buffer, options) — write ArrayBuffer with options
     * - write(string, position?, encoding?) — write string
     */
    write(buffer: ArrayBuffer, offset?: number, length?: number, position?: number | null): Promise<FileHandleWriteResult>;
    write(buffer: ArrayBuffer, options?: FileHandleWriteBufferOptions): Promise<FileHandleWriteResult>;
    write(data: string, position?: number | null, encoding?: BufferEncoding): Promise<FileHandleWriteResult>;

    /** Flush data to disk (fsync) */
    sync(): Promise<void>;
  }

  type OpenFlags = 'r' | 'r+' | 'w' | 'w+' | 'a' | 'a+' | 'wx' | 'w+x' | 'ax' | 'a+x' | number;

  interface FilesystemPromises {
    readFile(path: string, options: BufferEncoding | ReadFileOptions): Promise<string>;
    readFile(path: string, options?: ReadFileOptions): Promise<ArrayBuffer>;
    writeFile(path: string, data: string | ArrayBuffer, options?: BufferEncoding | WriteFileOptions): Promise<void>;
    appendFile(path: string, data: string | ArrayBuffer, options?: BufferEncoding | WriteFileOptions): Promise<void>;
    mkdir(path: string, options?: number | MkdirOptions): Promise<void>;
    readdir(path: string): Promise<string[]>;
    stat(path: string): Promise<Stats>;
    lstat(path: string): Promise<Stats>;
    unlink(path: string): Promise<void>;
    rename(oldPath: string, newPath: string): Promise<void>;
    rmdir(path: string): Promise<void>;
    rm(path: string, options?: RmOptions): Promise<void>;
    copyFile(src: string, dest: string): Promise<void>;
    realpath(path: string): Promise<string>;
    access(path: string, mode?: number): Promise<void>;
    chmod(path: string, mode: number): Promise<void>;
    open(path: string, flags?: OpenFlags, mode?: number): Promise<FileHandle>;
    readonly constants: FSConstants;
  }

  // ---- Filesystem object ----

  interface Filesystem {
    readFileSync(path: string, options: BufferEncoding | ReadFileOptions): string;
    readFileSync(path: string, options?: ReadFileOptions): ArrayBuffer;
    writeFileSync(path: string, data: string | ArrayBuffer, options?: BufferEncoding | WriteFileOptions): void;
    appendFileSync(path: string, data: string | ArrayBuffer, options?: BufferEncoding | WriteFileOptions): void;
    existsSync(path: string): boolean;
    mkdirSync(path: string, options?: number | MkdirOptions): void;
    readdirSync(path: string): string[];
    statSync(path: string): Stats;
    lstatSync(path: string): Stats;
    unlinkSync(path: string): void;
    renameSync(oldPath: string, newPath: string): void;
    rmdirSync(path: string): void;
    rmSync(path: string, options?: RmOptions): void;
    copyFileSync(src: string, dest: string): void;
    realpathSync(path: string): string;
    accessSync(path: string, mode?: number): void;
    chmodSync(path: string, mode: number): void;

    readonly constants: FSConstants;
    readonly promises: FilesystemPromises;
  }

  const fs: Filesystem;
  export default fs;

  export function readFileSync(path: string, options: BufferEncoding | ReadFileOptions): string;
  export function readFileSync(path: string, options?: ReadFileOptions): ArrayBuffer;
  export function writeFileSync(path: string, data: string | ArrayBuffer, options?: BufferEncoding | WriteFileOptions): void;
  export function appendFileSync(path: string, data: string | ArrayBuffer, options?: BufferEncoding | WriteFileOptions): void;
  export function existsSync(path: string): boolean;
  export function mkdirSync(path: string, options?: number | MkdirOptions): void;
  export function readdirSync(path: string): string[];
  export function statSync(path: string): Stats;
  export function lstatSync(path: string): Stats;
  export function unlinkSync(path: string): void;
  export function renameSync(oldPath: string, newPath: string): void;
  export function rmdirSync(path: string): void;
  export function rmSync(path: string, options?: RmOptions): void;
  export function copyFileSync(src: string, dest: string): void;
  export function realpathSync(path: string): string;
  export function accessSync(path: string, mode?: number): void;
  export function chmodSync(path: string, mode: number): void;
  export const constants: FSConstants;
}

// ============================================================
// node:fs/promises
// ============================================================

declare module "node:fs/promises" {

  // Reuse types from node:fs
  type ReadFileOptions = import("node:fs").ReadFileOptions;
  type WriteFileOptions = import("node:fs").WriteFileOptions;
  type MkdirOptions = import("node:fs").MkdirOptions;
  type RmOptions = import("node:fs").RmOptions;
  type Stats = import("node:fs").Stats;
  type FSConstants = import("node:fs").FSConstants;
  type OpenFlags = import("node:fs").OpenFlags;
  type FileHandle = import("node:fs").FileHandle;
  type FileHandleReadOptions = import("node:fs").FileHandleReadOptions;
  type FileHandleWriteBufferOptions = import("node:fs").FileHandleWriteBufferOptions;
  type FileHandleReadResult = import("node:fs").FileHandleReadResult;
  type FileHandleWriteResult = import("node:fs").FileHandleWriteResult;

  interface FilesystemPromisesApi {
    readFile(path: string, options: BufferEncoding | ReadFileOptions): Promise<string>;
    readFile(path: string, options?: ReadFileOptions): Promise<ArrayBuffer>;
    writeFile(path: string, data: string | ArrayBuffer, options?: BufferEncoding | WriteFileOptions): Promise<void>;
    appendFile(path: string, data: string | ArrayBuffer, options?: BufferEncoding | WriteFileOptions): Promise<void>;
    mkdir(path: string, options?: number | MkdirOptions): Promise<void>;
    readdir(path: string): Promise<string[]>;
    stat(path: string): Promise<Stats>;
    lstat(path: string): Promise<Stats>;
    unlink(path: string): Promise<void>;
    rename(oldPath: string, newPath: string): Promise<void>;
    rmdir(path: string): Promise<void>;
    rm(path: string, options?: RmOptions): Promise<void>;
    copyFile(src: string, dest: string): Promise<void>;
    realpath(path: string): Promise<string>;
    access(path: string, mode?: number): Promise<void>;
    chmod(path: string, mode: number): Promise<void>;
    open(path: string, flags?: OpenFlags, mode?: number): Promise<FileHandle>;
    readonly constants: FSConstants;
  }

  const promises: FilesystemPromisesApi;
  export default promises;

  export function open(path: string, flags?: OpenFlags, mode?: number): Promise<FileHandle>;
  export function readFile(path: string, options: BufferEncoding | ReadFileOptions): Promise<string>;
  export function readFile(path: string, options?: ReadFileOptions): Promise<ArrayBuffer>;
  export function writeFile(path: string, data: string | ArrayBuffer, options?: BufferEncoding | WriteFileOptions): Promise<void>;
  export function appendFile(path: string, data: string | ArrayBuffer, options?: BufferEncoding | WriteFileOptions): Promise<void>;
  export function mkdir(path: string, options?: number | MkdirOptions): Promise<void>;
  export function readdir(path: string): Promise<string[]>;
  export function stat(path: string): Promise<Stats>;
  export function lstat(path: string): Promise<Stats>;
  export function unlink(path: string): Promise<void>;
  export function rename(oldPath: string, newPath: string): Promise<void>;
  export function rmdir(path: string): Promise<void>;
  export function rm(path: string, options?: RmOptions): Promise<void>;
  export function copyFile(src: string, dest: string): Promise<void>;
  export function realpath(path: string): Promise<string>;
  export function access(path: string, mode?: number): void;
  export function chmod(path: string, mode: number): Promise<void>;
  export const constants: FSConstants;
}

// ============================================================
// node:buffer
// ============================================================

declare module "node:buffer" {

  type BufferEncoding = 'utf8' | 'utf-8' | 'ascii' | 'latin1' | 'binary' | 'hex' | 'base64' | 'base64url';

  interface BufferConstants {
    MAX_LENGTH: number;
    MAX_STRING_LENGTH: number;
  }

  interface Buffer extends Uint8Array {
    toString(encoding?: BufferEncoding, start?: number, end?: number): string;
    slice(start?: number, end?: number): Buffer;
    subarray(start?: number, end?: number): Buffer;
    write(string: string, offset?: number, length?: number, encoding?: BufferEncoding): number;
    fill(value: string | number, offset?: number, end?: number, encoding?: BufferEncoding): Buffer;
    copy(target: Uint8Array, targetStart?: number, sourceStart?: number, sourceEnd?: number): number;
    equals(otherBuffer: Uint8Array): boolean;
    compare(otherBuffer: Uint8Array): number;
    indexOf(value: string | number | Uint8Array, byteOffset?: number, encoding?: BufferEncoding): number;
    includes(value: string | number | Uint8Array, byteOffset?: number, encoding?: BufferEncoding): boolean;
    toJSON(): { type: 'Buffer'; data: number[] };
    swap16(): Buffer;
    swap32(): Buffer;
    swap64(): Buffer;
    readUInt8(offset?: number): number;
    writeUInt8(value: number, offset?: number): Buffer;
    readUInt16BE(offset?: number): number;
    readUInt16LE(offset?: number): number;
    writeUInt16BE(value: number, offset?: number): Buffer;
    writeUInt16LE(value: number, offset?: number): Buffer;
    readUInt32BE(offset?: number): number;
    readUInt32LE(offset?: number): number;
    writeUInt32BE(value: number, offset?: number): Buffer;
    writeUInt32LE(value: number, offset?: number): Buffer;
    readInt32BE(offset?: number): number;
    readInt32LE(offset?: number): number;
    writeInt32BE(value: number, offset?: number): Buffer;
    writeInt32LE(value: number, offset?: number): Buffer;
  }

  interface BufferConstructor {
    prototype: Buffer;
    alloc(size: number, fill?: string | number): Buffer;
    allocUnsafe(size: number): Buffer;
    from(string: string, encoding?: BufferEncoding): Buffer;
    from(arrayBuffer: ArrayBuffer, byteOffset?: number, length?: number): Buffer;
    from(array: ReadonlyArray<number>): Buffer;
    from(buffer: Uint8Array): Buffer;
    isBuffer(obj: any): obj is Buffer;
    concat(list: ReadonlyArray<Uint8Array>, totalLength?: number): Buffer;
    byteLength(string: string | ArrayBuffer | Uint8Array, encoding?: BufferEncoding): number;
    compare(buf1: Uint8Array, buf2: Uint8Array): number;
    readonly constants: BufferConstants;
  }

  const Buffer: BufferConstructor;
  export default Buffer;

  export { Buffer };
  export function SlowBuffer(size: number): Buffer;
}

// ============================================================
// Global process
// ============================================================

declare var process: import("node:process").Process;

// ============================================================
// Global Buffer
// ============================================================

declare var Buffer: import("node:buffer").BufferConstructor;

// ============================================================
// node:timers
// ============================================================

declare module "node:timers" {

  interface Timeout {}

  function setTimeout<TArgs extends any[]>(
    callback: (...args: TArgs) => void,
    ms?: number,
    ...args: TArgs
  ): Timeout;
  function clearTimeout(timeout: Timeout | number | undefined): void;
  function setInterval<TArgs extends any[]>(
    callback: (...args: TArgs) => void,
    ms?: number,
    ...args: TArgs
  ): Timeout;
  function clearInterval(timeout: Timeout | number | undefined): void;
  function setImmediate<TArgs extends any[]>(
    callback: (...args: TArgs) => void,
    ...args: TArgs
  ): Timeout;
  function clearImmediate(timeout: Timeout | number | undefined): void;

  export {
    setTimeout,
    clearTimeout,
    setInterval,
    clearInterval,
    setImmediate,
    clearImmediate,
  };
}

// ============================================================
// Global timers
// ============================================================

declare function setTimeout<TArgs extends any[]>(
  callback: (...args: TArgs) => void,
  ms?: number,
  ...args: TArgs
): import("node:timers").Timeout;
declare function clearTimeout(timeout: import("node:timers").Timeout | number | undefined): void;
declare function setInterval<TArgs extends any[]>(
  callback: (...args: TArgs) => void,
  ms?: number,
  ...args: TArgs
): import("node:timers").Timeout;
declare function clearInterval(timeout: import("node:timers").Timeout | number | undefined): void;
declare function setImmediate<TArgs extends any[]>(
  callback: (...args: TArgs) => void,
  ...args: TArgs
): import("node:timers").Timeout;
declare function clearImmediate(timeout: import("node:timers").Timeout | number | undefined): void;

// ============================================================
// node:child_process
// ============================================================

declare module "node:child_process" {

  type BufferEncoding = 'utf8' | 'utf-8' | 'ascii' | 'latin1' | 'binary' | 'hex' | 'base64' | 'base64url';

  // ---- Options ----

  interface ExecSyncOptions {
    cwd?: string;
    encoding?: BufferEncoding | 'buffer';
    env?: Record<string, string>;
    timeout?: number;
    maxBuffer?: number;
    input?: string | ArrayBuffer;
  }

  interface ExecOptions extends ExecSyncOptions {
    shell?: boolean | string;
  }

  interface SpawnOptions {
    cwd?: string;
    env?: Record<string, string>;
    shell?: boolean | string;
    stdio?: 'pipe' | 'ignore' | ('pipe' | 'ignore')[];
  }

  interface SpawnSyncOptions extends SpawnOptions {
    encoding?: BufferEncoding | 'buffer';
    maxBuffer?: number;
    timeout?: number;
    input?: string | ArrayBuffer;
  }

  interface ExecFileOptions extends SpawnOptions {
    encoding?: BufferEncoding | 'buffer';
    maxBuffer?: number;
    timeout?: number;
  }

  interface ForkOptions {
    cwd?: string;
    env?: Record<string, string>;
    execPath?: string;
    stdio?: 'pipe' | 'ignore' | ('pipe' | 'ignore')[];
  }

  // ---- Error ----

  interface ExecException extends Error {
    status?: number | null;
    code?: number | null;
    cmd?: string;
    stdout?: string | ArrayBuffer;
    stderr?: string | ArrayBuffer;
  }

  // ---- SpawnSyncResult ----

  interface SpawnSyncResult {
    pid: number;
    status: number | null;
    stdout: string | ArrayBuffer;
    stderr: string | ArrayBuffer;
    output?: unknown;
    error?: Error;
  }

  // ---- ChildProcess (async) ----

  interface ChildProcess {
    readonly pid: number;
    exitCode: number | null;
    signalCode: string | null;
    killed: boolean;
    stdin: Writable | null;
    stdout: Readable | null;
    stderr: Readable | null;
    stdio: (Writable | Readable | null)[];

    kill(signal?: string | number): boolean;
    on(event: 'exit', listener: (code: number | null, signal: string | null) => void): this;
    on(event: 'close', listener: (code: number | null, signal: string | null) => void): this;
    on(event: 'error', listener: (err: Error) => void): this;
    on(event: string, listener: (...args: any[]) => void): this;
    unref(): void;
    ref(): void;
  }

  interface Readable {
    read(): ArrayBuffer | null;
  }

  interface Writable {
    write(data: string | ArrayBuffer): boolean;
    end(): void;
  }

  // ---- Functions ----

  function execSync(command: string, options?: ExecSyncOptions): string;
  function execSync(command: string, options: ExecSyncOptions & { encoding: 'buffer' }): ArrayBuffer;

  function spawnSync(command: string, args?: string[], options?: SpawnSyncOptions): SpawnSyncResult;

  function execFileSync(file: string, args?: string[], options?: ExecFileOptions): string;
  function execFileSync(file: string, args: string[], options: ExecFileOptions & { encoding: 'buffer' }): ArrayBuffer;

  function spawn(command: string, args?: string[], options?: SpawnOptions): ChildProcess;

  function execFile(
    file: string,
    callback: (error: ExecException | null, stdout: string, stderr: string) => void
  ): ChildProcess;
  function execFile(
    file: string,
    args: string[],
    callback: (error: ExecException | null, stdout: string, stderr: string) => void
  ): ChildProcess;
  function execFile(
    file: string,
    args: string[],
    options: ExecFileOptions,
    callback: (error: ExecException | null, stdout: string, stderr: string) => void
  ): ChildProcess;

  function exec(
    command: string,
    callback: (error: ExecException | null, stdout: string, stderr: string) => void
  ): ChildProcess;
  function exec(
    command: string,
    options: ExecOptions,
    callback: (error: ExecException | null, stdout: string, stderr: string) => void
  ): ChildProcess;

  function fork(modulePath: string, args?: string[], options?: ForkOptions): ChildProcess;
}
