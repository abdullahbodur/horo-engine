#!/usr/bin/env node
/* eslint-disable no-console */
const fs = require('fs');
const path = require('path');

function findEngineBinDir(engineRoot) {
  const exeName = process.platform === 'win32' ? 'HoroEngine.exe' : 'HoroEngine';
  const candidates = [];
  const pushIfExists = (candidate) => {
    if (fs.existsSync(candidate)) {
      candidates.push(candidate);
    }
  };

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

  for (const candidate of candidates) {
    const exePath = path.join(candidate, exeName);
    if (fs.existsSync(exePath)) {
      return { binDir: candidate, exePath };
    }
  }
  return null;
}

function stageEngineRuntime(studioRoot) {
  const engineRoot = path.resolve(studioRoot, '..');
  const resolved = findEngineBinDir(engineRoot);
  if (!resolved) {
    throw new Error(
      [
        'HoroEngine executable was not found for packaging.',
        `Expected under: ${path.join(engineRoot, 'build', '<config>', 'bin')}`,
        'Build engine first, then package studio.',
      ].join('\n')
    );
  }

  const targetDir = path.join(studioRoot, 'engine-runtime');
  fs.rmSync(targetDir, { recursive: true, force: true });
  fs.mkdirSync(targetDir, { recursive: true });
  fs.cpSync(resolved.binDir, targetDir, { recursive: true, force: true });

  // Also bundle the SDK (shaders, editor_schema.json) so the engine can find
  // its assets at runtime. The engine looks for sdk/ relative to its cwd
  // (the engine-runtime/ dir) at exeDir/sdk, exeDir/../sdk, etc.
  // Placing it inside engine-runtime/sdk/ ensures it is found as exeDir/sdk.
  const sdkSrcDir = path.join(resolved.binDir, '..', 'sdk');
  if (fs.existsSync(sdkSrcDir)) {
    const sdkTargetDir = path.join(targetDir, 'sdk');
    fs.mkdirSync(sdkTargetDir, { recursive: true });
    fs.cpSync(sdkSrcDir, sdkTargetDir, { recursive: true, force: true });
    console.log(`[horo-studio] Staged engine SDK from: ${path.resolve(sdkSrcDir)}`);
  } else {
    console.warn(`[horo-studio] WARNING: SDK not found at ${sdkSrcDir} — engine may not render correctly.`);
    console.warn(`[horo-studio] Run 'cmake --build build/<config>' first to generate the SDK.`);
  }

  console.log(`[horo-studio] Staged engine runtime from: ${resolved.binDir}`);
  console.log(`[horo-studio] Staged engine executable: ${resolved.exePath}`);
  console.log(`[horo-studio] Runtime output: ${targetDir}`);
}

if (require.main === module) {
  try {
    const studioRoot = path.resolve(__dirname, '..');
    stageEngineRuntime(studioRoot);
  } catch (error) {
    console.error(error && error.message ? error.message : String(error));
    process.exit(1);
  }
}

module.exports = {
  stageEngineRuntime,
};
