#!/usr/bin/env node
/* eslint-disable no-console */
const fs = require('fs');
const net = require('net');
const os = require('os');
const path = require('path');
const { spawn } = require('child_process');

function walk(dir, visitor) {
  const entries = fs.readdirSync(dir, { withFileTypes: true });
  for (const entry of entries) {
    const full = path.join(dir, entry.name);
    visitor(full, entry);
    if (entry.isDirectory()) {
      walk(full, visitor);
    }
  }
}

function findElectronApp(distDir) {
  const platform = process.platform;
  const matches = [];

  walk(distDir, (full, entry) => {
    if (platform === 'darwin' && entry.isDirectory()) {
      if (entry.name === 'HoroStudio.app' || entry.name === 'Horo Studio.app') {
        matches.push(full);
      }
    }

    if (platform === 'win32' && entry.isFile()) {
      if (
        entry.name === 'HoroStudio.exe' ||
        entry.name === 'Horo Studio.exe' ||
        entry.name === 'horo-studio.exe'
      ) {
        matches.push(full);
      }
    }

    if (platform === 'linux' && entry.isFile()) {
      if (entry.name === 'horo-studio' || entry.name === 'HoroStudio') {
        matches.push(full);
      }
    }
  });

  if (matches.length === 0) {
    throw new Error(`No packaged Electron app found under: ${distDir}`);
  }

  return matches.sort((a, b) => b.length - a.length)[0];
}

function getTheiaUserDataRoots() {
  const home = os.homedir();
  if (process.platform === 'darwin') {
    const base = path.join(home, 'Library', 'Application Support');
    return [
      path.join(base, 'horo-studio'),
      path.join(base, 'Horo Studio'),
    ];
  }

  if (process.platform === 'win32') {
    const appData = process.env.APPDATA || path.join(home, 'AppData', 'Roaming');
    return [
      path.join(appData, 'horo-studio'),
      path.join(appData, 'Horo Studio'),
    ];
  }

  return [
    path.join(home, '.config', 'horo-studio'),
    path.join(home, '.config', 'Horo Studio'),
  ];
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

function clearLayoutState() {
  const subPaths = ['IndexedDB', 'Local Storage', 'Session Storage'];
  for (const root of getTheiaUserDataRoots()) {
    for (const subPath of subPaths) {
      fs.rmSync(path.join(root, subPath), { recursive: true, force: true });
    }
  }
}

function launchElectron(appPath, debugLogs = false) {
  const env = { ...process.env };
  delete env.ELECTRON_RUN_AS_NODE;
  if (debugLogs) {
    env.ELECTRON_ENABLE_LOGGING = '1';
    env.THEIA_LOG_LEVEL = env.THEIA_LOG_LEVEL || 'debug';
  }

  if (process.platform === 'darwin') {
    const child = spawn('open', ['-n', appPath], {
      detached: !debugLogs,
      stdio: debugLogs ? 'inherit' : 'ignore',
      env,
    });
    if (!debugLogs) {
      child.unref();
    }
    return;
  }

  const child = spawn(appPath, [], {
    detached: !debugLogs,
    stdio: debugLogs ? 'inherit' : 'ignore',
    env,
  });
  if (!debugLogs) {
    child.unref();
  }
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

function findEngineExecutable(studioRootDir) {
  const engineRoot = path.resolve(studioRootDir, '..');
  const exeName = process.platform === 'win32' ? 'HoroEngine.exe' : 'HoroEngine';
  const candidates = collectEngineBinDirs(engineRoot).map((binDir) => path.join(binDir, exeName));
  return candidates.find((candidate) => fs.existsSync(candidate)) || null;
}

function engineExecutableCandidates(studioRootDir) {
  const engineRoot = path.resolve(studioRootDir, '..');
  const exeName = process.platform === 'win32' ? 'HoroEngine.exe' : 'HoroEngine';
  return collectEngineBinDirs(engineRoot).map((binDir) => path.join(binDir, exeName));
}

async function ensureEngineRunning(studioRootDir, debugLogs = false, timeoutMs = 20000) {
  if (await isTcpPortOpen('127.0.0.1', 39281)) {
    console.log('Engine already running (MCP:39281).');
    return;
  }

  const executable = findEngineExecutable(studioRootDir);
  if (!executable) {
    console.warn('[horo-studio] HoroEngine executable not found. Checked:');
    for (const candidate of engineExecutableCandidates(studioRootDir)) {
      console.warn(`- ${candidate}`);
    }
    console.warn('[horo-studio] UI may show "Engine not connected".');
    return;
  }

  const env = { ...process.env, MONOLITH_GLFW_VISIBLE: process.env.MONOLITH_GLFW_VISIBLE || '0' };

  const child = spawn(executable, [], {
    cwd: path.resolve(studioRootDir, '..'),
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
    console.log('Engine connected (MCP:39281).');
  } else {
    console.warn('[horo-studio] Engine starting but MCP not ready yet; continuing launch.');
  }
}

async function main() {
  const keepLayout = process.argv.includes('--keep-layout');
  const debugLogs = process.argv.includes('--debug-logs');
  const skipEngine = process.argv.includes('--no-engine');
  const rootDir = path.resolve(__dirname, '..');
  const distDir = path.join(rootDir, 'dist');

  if (!fs.existsSync(distDir)) {
    throw new Error(`dist directory does not exist: ${distDir}. Run packaging first.`);
  }

  if (!keepLayout) {
    clearLayoutState();
  }
  if (!skipEngine) {
    await ensureEngineRunning(rootDir, debugLogs);
  }

  const appPath = findElectronApp(distDir);
  console.log(`Launching Electron app: ${appPath}${debugLogs ? ' (debug logs enabled)' : ''}`);
  launchElectron(appPath, debugLogs);
}

main().catch((error) => {
  console.error(error && error.message ? error.message : String(error));
  process.exit(1);
});
