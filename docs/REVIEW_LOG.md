# Journal des revues

Les premiers lots ci-dessous décrivent leurs vérifications historiques. Le test de fumée ne vérifiait que l’énumération des moniteurs : il ne suffisait pas à valider la capture ou l’affichage. Les tests de pixels et de bureau ajoutés en P27 remplacent cette limite.

## P00 — initialiser le projet

- Vérifications : configuration CMake, compilation Debug et test `screenfx_smoke`.
- Revue : les identifiants Win32 et les entrées de menu utilisent des types explicites ; aucun problème restant.

## P03–P06 — capture et superposition

- Vérifications : compilation MSVC/Debug, compilation des deux shaders HLSL et test `screenfx_smoke`.
- Revue : les appels Direct3D sont sérialisés par le mutex du device ; la session de capture copie les images dans une texture appartenant à l’application avant de libérer le frame Windows ; la fenêtre de sortie est marquée `WDA_EXCLUDEFROMCAPTURE` et retourne `HTTRANSPARENT` pour les interactions.
- Limites à valider manuellement : rendu réel sur l’écran, recapture par Windows Graphics Capture et comportement des clics sur plusieurs processus.

## P07–P20 — panneau, réglages et cadence

- Vérifications : compilation MSVC/Debug, génération des shaders HLSL et test `screenfx_smoke`.
- Revue : le panneau utilise uniquement des contrôles Win32 et reste disponible même si Direct3D ne peut pas démarrer ; les réglages sont bornés, sérialisés avec une précision stable et écrits par remplacement atomique ; le mode par défaut passe `MinUpdateInterval(0)` et présente avec `Present(0, DXGI_PRESENT_ALLOW_TEARING)` quand le matériel le permet.
- Robustesse : l’arrêt révoque le callback WGC et attend les callbacks en cours ; les frames remplacées sont comptées ; les redimensionnements et redémarrages de capture vérifient leurs erreurs ; l’attente du moteur se fait sur l’événement de frame sans réveil périodique de 2 ms.
- Limites à valider manuellement : débit réel du compositeur Windows, compatibilité de `MinUpdateInterval(0)` selon la version de Windows, et rendu sur plusieurs moniteurs avec fréquences différentes.

## P21 — distribution et ergonomie

- Vérifications : compilation Release, test `screenfx_smoke` en Release et installation CMake dans un préfixe de test.
- Revue : le processus est déclaré sensible au DPI par moniteur, l’exécutable retourne une erreur si sa fenêtre ne peut pas être créée, et l’installation embarque les deux fichiers shader dans `bin/shaders`.

## P26 — éviter l’écran noir sans capture

- Vérifications : compilation Debug et Release, test de fumée CTest dans les deux configurations.
- Revue : l’overlay reste caché jusqu’au rendu d’une première frame valide ; les erreurs de `FrameArrived` sont conservées et affichées dans le panneau ; la destruction de l’overlay réinitialise son état d’exclusion.
- Diagnostic observé dans le compte isolé utilisé par les outils : `CreateForMonitor` renvoyait `0x80070424`. Ce résultat ne décrivait pas la session interactive de l’utilisateur et n’expliquait pas à lui seul son écran noir.

## P27 — réparer et vérifier la capture, le rendu et les réglages

- Cause de capture corrigée : extraire la texture par `IDirect3DDxgiInterfaceAccess::GetInterface`; une requête directe `ID3D11Texture2D` sur la surface WinRT échouait.
- Durée de vie : les callbacks conservent un état de session indépendant; après l’arrêt ils ne touchent plus l’application ni le device. Quatre textures réutilisables sont protégées par un bail tant qu’une image est consommée. Chaque frame Windows est fermée, y compris en cas d’exception.
- Présentation : swapchain DirectComposition `FLIP_SEQUENTIAL`, détachement des ressources avant redimensionnement/destruction, protection multithread D3D11 et propagation des erreurs GPU. La superposition utilise les styles Windows permettant aux clics de traverser les processus.
- Shader : le seuil maximal du halo ne produit plus un `smoothstep` dégénéré. Les shaders sont recopiés même lorsqu’une modification ne relance pas l’édition de liens.
- Réglages : validation JSON complète, types et versions vérifiés, nombres finis bornés, fichier limité à 64 Ko, fichiers invalides préservés et remplacement atomique par fichier temporaire unique.
- Tests Debug réussis : CTest 3/3 (moniteurs, pixels WARP, réglages). Le test de pixels compare l’identité, les scanlines/masque, le contournement des effets et le halo maximal; la couche de validation D3D11 ne signale pas d’erreur dans ce test.
- Tests interactifs réussis : `screenfx_graphics_tests --desktop` dans la session utilisateur. Trois cycles capture/arrêt, pixels du bureau non noirs, présentation sans plafond et VSync, redimensionnement, affichage réel d’une petite mire, traversée du hit-test et exclusion de la superposition vérifiée par les pixels verts du fond. Aucune image n’est sauvegardée par ce test.
- Revue : lecture des fichiers et du diff, vérification des verrous et de l’ordre de destruction. Les travaux partiels des sous-agents ont été intégrés puis revérifiés localement; leur revue finale indépendante n’a pas abouti à cause de la limite de session.

## P28 — fiabiliser le panneau et le cycle de vie

- Activation : traiter les actions avant de choisir l’événement d’attente évite de rester bloqué après la première activation. La superposition n’apparaît qu’après une présentation réussie; erreur de capture/GPU ou délai initial de cinq secondes : elle est retirée et le panneau explique l’arrêt.
- Redémarrage : masquer l’ancienne image avant changement de moniteur ou de cadence. Garder le panneau au-dessus du filtre. Conserver les raccourcis disponibles et refuser l’activation si l’arrêt d’urgence est occupé.
- Fermeture : libérer les raccourcis et l’icône avant de perdre le HWND; détruire les objets WinRT avant l’appartement; intercepter les erreurs au point d’entrée. Un deuxième lancement retrouve le panneau du processus existant.
- Réglages : afficher les erreurs de chargement et de sauvegarde. Le panneau adapte ses polices au DPI, offre le défilement et la navigation Tab, et actualise la liste des moniteurs même si leur nombre ne change pas.
- Vérifications : compilation Debug et CTest 3/3. Contrôle Computer Use du panneau sur le bureau 3840 × 2160 : capture et présentation progressent, Ctrl+Alt+F12 arrête le filtre, la case d’activation relance les images. Un deuxième lancement retrouve le même HWND et un seul processus. Le test automatisé P27 vérifie séparément le rendu en VSync et sans plafond.
- Limites matérielles non simulées : débranchement réel d’un écran, changement de pilote, plusieurs DPI simultanés, HDR et jeux exclusifs. Ces cas ne sont pas présentés comme validés.

## P29 — reconstruire et préparer le lancement direct

- `build.ps1` détecte Visual Studio avec `vswhere`, initialise son environnement dans un processus enfant, configure le projet, compile et exécute CTest. L’option `-Package` installe le programme et ses shaders dans `dist/bin`.
- Runtime MSVC lié statiquement : inspection des dépendances de l’exécutable Release, aucune DLL redistribuable VC++ requise.
- Vérifications finales réussies : script en Debug et Release, CTest 3/3 dans chaque configuration, installation, puis test `--desktop` sur la version Release dans la session interactive. Les empreintes de l’exécutable et des deux shaders correspondent entre la compilation et la distribution. `git diff --check` ne signale pas d’erreur.
- Livrables : `build/release/ScreenFX.exe` et `dist/bin/ScreenFX.exe`, chacun accompagné de son dossier `shaders`. L’instance Debug ouverte pour les tests a été fermée.
- Documentation : lancement par double-clic, effets initialement neutres, raccourcis, reconstruction sans CMake dans le PATH, dépannage et limites de compatibilité décrits dans le README.

## P30 — GPU color tint

- Added RGB tint and strength, with neutral defaults compatible with existing settings. Tint multiplies the processed image before the global effect blend; setting tint strength or global intensity to zero bypasses it.
- Reviewed the CPU/HLSL constant layout and asserted its size and offsets. Pixel tests cover exact RGB channels, 50% strength and both bypass controls.
- Validation: Debug and Release graphics tests passed, including the Release desktop test for visible presentation, capture exclusion, click-through hit testing, resizing and three capture restarts.

## P31 — English interface, manual custom presets and color picker

- Translated application labels, status/error messages, tray menu, README and build-script messages to English.
- Added an editable preset name list with explicit **Save preset** and **Apply preset** actions. Saving a matching name updates it; applying copies only effect values. Selecting, typing, changing effects, applying and closing never write the custom preset file.
- Added versioned `presets.json` storage beside `settings.json`, reusing bounded JSON validation and atomic file replacement. Names support Unicode and escaping; duplicates, invalid names, non-finite numbers and unsupported file versions are rejected without replacing existing data. Limits: 64 presets, 80 UTF-16 code units per name and 256 KiB per preset document.
- Added the native color picker, a color swatch/hex label and tint-strength slider. The overlay is hidden during the modal dialog, then shown only after presentation resumes. Selecting a color activates tint if its strength was zero; cancellation preserves it.
- Independent UI review found two minor issues, both fixed: keyboard order for the tint button and a README label mismatch. Root reviewed the storage implementation and integrated fixture tests after the storage sub-agent stopped progressing.
- Validation: Debug and Release CTest 5/5. Tests cover saved preset Unicode/tint roundtrips, corruption/version protection, limits, failed replacement, explicit-only UI actions and tint synchronization. The panel/preset test timeouts allow 30 seconds because first launches in this test environment took about 20 seconds; subsequent Release tests all completed in 0.22 seconds total.
- Interactive check: English panel layout and native color dialog verified; Ctrl+Alt+F12 stopped capture while the dialog was open, and cancelling returned to the stopped panel with the same color. Test presets were written only in temporary fixture folders, not the user's preset library.
- Release package rebuilt in `build/release` and `dist/bin`, with both compiled shaders. The Debug instance used for UI checks was closed.

## P32 — Reduce GPU work and reuse capture texture views

- Added uniform shader branches for disabled effects, zero global intensity, neutral color adjustments and full-strength blending. Preserved effect order, thresholds, clamping and opaque output. Rewrote branch exits to eliminate FXC uninitialized-variable warnings.
- Reused up to four shader resource views across capture texture rotations, with strong COM references, bounded replacement, and clearing on resize/shutdown. Capture leases, copy ordering and synchronization remain intact. Removed the redundant full-target black clear.
- Root reviewed the renderer lifetime/locking and shader math. The shader sub-agent supplied the initial optimization; root compared alternatives on hardware and retained the version with the strongest measured neutral/CRT gains. Extra attempts to launch an independent renderer reviewer hit the agent limit, so that portion received root review.
- Added a frozen P31 shader reference, 95 pixel comparisons using identical animation constants, 100 texture rotations with view-identity/pixel checks, and cache eviction/cleanup checks. Default verification uses WARP; an opt-in hardware benchmark measures GPU timestamps with warmup and alternating samples.
- Validation: Debug/Release CTest 6/6, warning-free shader compilation, hardware equivalence with maximum channel difference zero, and successful Release `--desktop` capture/composition/resize/exclusion tests. On RX 6600 at 4K, the final neutral and CRT-with-tint GPU passes took 55.9% and 31.0% less time; the heavy case took 4.9% more. These are shader-pass measurements, not end-to-end FPS or CPU measurements. Method and limits are in `PERFORMANCE.md`.

## P33 — Reduce panel redraws while keeping controls responsive

- Throttled only frame-counter labels to a 250 ms interval, with selective updates. Errors/status and capture exclusion remain immediate. Hidden/minimized panels skip synchronization and refresh when shown again, including when the application skipped all hidden Render calls.
- Replaced per-frame monitor-label allocation with metadata comparisons and cached slider positions, checkboxes, selections and tint display. Custom preset storage still requires the explicit Save preset action.
- A dedicated panel-review sub-agent completed the initial UI patch and found a stale snapshot after a user edit followed by a reset/rejected change before Render. It fixed the snapshot updates and added regression tests; root reviewed the diff and ran the builds.
- Deterministic tests count actual Win32 messages around the 250 ms boundary, check immediate errors/exclusion, control rollback, monitor changes, and visibility restoration. They also retain the explicit-only preset action tests.
- Validation: final Debug and Release CTest 6/6; Release installed in `dist/bin`. Executable and both production shader hashes match `build/release`. No application process was left running by the tests. The new benchmark/reference shader are excluded from the installed package.

## P34 — Reduce Windows VSync latency

- Created a DXGI frame-latency waitable swapchain. VSync sets the maximum presentation queue to one frame; Uncapped retains the usual limit of three without adding a refresh wait.
- The main loop waits for presentation readiness alongside capture events and Windows messages, before acquiring the newest captured frame. Readiness is latched when an auto-reset event is consumed; an idle desktop does not spin while waiting for capture.
- VSync uses nonblocking `Present` and retries `DXGI_ERROR_WAS_STILL_DRAWING` without stopping capture. DXGI and the immediate context remain serialized as required, but no explicit VSync sleep holds their shared lock. Resize retains the waitable flag, and shutdown/destruction closes the event.
- The VSync sub-agent implemented the change and tests; root reviewed the diff and ran validation after the sub-agent reached its usage limit. Tests cover event latching, idle behavior, cleanup/restart, queue depth, mode switching, resizing, capture progress and 40 real VSync frames.
- Validation: Debug/Release CTest 6/6 and the Release desktop regression passed on the RX 6600. For the 40-frame test, CPU draw-plus-Present duration was 0.1327 ms median and 0.179 ms maximum. This measures submission cost, not input-to-photon latency; a capture overlay still has capture/compositor/display latency. Release package rebuilt in `build/release` and `dist/bin`.

## P35 — Linux compositor backends and Qt settings panel

- Added a C++20/Qt 6 Linux application with English effect controls, numeric values, a color picker, explicit Save settings and Save preset actions, preset application, single-instance D-Bus control and a terminal-accessible `--stop`. Linux starts stopped unless `--enable` is requested; closing the panel restores the compositor before exiting.
- Added a native KWin 6.6 effect and a Hyprland screen-shader controller. Rendering stays in the compositor and adds no screen-capture overlay. KWin manages per-output GPU buffers and stops on controller disappearance; Hyprland remembers previous runtime state, rolls back failed activation and uses a helper for controller-crash recovery. A shader selected by the user after ScreenFX is preserved. Compositor synchronization remains authoritative; no Linux software FPS limiter was introduced.
- Shared effect definitions feed both Linux GLSL targets. Added versioned JSON storage under the XDG configuration root, compatible effect/preset fields, bounded validation, rejection of duplicate JSON keys and unsafe file paths, atomic replacement and preservation of invalid or unsupported files. Custom presets are written only by an explicit Save preset action.
- Validation: Ubuntu 26.04/WSL builds succeeded with Qt 6.10.2 and KWin 6.6.6. Linux CTest passed 4/4: settings/presets, real Qt panel save actions, Mesa GLES 3.0 and GLSL 1.40 pixel tests, and backend IPC/rollback/recovery/crash-guard fixtures. An independent Hyprland-only build also passed its tests. Windows Release CTest passed 6/6 after the cross-platform CMake changes, and its package was refreshed.
- Desktop validation remains incomplete. The optional nested-KWin test detected QPainter instead of OpenGL because this environment has no `/dev/dri`, and returned SKIP (77). No physical KDE or Hyprland session, multi-monitor output, HDR/color-management behavior, Linux GPU performance or end-to-end latency has been validated. Hyprland fixtures simulate its CLI; they do not prove compatibility with a live compositor.
- Sub-agents supplied the GLSL port, JSON store, documentation and a bounded backend review. Root integrated them, reviewed the KWin rendering calls against its current native effects, and completed validation. The independent reviewer identified a failed-restoration bug: after restoring the prior shader, a rejected damage command made a retry lose ownership. Recovery now journals the restoration phase before changing the shader; a regression test rejects the damage restore and verifies the next Stop repairs both values.
- Hyprland uses `hyprctl -j status` to select legacy keyword commands or Lua `hl.config` expressions. Paths are encoded as fixed-width UTF-8 byte escapes in Lua; tests cover quotes, backslashes, Unicode and newlines. First activation with animated grain establishes a static shader before disabling damage tracking, then activates animation, avoiding Hyprland's time-uniform error during setup.
- Installation and use are documented in `LINUX.md`, including KWin ABI matching, configuring the Qt plugin destination instead of assuming `/usr/local` is searched, explicit JSON saves, runtime recovery, the optional diagnostic harness and manifest-based uninstallation. Documentation review checked CLI options, save behavior, backend APIs and CMake install rules against the implementation. Live compositor compatibility remains unverified for both Hyprland configuration providers.
