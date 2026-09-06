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
