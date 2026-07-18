# Horo Engine Project Kanban

Bu dosya Horo Engine'in mimari hedefleri ile gerçek repository durumunu aynı
yerde izlemek için kullanılan yaşayan kanbandır. Normatif davranışın kaynağı
[`docs/architecture/`](./architecture/README.md) belgeleridir; bu dosya o
sözleşmelerin uygulanma ve doğrulanma durumunu özetler.

<a id="kanban"></a>

## Kanban

| Yapılıyor | İncelenecek                                                                                                                                                                                                                                                                                                                          | Hazır                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           | Bağımlı                                                                                                                                                                                                                | Sonra                                                                                                                                                                                                                                                                                                                                 | Tamamlandı                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
|-----------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| —         | [FND-001 · Foundation kontrat denetimi](#fnd-001)<br>[INP-001 · Input entegrasyon denetimi](#inp-001)<br>[MTH-001 · Scene math ve viewport denetimi](#mth-001)<br>[DOC-001 · Scene document kalıcılık denetimi](#doc-001)<br>[RND-002 · OpenGL/Metal parity denetimi](#rnd-002)<br>[TST-001 · Test matrisi ve CI denetimi](#tst-001)<br>[SCN-001 · Scene runtime ve ECS temeli](#scn-001) | [AST-001 · Asset pipeline temeli](#ast-001)<br>[MAT-001 · Material ve shader temeli](#mat-001)<br>[EDT-002 · Inspector authoring](#edt-002)<br>[EDT-003 · Content Browser asset işlemleri](#edt-003)<br>[EDT-004 · Console, output ve operations panelleri](#edt-004)<br>[PRJ-001 · Project save/open/recovery](#prj-001)<br>[CLI-001 · Headless CLI host](#cli-001)<br>[MCP-001 · MCP host ve tool registry](#mcp-001)<br>[OBS-001 · Observability temeli](#obs-001)<br>[SEC-001 · Runtime güvenlik politikaları](#sec-001) | [GME-001 · Play mode ve gameplay runtime](#gme-001)<br>[PHY-001 · Physics runtime](#phy-001)<br>[AUD-001 · Audio runtime](#aud-001)<br>[PKG-001 · Package sistemi](#pkg-001)<br>[REL-001 · Release pipeline](#rel-001) | [NET-001 · Networking](#net-001)<br>[GUI-001 · Runtime Game UI/HUD](#gui-001)<br>[PLT-001 · Platform services](#plt-001)<br>[EXT-001 · Extension ve gameplay module host](#ext-001)<br>[PFB-001 · Prefab sistemi](#pfb-001)<br>[ADV-001 · Advanced renderer özellikleri](#adv-001)<br>[ADV-002 · Gelişmiş dünya sistemleri](#adv-002) | [RUN-001 · Runtime lifecycle ve frame scheduler](#run-001)<br>[ARC-001 · Desired tree ve target uyumu](#arc-001)<br>[EDT-001 · Editor vertical slice sağlamlaştırma](#edt-001)<br>[RND-001 · Generic renderer resource geçişi](#rnd-001)<br>[BAS-001 · Foundation primitives](#bas-001)<br>[BAS-002 · Scene math baseline](#bas-002)<br>[BAS-003 · Runtime input baseline](#bas-003)<br>[BAS-004 · Editor host ve workspace baseline](#bas-004)<br>[BAS-005 · Hierarchy create ve typed primitives](#bas-005)<br>[BAS-006 · Procedural mesh ve viewport baseline](#bas-006)<br>[BAS-007 · macOS app identity](#bas-007) |

## Durum Sözleşmesi

| Durum           | Anlamı                                                                             | Çıkış ölçütü                                                             |
|-----------------|------------------------------------------------------------------------------------|--------------------------------------------------------------------------|
| **Yapılıyor**   | Aktif local değişiklik veya devam eden mimari geçiş vardır.                        | Kod, test ve belge aynı kontratta birleşir; ticket İncelenecek'e geçer.  |
| **İncelenecek** | Uygulama vardır fakat kapsam/parity/lifecycle kanıtı tamamlanmalıdır.              | Kabul ölçütleri ve ilgili doğrulama matrisi geçer.                       |
| **Hazır**       | Mimari tanımlıdır ve başlanmasını engelleyen bilinen bağımlılık yoktur.            | Çalışma başladığında Yapılıyor'a geçer.                                  |
| **Bağımlı**     | Ticket doğru tasarlanabilir fakat önce belirtilen temel ticketlar kapanmalıdır.    | Bütün zorunlu bağımlılıklar Tamamlandı veya yeterli dar kontrata ulaşır. |
| **Sonra**       | Geçerli mimari hedef, fakat temel editor/runtime tamamlanmadan öncelikli değildir. | Ürün temelinin kapanması ve açık bir milestone kararı gerekir.           |
| **Tamamlandı**  | Dar kapsam implementasyon ve otomatik test kanıtıyla mevcuttur.                    | Regresyonda yeniden açılır; genişletme ayrı ticket olur.                 |

### Kanban kuralları

- Aynı anda en fazla üç ticket **Yapılıyor** durumunda tutulur.
- Bir klasörün veya mimari belgenin varlığı implementasyon kanıtı değildir.
- Boş placeholder dosyaları ilerleme sayılmaz.
- **Tamamlandı** için gerçek tüketici, regression testi ve ilgili mimari belgeyle
  uyum gerekir.
- Ticket kapsamı büyürse mevcut ticket belirsizleştirilmez; yeni ID açılır ve
  bağımlılık linklenir.
- Durum değiştirildiğinde `Son doğrulama` satırı komut, tarih ve sonucu taşımalıdır.

## Genel Durum Özeti

18 Temmuz 2026 itibarıyla aktif CMake yüzeyi Foundation, Platform, Runtime,
Input, Scene Model, Render API/Frontend, OpenGL, Metal, Editor Model ve Editor
GUI katmanlarını kapsar. Runtime lifecycle ve frame scheduler artık graphical
HoroEditor ile null-renderer headless test hostunun ortak kontratıdır. Runtime
scene Phase 1; immutable definition, ownership ve editor viewport tüketimini ekler.
Assets, tam ECS, physics, audio, networking, game UI, CLI/MCP, gameplay,
extensions, packages, observability, release ve security alanlarının çoğu hâlâ
placeholder veya sonraki ticket kapsamındadır.

Önerilen temel ilerleme sırası:

1. [ARC-001](#arc-001), [EDT-001](#edt-001) ve [RND-001](#rnd-001) ile mevcut
   local dilimi stabilize et.
2. [SCN-001](#scn-001) canonical doğrulamasını kapatırken [AST-001](#ast-001)
   provider ve registry temelini başlat.
3. [AST-001](#ast-001) entegrasyonu ve [MAT-001](#mat-001) ile editor viewport verisini gerçek
   asset/resource akışına bağla.
4. Editor authoring ve persistence ticketlarını kapat.
5. Gameplay, physics ve audio gibi bağımlı sistemleri ancak bu omurgadan sonra aç.

## Ticket Detayları

### Aktif çalışma

<a id="arc-001"></a>

### ARC-001 — Desired tree ve CMake target uyumu

- **Durum:** Tamamlandı
- **Öncelik:** P0
- **Özet:** Gerçek dosya/target adlarını
  [Desired Project Trees](./architecture/desired-project-tree.md) ve
  [System Design](./architecture/foundation/system-design.md) ile karşılaştır;
  transitional editor-private sınırları açıkça kaydet.
- **Kabul:** Her aktif target'ın sahibi, public/private sınırı ve hedef tree yolu
  belirlenmiş; boş placeholder'lar implementasyon gibi görünmüyor; ters bağımlılık
  kalmamış.
- **Kanıt:** `src/CMakeLists.txt`, `apps/CMakeLists.txt`, `include/Horo/`, `src/`
  ve tüketici testleri birlikte denetlenir.
- **Bağımlılık:** Yok.
- **Son doğrulama:** 2026-07-17 — GUI-off 35/35 ve canonical target graph build geçti.

[↑ Kanbana dön](#kanban)

<a id="edt-001"></a>

### EDT-001 — Editor vertical slice sağlamlaştırma

- **Durum:** Tamamlandı
- **Öncelik:** P0
- **Özet:** Screen host, modal host, workspace controller, hierarchy, selection,
  viewport, input capture ve shutdown akışını tek güvenli editor yaşam döngüsü
  olarak tamamla.
- **Kabul:** Açılış, project workspace, renderer restart ve kapanışta borrowed
  service/lifetime hatası yok; modal/focus kaybı aktif interaction'ları iptal eder;
  editor state yalnız controller/document sahiplerinde kalır.
- **Mimari:** [GUI Screen Host](./architecture/editor/gui-screen-host.md),
  [Editor Modal Host](./architecture/editor/editor-modal-host.md),
  [Editor Panel Host](./architecture/editor/editor-panel-host.md).
- **Bağımlılık:** [ARC-001](#arc-001).
- **Son doğrulama:** 2026-07-17 — lifecycle/modal/screen-host testleri ile OpenGL ve Metal
  first-frame shutdown ASAN altında geçti (`detect_leaks=0`; macOS ASAN leak detection desteklemiyor).

[↑ Kanbana dön](#kanban)

<a id="rnd-001"></a>

### RND-001 — Generic renderer resource geçişi

- **Durum:** Tamamlandı
- **Öncelik:** P0
- **Özet:** Editor-private viewport köprüsündeki generic mesh yolunu Horo-owned
  public render resource/extraction sözleşmesine taşı; backend'e primitive veya
  editor document türü sızdırma.
- **Kabul:** Resource identity, upload, stale generation ve deferred retirement
  Render API/Frontend kontratında tanımlı; OpenGL ve Metal aynı snapshot'ı tüketir;
  normal frame'de geometry upload/allocation yoktur.
- **Mimari:** [Rendering Architecture](./architecture/runtime/rendering-architecture.md),
  [Backend Parity](./architecture/runtime/render-backend-parity-contract.md).
- **Bağımlılık:** [ARC-001](#arc-001). Parity kabulü [RND-002](#rnd-002) ile
  doğrulanır.
- **Son doğrulama:** 2026-07-17 — OpenGL/Metal viewport smoke ve first-frame dahil 47/47 geçti.

[↑ Kanbana dön](#kanban)

### İncelenecek alanlar

<a id="fnd-001"></a>

### FND-001 — Foundation public kontrat denetimi

- **Durum:** İncelenecek
- **Öncelik:** P0
- **Özet:** Result/error, diagnostics, configuration, jobs, cancellation,
  handles, platform ve data bus API'lerinin birbirleriyle ve dependency yönüyle
  uyumunu denetle.
- **Kabul:** Stable error domain'leri, shutdown/cancellation ve thread-affinity
  kuralları public header Doxygen sözleşmelerinde ve testlerde kanıtlıdır.
- **Mimari:** [Error and Diagnostics](./architecture/foundation/error-and-diagnostics.md),
  [Concurrency](./architecture/foundation/concurrency-and-jobs.md),
  [Ownership](./architecture/foundation/ownership-and-resource-lifetime.md).
- **Bağımlılık:** [BAS-001](#bas-001).

[↑ Kanbana dön](#kanban)

<a id="inp-001"></a>

### INP-001 — Runtime input entegrasyon denetimi

- **Durum:** İncelenecek
- **Öncelik:** P0
- **Özet:** Backend-neutral snapshot/router/action/capture hattının bütün editor
  interaction'larında tek eligibility otoritesi olduğunu doğrula.
- **Kabul:** SDL ve virtual/headless kaynak parity'si; modal, focused widget ve
  tool capture öncelikleri; profile persistence, gamepad lifecycle ve fixed-tick
  replay uçtan uca kanıtlanır.
- **Mimari:** [Input Architecture](./architecture/runtime/input-architecture.md).
- **Bağımlılık:** [BAS-003](#bas-003), [EDT-001](#edt-001).

[↑ Kanbana dön](#kanban)

<a id="mth-001"></a>

### MTH-001 — Scene math ve viewport tüketici denetimi

- **Durum:** İncelenecek
- **Öncelik:** P0
- **Özet:** Kamera, picking, focus selected ve Move/Rotate/Scale gizmo kodunda
  tekrar eden ya da sessiz fallback yapan yerel matematik kalmadığını doğrula.
- **Kabul:** Perspective/orthographic, iki clip-depth convention, parented
  negative/non-uniform scale ve singular failure yolları ortak API'den geçer.
- **Mimari:** [Scene Math](./architecture/foundation/scene-math.md).
- **Bağımlılık:** [BAS-002](#bas-002), [EDT-001](#edt-001).

[↑ Kanbana dön](#kanban)

<a id="doc-001"></a>

### DOC-001 — Scene document ve command kalıcılık denetimi

- **Durum:** İncelenecek
- **Öncelik:** P0
- **Özet:** Typed scene object/component snapshot, duplicate, undo/redo, dirty
  state, save boundary ve crash recovery sözleşmelerini gerçek document modeliyle
  karşılaştır.
- **Kabul:** Preview state document'e sızmaz; tek user action tek undo kaydıdır;
  unknown/version-skewed data typed failure üretir; round-trip veri kaybetmez.
- **Mimari:** [Editor Document Model](./architecture/editor/editor-document-model.md),
  [Project Model](./architecture/editor/project-model.md).
- **Bağımlılık:** [BAS-005](#bas-005). Bulgular [PRJ-001](#prj-001) kapsamını
  kesinleştirir.

[↑ Kanbana dön](#kanban)

<a id="rnd-002"></a>

### RND-002 — OpenGL ve Metal parity denetimi

- **Durum:** İncelenecek
- **Öncelik:** P0
- **Özet:** Backend seçimi, presentation, resize, first-frame, resource lifetime,
  selection ve primitive draw sonuçlarının eşit kontratta kaldığını doğrula.
- **Kabul:** OpenGL-only, Metal-only ve combined konfigürasyonları geçer; silent
  fallback yoktur; shutdown frames-in-flight kaynaklarını güvenli bırakır.
- **Mimari:** [Backend Parity](./architecture/runtime/render-backend-parity-contract.md),
  [Renderer Availability](./architecture/runtime/renderer-distribution-and-availability.md).
- **Bağımlılık:** [BAS-006](#bas-006), [RND-001](#rnd-001).

[↑ Kanbana dön](#kanban)

<a id="tst-001"></a>

### TST-001 — Test matrisi ve CI denetimi

- **Durum:** İncelenecek
- **Öncelik:** P0
- **Özet:** Yerel skeleton testlerini CI matrix, sanitizer, backend-only ve
  headless kapılarıyla hizala.
- **Kabul:** Public-header tüketicileri, ASAN/UBSAN, OpenGL-only, Metal-only,
  combined ve GUI-off konfigürasyonları tanımlı; GPU gereksinimli testler doğru
  etiketli ve opt-in davranır.
- **Mimari:** [Testing Architecture](./architecture/delivery/testing-architecture.md),
  [Quality and CI](./architecture/delivery/quality-and-ci.md).
- **Son doğrulama:** 2026-07-17 — `ctest --test-dir build/skeleton
  --output-on-failure`: 47/47 geçti.

[↑ Kanbana dön](#kanban)

### Hazır işler

<a id="run-001"></a>

### RUN-001 — Runtime lifecycle ve frame scheduler

- **Durum:** Tamamlandı
- **Öncelik:** P0
- **Özet:** Startup, fixed update, variable update, render extraction,
  presentation, suspend/resume ve ters sırada shutdown için gerçek runtime
  omurgasını oluştur.
- **Kabul:** Headless ve graphical host aynı lifecycle kontratını compose eder;
  cancellation, partial initialization, bounded catch-up, suspend/resume,
  presentation gate ve allocation-free scheduler success path test edilir.
- **Mimari:** [Runtime Lifecycle](./architecture/runtime/runtime-lifecycle.md).
- **Alt işler:** RUN-001A zaman/phase kontratları; RUN-001B lifecycle/rollback;
  RUN-001C scheduler invariant'ları; RUN-001D graphical/headless entegrasyon —
  tamamı kapandı.
- **Kanıt:** `HoroEngine::Runtime`, HoroEditor runtime participant'ı,
  null-renderer test hostu ve `HoroRuntimeLifecycleTests`.
- **Son doğrulama:** 2026-07-18 — canonical Windows Debug 44/44 ve GUI-off
  37/37 geçti; `HoroEditor --exit-after-first-frame` OpenGL smoke geçti.

[↑ Kanbana dön](#kanban)

<a id="scn-001"></a>

### SCN-001 — Scene runtime ve ECS temeli

- **Durum:** İncelenecek
- **Öncelik:** P0
- **Özet:** Authored scene document'ten bağımsız immutable definition, runtime
  ownership, generation-checked identity, transactional transition ve dar
  structural command sınırını hayata geçir.
- **Kabul:** Runtime scene ownership açık; editor viewport aktif
  `RuntimeSceneView` değerini gerçek kaynak olarak tüketir; load/unload,
  replacement, atomic structural mutation, stale reference ve allocation-free
  steady-state testleri vardır.
- **Alt işler:** SCN-001A immutable definition/conversion; SCN-001B identity,
  storage ve retirement; SCN-001C lifecycle/structural boundary; SCN-001D editor
  viewport migration — tamamı uygulanmıştır.
- **Kanıt:** `HoroEngine::RuntimeScene`, `RuntimeSceneService`,
  `HoroRuntimeSceneTests` ve runtime-backed editor extraction/picking/controller
  testleri.
- **Mimari:** [Scene Runtime](./architecture/runtime/scene-runtime.md).
- **Bağımlılık:** [RUN-001](#run-001). AST-001A identity kontratları üzerinden
  başlayabilir; AST-001B bu ticket'ın kapanmasını bekler.
- **Son doğrulama:** 2026-07-18 — Windows Debug'ta RuntimeScene ve
  runtime-backed editor testleri 5/5, GUI-off kontrat matrisi 4/4 ve OpenGL
  first-frame smoke geçti. Canonical tam build, çalışma ağacındaki kapsam dışı
  Foundation constexpr test derleme hataları nedeniyle henüz kapanmadı.

[↑ Kanbana dön](#kanban)

<a id="ast-001"></a>

### AST-001 — Asset pipeline temeli

- **Durum:** Hazır
- **Öncelik:** P1
- **Özet:** Stable asset ID, registry, importer, cooker, cache, runtime provider
  ve hot-reload akışının minimal çalışan dikey dilimini kur.
- **Kabul:** Source path runtime identity değildir; malformed input, cache miss,
  cancellation ve atomic publish test edilir; headless cook mümkündür.
- **Mimari:** [Asset Pipeline](./architecture/runtime/asset-pipeline.md).
- **Alt işler:** AST-001A stable asset ID/provider/registry kontratı SCN-001 ile
  paralel başlayabilir; AST-001B runtime-scene resolution için SCN-001'i bekler.
- **Bağımlılık:** AST-001A için [RUN-001](#run-001); AST-001B için
  [SCN-001](#scn-001) ve AST-001A.
- **Mevcut boşluk:** `src/runtime/assets/` placeholder.

[↑ Kanbana dön](#kanban)

<a id="mat-001"></a>

### MAT-001 — Material ve shader temeli

- **Durum:** Hazır
- **Öncelik:** P1
- **Özet:** Geçici `core.materials.default` çözümünü typed material instance,
  shader variant ve backend-neutral pipeline resource sözleşmesine taşı.
- **Kabul:** Primitive enum'u shader/pipeline seçimine sızmaz; OpenGL ve Metal
  aynı material parametrelerini çözer; invalid binding typed diagnostic üretir.
- **Mimari:** [Material and Shader Model](./architecture/runtime/material-and-shader-model.md).
- **Bağımlılık:** AST-001B, [RND-001](#rnd-001).

[↑ Kanbana dön](#kanban)

<a id="edt-002"></a>

### EDT-002 — Inspector typed authoring

- **Durum:** Hazır
- **Öncelik:** P1
- **Özet:** Transform ve typed scene component'lerini shared controls üzerinden
  düzenleyen, multi-selection ve validation destekli Inspector akışını tamamla.
- **Kabul:** Editler command/transaction üretir; no-op history oluşturmaz; mixed
  values, invalid numbers, localization ve narrow layout test edilir.
- **Mimari:** [Editor Panel Host](./architecture/editor/editor-panel-host.md),
  [Editor Document Model](./architecture/editor/editor-document-model.md).
- **Bağımlılık:** [DOC-001](#doc-001).

[↑ Kanbana dön](#kanban)

<a id="edt-003"></a>

### EDT-003 — Content Browser asset işlemleri

- **Durum:** Hazır
- **Öncelik:** P1
- **Özet:** Content Browser'ı gerçek project asset index, import, rename, move,
  delete, reveal ve drag payload use-case'lerine bağla.
- **Kabul:** UI doğrudan filesystem mutate etmez; path traversal, collision,
  rollback, missing file ve non-ASCII path senaryoları test edilir.
- **Mimari:** [Project Model](./architecture/editor/project-model.md),
  [Asset Pipeline](./architecture/runtime/asset-pipeline.md).
- **Bağımlılık:** [AST-001](#ast-001), [PRJ-001](#prj-001).

[↑ Kanbana dön](#kanban)

<a id="edt-004"></a>

### EDT-004 — Console, Build Output ve Operations panelleri

- **Durum:** Hazır
- **Öncelik:** P1
- **Özet:** Bottom panel tablarını typed log/build/job modellerine bağla; filtre,
  cancellation, progress ve source navigation davranışlarını tamamla.
- **Kabul:** Panel state authoritative service değildir; bounded history ve
  thread-safe snapshot tüketir; hata satırı ilgili kaynağa güvenli yönlenir.
- **Mimari:** [Debug Console](./architecture/runtime/debug-console-and-overlays.md),
  [Observability](./architecture/observability/observability.md).
- **Bağımlılık:** [OBS-001](#obs-001), [RUN-001](#run-001).

[↑ Kanbana dön](#kanban)

<a id="prj-001"></a>

### PRJ-001 — Project save, open, autosave ve recovery

- **Durum:** Hazır
- **Öncelik:** P0
- **Özet:** Project metadata ve creation temelini atomic document save, recent
  project, workspace persistence, autosave journal ve crash recovery ile tamamla.
- **Kabul:** Malformed/unknown version writable açılmaz; atomic replace ve
  recovery kullanıcı verisini kaybetmez; leave guard dirty document'i korur.
- **Mimari:** [Project Model](./architecture/editor/project-model.md),
  [Editor Document Model](./architecture/editor/editor-document-model.md).
- **Bağımlılık:** [DOC-001](#doc-001), [EDT-001](#edt-001).

[↑ Kanbana dön](#kanban)

<a id="cli-001"></a>

### CLI-001 — Headless CLI host

- **Durum:** Hazır
- **Öncelik:** P1
- **Özet:** `horo-engine` executable'ını shared application use-case'lerini
  kullanan gerçek command registry, parser, progress ve exit-code hostuna dönüştür.
- **Kabul:** GUI/GPU bağımlılığı yok; JSON ve human output ayrıdır; signal/cancel
  ve typed error-to-exit mapping test edilir.
- **Mimari:** [CLI Architecture](./architecture/interfaces/cli-architecture.md).
- **Mevcut boşluk:** `src/interfaces/cli/` placeholder.

[↑ Kanbana dön](#kanban)

<a id="mcp-001"></a>

### MCP-001 — MCP host ve tool registry

- **Durum:** Hazır
- **Öncelik:** P1
- **Özet:** Editor ve headless hostun aynı typed application operations'ını
  versioned MCP tools olarak sunmasını sağla.
- **Kabul:** Request lifetime, main-thread dispatch, cancellation, capability
  policy, bounded payload ve structured errors test edilir; UI callback business
  logic kaynağı değildir.
- **Mimari:** [MCP Architecture](./architecture/interfaces/mcp-architecture.md),
  [System Design](./architecture/foundation/system-design.md).
- **Mevcut boşluk:** `src/interfaces/mcp/` placeholder.

[↑ Kanbana dön](#kanban)

<a id="obs-001"></a>

### OBS-001 — Logging, metrics ve diagnostic bundle temeli

- **Durum:** Hazır
- **Öncelik:** P1
- **Özet:** Structured logging, context propagation, bounded sinks, core metrics
  ve redacted diagnostic bundle için minimal çalışan observability katmanı kur.
- **Kabul:** Secrets loglanmaz; background sink shutdown'ı deterministiktir;
  editor ve headless host aynı schema'yı kullanır.
- **Mimari:** [Observability](./architecture/observability/observability.md),
  [Logging](./architecture/observability/observability-logging.md).
- **Mevcut boşluk:** `src/observability/` placeholder.

[↑ Kanbana dön](#kanban)

<a id="sec-001"></a>

### SEC-001 — Runtime güvenlik politikaları

- **Durum:** Hazır
- **Öncelik:** P1
- **Özet:** Project trust, canonical path, process launch, credentials, parser
  limitleri ve network policy'yi shared application boundary olarak uygula.
- **Kabul:** Untrusted project default-deny davranır; symlink/path traversal,
  oversized parser input ve credential redaction test edilir.
- **Mimari:** [Application Security](./architecture/security/application-security.md),
  [Platform Abstraction](./architecture/foundation/platform-abstraction.md).
- **Mevcut boşluk:** `src/security/` placeholder.

[↑ Kanbana dön](#kanban)

### Bağımlı işler

<a id="gme-001"></a>

### GME-001 — Play mode ve gameplay runtime integration

- **Durum:** Bağımlı
- **Öncelik:** P1
- **Özet:** Edit-time document'ten izole runtime instance, play/stop transitions,
  behavior lifecycle ve fixed-tick gameplay input tüketimini oluştur.
- **Kabul:** Play sırasında authored document istemeden değişmez; stop temiz
  rollback yapar; behavior callbacks ve structural changes deterministiktir.
- **Mimari:** [Gameplay Runtime Integration](./architecture/extensions/gameplay-runtime-integration.md).
- **Bağımlılık:** [RUN-001](#run-001), [SCN-001](#scn-001), AST-001B,
  [MAT-001](#mat-001), [PRJ-001](#prj-001).

[↑ Kanbana dön](#kanban)

<a id="phy-001"></a>

### PHY-001 — Physics runtime temeli

- **Durum:** Bağımlı
- **Öncelik:** P2
- **Özet:** Fixed-step world, rigid body/collider ownership, transform authority,
  broadphase/narrowphase ve temel query kontratını uygula.
- **Kabul:** Scene sync ve destruction sırası açık; deterministic test fixtures,
  collision events ve raycast typed results vardır.
- **Mimari:** [Physics Architecture](./architecture/runtime/physics-architecture.md).
- **Bağımlılık:** [RUN-001](#run-001), [SCN-001](#scn-001).

[↑ Kanbana dön](#kanban)

<a id="aud-001"></a>

### AUD-001 — Audio runtime temeli

- **Durum:** Bağımlı
- **Öncelik:** P2
- **Özet:** Real-time-safe command queue, mixer, voice registry, streaming,
  spatialization ve null backend için temel audio dikey dilimini kur.
- **Kabul:** Audio callback allocation/blocking yapmaz; device loss ve shutdown
  güvenlidir; headless null audio aynı API'yi sağlar.
- **Mimari:** [Audio Architecture](./architecture/runtime/audio-architecture.md).
- **Bağımlılık:** [RUN-001](#run-001), [SCN-001](#scn-001), [AST-001](#ast-001).

[↑ Kanbana dön](#kanban)

<a id="pkg-001"></a>

### PKG-001 — Package sistemi temeli

- **Durum:** Bağımlı
- **Öncelik:** P2
- **Özet:** Manifest, resolver, lockfile, content-addressed cache, restore ve
  transactional lifecycle için minimal package vertical slice oluştur.
- **Kabul:** Clean-machine restore, offline failure, trust, hash mismatch,
  rollback ve concurrent lease senaryoları test edilir.
- **Mimari:** [Package System](./architecture/packages/package-system.md),
  [Package Restore](./architecture/packages/package-restore.md).
- **Bağımlılık:** [AST-001](#ast-001), [SEC-001](#sec-001), [OBS-001](#obs-001).

[↑ Kanbana dön](#kanban)

<a id="rel-001"></a>

### REL-001 — Release pipeline

- **Durum:** Bağımlı
- **Öncelik:** P2
- **Özet:** Reproducible candidate, manifest, signing, SBOM, promotion,
  distribution ve rollback akışını `horopak`/application use-case'leriyle kur.
- **Kabul:** Cancellation ve partial publish güvenlidir; secrets korunur; aynı
  input aynı artifact identity'yi üretir.
- **Mimari:** [Release Architecture](./architecture/release/release.md),
  [Release Security](./architecture/release/release-security.md).
- **Bağımlılık:** [AST-001](#ast-001), [PKG-001](#pkg-001), [SEC-001](#sec-001).

[↑ Kanbana dön](#kanban)

### Sonraki konular

<a id="net-001"></a>

### NET-001 — Networking

- **Durum:** Sonra
- **Öncelik:** P3
- **Özet:** Typed protocols, bounded transport I/O, connection lifecycle ve null
  transport temeli; replication ayrı genişleme ticket'ı olarak kalır.
- **Mimari:** [Networking Architecture](./architecture/runtime/networking-architecture.md).
- **Başlama koşulu:** [RUN-001](#run-001), [SCN-001](#scn-001), [SEC-001](#sec-001).

[↑ Kanbana dön](#kanban)

<a id="gui-001"></a>

### GUI-001 — Runtime Game UI ve HUD

- **Durum:** Sonra
- **Öncelik:** P3
- **Özet:** Editor ImGui katmanından bağımsız runtime canvas, focus/navigation,
  layout, input ve rendering sözleşmesini kur.
- **Mimari:** [Game UI and HUD](./architecture/runtime/game-ui-and-hud.md).
- **Başlama koşulu:** [RUN-001](#run-001), [RND-001](#rnd-001), [INP-001](#inp-001).

[↑ Kanbana dön](#kanban)

<a id="plt-001"></a>

### PLT-001 — Platform services

- **Durum:** Sonra
- **Öncelik:** P3
- **Özet:** Achievements, cloud saves, presence ve friends için backend-neutral
  frontend, offline queue ve null/test backend oluştur.
- **Mimari:** [Platform Services](./architecture/runtime/platform-services-architecture.md).
- **Başlama koşulu:** [RUN-001](#run-001), [SEC-001](#sec-001), [OBS-001](#obs-001).

[↑ Kanbana dön](#kanban)

<a id="ext-001"></a>

### EXT-001 — Extension ve gameplay module host

- **Durum:** Sonra
- **Öncelik:** P3
- **Özet:** Versioned ABI, manifest, capabilities, contribution registry ve
  unload/shutdown politikalarını çalışan host ile tamamla.
- **Mimari:** [Extension System](./architecture/extensions/plugin-system.md),
  [Gameplay Module](./architecture/extensions/gameplay-module.md).
- **Başlama koşulu:** [RUN-001](#run-001), [SEC-001](#sec-001), [PKG-001](#pkg-001).

[↑ Kanbana dön](#kanban)

<a id="pfb-001"></a>

### PFB-001 — Prefab sistemi

- **Durum:** Sonra
- **Öncelik:** P3
- **Özet:** Stable prefab asset identity, instance overrides, nested expansion,
  cycle validation ve editor apply/revert workflow'unu oluştur.
- **Mimari:** [Prefab Architecture](./architecture/runtime/prefab-architecture.md).
- **Başlama koşulu:** [SCN-001](#scn-001), [AST-001](#ast-001), [DOC-001](#doc-001).

[↑ Kanbana dön](#kanban)

<a id="adv-001"></a>

### ADV-001 — Advanced renderer özellikleri

- **Durum:** Sonra
- **Öncelik:** P3
- **Özet:** Lighting, shadows, PBR, post-processing, culling, decals ve virtual
  texturing temel renderer/resource sistemi kapandıktan sonra ayrı ticketlara
  bölünecek.
- **Mimari:** [Advanced Rendering](./architecture/runtime/advanced-rendering-architecture.md),
  [LOD and Culling](./architecture/runtime/lod-and-culling-architecture.md).
- **Başlama koşulu:** [RND-001](#rnd-001), [MAT-001](#mat-001), [AST-001](#ast-001).

[↑ Kanbana dön](#kanban)

<a id="adv-002"></a>

### ADV-002 — Gelişmiş dünya sistemleri

- **Durum:** Sonra
- **Öncelik:** P3
- **Özet:** Animation, VFX, terrain, world streaming, navigation, cinematic,
  destruction, PCG, multiplayer ve XR alanları temel runtime sonrasında kendi
  milestone ve ticket setlerine ayrılacak.
- **Mimari:** İlgili belgeler
  [`docs/architecture/runtime/`](./architecture/runtime/) altında normatiftir.
- **Başlama koşulu:** [RUN-001](#run-001), [SCN-001](#scn-001), [AST-001](#ast-001).

[↑ Kanbana dön](#kanban)

### Tamamlanan dar temeller

<a id="bas-001"></a>

### BAS-001 — Foundation primitives

- **Durum:** Tamamlandı
- **Özet:** Result/error, paths/time/handles, cancellation/progress,
  configuration, diagnostics, jobs, platform ve data bus için çalışan public
  temel ve regression testleri vardır.
- **Kanıt:** `HoroFoundation*`, `HoroPlatformTests` ve `HoroDataBusTests`.
- **Son doğrulama:** 2026-07-17 — skeleton suite içinde geçti.
- **Devam:** Geniş kontrat denetimi [FND-001](#fnd-001).

[↑ Kanbana dön](#kanban)

<a id="bas-002"></a>

### BAS-002 — Scene math baseline

- **Durum:** Tamamlandı
- **Özet:** Vector, quaternion, matrix/TRS, perspective/orthographic, project,
  unproject, rays, bounds ve fallible math sözleşmesinin çalışan temeli vardır.
- **Kanıt:** `include/Horo/Math/SceneMath.h`, `HoroSceneMathTests`.
- **Son doğrulama:** 2026-07-17 — skeleton suite içinde geçti.
- **Devam:** Tüketici denetimi [MTH-001](#mth-001).

[↑ Kanbana dön](#kanban)

<a id="bas-003"></a>

### BAS-003 — Runtime input baseline

- **Durum:** Tamamlandı
- **Özet:** Backend-neutral snapshot, contexts, actions, capture, profiles,
  virtual gamepad ve SDL adapter için çalışan temel vardır.
- **Kanıt:** `include/Horo/Runtime/Input.h`, `HoroInputTests`, `HoroInputSdl`.
- **Son doğrulama:** 2026-07-17 — skeleton suite içinde geçti.
- **Devam:** Editor entegrasyon denetimi [INP-001](#inp-001).

[↑ Kanbana dön](#kanban)

<a id="bas-004"></a>

### BAS-004 — Editor host ve workspace baseline

- **Durum:** Tamamlandı
- **Özet:** Welcome/project routes, screen/modal host, workspace layout/panel
  host, settings, localization, hierarchy/selection ve temel viewport controller
  için çalışan model ve render testleri vardır.
- **Kanıt:** `HoroEngine::EditorModel`, `HoroEngine::EditorServices`,
  `HoroEngine::Gui` ve editor controller/model/render testleri.
- **Son doğrulama:** 2026-07-17 — ilgili 24 editor testi skeleton suite içinde geçti.
- **Devam:** Lifecycle hardening [EDT-001](#edt-001).

[↑ Kanbana dön](#kanban)

<a id="bas-005"></a>

### BAS-005 — Hierarchy create ve typed primitives

- **Durum:** Tamamlandı
- **Özet:** Menu bar ve hierarchy context menu aynı typed primitive catalog ve
  create use-case yolunu kullanır; scene objects component verisini undo/redo ile
  korur.
- **Kanıt:** `PrimitiveCatalog`, `SceneDocument`, hierarchy/menu/controller testleri.
- **Son doğrulama:** 2026-07-17 — skeleton suite içinde geçti.
- **Devam:** Kalıcılık denetimi [DOC-001](#doc-001).

[↑ Kanbana dön](#kanban)

<a id="bas-006"></a>

### BAS-006 — Procedural mesh ve viewport baseline

- **Durum:** Tamamlandı
- **Özet:** Box, Sphere, Capsule, Cylinder, Cone, Plane ve Quad deterministic
  generic mesh generation/cache/extraction ile OpenGL ve Metal viewport'ta çizilir.
- **Kanıt:** `HoroPrimitiveMeshTests`, viewport scene/picking testleri, OpenGL ve
  Metal smoke/first-frame testleri.
- **Son doğrulama:** 2026-07-17 — skeleton suite içinde geçti.
- **Devam:** Public resource geçişi [RND-001](#rnd-001).

[↑ Kanbana dön](#kanban)

<a id="bas-007"></a>

### BAS-007 — macOS app identity

- **Durum:** Tamamlandı
- **Özet:** HoroEditor macOS'ta terminal executable görünümü yerine isim, bundle
  identifier, version ve çok çözünürlüklü icon taşıyan `.app` bundle üretir.
- **Kanıt:** `apps/CMakeLists.txt`, `apps/HoroEditor/resources/HoroEditor.icns` ve
  üretilen `Info.plist`.
- **Son doğrulama:** 2026-07-17 — `HoroEditor.app` build ve bundle metadata kontrolü geçti.
- **Devam:** Signing/notarization [REL-001](#rel-001) kapsamındadır.

[↑ Kanbana dön](#kanban)

## Yeni Ticket Şablonu

Yeni ticket açarken aşağıdaki yapıyı kopyalayın ve kanban tablosuna aynı anchor'ı
linkleyin:

```markdown
<a id="area-000"></a>

### AREA-000 — Kısa sonuç odaklı başlık

- **Durum:** Hazır
- **Öncelik:** P0/P1/P2/P3
- **Özet:** Çözülecek tek mimari veya ürün boşluğu.
- **Kabul:** Gözlenebilir sonuç ve zorunlu regression kanıtı.
- **Mimari:** Owning architecture document linki.
- **Bağımlılık:** [AREA-000](#area-000) veya Yok.
- **Son doğrulama:** YYYY-MM-DD — komut ve sonuç.

[↑ Kanbana dön](#kanban)
```

## Envanter Notu

Bu ilk kanban snapshot'ı 2026-07-17 tarihinde şu kanıtlarla oluşturuldu:

- `docs/architecture/README.md`, `foundation/system-design.md` ve
  `desired-project-tree.md` okuma sırası;
- public header, CMake target, implementation file ve test registration taraması;
- placeholder klasörlerde non-empty source kontrolü;
- `ctest --test-dir build/skeleton --output-on-failure` sonucu: **47/47 geçti**;
- mevcut local worktree korundu; kanban hiçbir değişikliği commit veya stage etmedi.
