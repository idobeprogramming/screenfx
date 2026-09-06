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
