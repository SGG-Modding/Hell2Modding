# Contributing guide

## Windows - C++ development environment setup

There's usually 2 main ways of developing C++ code on Windows: 
1. Heavy: Using the dedicated Visual Studio Community IDE.
2. Light: Installing only the necessary MSVC C++ build tools for compilation.

> [!TIP]
> Both approaches install all the necessary tools and thus allow for code development outside of Visual Studio IDE.
> With that said, the heavy installation with the IDE is usually the recommended beginner friendly approach as the installer automatically does most of the heavy lifting for you.

### Heavy - Visual Studio IDE
Grab the latest version of Visual Studio Community on the [download page](https://visualstudio.microsoft.com/downloads/) and follow the [installation instructions](https://learn.microsoft.com/en-us/cpp/build/vscpp-step-0-installation?view=msvc-170) on the official microsoft page.

> [!TIP]
> Make sure that at least **Desktop development with C++** workload is selected

~~Launch the IDE and start hating it!~~

### Light - Build Tools only

#### CLI install:
> [!NOTE]
> If you prefer to avoid running unknown commands, follow the [Manual install](#manual-install) section below instead.

Launch a powershell terminal from anywhere then run the following to download the *build tools executable*:
```powershell
Invoke-WebRequest -Uri "https://aka.ms/vs/stable/vs_buildtools.exe" -OutFile "vs_buildtools.exe"
```
Run the following to install the **Desktop development with C++** workload with the recommended components:
```powershell
./vs_buildtools.exe --quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended 
```
This will launch the visual studio installer, it may ask for an elevation of priviledges then it will close itself and install the tools in the background (because of the `--quiet` option). 

After a moment, depending on your internet speed, the tools will be installed on your system. 
The installation can be checked by running:
```powershell
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
```
If the installation was successful, it should display the following: 
> C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools

Otherwise it may have not yet finished installing.

> [!TIP]
> Alternatively, you can also manually check inside the directory `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools` which will be populated when it has finished installing.

#### Manual install:

Download the latest build tools from the [download page](https://visualstudio.microsoft.com/downloads/).

> [!TIP]
> For some reason, the download button is hidden at the bottom of the page.
> Go under the section **[All Downloads]**, expand **[Tools for Visual Studio]** and get **Build Tools for Visual Studio 2026**

Launch the executable then from the visual studio installer page, make sure to select the **Desktop development with C++** among other tools you may be interested in. 

## Visual Studio Code setup

Get the extension **C/C++ Extension Pack** from the marketplace (`ms-vscode.cpptools-extension-pack`)  

> WIP
