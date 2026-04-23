#!/usr/bin/env node
/* eslint-disable no-console */
const fs = require('fs');
const net = require('net');
const path = require('path');
const { spawn } = require('child_process');

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

function collectEngineDirs(studioRoot) {
  const engineRoot = path.resolve(studioRoot, '..');
  const dirs = [];
  const pushIfExists = (candidate) => {
    if (fs.existsSync(candidate)) {
      dirs.push(candidate);
    }
  };

  if (process.resourcesPath) {
    pushIfExists(path.join(process.resourcesPath, 'engine-runtime'));
  }
  pushIfExists(path.join(studioRoot, 'engine-runtime'));
  pushIfExists(path.join(engineRoot, 'build', 'release', 'bin'));
  pushIfExists(path.join(engineRoot, 'build', 'relwithdebinfo', 'bin'));
  pushIfExists(path.join(engineRoot, 'build', 'debug', 'bin'));
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

function findEngineExecutable(studioRoot) {
  const exeName = process.platform === 'win32' ? 'HoroEngine.exe' : 'HoroEngine';

  const candidates = collectEngineDirs(studioRoot).map((dir) => path.join(dir, exeName));
  const exePath = candidates.find((candidate) => fs.existsSync(candidate));
  if (exePath) {
    return {
      exePath,
      cwd: path.dirname(exePath),
    };
  }
  return null;
}

async function ensureEngineRunning() {
  if (await isTcpPortOpen('127.0.0.1', 39281)) {
    return;
  }

  const studioRoot = path.resolve(__dirname, '..');
  const engine = findEngineExecutable(studioRoot);
  if (!engine) {
    console.warn('[horo-studio] HoroEngine executable not found. UI may start disconnected.');
    return;
  }
  const env = { ...process.env, MONOLITH_GLFW_VISIBLE: process.env.MONOLITH_GLFW_VISIBLE || '0' };

  const child = spawn(engine.exePath, [], {
    cwd: engine.cwd,
    env,
    detached: false,
    stdio: 'ignore',
    shell: false,
  });

  const { app } = require('electron');
  app.on('before-quit', () => {
    try {
      if (process.platform === 'win32') {
        spawn('taskkill', ['/PID', String(child.pid), '/T', '/F'], { stdio: 'ignore', shell: true });
      } else {
        child.kill('SIGTERM');
      }
    } catch {
      // ignore shutdown race
    }
  });

  const ready = await waitForTcpPort('127.0.0.1', 39281, 20000);
  if (!ready) {
    console.warn('[horo-studio] Engine process started but MCP is not ready yet.');
  }
}

(async () => {
  await ensureEngineRunning();
  require('../lib/backend/electron-main.js');
})().catch((error) => {
  console.error(error && error.message ? error.message : String(error));
  process.exit(1);
});
