#!/usr/bin/env node
/* eslint-disable no-console */
const crypto = require('crypto');
const fs = require('fs');
const net = require('net');
const path = require('path');
const { spawn } = require('child_process');
const readline = require('readline');

function bin(name) {
  return process.platform === 'win32' ? `${name}.cmd` : name;
}

function localBin(rootDir, name) {
  const p = path.join(rootDir, 'node_modules', '.bin', bin(name));
  if (!fs.existsSync(p)) {
    throw new Error(`Missing local binary: ${p}. Run npm install first.`);
  }
  return p;
}

function electronExecutable(rootDir) {
  const base = path.join(rootDir, 'node_modules', 'electron', 'dist');
  if (process.platform === 'win32') {
    return path.join(base, 'electron.exe');
  }
  if (process.platform === 'darwin') {
    return path.join(base, 'Electron.app', 'Contents', 'MacOS', 'Electron');
  }
  return path.join(base, 'electron');
}

function hasPackagedElectronApp(rootDir) {
  const distDir = path.join(rootDir, 'dist');
  if (!fs.existsSync(distDir)) {
    return false;
  }
  const platform = process.platform;
  let found = false;
  const walk = (dir) => {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
      const full = path.join(dir, entry.name);
      if (platform === 'win32' && entry.isFile()) {
        if (entry.name === 'HoroStudio.exe' || entry.name === 'Horo Studio.exe' || entry.name === 'horo-studio.exe') {
          found = true;
          return;
        }
      }
      if (platform === 'darwin' && entry.isDirectory() && (entry.name === 'HoroStudio.app' || entry.name === 'Horo Studio.app')) {
        found = true;
        return;
      }
      if (platform === 'linux' && entry.isFile() && (entry.name === 'horo-studio' || entry.name === 'HoroStudio')) {
        found = true;
        return;
      }
      if (!found && entry.isDirectory()) {
        walk(full);
      }
      if (found) {
        return;
      }
    }
  };
  walk(distDir);
  return found;
}

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function isTcpPortOpen(host, port, timeoutMs = 700) {
  return new Promise((resolve) => {
    const socket = new net.Socket();
    let settled = false;

    const done = (ok) => {
      if (settled) {
        return;
      }
      settled = true;
      socket.destroy();
      resolve(ok);
    };

    socket.setTimeout(timeoutMs);
    socket.once('connect', () => done(true));
    socket.once('timeout', () => done(false));
    socket.once('error', () => done(false));
    socket.connect(port, host);
  });
}

async function waitForTcpPort(host, port, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (await isTcpPortOpen(host, port)) {
      return true;
    }
    await sleep(300);
  }
  return false;
}

function engineRootDir(studioRootDir) {
  return path.resolve(studioRootDir, '..');
}

function collectEngineBinDirs(engineRoot) {
  const dirs = [];
  const pushIfExists = (candidate) => {
    if (fs.existsSync(candidate)) {
      dirs.push(candidate);
    }
  };

  pushIfExists(path.join(engineRoot, 'build', 'debug', 'bin'));
  pushIfExists(path.join(engineRoot, 'build', 'release', 'bin'));
  pushIfExists(path.join(engineRoot, 'build', 'relwithdebinfo', 'bin'));
  pushIfExists(path.join(engineRoot, 'bin'));

  const buildRoot = path.join(engineRoot, 'build');
  if (fs.existsSync(buildRoot)) {
    for (const presetDir of fs.readdirSync(buildRoot, { withFileTypes: true })) {
      if (!presetDir.isDirectory()) {
        continue;
      }
      const binRoot = path.join(buildRoot, presetDir.name, 'bin');
      if (!fs.existsSync(binRoot)) {
        continue;
      }
      pushIfExists(binRoot);
      for (const cfgDir of fs.readdirSync(binRoot, { withFileTypes: true })) {
        if (cfgDir.isDirectory()) {
          pushIfExists(path.join(binRoot, cfgDir.name));
        }
      }
    }
  }

  return [...new Set(dirs)];
}

function findEngineExecutable(studioRootDir) {
  const exeName = process.platform === 'win32' ? 'HoroEngine.exe' : 'HoroEngine';
  const candidates = collectEngineBinDirs(engineRootDir(studioRootDir)).map((binDir) => path.join(binDir, exeName));
  const found = candidates.find((candidate) => fs.existsSync(candidate));
  return found || null;
}

function engineExecutableCandidates(studioRootDir) {
  const exeName = process.platform === 'win32' ? 'HoroEngine.exe' : 'HoroEngine';
  return collectEngineBinDirs(engineRootDir(studioRootDir)).map((binDir) => path.join(binDir, exeName));
}

async function ensureEngineRunning(studioRootDir, debugLogs = false, timeoutMs = 20000) {
  if (await isTcpPortOpen('127.0.0.1', 39281)) {
    console.log('==> engine already running (MCP:39281)');
    return;
  }

  const executable = findEngineExecutable(studioRootDir);
  if (!executable) {
    console.warn('[horo-studio] Engine executable not found. Checked:');
    for (const candidate of engineExecutableCandidates(studioRootDir)) {
      console.warn(`- ${candidate}`);
    }
    console.warn('[horo-studio] Required backend binary: HoroEngine(.exe)');
    console.warn('[horo-studio] UI will start, but it may show "Engine not connected".');
    return;
  }

  console.log(`==> starting engine backend: ${executable}`);
  const env = { ...process.env, MONOLITH_GLFW_VISIBLE: process.env.MONOLITH_GLFW_VISIBLE || '0' };
  // Use the bin/ directory as cwd so the engine resolves its SDK paths correctly.
  // The engine discovers sdk/ via current_path(), looking for ../sdk, ./sdk, etc.
  const child = spawn(executable, [], {
    cwd: path.dirname(executable),
    env,
    detached: !debugLogs,
    stdio: debugLogs ? 'inherit' : 'ignore',
    shell: false,
  });
  if (!debugLogs) {
    child.unref();
  }

  const ready = await waitForTcpPort('127.0.0.1', 39281, timeoutMs);
  if (ready) {
    console.log('==> engine connected (MCP:39281)');
    return;
  }

  console.warn('[horo-studio] Engine process started but MCP is not ready yet.');
  console.warn('[horo-studio] Studio will continue launching; connection should appear once engine finishes booting.');
}

function launchDevElectron(rootDir, debugLogs = false) {
  const exe = electronExecutable(rootDir);
  if (!fs.existsSync(exe)) {
    throw new Error(`Electron executable not found: ${exe}`);
  }
  const env = { ...process.env };
  delete env.ELECTRON_RUN_AS_NODE;
  if (debugLogs) {
    env.ELECTRON_ENABLE_LOGGING = '1';
    env.THEIA_LOG_LEVEL = env.THEIA_LOG_LEVEL || 'debug';
  }
  const child = spawn(exe, ['.'], {
    cwd: rootDir,
    env,
    detached: !debugLogs,
    stdio: debugLogs ? 'inherit' : 'ignore',
    shell: false,
  });
  if (!debugLogs) {
    child.unref();
  }
}

function stepBar(step, total, width = 16) {
  const done = Math.max(0, Math.min(width, Math.round((step / total) * width)));
  return `[${'#'.repeat(done)}${'-'.repeat(width - done)}]`;
}

function run(command, args, options = {}) {
  return new Promise((resolve, reject) => {
    const isWindowsCmd = process.platform === 'win32' && /\.(cmd|bat)$/i.test(command);
    const child = spawn(command, args, {
      cwd: options.cwd || process.cwd(),
      env: options.env || process.env,
      shell: isWindowsCmd,
      stdio: ['ignore', 'pipe', 'pipe'],
    });

    let output = '';
    const maxCapturedBytes = typeof options.maxCapturedBytes === 'number' ? options.maxCapturedBytes : 512 * 1024;
    let lastOutputAt = Date.now();
    const startedAt = Date.now();
    let showingProgress = false;

    const ticker = setInterval(() => {
      if (!options.progressText) {
        return;
      }
      if (Date.now() - lastOutputAt < 1200) {
        return;
      }
      const elapsedSec = Math.floor((Date.now() - startedAt) / 1000);
      process.stdout.write(`\r   ${options.progressText} ... ${elapsedSec}s`);
      showingProgress = true;
    }, 1000);

    const flushProgressLine = () => {
      if (!showingProgress) {
        return;
      }
      process.stdout.write('\r');
      process.stdout.write(' '.repeat(100));
      process.stdout.write('\r');
      showingProgress = false;
    };

    const onData = (chunk, stream) => {
      const text = chunk.toString();
      output += text;
      if (output.length > maxCapturedBytes) {
        output = output.slice(output.length - maxCapturedBytes);
      }
      lastOutputAt = Date.now();
      flushProgressLine();
      stream.write(chunk);
    };

    child.stdout.on('data', (chunk) => onData(chunk, process.stdout));
    child.stderr.on('data', (chunk) => onData(chunk, process.stderr));
    child.on('error', (error) => {
      clearInterval(ticker);
      flushProgressLine();
      reject({ code: 1, output, error });
    });
    child.on('close', (code) => {
      clearInterval(ticker);
      flushProgressLine();
      if (code === 0) {
        resolve({ code: 0, output });
      } else {
        reject({ code: code || 1, output });
      }
    });
  });
}

function askYesNo(question, defaultYes = true) {
  return new Promise((resolve) => {
    if (!process.stdin.isTTY || !process.stdout.isTTY) {
      resolve(null);
      return;
    }
    const rl = readline.createInterface({
      input: process.stdin,
      output: process.stdout,
    });
    const suffix = defaultYes ? ' [Y/n]' : ' [y/N]';
    rl.question(`${question}${suffix} `, (answer) => {
      rl.close();
      const normalized = String(answer || '').trim().toLowerCase();
      if (!normalized) {
        resolve(defaultYes);
        return;
      }
      resolve(normalized === 'y' || normalized === 'yes' || normalized === 'e' || normalized === 'evet');
    });
  });
}

async function installSpectreWithWinget() {
  if (process.platform !== 'win32') {
    return false;
  }

  const setupExe = 'C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\setup.exe';
  const vswhereExe = 'C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe';
  const spectreComponentId = 'Microsoft.VisualStudio.Component.VC.Runtimes.x86.x64.Spectre';

  if (fs.existsSync(setupExe) && fs.existsSync(vswhereExe)) {
    try {
      const vswhereResult = await run(vswhereExe, ['-products', '*', '-format', 'json']);
      const instances = JSON.parse(vswhereResult.output || '[]');
      if (Array.isArray(instances) && instances.length > 0) {
        console.log('[horo-studio] Mevcut Visual Studio instance\'lari uzerinden Spectre bileşeni ekleniyor...');
        for (const instance of instances) {
          const installPath = instance && instance.installationPath;
          if (!installPath || !fs.existsSync(installPath)) {
            continue;
          }
          await run(setupExe, [
            'modify',
            '--installPath', installPath,
            '--add', spectreComponentId,
            '--passive',
            '--norestart',
          ], {
            progressText: `Installing Spectre libs for ${installPath}`,
          });
        }
        return true;
      }
    } catch {
      // Fallback to winget below.
    }
  }

  try {
    await run('winget', ['--version']);
  } catch {
    console.error('[horo-studio] winget bulunamadi. Lutfen Visual Studio Installer ile manuel kurulum yap.');
    return false;
  }

  console.log('[horo-studio] winget ile Spectre kutuphaneleri kuruluyor...');
  const override = `--wait --passive --norestart --add ${spectreComponentId}`;
  const candidates = [
    'Microsoft.VisualStudio.2022.Community',
    'Microsoft.VisualStudio.2022.BuildTools',
  ];
  for (const candidateId of candidates) {
    try {
      await run('winget', [
        'install',
        '--id', candidateId,
        '--exact',
        '--accept-package-agreements',
        '--accept-source-agreements',
        '--override', override,
      ], {
        progressText: `Installing Spectre libs via ${candidateId}`,
      });
    } catch {
      // Try the next candidate.
    }
  }
  return true;
}

function hasArg(name) {
  return process.argv.slice(2).includes(name);
}

async function runStep(step, total, label, command, args, options = {}) {
  console.log(`==> ${stepBar(step, total)} (${step}/${total}) ${label}`);
  const start = Date.now();
  try {
    await run(command, args, { ...options, progressText: label });
    const elapsed = ((Date.now() - start) / 1000).toFixed(1);
    console.log(`==> completed (${elapsed}s): ${label}`);
  } catch (failure) {
    const output = String((failure && failure.output) || '');
    if (/MSB8040/i.test(output)) {
      console.error('');
      console.error('[horo-studio] MSB8040: Visual Studio Spectre libraries are missing.');
      console.error('Open Visual Studio Installer -> Modify -> Individual components and install:');
      console.error("- MSVC v143 - VS 2022 C++ x64/x86 Spectre-mitigated libs (Latest)");
      console.error("- MSVC v143 - VS 2022 C++ ARM64 Spectre-mitigated libs (Latest) (optional)");
      const answer = hasArg('--auto-install-spectre')
        ? true
        : await askYesNo('Winget ile otomatik kurulum denensin mi?');
      const shouldInstall = answer === null ? true : answer;
      if (shouldInstall) {
        try {
          if (answer === null) {
            console.error('[horo-studio] Interaktif terminal algilanamadi, otomatik kurulum deneniyor.');
          }
          const installed = await installSpectreWithWinget();
          if (installed) {
            console.error('[horo-studio] Kurulum komutu tamamlandi. Simdi tekrar calistir: npm run studio:setup-electron');
          }
        } catch (installError) {
          console.error('[horo-studio] Otomatik kurulum basarisiz oldu.');
          console.error(installError && installError.error ? installError.error.message : String(installError));
          console.error('Manuel olarak Visual Studio Installer icinden Spectre bileşenlerini kurup tekrar dene.');
        }
      } else {
        console.error('After install, run again: npm run studio:setup-electron');
      }
    }
    if (failure && failure.error) {
      console.error(failure.error.message);
    }
    process.exit((failure && failure.code) || 1);
  }
}

function findRgBinary(rootDir) {
  const rgDir = path.join(rootDir, 'node_modules', '@vscode', 'ripgrep', 'bin');
  const names = process.platform === 'win32' ? ['rg.exe', 'rg'] : ['rg', 'rg.exe'];
  for (const name of names) {
    const file = path.join(rgDir, name);
    if (fs.existsSync(file)) {
      return file;
    }
  }
  return null;
}

async function ensureRipgrep(rootDir, step, total) {
  if (findRgBinary(rootDir)) {
    return;
  }
  await runStep(
    step,
    total,
    'Restoring ripgrep binary',
    process.execPath,
    [path.join(rootDir, 'node_modules', '@vscode', 'ripgrep', 'lib', 'postinstall.js')],
    { cwd: rootDir }
  );
}

async function compileHoroFrontend(rootDir, step, total) {
  await runStep(
    step,
    total,
    'Compiling Horo frontend extension',
    localBin(rootDir, 'tsc'),
    ['-p', 'tsconfig.json'],
    { cwd: path.join(rootDir, 'extension') }
  );
}

async function fixMacAsarHash(rootDir) {
  if (process.platform !== 'darwin') {
    return;
  }

  const distDir = path.join(rootDir, 'dist');
  if (!fs.existsSync(distDir)) {
    return;
  }

  const appCandidates = [];
  const walk = (dir) => {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
      const full = path.join(dir, entry.name);
      if (entry.isDirectory() && entry.name.endsWith('.app')) {
        appCandidates.push(full);
        continue;
      }
      if (entry.isDirectory()) {
        walk(full);
      }
    }
  };
  walk(distDir);
  if (appCandidates.length === 0) {
    return;
  }

  const appDir = appCandidates[0];
  const asarPath = path.join(appDir, 'Contents', 'Resources', 'app.asar');
  const plistPath = path.join(appDir, 'Contents', 'Info.plist');
  if (!fs.existsSync(asarPath) || !fs.existsSync(plistPath)) {
    return;
  }

  const hash = crypto.createHash('sha256').update(fs.readFileSync(asarPath)).digest('hex');
  await run('/usr/libexec/PlistBuddy', ['-c', `Set :ElectronAsarIntegrity:Resources/app.asar:hash ${hash}`, plistPath], { cwd: rootDir });
  await run('codesign', ['--force', '--deep', '--sign', '-', appDir], { cwd: rootDir });
}

async function main() {
  const rootDir = path.resolve(__dirname, '..');
  const args = process.argv.slice(2);
  const skipEngine = args.includes('--no-engine');

  if (args.includes('--setup')) {
    const total = 4;
    if (process.env.npm_execpath && fs.existsSync(process.env.npm_execpath)) {
      await runStep(
        1,
        total,
        'Rebuilding native modules for Node.js',
        process.execPath,
        [process.env.npm_execpath, 'rebuild', '--build-from-source'],
        { cwd: rootDir }
      );
    } else {
      await runStep(
        1,
        total,
        'Rebuilding native modules for Node.js',
        bin('npm'),
        ['rebuild', '--build-from-source'],
        { cwd: rootDir }
      );
    }
    await compileHoroFrontend(rootDir, 2, total);
    await runStep(
      3,
      total,
      'Restoring ripgrep binary',
      process.execPath,
      [path.join(rootDir, 'node_modules', '@vscode', 'ripgrep', 'lib', 'postinstall.js')],
      { cwd: rootDir }
    );
    await runStep(
      4,
      total,
      'Building Theia (browser mode)',
      localBin(rootDir, 'theia'),
      ['build', '--mode', 'development'],
      { cwd: rootDir }
    );
    return;
  }

  if (args.includes('--setup-electron')) {
    const total = 7;
    await runStep(
      1,
      total,
      'Rebuilding native modules for Electron ABI',
      localBin(rootDir, 'electron-rebuild'),
      ['-f'],
      { cwd: rootDir }
    );
    await compileHoroFrontend(rootDir, 2, total);
    await ensureRipgrep(rootDir, 3, total);
    await runStep(
      4,
      total,
      'Building Theia (Electron target)',
      localBin(rootDir, 'theia'),
      ['build', '--app-target=electron', '--mode', 'development'],
      { cwd: rootDir, env: { ...process.env, NODE_OPTIONS: '--max-old-space-size=4096' } }
    );
    await runStep(
      5,
      total,
      'Staging engine runtime for packaged app',
      process.execPath,
      [path.join(rootDir, 'scripts', 'stage-engine-runtime.js')],
      { cwd: rootDir }
    );
    await runStep(
      6,
      total,
      'Packaging with electron-builder --dir',
      localBin(rootDir, 'electron-builder'),
      ['--dir', '--config.win.signAndEditExecutable=false'],
      {
        cwd: rootDir,
        env: {
          ...process.env,
          NODE_OPTIONS: '--max-old-space-size=4096',
          CSC_IDENTITY_AUTO_DISCOVERY: 'false',
        },
      }
    );
    await runStep(
      7,
      total,
      'Fixing ASAR integrity hash (macOS only)',
      process.execPath,
      ['-e', 'process.exit(0)'],
      { cwd: rootDir }
    );
    await fixMacAsarHash(rootDir);
    return;
  }

  if (args.includes('--electron')) {
    const keepLayout = args.includes('--keep-layout');
    const debugLogs = args.includes('--debug-logs');
    if (!skipEngine) {
      await ensureEngineRunning(rootDir, debugLogs);
    }
    if (hasPackagedElectronApp(rootDir)) {
      await runStep(
        1,
        1,
        'Launching packaged Electron app',
        process.execPath,
        [
          path.join(rootDir, 'scripts', 'studio-electron.js'),
          ...(keepLayout ? ['--keep-layout'] : []),
          ...(debugLogs ? ['--debug-logs'] : []),
          ...(skipEngine ? ['--no-engine'] : []),
        ],
        { cwd: rootDir }
      );
      return;
    }
    const total = 3;
    await compileHoroFrontend(rootDir, 1, total);
    await runStep(
      2,
      total,
      'Building Theia (Electron target)',
      localBin(rootDir, 'theia'),
      ['build', '--app-target=electron', '--mode', 'development'],
      { cwd: rootDir, env: { ...process.env, NODE_OPTIONS: '--max-old-space-size=4096' } }
    );
    console.log(`==> ${stepBar(3, total)} (3/${total}) Launching development Electron app${debugLogs ? ' (debug logs enabled)' : ''}`);
    launchDevElectron(rootDir, debugLogs);
    console.log('==> completed: Launching development Electron app');
    return;
  }

  await ensureRipgrep(rootDir, 1, 1);
  if (!skipEngine) {
    await ensureEngineRunning(rootDir, false);
  }
  await compileHoroFrontend(rootDir, 1, 2);
  const passthrough = args.filter((arg) => arg !== '--keep-layout');
  await runStep(
    2,
    2,
    'Starting Horo Studio at http://localhost:3000',
    localBin(rootDir, 'theia'),
    ['start', '--port', '3000', '--plugins=local-dir:plugins', ...passthrough],
    { cwd: rootDir }
  );
}

main().catch((error) => {
  console.error(error && error.message ? error.message : String(error));
  process.exit(1);
});
