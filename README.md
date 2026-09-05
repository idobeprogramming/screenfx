# ScreenFX

ScreenFX est une application Windows qui capture un moniteur, applique des effets GPU et restitue le résultat dans une superposition plein écran. La première cible est le bureau et les jeux en fenêtre ou sans bordure.

## Stack

- C++20, Win32 et C++/WinRT
- Windows Graphics Capture
- Direct3D 11, DXGI et DirectComposition
- HLSL Shader Model 5
- Contrôles Win32 natifs pour le panneau de réglages
- CMake + Ninja + MSVC

Le mode de fréquence par défaut est sans plafond logiciel. Le débit réellement affiché dépend de Windows, du GPU et de l’écran.

## Développement

Le projet est conçu pour être exécuté par petits lots. Chaque lot doit être compilé, relu dans le diff, puis commité avant de passer au suivant.

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure
```

Le générateur Ninja doit être lancé depuis un terminal développeur Visual Studio afin que MSVC soit disponible. Le panneau ne télécharge aucune dépendance externe.

Pour produire un dossier installable :

```powershell
cmake --build --preset windows-release
cmake --install build/release --prefix dist
```

Le programme installé et ses deux fichiers shader sont placés ensemble dans `dist/bin`.

## Dépannage

L’overlay reste masqué tant qu’aucune image valide n’a été reçue. Si le panneau indique
`0x80070424`, le service Windows Graphics Capture n’est pas disponible dans la session
qui a lancé le programme. Fermez cette instance et relancez `ScreenFX.exe` depuis la
session Windows interactive de l’utilisateur, plutôt que depuis un service ou un compte
technique.

## Raccourcis

- `Ctrl+Alt+F10` : activer ou désactiver le filtre
- `Ctrl+Alt+F11` : afficher le panneau
- `Ctrl+Alt+F12` : arrêter immédiatement
