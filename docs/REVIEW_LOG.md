# Journal des revues

Les lots sont validés après compilation, test de fumée et revue du diff.

## P00 — initialiser le projet

- Vérifications : configuration CMake, compilation Debug et test `screenfx_smoke`.
- Revue : les identifiants Win32 et les entrées de menu utilisent des types explicites ; aucun problème restant.

## P03–P06 — capture et superposition

- Vérifications : compilation MSVC/Debug, compilation des deux shaders HLSL et test `screenfx_smoke`.
- Revue : les appels Direct3D sont sérialisés par le mutex du device ; la session de capture copie les images dans une texture appartenant à l’application avant de libérer le frame Windows ; la fenêtre de sortie est marquée `WDA_EXCLUDEFROMCAPTURE` et retourne `HTTRANSPARENT` pour les interactions.
- Limites à valider manuellement : rendu réel sur l’écran, recapture par Windows Graphics Capture et comportement des clics sur plusieurs processus.
