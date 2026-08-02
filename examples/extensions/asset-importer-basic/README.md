# Basic Asset Importer Extension

This example is a real loadable native extension. It demonstrates the
`asset.importer` C ABI without passing C++ engine objects across the dynamic
library boundary.

The module registers:

- contribution `com.horo.examples.asset-importer-basic.raw`
- source extension `.hraw`
- asset type `example.raw`
- a memorable `invertPreview` boolean setting
- an import callback that copies the source into a versioned editor payload
- a rendering-neutral RGBA8 preview callback

Build the repository with `HORO_BUILD_EXAMPLES=ON`. The build places
`extension.json` beside the platform module binary, producing a package
directory that can be passed as an absolute path to
`Horo::Extensions::ExtensionManager::LoadExtension`.
