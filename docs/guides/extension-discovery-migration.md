# Approved extension discovery migration

HORO-72 replaces `ExtensionManager::DiscoverExtensions(directory)` with
`Horo::Extensions::Discovery::DiscoverDeclaredPackages`, declared by
`Horo/Extensions/ExtensionDiscovery.h` and owned by `HoroExtensions`.

The removed API scanned arbitrary directories for `extension.json`, without
root approval or containment checks. Its only repository callers were the
extension manager tests; those now exercise declared-package discovery through
the public header. Public-header consumer generation covers the new header.

Host/package composition now supplies root requests with explicit provenance,
local approval decisions and the package locations selected by its package
graph. No environment variable, working directory, manifest or directory entry
can implicitly add a root or a package. A project request does not approve itself.

System roots require product-policy provenance; user and development roots
require user-local provenance. Project roots require explicit approval of their
portable request. Development roots additionally require both the development
profile and invocation opt-in, and produce non-portable diagnostics.

Accepted paths must exist and canonicalize strictly inside their approved root.
Parent traversal and symlink escapes fail. Unknown root references and duplicate
accepted package IDs fail without a partial result. Successful packages and root
diagnostics are ordered by their stable IDs, not filesystem enumeration.

This API is a discovery preflight, not a package verifier or activation candidate.
It neither selects versions nor implements development graph substitution. Those
remain package-system responsibilities under ADR-054 and ADR-058. Callers must
retain verified install identity, trust and lifetime ownership through activation;
a canonical path alone is not an execution grant or protection from later file
mutation. The existing directory-based loader and legacy inventory are not
upgraded to verified-install activation by this change.
