# Journal des revues

Les lots sont validés après compilation, test de fumée et revue du diff.

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
