; Instalador de StemLab para Windows (Inno Setup 6).
;
; No se compila a mano: lo hace installer\build-installer.ps1, que prepara la
; carpeta StagingDir (StemLab.exe + python\ con los scripts y un Python
; autónomo con Demucs) y ejecuta:
;
;     ISCC.exe /DStagingDir=<carpeta> /DLongestPath=<n> /O<salida> installer\StemLab.iss
;
; (LongestPath: la longitud de la ruta más larga dentro de StagingDir.)
;
; Se instala solo para el usuario actual, sin permisos de administrador, en
; %LOCALAPPDATA%\Programs\StemLab. La versión sale del propio StemLab.exe (la
; de project() en CMakeLists.txt): no hay que cambiarla aquí.

#if !Defined(StagingDir) || !Defined(LongestPath)
  #error Faltan /DStagingDir y /DLongestPath: ejecuta installer\build-installer.ps1
#endif

#define FullVersion GetVersionNumbersString(AddBackslash(StagingDir) + "StemLab.exe")
#define AppVersion Copy(FullVersion, 1, RPos(".", FullVersion) - 1)

[Setup]
; Identificador fijo: con él, instalar otra versión actualiza la anterior.
AppId={{B5301BBB-082C-4D9F-9334-116B74D46CC8}
AppName=StemLab
AppVersion={#AppVersion}
AppVerName=StemLab {#AppVersion}
AppPublisher=StemLab
AppPublisherURL=https://github.com/Andres-Maya/StemLab
AppSupportURL=https://github.com/Andres-Maya/StemLab/issues
AppUpdatesURL=https://github.com/Andres-Maya/StemLab/releases
VersionInfoVersion={#FullVersion}
DefaultDirName={autopf}\StemLab
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
; Refresca los iconos de los .stemlab al instalar y al desinstalar.
ChangesAssociations=yes
CloseApplications=yes
OutputBaseFilename=StemLab-Setup
SetupIconFile=..\Resources\StemLab.ico
UninstallDisplayIcon={app}\StemLab.exe
UninstallDisplayName=StemLab
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
LZMANumBlockThreads=4

[Languages]
; El asistente usa el idioma de Windows si es uno de estos (si no, inglés).
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[InstallDelete]
; Al actualizar, el Python de la versión anterior se sustituye entero: no
; quedan paquetes antiguos mezclados con los nuevos.
Type: filesandordirs; Name: "{app}\python"

[Files]
Source: "{#StagingDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\StemLab"; Filename: "{app}\StemLab.exe"
Name: "{autodesktop}\StemLab"; Filename: "{app}\StemLab.exe"; Tasks: desktopicon

[Registry]
; StemLab asocia los .stemlab al arrancar (Source/Application/FileAssociation):
; el instalador no los crea, solo los quita al desinstalar.
Root: HKCU; Subkey: "Software\Classes\.stemlab"; Flags: dontcreatekey uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\StemLab.Project"; Flags: dontcreatekey uninsdeletekey

[UninstallDelete]
; Lo que se crea al usarlo: la caché de Python y el icono de los proyectos.
; Los ajustes (AppData\Roaming\StemLab) y los proyectos del usuario se quedan.
Type: filesandordirs; Name: "{app}\python"
Type: filesandordirs; Name: "{localappdata}\StemLab\Icons"
Type: dirifempty; Name: "{localappdata}\StemLab"

[Run]
Filename: "{app}\StemLab.exe"; Description: "{cm:LaunchProgram,StemLab}"; Flags: nowait postinstall skipifsilent

[Code]
// Windows no admite rutas de más de 259 caracteres (MAX_PATH) y la más larga
// de python\ ya ocupa {#LongestPath}: con una carpeta más larga, la instalación
// fallaría a medias. Se avisa antes de empezar (también con /SILENT y /DIR=).
const
  MaxDirLength = 258 - {#LongestPath};

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := (CurPageID <> wpSelectDir) or (Length(WizardDirValue) <= MaxDirLength);

  if not Result then
    SuppressibleMsgBox('La ruta de la carpeta de instalación es demasiado larga para Windows. Elige una de '
      + IntToStr(MaxDirLength) + ' caracteres como máximo.', mbError, MB_OK, IDOK);
end;
