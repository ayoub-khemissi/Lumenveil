<#
.SYNOPSIS
  Keeps a Lordaeron game folder up to date: the client patches it holds (patch-W.MPQ, patch-Z.MPQ) and the
  Lumenveil graphics mod when it is installed. Started by Lordaeron-Update.cmd and Lordaeron-Play.cmd.
#>
param(
    [switch]$Play,
    [switch]$Test,
    [switch]$Stable,
    [switch]$Rollback,
    [string]$Root = '',
    [string]$Site = 'https://lordaeron.eu'
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$UpdaterVersion = 3
$Utf8 = New-Object System.Text.UTF8Encoding $false

$Package = Split-Path -Parent $PSScriptRoot
$InstallFrom = ''
$Own = ''
$Work = ''
$Backups = ''

$Texts = @{
    en = @{
        Title = 'Lordaeron update'
        NoGame = 'No game folder was chosen. Unzip Lumenveil anywhere and double-click Lordaeron-Play.cmd again, or copy its files next to Wow.exe.'
        PickFolder = 'Choose your World of Warcraft 3.3.5 folder, the one that holds Wow.exe'
        NotGameFolder = 'There is no Wow.exe in {0}: choose the folder that holds it.'
        Installed = 'Lumenveil {0} is installed in {1}.'
        CanDelete = 'You can delete the folder you unzipped it in: {0}'
        ShortcutCreated = 'A Lordaeron icon is now on your desktop: start the game with it, and Lumenveil and the client patches stay up to date by themselves.'
        PressToPlay = 'Press Enter to start the game.'
        Checking = 'Checking for updates on lordaeron.eu...'
        Offline = 'lordaeron.eu could not be reached ({0}). Nothing was changed.'
        PatchCurrent = '{0} is up to date ({1}).'
        PatchNew = '{0}: new version from {1}, downloading {2} MB...'
        PatchDone = '{0} updated.'
        CacheCleared = 'The game''s Cache folder was emptied, as the client''s data changed.'
        LumenveilCurrent = 'Lumenveil {0} is up to date{1}.'
        LumenveilNew = 'Lumenveil {0} -> {1}{2}, downloading {3} MB...'
        LumenveilHeld = 'Lumenveil {0} is skipped: you went back from it. A newer version will install as usual.'
        TestChannel = ' (test channel)'
        ChannelSet = 'Lumenveil now comes from the {0} channel.'
        WaitGame = 'The game is open in this folder: close it to finish the update. Waiting...'
        Damaged = 'The download of {0} is damaged (its SHA-256 does not match): {0} was left as it was. Try again later.'
        SettingsKept = 'Your Lumenveil settings were kept; settings you never changed take the new defaults.'
        Setup = 'Lumenveil setup: the lamps and the water of your game files, up to half a minute...'
        SetupFailed = 'The Lumenveil setup failed: the details are in Lumenveil\setup.log.'
        WhatsNew = 'New in Lumenveil {0}:'
        Credits = 'Lumenveil is free software (GNU GPL v2) by the Lumenveil authors, github.com/lumenveil/Lumenveil, built on CoAVolFog. Lordaeron''s build and its source: github.com/ayoub-khemissi/Lumenveil.'
        RolledBack = 'Lumenveil is back to {0}; {1} is skipped until a newer version comes out.'
        RolledBackUnknown = 'Lumenveil is back to the version it had before; {0} is skipped until a newer version comes out.'
        NoBackup = 'There is no earlier Lumenveil version to go back to.'
        UpdaterUpdated = 'The updater itself was updated.'
        Failed = 'The update stopped: {0}'
        Starting = 'Starting the game...'
        Done = 'All up to date.'
        PressKey = 'Press Enter to close this window.'
    }
    fr = @{
        Title = 'Mise à jour de Lordaeron'
        NoGame = 'Aucun dossier de jeu n''a été choisi. Décompressez Lumenveil n''importe où et relancez Lordaeron-Play.cmd, ou copiez ses fichiers à côté de Wow.exe.'
        PickFolder = 'Choisissez votre dossier World of Warcraft 3.3.5, celui qui contient Wow.exe'
        NotGameFolder = 'Il n''y a pas de Wow.exe dans {0} : choisissez le dossier qui le contient.'
        Installed = 'Lumenveil {0} est installé dans {1}.'
        CanDelete = 'Vous pouvez supprimer le dossier où vous l''avez décompressé : {0}'
        ShortcutCreated = 'Une icône Lordaeron est maintenant sur votre bureau : lancez le jeu avec elle, et Lumenveil et les patchs du client restent à jour tout seuls.'
        PressToPlay = 'Appuyez sur Entrée pour lancer le jeu.'
        Checking = 'Recherche des mises à jour sur lordaeron.eu...'
        Offline = 'lordaeron.eu est injoignable ({0}). Rien n''a été modifié.'
        PatchCurrent = '{0} est à jour ({1}).'
        PatchNew = '{0} : nouvelle version du {1}, téléchargement de {2} Mo...'
        PatchDone = '{0} mis à jour.'
        CacheCleared = 'Le dossier Cache du jeu a été vidé, car les données du client ont changé.'
        LumenveilCurrent = 'Lumenveil {0} est à jour{1}.'
        LumenveilNew = 'Lumenveil {0} -> {1}{2}, téléchargement de {3} Mo...'
        LumenveilHeld = 'Lumenveil {0} est ignoré : vous en êtes revenu. Une version plus récente s''installera normalement.'
        TestChannel = ' (canal de test)'
        ChannelSet = 'Lumenveil vient désormais du canal {0}.'
        WaitGame = 'Le jeu est ouvert dans ce dossier : fermez-le pour terminer la mise à jour. En attente...'
        Damaged = 'Le téléchargement de {0} est endommagé (son SHA-256 ne correspond pas) : {0} n''a pas été modifié. Réessayez plus tard.'
        SettingsKept = 'Vos réglages Lumenveil sont conservés ; ceux que vous n''avez jamais changés prennent les nouvelles valeurs par défaut.'
        Setup = 'Préparation de Lumenveil : les lampes et l''eau de vos fichiers de jeu, jusqu''à une demi-minute...'
        SetupFailed = 'La préparation de Lumenveil a échoué : les détails sont dans Lumenveil\setup.log.'
        WhatsNew = 'Nouveautés de Lumenveil {0} :'
        Credits = 'Lumenveil est un logiciel libre (GNU GPL v2) des auteurs de Lumenveil, github.com/lumenveil/Lumenveil, basé sur CoAVolFog. La version de Lordaeron et ses sources : github.com/ayoub-khemissi/Lumenveil.'
        RolledBack = 'Lumenveil est revenu à la version {0} ; la {1} est ignorée jusqu''à une version plus récente.'
        RolledBackUnknown = 'Lumenveil est revenu à sa version précédente ; la {0} est ignorée jusqu''à une version plus récente.'
        NoBackup = 'Aucune version précédente de Lumenveil vers laquelle revenir.'
        UpdaterUpdated = 'L''outil de mise à jour lui-même a été mis à jour.'
        Failed = 'La mise à jour s''est arrêtée : {0}'
        Starting = 'Lancement du jeu...'
        Done = 'Tout est à jour.'
        PressKey = 'Appuyez sur Entrée pour fermer cette fenêtre.'
    }
    de = @{
        Title = 'Lordaeron-Update'
        NoGame = 'Es wurde kein Spielordner gewählt. Entpacke Lumenveil an einem beliebigen Ort und starte Lordaeron-Play.cmd erneut, oder kopiere die Dateien neben Wow.exe.'
        PickFolder = 'Wähle deinen World of Warcraft 3.3.5-Ordner, den mit Wow.exe'
        NotGameFolder = 'In {0} gibt es keine Wow.exe: wähle den Ordner, der sie enthält.'
        Installed = 'Lumenveil {0} ist in {1} installiert.'
        CanDelete = 'Du kannst den Ordner löschen, in den du es entpackt hast: {0}'
        ShortcutCreated = 'Auf deinem Desktop liegt jetzt ein Lordaeron-Symbol: starte das Spiel damit, und Lumenveil und die Client-Patches bleiben von selbst aktuell.'
        PressToPlay = 'Drücke Enter, um das Spiel zu starten.'
        Checking = 'Suche nach Updates auf lordaeron.eu...'
        Offline = 'lordaeron.eu ist nicht erreichbar ({0}). Es wurde nichts geändert.'
        PatchCurrent = '{0} ist aktuell ({1}).'
        PatchNew = '{0}: neue Version vom {1}, lade {2} MB herunter...'
        PatchDone = '{0} aktualisiert.'
        CacheCleared = 'Der Cache-Ordner des Spiels wurde geleert, da sich die Daten des Clients geändert haben.'
        LumenveilCurrent = 'Lumenveil {0} ist aktuell{1}.'
        LumenveilNew = 'Lumenveil {0} -> {1}{2}, lade {3} MB herunter...'
        LumenveilHeld = 'Lumenveil {0} wird übersprungen: du bist davon zurückgegangen. Eine neuere Version wird wie gewohnt installiert.'
        TestChannel = ' (Testkanal)'
        ChannelSet = 'Lumenveil kommt jetzt aus dem Kanal {0}.'
        WaitGame = 'Das Spiel ist in diesem Ordner geöffnet: schließe es, um das Update abzuschließen. Warte...'
        Damaged = 'Der Download von {0} ist beschädigt (SHA-256 stimmt nicht): {0} wurde nicht verändert. Versuche es später erneut.'
        SettingsKept = 'Deine Lumenveil-Einstellungen bleiben erhalten; nie geänderte Einstellungen erhalten die neuen Standardwerte.'
        Setup = 'Lumenveil-Einrichtung: die Lampen und das Wasser deiner Spieldateien, bis zu einer halben Minute...'
        SetupFailed = 'Die Lumenveil-Einrichtung ist fehlgeschlagen: Details in Lumenveil\setup.log.'
        WhatsNew = 'Neu in Lumenveil {0}:'
        Credits = 'Lumenveil ist freie Software (GNU GPL v2) der Lumenveil-Autoren, github.com/lumenveil/Lumenveil, auf Basis von CoAVolFog. Lordaerons Version und ihr Quellcode: github.com/ayoub-khemissi/Lumenveil.'
        RolledBack = 'Lumenveil ist zurück auf {0}; {1} wird übersprungen, bis eine neuere Version erscheint.'
        RolledBackUnknown = 'Lumenveil ist zurück auf der vorherigen Version; {0} wird übersprungen, bis eine neuere Version erscheint.'
        NoBackup = 'Es gibt keine frühere Lumenveil-Version, zu der zurückgekehrt werden kann.'
        UpdaterUpdated = 'Das Update-Programm selbst wurde aktualisiert.'
        Failed = 'Das Update wurde abgebrochen: {0}'
        Starting = 'Spiel wird gestartet...'
        Done = 'Alles ist aktuell.'
        PressKey = 'Drücke Enter, um dieses Fenster zu schließen.'
    }
    es = @{
        Title = 'Actualización de Lordaeron'
        NoGame = 'No se ha elegido ninguna carpeta del juego. Descomprime Lumenveil en cualquier sitio y vuelve a ejecutar Lordaeron-Play.cmd, o copia sus archivos junto a Wow.exe.'
        PickFolder = 'Elige tu carpeta de World of Warcraft 3.3.5, la que contiene Wow.exe'
        NotGameFolder = 'No hay ningún Wow.exe en {0}: elige la carpeta que lo contiene.'
        Installed = 'Lumenveil {0} está instalado en {1}.'
        CanDelete = 'Puedes borrar la carpeta donde lo descomprimiste: {0}'
        ShortcutCreated = 'Ahora tienes un icono de Lordaeron en el escritorio: inicia el juego con él, y Lumenveil y los parches del cliente se mantienen al día solos.'
        PressToPlay = 'Pulsa Intro para iniciar el juego.'
        Checking = 'Buscando actualizaciones en lordaeron.eu...'
        Offline = 'No se pudo conectar con lordaeron.eu ({0}). No se ha cambiado nada.'
        PatchCurrent = '{0} está al día ({1}).'
        PatchNew = '{0}: nueva versión del {1}, descargando {2} MB...'
        PatchDone = '{0} actualizado.'
        CacheCleared = 'Se ha vaciado la carpeta Cache del juego, ya que los datos del cliente han cambiado.'
        LumenveilCurrent = 'Lumenveil {0} está al día{1}.'
        LumenveilNew = 'Lumenveil {0} -> {1}{2}, descargando {3} MB...'
        LumenveilHeld = 'Lumenveil {0} se omite: volviste de esa versión. Una versión más reciente se instalará como siempre.'
        TestChannel = ' (canal de pruebas)'
        ChannelSet = 'Lumenveil llega ahora del canal {0}.'
        WaitGame = 'El juego está abierto en esta carpeta: ciérralo para terminar la actualización. Esperando...'
        Damaged = 'La descarga de {0} está dañada (su SHA-256 no coincide): {0} no se ha modificado. Inténtalo más tarde.'
        SettingsKept = 'Tus ajustes de Lumenveil se conservan; los que nunca cambiaste toman los nuevos valores por defecto.'
        Setup = 'Instalación de Lumenveil: las lámparas y el agua de tus archivos del juego, hasta medio minuto...'
        SetupFailed = 'La instalación de Lumenveil ha fallado: los detalles están en Lumenveil\setup.log.'
        WhatsNew = 'Novedades de Lumenveil {0}:'
        Credits = 'Lumenveil es software libre (GNU GPL v2) de los autores de Lumenveil, github.com/lumenveil/Lumenveil, basado en CoAVolFog. La versión de Lordaeron y su código: github.com/ayoub-khemissi/Lumenveil.'
        RolledBack = 'Lumenveil ha vuelto a {0}; {1} se omite hasta que salga una versión más reciente.'
        RolledBackUnknown = 'Lumenveil ha vuelto a la versión que tenía antes; {0} se omite hasta que salga una versión más reciente.'
        NoBackup = 'No hay ninguna versión anterior de Lumenveil a la que volver.'
        UpdaterUpdated = 'El propio actualizador se ha actualizado.'
        Failed = 'La actualización se ha detenido: {0}'
        Starting = 'Iniciando el juego...'
        Done = 'Todo está al día.'
        PressKey = 'Pulsa Intro para cerrar esta ventana.'
    }
    ru = @{
        Title = 'Обновление Lordaeron'
        NoGame = 'Папка игры не выбрана. Распакуйте Lumenveil в любое место и снова запустите Lordaeron-Play.cmd или скопируйте его файлы рядом с Wow.exe.'
        PickFolder = 'Выберите папку World of Warcraft 3.3.5, в которой находится Wow.exe'
        NotGameFolder = 'В {0} нет Wow.exe: выберите папку, в которой он находится.'
        Installed = 'Lumenveil {0} установлен в {1}.'
        CanDelete = 'Можно удалить папку, в которую вы его распаковали: {0}'
        ShortcutCreated = 'На рабочем столе появился значок Lordaeron: запускайте игру с него, и Lumenveil и патчи клиента будут обновляться сами.'
        PressToPlay = 'Нажмите Enter, чтобы запустить игру.'
        Checking = 'Поиск обновлений на lordaeron.eu...'
        Offline = 'Не удалось связаться с lordaeron.eu ({0}). Ничего не изменено.'
        PatchCurrent = '{0}: последняя версия ({1}).'
        PatchNew = '{0}: новая версия от {1}, загрузка {2} МБ...'
        PatchDone = '{0} обновлён.'
        CacheCleared = 'Папка Cache игры очищена, так как данные клиента изменились.'
        LumenveilCurrent = 'Lumenveil {0}: последняя версия{1}.'
        LumenveilNew = 'Lumenveil {0} -> {1}{2}, загрузка {3} МБ...'
        LumenveilHeld = 'Lumenveil {0} пропущен: вы вернулись с этой версии. Более новая версия установится как обычно.'
        TestChannel = ' (тестовый канал)'
        ChannelSet = 'Lumenveil теперь приходит из канала {0}.'
        WaitGame = 'Игра открыта в этой папке: закройте её, чтобы завершить обновление. Ожидание...'
        Damaged = 'Загрузка {0} повреждена (SHA-256 не совпадает): {0} не изменён. Попробуйте позже.'
        SettingsKept = 'Ваши настройки Lumenveil сохранены; те, что вы не меняли, получают новые значения по умолчанию.'
        Setup = 'Настройка Lumenveil: лампы и вода из ваших файлов игры, до полуминуты...'
        SetupFailed = 'Настройка Lumenveil не удалась: подробности в Lumenveil\setup.log.'
        WhatsNew = 'Новое в Lumenveil {0}:'
        Credits = 'Lumenveil - свободная программа (GNU GPL v2) авторов Lumenveil, github.com/lumenveil/Lumenveil, на основе CoAVolFog. Версия Lordaeron и её исходный код: github.com/ayoub-khemissi/Lumenveil.'
        RolledBack = 'Lumenveil возвращён к {0}; {1} пропускается до выхода более новой версии.'
        RolledBackUnknown = 'Lumenveil возвращён к прежней версии; {0} пропускается до выхода более новой версии.'
        NoBackup = 'Нет более ранней версии Lumenveil, к которой можно вернуться.'
        UpdaterUpdated = 'Программа обновления сама обновилась.'
        Failed = 'Обновление прервано: {0}'
        Starting = 'Запуск игры...'
        Done = 'Всё обновлено.'
        PressKey = 'Нажмите Enter, чтобы закрыть это окно.'
    }
}

function Get-Language([string]$folder) {
    if ($folder) {
        $locale = Get-ChildItem -LiteralPath (Join-Path $folder 'Data') -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -cmatch '^[a-z]{2}[A-Z]{2}$' } | Select-Object -First 1
        if ($locale -and $Texts.ContainsKey($locale.Name.Substring(0, 2))) { return $locale.Name.Substring(0, 2) }
    }
    $system = (Get-UICulture).TwoLetterISOLanguageName
    if ($Texts.ContainsKey($system)) { return $system }
    return 'en'
}

$Lang = Get-Language ''

function Text([string]$key) {
    $table = $Texts[$Lang]
    if (-not $table.ContainsKey($key)) { $table = $Texts['en'] }
    $text = $table[$key]
    if ($args.Count -gt 0) { return [string]::Format($text, $args) }
    return $text
}

function Log([string]$line) {
    if (-not $Own) { return }
    try {
        $file = Join-Path $Own 'update.log'
        if ((Test-Path -LiteralPath $file) -and (Get-Item -LiteralPath $file).Length -gt 262144) {
            Remove-Item -LiteralPath $file -Force
        }
        [IO.File]::AppendAllText($file, ('{0:yyyy-MM-dd HH:mm:ss} {1}' -f (Get-Date), $line) + "`r`n", $Utf8)
    } catch { }
}

function Say([string]$line, [string]$colour = 'Gray') {
    Write-Host $line -ForegroundColor $colour
    Log $line
}

function Get-Feed([string]$url) {
    $file = Join-Path $Work ('feed-' + [Guid]::NewGuid().ToString('N') + '.json')
    try {
        $separator = '?'
        if ($url.Contains('?')) { $separator = '&' }
        Invoke-WebRequest -Uri ($url + $separator + 't=' + [DateTime]::UtcNow.Ticks) -UseBasicParsing -TimeoutSec 20 `
            -OutFile $file
        return [IO.File]::ReadAllText($file, $Utf8) | ConvertFrom-Json
    } finally {
        Remove-Item -LiteralPath $file -Force -ErrorAction SilentlyContinue
    }
}

function Get-Download([string]$url, [string]$file, [long]$size, [string]$sha256, [string]$name) {
    Invoke-WebRequest -Uri $url -UseBasicParsing -TimeoutSec 600 -OutFile $file
    $hash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
    if (($size -gt 0 -and (Get-Item -LiteralPath $file).Length -ne $size) -or $hash -ne $sha256.ToLowerInvariant()) {
        Remove-Item -LiteralPath $file -Force -ErrorAction SilentlyContinue
        throw (Text 'Damaged' $name)
    }
}

function Get-VersionText([Version]$version) {
    if ($version -eq [Version]'0.0.0') { return '?' }
    return [string]$version
}

function Get-Megabytes([long]$bytes) {
    $culture = [Globalization.CultureInfo]::InvariantCulture
    if ($Lang -ne 'en') { $culture = [Globalization.CultureInfo]::GetCultureInfo($Lang) }
    return [Math]::Max(0.1, [Math]::Round($bytes / 1MB, 1)).ToString($culture)
}

function Test-GameOpen {
    $exe = Join-Path $Root 'Wow.exe'
    foreach ($process in @(Get-Process -Name 'Wow' -ErrorAction SilentlyContinue)) {
        try {
            if (-not $process.Path -or $process.Path -ieq $exe) { return $true }
        } catch { return $true }
    }
    return $false
}

function Wait-GameClosed {
    if (-not (Test-GameOpen)) { return }
    Say (Text 'WaitGame') 'Yellow'
    while (Test-GameOpen) { Start-Sleep -Seconds 2 }
}

function Read-IniValues($lines) {
    $values = @{}
    $section = ''
    if (-not $lines) { return $values }
    foreach ($line in $lines) {
        $trimmed = $line.Trim()
        if ($trimmed -match '^\[(.+)\]$') { $section = $Matches[1].Trim(); continue }
        if (-not $trimmed -or $trimmed.StartsWith(';')) { continue }
        $at = $trimmed.IndexOf('=')
        if ($at -lt 1) { continue }
        $values[$section + "`n" + $trimmed.Substring(0, $at).Trim()] = $trimmed.Substring($at + 1).Trim()
    }
    return $values
}

function Test-PlayerValue($old, $mine, [string]$id) {
    return $mine.ContainsKey($id) -and (-not $old.ContainsKey($id) -or $mine[$id] -ne $old[$id])
}

function Merge-Ini($oldLines, $newLines, $mineLines) {
    $old = Read-IniValues $oldLines
    $new = Read-IniValues $newLines
    $mine = Read-IniValues $mineLines
    $out = New-Object System.Collections.Generic.List[string]
    $sectionOrder = New-Object System.Collections.Generic.List[string]
    $mineOrder = New-Object System.Collections.Generic.List[string]
    $section = ''
    foreach ($line in $mineLines) {
        $trimmed = $line.Trim()
        if ($trimmed -match '^\[(.+)\]$') { $section = $Matches[1].Trim(); continue }
        if (-not $trimmed -or $trimmed.StartsWith(';')) { continue }
        $at = $trimmed.IndexOf('=')
        if ($at -ge 1) { $mineOrder.Add($section + "`n" + $trimmed.Substring(0, $at).Trim()) }
    }
    $extras = {
        param([string]$name)
        foreach ($id in $mineOrder) {
            $parts = $id.Split("`n")
            if ($parts[0] -ne $name -or $new.ContainsKey($id)) { continue }
            if (Test-PlayerValue $old $mine $id) { $out.Add($parts[1] + '=' + $mine[$id]) }
        }
    }
    $section = ''
    $sectionOrder.Add('')
    foreach ($line in $newLines) {
        $trimmed = $line.Trim()
        if ($trimmed -match '^\[(.+)\]$') {
            & $extras $section
            $section = $Matches[1].Trim()
            $sectionOrder.Add($section)
            $out.Add($line)
            continue
        }
        $at = $trimmed.IndexOf('=')
        if (-not $trimmed -or $trimmed.StartsWith(';') -or $at -lt 1) { $out.Add($line); continue }
        $key = $trimmed.Substring(0, $at).Trim()
        $id = $section + "`n" + $key
        if ($mine.ContainsKey($id)) {
            if (Test-PlayerValue $old $mine $id) { $out.Add($key + '=' + $mine[$id]) } else { $out.Add($line) }
        } elseif ($old.ContainsKey($id) -and $mineLines) {
            continue
        } else {
            $out.Add($line)
        }
    }
    & $extras $section
    $added = @{}
    foreach ($id in $mineOrder) {
        $name = $id.Split("`n")[0]
        if ($sectionOrder.Contains($name) -or -not (Test-PlayerValue $old $mine $id)) { continue }
        if (-not $added.ContainsKey($name)) {
            $out.Add('[' + $name + ']')
            $added[$name] = $true
        }
        $out.Add($id.Split("`n")[1] + '=' + $mine[$id])
    }
    return $out
}

function Write-Lines([string]$file, $lines) {
    [IO.File]::WriteAllText($file, (($lines -join "`r`n") + "`r`n"), $Utf8)
}

function Read-Lines([string]$file) {
    if (Test-Path -LiteralPath $file) { return [IO.File]::ReadAllLines($file) }
    return $null
}

function Get-InstalledLumenveil {
    $dll = Join-Path $Root 'CoAVolFog.dll'
    if (-not (Test-Path -LiteralPath $dll) -or -not (Test-Path -LiteralPath (Join-Path $Root 'version.dll'))) {
        return $null
    }
    $parsed = $null
    $text = (Get-Item -LiteralPath $dll).VersionInfo.ProductVersion
    if ($text -and [Version]::TryParse($text.Trim(), [ref]$parsed)) { return $parsed }
    $state = Join-Path $Own 'lumenveil.txt'
    if ((Test-Path -LiteralPath $state) -and [Version]::TryParse(([IO.File]::ReadAllText($state)).Trim(), [ref]$parsed)) {
        return $parsed
    }
    return [Version]'0.0.0'
}

function Test-SetupMissing {
    return -not (Test-Path -LiteralPath (Join-Path $Root 'lampdata.bin')) -or
        -not (Test-Path -LiteralPath (Join-Path $Root 'liquiddata.bin'))
}

function Invoke-Setup {
    $setup = Join-Path $Root 'Lumenveil-Setup.cmd'
    $script:SetupRan = $true
    if (-not $env:ComSpec -or -not (Test-Path -LiteralPath $setup)) { return }
    Say (Text 'Setup')
    $process = Start-Process -FilePath $env:ComSpec -ArgumentList @('/c', "`"$setup`" /auto") -WorkingDirectory $Root `
        -NoNewWindow -Wait -PassThru
    if ($process.ExitCode -ne 0) { Say (Text 'SetupFailed') 'Red' }
}

function Test-UpdaterFile([string]$relative) {
    return $relative -like 'Lordaeron*'
}

function Get-RelativeFiles([string]$folder) {
    $base = (Resolve-Path -LiteralPath $folder).Path.TrimEnd('\', '/')
    return @(Get-ChildItem -LiteralPath $folder -Recurse -File | ForEach-Object { $_.FullName.Substring($base.Length + 1) })
}

function Copy-Tree([string]$from, [string]$to, $files) {
    foreach ($relative in $files) {
        $target = Join-Path $to $relative
        $parent = Split-Path -Parent $target
        if (-not (Test-Path -LiteralPath $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
        Copy-Item -LiteralPath (Join-Path $from $relative) -Destination $target -Force
    }
}

function Test-SameFile([string]$a, [string]$b) {
    if (-not (Test-Path -LiteralPath $a) -or -not (Test-Path -LiteralPath $b)) { return $false }
    return (Get-FileHash -LiteralPath $a -Algorithm SHA256).Hash -eq (Get-FileHash -LiteralPath $b -Algorithm SHA256).Hash
}

function Update-Patches($manifest) {
    $changed = $false
    foreach ($entry in $manifest.PSObject.Properties) {
        $name = $entry.Name
        $info = $entry.Value
        $local = Join-Path (Join-Path $Root 'Data') $name
        if (-not (Test-Path -LiteralPath $local) -or -not $info.sha256) { continue }
        $hash = (Get-FileHash -LiteralPath $local -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($hash -eq ([string]$info.sha256).ToLowerInvariant()) {
            Say (Text 'PatchCurrent' $name $info.version)
            continue
        }
        Say (Text 'PatchNew' $name $info.version (Get-Megabytes $info.size)) 'Cyan'
        $download = Join-Path $Work $name
        Get-Download ($Site + '/patches/' + $name) $download ([long]$info.size) ([string]$info.sha256) $name
        Wait-GameClosed
        Move-Item -LiteralPath $download -Destination $local -Force
        Say (Text 'PatchDone' $name) 'Green'
        $changed = $true
    }
    if ($changed) {
        $cache = Join-Path $Root 'Cache'
        if (Test-Path -LiteralPath $cache) {
            Remove-Item -LiteralPath $cache -Recurse -Force
            Say (Text 'CacheCleared') 'Green'
        }
    }
}

function Update-Updater($feed) {
    if (-not $feed.updater -or -not $feed.updater.url -or [int]$feed.updater.version -le $UpdaterVersion) { return }
    $zip = Join-Path $Work 'Lordaeron-Updater.zip'
    Get-Download ([string]$feed.updater.url) $zip ([long]$feed.updater.size) ([string]$feed.updater.sha256) `
        'Lordaeron-Updater.zip'
    $stage = Join-Path $Work 'updater'
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
    Expand-Archive -LiteralPath $zip -DestinationPath $stage -Force
    $files = @(Get-RelativeFiles $stage | Where-Object { -not (Test-SameFile (Join-Path $stage $_) (Join-Path $Root $_)) })
    Copy-Tree $stage $Root $files
    Remove-Item -LiteralPath $stage -Recurse -Force
    Remove-Item -LiteralPath $zip -Force
    Say (Text 'UpdaterUpdated') 'Green'
}

function Get-DllVersion([string]$folder) {
    $dll = Join-Path $folder 'CoAVolFog.dll'
    if (-not (Test-Path -LiteralPath $dll)) { return $null }
    $parsed = $null
    $text = (Get-Item -LiteralPath $dll).VersionInfo.ProductVersion
    if ($text -and [Version]::TryParse($text.Trim(), [ref]$parsed)) { return $parsed }
    return [Version]'0.0.0'
}

function Install-Package([string]$stage, $installed, [Version]$version) {
    Wait-GameClosed
    $settings = @('CoAVolFog.ini', 'CoAVolFog-regions.ini')
    $files = @(Get-RelativeFiles $stage)
    if ($installed) {
        $backup = Join-Path $Backups ('Lumenveil-' + $installed)
        if (Test-Path -LiteralPath $backup) { Remove-Item -LiteralPath $backup -Recurse -Force }
        New-Item -ItemType Directory -Path $backup -Force | Out-Null
        $existing = @($files + $settings | Select-Object -Unique |
            Where-Object { -not (Test-UpdaterFile $_) -and (Test-Path -LiteralPath (Join-Path $Root $_)) })
        Copy-Tree $Root $backup $existing
        $added = @($files | Where-Object { -not (Test-UpdaterFile $_) -and $existing -notcontains $_ })
        [IO.File]::WriteAllText((Join-Path $backup 'added.txt'), (($added -join "`r`n") + "`r`n"), $Utf8)
    }
    $oldDefaults = @{}
    foreach ($name in $settings) { $oldDefaults[$name] = Read-Lines (Join-Path (Join-Path $Root 'Lumenveil\defaults') $name) }
    $copied = @($files | Where-Object {
        $settings -notcontains $_ -and -not (Test-SameFile (Join-Path $stage $_) (Join-Path $Root $_))
    })
    Copy-Tree $stage $Root $copied
    $kept = $false
    foreach ($name in $settings) {
        $shipped = Join-Path $stage $name
        $mine = Join-Path $Root $name
        if (-not (Test-Path -LiteralPath $shipped)) { continue }
        if (-not (Test-Path -LiteralPath $mine)) {
            if (-not $installed -or $name -eq 'CoAVolFog.ini') { Copy-Item -LiteralPath $shipped -Destination $mine -Force }
            continue
        }
        Write-Lines $mine (Merge-Ini $oldDefaults[$name] (Read-Lines $shipped) (Read-Lines $mine))
        $kept = $true
    }
    [IO.File]::WriteAllText((Join-Path $Own 'lumenveil.txt'), [string]$version, $Utf8)
    if ($kept) { Say (Text 'SettingsKept') }
    Get-ChildItem -LiteralPath $Backups -Directory | Sort-Object LastWriteTime -Descending | Select-Object -Skip 2 |
        ForEach-Object { Remove-Item -LiteralPath $_.FullName -Recurse -Force }
}

function Install-FromFolder {
    $version = Get-DllVersion $InstallFrom
    $installed = Get-InstalledLumenveil
    if ($installed -and $version -ne [Version]'0.0.0' -and $installed -ge $version) { return }
    Install-Package $InstallFrom $installed $version
    Say (Text 'Installed' (Get-VersionText $version) $Root) 'Green'
    Say (Text 'CanDelete' $InstallFrom)
    $script:Notable = $true
}

function Update-Lumenveil($feed, [Version]$installed, [string]$channelNote) {
    $latest = [Version]([string]$feed.version)
    $hold = Join-Path $Own 'hold.txt'
    if (Test-Path -LiteralPath $hold) {
        $held = $null
        if ([Version]::TryParse(([IO.File]::ReadAllText($hold)).Trim(), [ref]$held) -and $latest -le $held) {
            if ($latest -eq $held) { Say (Text 'LumenveilHeld' $latest) 'Yellow' }
            return
        }
        Remove-Item -LiteralPath $hold -Force
    }
    if ($installed -ge $latest) {
        Say (Text 'LumenveilCurrent' (Get-VersionText $installed) $channelNote)
        return
    }
    Say (Text 'LumenveilNew' (Get-VersionText $installed) $latest $channelNote (Get-Megabytes $feed.size)) 'Cyan'
    $zip = Join-Path $Work ('Lumenveil-' + $latest + '.zip')
    Get-Download ([string]$feed.url) $zip ([long]$feed.size) ([string]$feed.sha256) ('Lumenveil ' + $latest)
    $stage = Join-Path $Work ('Lumenveil-' + $latest)
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
    Expand-Archive -LiteralPath $zip -DestinationPath $stage -Force
    Install-Package $stage $installed $latest
    Remove-Item -LiteralPath $stage -Recurse -Force
    Remove-Item -LiteralPath $zip -Force
    Invoke-Setup
    if ($feed.notes) {
        Write-Host ''
        Say (Text 'WhatsNew' $latest) 'Cyan'
        foreach ($line in ([string]$feed.notes).Split("`n")) { Say ('  ' + $line.TrimEnd()) }
    }
    Write-Host ''
    Say (Text 'Credits') 'DarkGray'
    $script:Updated = $true
}

function Select-GameFolder {
    try {
        Add-Type -AssemblyName System.Windows.Forms
        $owner = New-Object System.Windows.Forms.Form
        $owner.TopMost = $true
        $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
        $dialog.Description = Text 'PickFolder'
        $dialog.ShowNewFolderButton = $false
        while ($dialog.ShowDialog($owner) -eq [System.Windows.Forms.DialogResult]::OK) {
            if (Test-Path -LiteralPath (Join-Path $dialog.SelectedPath 'Wow.exe')) { return $dialog.SelectedPath }
            Write-Host (Text 'NotGameFolder' $dialog.SelectedPath) -ForegroundColor Yellow
        }
    } catch { }
    return ''
}

function Add-Shortcut {
    $done = Join-Path $Own 'shortcut.txt'
    $play = Join-Path $Root 'Lordaeron-Play.cmd'
    if ((Test-Path -LiteralPath $done) -or -not (Test-Path -LiteralPath $play)) { return }
    try {
        $desktop = [Environment]::GetFolderPath('Desktop')
        $shell = New-Object -ComObject WScript.Shell
        $path = Join-Path $desktop 'Lordaeron.lnk'
        if ((Test-Path -LiteralPath $path) -and $shell.CreateShortcut($path).WorkingDirectory -ine $Root) {
            $path = Join-Path $desktop ('Lordaeron (' + (Split-Path -Leaf $Root) + ').lnk')
        }
        $link = $shell.CreateShortcut($path)
        $link.TargetPath = $play
        $link.WorkingDirectory = $Root
        $link.IconLocation = (Join-Path $Root 'Wow.exe') + ',0'
        $link.Description = 'World of Warcraft on Lordaeron'
        $link.Save()
        [IO.File]::WriteAllText($done, $path, $Utf8)
        Say (Text 'ShortcutCreated') 'Green'
        $script:Notable = $true
    } catch {
        Log ('no desktop shortcut: ' + $_.Exception.Message)
    }
}

function Restore-Lumenveil {
    $installed = Get-InstalledLumenveil
    $backup = Get-ChildItem -LiteralPath $Backups -Directory -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $installed -or -not $backup) {
        Say (Text 'NoBackup') 'Yellow'
        return
    }
    Wait-GameClosed
    $previous = $backup.Name.Substring('Lumenveil-'.Length)
    $addedFile = Join-Path $backup.FullName 'added.txt'
    if (Test-Path -LiteralPath $addedFile) {
        foreach ($relative in [IO.File]::ReadAllLines($addedFile)) {
            if (-not $relative.Trim()) { continue }
            $target = Join-Path $Root $relative.Trim()
            if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Force }
        }
        Remove-Item -LiteralPath $addedFile -Force
    }
    Copy-Tree $backup.FullName $Root @(Get-RelativeFiles $backup.FullName)
    [IO.File]::WriteAllText((Join-Path $Own 'lumenveil.txt'), $previous, $Utf8)
    [IO.File]::WriteAllText((Join-Path $Own 'hold.txt'), [string]$installed, $Utf8)
    Remove-Item -LiteralPath $backup.FullName -Recurse -Force
    Invoke-Setup
    if ($previous -eq '0.0.0') { Say (Text 'RolledBackUnknown' $installed) 'Green' }
    else { Say (Text 'RolledBack' $previous $installed) 'Green' }
}

$failed = $false
$script:SetupRan = $false
$script:Notable = $false
$script:Updated = $false
try {
    $Host.UI.RawUI.WindowTitle = Text 'Title'
} catch { }
try {
    if (-not $Root) {
        if (Test-Path -LiteralPath (Join-Path $Package 'Wow.exe')) {
            $Root = $Package
        } elseif (Test-Path -LiteralPath (Join-Path (Split-Path -Parent $Package) 'Wow.exe')) {
            $Root = Split-Path -Parent $Package
            $InstallFrom = $Package
        } else {
            $Root = Select-GameFolder
            $InstallFrom = $Package
        }
    }
    if (-not $Root -or -not (Test-Path -LiteralPath (Join-Path $Root 'Wow.exe'))) {
        $Root = ''
        throw (Text 'NoGame')
    }
    $Root = (Resolve-Path -LiteralPath $Root).Path
    $Own = Join-Path $Root 'Lordaeron'
    $Work = Join-Path $Own 'download'
    $Backups = Join-Path $Own 'backup'
    $Lang = Get-Language $Root
    foreach ($folder in @($Own, $Work, $Backups)) {
        if (-not (Test-Path -LiteralPath $folder)) { New-Item -ItemType Directory -Path $folder -Force | Out-Null }
    }
    Log ('updater ' + $UpdaterVersion + ' in ' + $Root)
    if ($InstallFrom -and (Test-Path -LiteralPath (Join-Path $InstallFrom 'CoAVolFog.dll'))) { Install-FromFolder }
    $channelFile = Join-Path $Own 'channel.txt'
    if ($Test) {
        [IO.File]::WriteAllText($channelFile, 'test', $Utf8)
        Say (Text 'ChannelSet' 'test') 'Cyan'
    } elseif ($Stable) {
        Remove-Item -LiteralPath $channelFile -Force -ErrorAction SilentlyContinue
        Say (Text 'ChannelSet' 'stable') 'Cyan'
    }
    if ($Rollback) {
        Restore-Lumenveil
    } else {
        $channel = 'latest'
        $channelNote = ''
        if ((Test-Path -LiteralPath $channelFile) -and ([IO.File]::ReadAllText($channelFile)).Trim() -eq 'test') {
            $channel = 'test'
            $channelNote = Text 'TestChannel'
        }
        Say (Text 'Checking')
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor
            [Net.SecurityProtocolType]::Tls12
        $manifest = $null
        $feed = $null
        try {
            $manifest = Get-Feed ($Site + '/patches/manifest.json')
            $feed = Get-Feed ($Site + '/patches/lumenveil/' + $channel + '.json')
        } catch {
            if (-not $manifest) { throw (Text 'Offline' $_.Exception.Message) }
        }
        Update-Patches $manifest
        $installed = Get-InstalledLumenveil
        if ($feed) {
            Update-Updater $feed
            if ($installed) { Update-Lumenveil $feed $installed $channelNote }
        }
        Say (Text 'Done') 'Green'
    }
} catch {
    $failed = $true
    Say (Text 'Failed' $_.Exception.Message) 'Red'
}
if ($Root -and -not $Rollback) {
    try {
        if (-not $script:SetupRan -and (Get-InstalledLumenveil) -and (Test-SetupMissing)) { Invoke-Setup }
        Add-Shortcut
    } catch {
        $failed = $true
        Say (Text 'Failed' $_.Exception.Message) 'Red'
    }
}
if ($Play -and $Root) {
    if ($failed) {
        Start-Sleep -Seconds 4
    } elseif ($script:Notable) {
        Write-Host ''
        Write-Host (Text 'PressToPlay')
        [void][Console]::ReadLine()
    } elseif ($script:Updated) {
        Start-Sleep -Seconds 5
    }
    $game = Join-Path $Root 'Wow.exe'
    if (Test-Path -LiteralPath $game) {
        Say (Text 'Starting')
        try { Start-Process -FilePath $game -WorkingDirectory $Root } catch { Say $_.Exception.Message 'Red' }
    }
} else {
    Write-Host ''
    Write-Host (Text 'PressKey')
    [void][Console]::ReadLine()
}
if ($failed) { exit 1 }
exit 0
