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

## Lancer

Double-cliquez sur `build/release/ScreenFX.exe`. Gardez le dossier `shaders` à côté de l’exécutable : il contient les deux shaders compilés nécessaires au rendu. Aucun terminal ni CMake n’est nécessaire pour lancer la version déjà compilée.

Le panneau s’ouvre; cochez **Filtre actif**. Les effets sont neutres au premier lancement : augmentez **Scanlines** et **Masque phosphore RGB** pour obtenir l’aspect CRT. Le bouton **Enregistrer** conserve les réglages. La croix masque le panneau; **Ctrl+Alt+F11** ou un nouveau double-clic sur l’exécutable le ramène. Pour quitter, utilisez le menu de l’icône ScreenFX près de l’horloge.

Cible : Windows 10 version 2004 ou ultérieure, x64, GPU compatible Direct3D 11. Le rendu est SDR; le HDR et les jeux en plein écran exclusif ne sont pas validés. Les images de capture restent en mémoire GPU.

## Développement

Installez Visual Studio avec **Développement Desktop en C++**, les outils **CMake pour Windows** et le **SDK Windows**. Depuis un PowerShell ordinaire dans le dossier du projet :

```powershell
.\build.ps1
```

Le script retrouve Visual Studio, configure et compile Release, puis exécute les tests. Pour Debug : `.\build.ps1 -Configuration Debug`. Il ne modifie pas le PATH de votre session.

Depuis un terminal développeur Visual Studio, les commandes CMake directes restent disponibles :

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure
```

Le panneau ne télécharge aucune dépendance externe.

Pour produire un dossier installable :

```powershell
.\build.ps1 -Package
```

Le programme installé et ses deux fichiers shader sont placés ensemble dans `dist/bin`. Le runtime C++ est lié statiquement.

Pour tester la capture et la présentation dans votre session Windows :

```powershell
.\build\release\screenfx_graphics_tests.exe --desktop
```

Ce test vérifie les pixels des shaders, trois redémarrages de capture, les deux modes de présentation et le redimensionnement. Une petite mire colorée vérifie ensuite l’affichage visible, le passage des clics et l’exclusion de capture. Elle disparaît automatiquement; aucune image n’est enregistrée. Ce test nécessite une session de bureau déverrouillée. Les tests CTest ordinaires n’affichent pas cette mire.

## Dépannage

La superposition reste masquée jusqu’à la première présentation réussie. Si aucune image n’est présentée en cinq secondes, ou si une erreur de capture/rendu survient, le filtre s’arrête et le panneau indique la cause. **Ctrl+Alt+F12** permet aussi de l’arrêter.

Si le panneau indique `0x80070424`, relancez l’application depuis votre session Windows interactive. Ce code a été observé dans le compte isolé des outils de test, alors que la capture fonctionne dans la session utilisateur; il ne justifie pas à lui seul de modifier les services Windows.

Les réglages sont dans `%LOCALAPPDATA%\ScreenFX\settings.json`. Un fichier invalide ou d’une version inconnue est conservé, et sa sauvegarde est refusée avec un message. Pour repartir des valeurs initiales, quittez ScreenFX puis renommez ce fichier afin d’en garder une copie.

## Raccourcis

- `Ctrl+Alt+F10` : activer ou désactiver le filtre
- `Ctrl+Alt+F11` : afficher le panneau
- `Ctrl+Alt+F12` : arrêter immédiatement
