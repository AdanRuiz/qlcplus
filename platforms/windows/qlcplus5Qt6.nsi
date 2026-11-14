;--------------------------------
;Include Modern UI
  !include "MUI2.nsh"

;--------------------------------
;Defines
!define QLCPLUS_HOME "c:\projects\qlcplus"
!define MUI_ICON "${QLCPLUS_HOME}\resources\icons\vertexav-director.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\nsis3-uninstall.ico"
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP "${NSISDIR}\Contrib\Graphics\Header\nsis3-vintage.bmp"
!define MUI_HEADERIMAGE_LEFT
!define MUI_PAGE_HEADER_TEXT "VertexAV Director"
!define MUI_COMPONENTSPAGE_NODESC

;--------------------------------
;General
Name "VertexAV Director"
OutFile "VertexAV-Director_0.2.0.exe"
InstallDir C:\VertexAV-Director
InstallDirRegKey HKCU "Software\VertexAVDirector" "Install_Dir"
RequestExecutionLevel user

!define MUI_LICENSEPAGE_TEXT_TOP "VertexAV Director is based on the Q Light Controller Plus engine. Do you accept the Apache 2.0 license?"

!insertmacro MUI_PAGE_LICENSE "${QLCPLUS_HOME}\platforms\windows\apache_2.0.txt"

;--------------------------------
; Pages
!insertmacro MUI_PAGE_COMPONENTS
Page directory
Page custom StartMenuGroupSelect "" ": Start Menu Folder"
Page instfiles

;--------------------------------
;Languages
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "Italian"
!insertmacro MUI_LANGUAGE "German"
!insertmacro MUI_LANGUAGE "Spanish"
!insertmacro MUI_LANGUAGE "SpanishInternational"
!insertmacro MUI_LANGUAGE "Czech"
!insertmacro MUI_LANGUAGE "French"
!insertmacro MUI_LANGUAGE "Finnish"
!insertmacro MUI_LANGUAGE "Japanese"
!insertmacro MUI_LANGUAGE "Catalan"

Function StartMenuGroupSelect
	Push $R1

	StartMenu::Select /checknoshortcuts "Don't create a start menu folder" /autoadd /lastused $R0 "VertexAV Director"
	Pop $R1

	StrCmp $R1 "success" success
	StrCmp $R1 "cancel" done
		; error
		MessageBox MB_OK $R1
		StrCpy $R0 "VertexAV Director" # use default
		Return
	success:
	Pop $R0

	done:
	Pop $R1
FunctionEnd

;--------------------------------
; Main application section (checked & disabled)
Section "VertexAV Director" SEC_MAIN
	SectionIn RO
	SetOutPath $INSTDIR

	# this part is only necessary if you used /checknoshortcuts
	StrCpy $R1 $R0 1
	StrCmp $R1 ">" skip
		CreateDirectory $SMPROGRAMS\$R0
		CreateShortCut '$SMPROGRAMS\$R0\VertexAV Director.lnk' $INSTDIR\qlcplus5.exe

		CreateDirectory $SMPROGRAMS\$R0
		CreateShortCut '$SMPROGRAMS\$R0\Uninstall.lnk' $INSTDIR\uninstall.exe

	skip:
SectionEnd

;--------------------------------
; Optional file association section
Section "Associate .qxw and .qxf files" SEC_ASSOC
	; Per-user classes (maps to HKCR for current user)
	WriteRegStr HKCU "Software\Classes\.qxw" "" "VertexAVDirector.Document"
	WriteRegStr HKCU "Software\Classes\VertexAVDirector.Document" "" "VertexAV Director Workspace"
	WriteRegStr HKCU "Software\Classes\VertexAVDirector.Document\DefaultIcon" "" "$INSTDIR\qlcplus5.exe,0"
	WriteRegStr HKCU "Software\Classes\VertexAVDirector.Document\shell\open\command" "" '"$INSTDIR\qlcplus5.exe" --open "%1"'

	WriteRegStr HKCU "Software\Classes\.qxf" "" "VertexAVDirectorFixture.Document"
	WriteRegStr HKCU "Software\Classes\VertexAVDirectorFixture.Document" "" "VertexAV Director Fixture"
	WriteRegStr HKCU "Software\Classes\VertexAVDirectorFixture.Document\DefaultIcon" "" "$INSTDIR\qlcplus5.exe,0"
	WriteRegStr HKCU "Software\Classes\VertexAVDirectorFixture.Document\shell\open\command" "" '"$INSTDIR\qlcplus5.exe" --open "%1"'

	; Notify Explorer to refresh icons
	System::Call 'SHELL32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd

;--------------------------------
; File installation section
Section
	File qlcplus5.exe
	File *.dll
	File *.qm
	File Sample.qxw
	File /r geometryloaders
	File /r iconengines
	File /r imageformats
	File /r multimedia
	File /r platforms
	File /r qml
	File /r renderers
	File /r sceneparsers
	File /r ColorFilters
	File /r Fixtures
	File /r Gobos
	File /r InputProfiles
	File /r Meshes
	File /r MidiTemplates
	File /r ModifiersTemplates
	File /r Plugins
	File /r RGBScripts
	File /r translations
	File /r Web

	WriteRegStr HKCU "SOFTWARE\VertexAVDirector" "Install_Dir" "$INSTDIR"

	WriteUninstaller $INSTDIR\uninstall.exe
SectionEnd

;--------------------------------
; Uninstallation

UninstPage uninstConfirm
UninstPage instfiles
Section "Uninstall"
	Delete $INSTDIR\uninstall.exe
	Delete $INSTDIR\qlcplus5.exe
	Delete $INSTDIR\*.dll
	Delete $INSTDIR\Sample.qxw
	Delete $INSTDIR\*.qm
	RMDir /r $INSTDIR\geometryloaders
	RMDir /r $INSTDIR\iconengines
	RMDir /r $INSTDIR\imageformats
	RMDir /r $INSTDIR\multimedia
	RMDir /r $INSTDIR\platforms
	RMDir /r $INSTDIR\renderers
	RMDir /r $INSTDIR\sceneparsers
	RMDir /r $INSTDIR\qml
	RMDir /r $INSTDIR\ColorFilters
	RMDir /r $INSTDIR\Fixtures
	RMDir /r $INSTDIR\Gobos
	RMDir /r $INSTDIR\InputProfiles
	RMDir /r $INSTDIR\Meshes
	RMDir /r $INSTDIR\MidiTemplates
	RMDir /r $INSTDIR\ModifiersTemplates
	RMDir /r $INSTDIR\Plugins
	RMDir /r $INSTDIR\RGBScripts
	RMDir /r $INSTDIR\translations
	RMDir /r $INSTDIR\Web

	RMDir $INSTDIR

	; Remove per-user QML cache
	RMDir /r "$LOCALAPPDATA\vertexav\VertexAV Director"
	RMDir "$LOCALAPPDATA\vertexav"

	DeleteRegKey HKCU "Software\VertexAVDirector"

	; Remove file associations (per-user), but only the ones that still point
	; here: QLC+ may be installed too and own the extensions by now
	ReadRegStr $0 HKCU "Software\Classes\.qxw" ""
	StrCmp $0 "VertexAVDirector.Document" 0 +2
		DeleteRegKey HKCU "Software\Classes\.qxw"
	DeleteRegKey HKCU "Software\Classes\VertexAVDirector.Document"
	ReadRegStr $0 HKCU "Software\Classes\.qxf" ""
	StrCmp $0 "VertexAVDirectorFixture.Document" 0 +2
		DeleteRegKey HKCU "Software\Classes\.qxf"
	DeleteRegKey HKCU "Software\Classes\VertexAVDirectorFixture.Document"

	; Refresh Explorer after uninstall
	System::Call 'SHELL32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd
