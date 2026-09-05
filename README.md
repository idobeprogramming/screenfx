# ScreenFX

ScreenFX est une application Windows qui capture un moniteur, applique des effets GPU et restitue le résultat dans une superposition plein écran. La première cible est le bureau et les jeux en fenêtre ou sans bordure.

## Stack

- C++20, Win32 et C++/WinRT
- Windows Graphics Capture
- Direct3D 11, DXGI et DirectComposition
- HLSL Shader Model 5
- Dear ImGui pour le panneau de réglages
- CMake + Ninja + MSVC

Le mode de fréquence par défaut sera sans plafond logiciel. Le débit réellement affiché dépend de Windows, du GPU et de l’écran.

## Développement

Le projet est conçu pour être exécuté par petits lots. Chaque lot doit être compilé, relu dans le diff, puis commitée avant de passer au suivant.

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure
```

Le générateur Ninja doit être lancé depuis un terminal développeur Visual Studio afin que MSVC soit disponible.

## Raccourcis

- `Ctrl+Alt+F10` : activer ou désactiver le filtre
- `Ctrl+Alt+F11` : afficher le panneau
- `Ctrl+Alt+F12` : arrêter immédiatement
