# Orizon OS

Orizon OS est maintenant un projet autonome et personnel. Ce depot est la
source d'autorite du systeme: il n'y a plus de synchronisation prevue avec un
autre OS ou un depot amont externe.

Pour connaitre l'etat exact de validation, les limites et ce qui est seulement
prepare pour le materiel futur, commence par
[docs/orizon/STATUS.md](docs/orizon/STATUS.md). La regle actuelle est simple:
la VM ZimaOS peut etre testee, mais aucune validation Lenovo, dongle USB, AP
Wi-Fi reel ou autre PC physique ne doit etre revendiquee sans capture materielle
fraiche.
Pour le bureau, la section "Desktop Hyprland-Style Truth Taxonomy" de
`STATUS.md` est la grille de lecture: implemente/VM-ready signifie Orizon en
VM, "simulated facade" signifie surface compatible Hyprland-style, "prepared"
signifie contrat ou paquet futur, et "not implemented" couvre toujours le vrai
Wayland/wlroots/upstream Hyprland, XWayland, layer-shell, Waybar actif,
taskbar/menu demarrer, flottant libre et drag manuel.

## Direction actuelle

Le point d'entree actif est `orizon-os-x86_64`, recentre en base minimale pour
le developpement noyau:

- demarrage stable en VM et sur cible `x86_64` UEFI
- interface framebuffer simple avec splash `Orizon OS`
- une seule console centrale avec historique persistant et autocompletion simple
- racines data persistantes `/workspace`, `/home`, `/system`, `/packages` et
  `/logs` quand une zone donnees Orizon est disponible, avec snapshots
  alternes, statut `persist status`, inventaire `persist slots`, restauration
  `persist restore previous` et reparation simple `persist repair`
- etat systeme installe/live lisible avec `system status`, checklist
  non-destructive `rescue`, reparation des fichiers initiaux via
  `system repair`, mini-init via `system init` avec restauration desktop
  VM-safe, audit `system doctor`,
  politique services via `system services`, journal admin via `system logs`,
  sante synthetique `system health`, snapshot administrateur
  `system snapshot`, export de configuration non sensible `system backup`,
  checklist `system firstboot`, marqueur `firstboot done` et hostname
  persistant avec `hostname set <nom>`
- installateur disque guide avec langue, clavier, GPT, ESP FAT32, mode
  dual-boot data sur partition choisie, mode ESP seul non destructif,
  verification du boot UEFI, selection explicite du disque/partition cible et
  reparation de l'ESP
- layout clavier persistant `fr-azerty` ou `us-qwerty` applique au boot
- pilotes materiel elargis: clavier USB HID plus propre, stockage
  AHCI/NVMe/VirtIO-blk,
  Ethernet Intel e1000/e1000e, Realtek RTL8139, VirtIO-net pour Proxmox/QEMU,
  chemin paquet USB Ethernet xHCI pour CDC-ECM brut et Realtek RTL815x,
  diagnostics persistants `/logs/usb.log` pour CDC-NCM/ASIX/SMSC/RNDIS, et
  detection stage-0 du Wi-Fi Intel CNVi
- commande `update` interne, disponible seulement apres installation disque,
  qui telecharge le manifeste GitHub, verifie les artefacts SHA-256 et reecrit
  soit l'ESP Orizon complet, soit uniquement `/EFI/Orizon` en dual boot data
- garde de boot post-update: Orizon arme un etat `pending`, bascule
  temporairement le default Limine vers le rollback pendant le boot de test,
  valide automatiquement le nouveau kernel quand il atteint le shell, puis
  expose `bootguard` pour voir ou confirmer l'etat de validation
- mini gestionnaire de paquets `pkg` avec format texte `.opkg`, SHA-256 du
  payload, installation de fichiers et script post-install minimal
- depot officiel de paquets GitHub `Orizon-Packages`, lu par `update` pour
  installer des composants separes du kernel
- premier profil bureau optionnel, inspire de Hyprland, desactive par defaut:
  choix dans l'installateur, commandes `desktop`, paquet
  `orizon-desktop-hypr`, config `/system/desktop.conf`,
  `/system/desktop-session.conf`, `/system/desktop-settings.conf` et
  `/home/orizon/.config/hypr/orizon-hypr.conf`, session theme/wallpaper/bar
  off by default,
  hub de settings `/system` synchronisable avec `desktop settings paths/export/sync`,
  carte de modules `/system/desktop-modules.conf` consultable avec
  `desktop modules`, carte architecture `/system/desktop-architecture.conf`
  consultable avec `desktop architecture`, carte backend
  `/system/desktop-backend.conf` consultable avec `desktop backend`, et
  protocole interne `/system/desktop-protocol.conf` consultable avec
  `desktop protocol`, samples modulaires generables/installables avec
  `pkg sample/install orizon-desktop-core|orizon-terminal|orizon-settings|orizon-launcher`,
  `orizon-desktop-core`/`orizon-terminal`/`orizon-settings`/`orizon-launcher`
  et `orizon-waybar` seulement prevu pour plus tard,
  dix workspaces runtime dynamiques, clients tiles avec adresses stables/focusHistoryID,
  dispatchers Hyprland-like, focus directionnel HJKL,
  fullscreen/fullscreenstate/pseudo/pseudotile/pin,
  runtime `desktop binds/rules/monitors/runtime/layers`, `layerrule`,
  variantes `bind`/`bindl`/`bindr`/`binde`/`bindm`, `unbind`, `binds:*`,
  `bezier/animation` et hints input/misc/layout/dwindle/master/gestures/xwayland, diagnostics
  (les binds souris `bindm` sont parses pour compatibilite seulement, sans free-drag),
  submaps clavier F9/F10/F11 avec diagnostics role/actions, sortie sticky reset
  et focus-follows-mouse VM,
  `desktop version/devices/keymap/systeminfo/backend/protocol/architecture/truth/layouts/layout-state/layout-tree/animations/configerrors/config-trace/rollinglog/focus-history/workspace-stack/client-model/rule-matches`, facade
  `desktop hyprctl [-j] version/systeminfo/backend/protocol/architecture/truth/clients/clientmodel/rulematches/workspaces/activeworkspace/activewindow/focushistory/workspacestack/monitors/layouts/layoutstate/layouttree/animations/decorations/render/descriptions/instances/modules/shortcuts/autostart/apps/app/launch/submap/devices/keymap/cursorpos/splash/session/configerrors/configtrace/rollinglog/getoption/keyword/dispatch/reload/binds/layers`, diagnostics JSON compacts
  `desktop hyprctl -j version|systeminfo|backend|protocol|architecture|truth|clients|workspaces|activeworkspace|activewindow|focushistory|workspacestack|clientmodel|rulematches|layoutstate|layouttree|monitors|devices|keymap|cursorpos|animations|decorations|render|layouts|descriptions|instances|modules|shortcuts|autostart|apps|app|launch|submap|splash|session|rollinglog|configerrors|configtrace|getoption|keyword|dispatch|reload|binds|layers`, mutation
  `desktop keyword`, lanceur F3, terminal F1/F2, raccourcis F4-F8 et submaps
  clavier F9/F10/F11; ce n'est
  pas encore le vrai Hyprland/Wayland
- console avec scrollback, defilement clavier `z`/`s`, pager `less <fichier>`
  en plein ecran, `tail`, `help shell`, commandes groupees avec `;`, sorties
  redirigees `>`/`>>`, pipes simples vers `grep/head/tail/wc/tee/less`,
  `shell status`, `history grep`, support molette souris PS/2, `edit` ameliore
  et navigation historique `Up/Down`
- diagnostics `sysinfo`, `hw`, `hw next`, `mounts`, `report`, `report next`,
  `report save` et `selftest`
  pour voir CPU, memoire, stockage, racines data, reseau, USB/PS2, installation,
  update, principaux peripheriques PCI et plan de capture materielle future
- service `ssh` experimental: listener TCP/22, banniere SSH Orizon, paquet
  `KEXINIT`, X25519, signature hote RSA de developpement, `ECDH_REPLY`,
  `NEWKEYS`, premiere lecture/reponse chiffree `SERVICE_REQUEST` /
  `SERVICE_ACCEPT`, authentification password explicite pour `orizon`, canal
  `session`, `pty-req`, `shell`, `exec`, mini-shell distant de diagnostic,
  configuration `/system/ssh.conf`, journal `/logs/ssh.log` et diagnostics
  `ssh status`
- inspection stockage avec `disks`, `partitions`, `storage detail`,
  `storage diag`, `storage vmcheck`, `logs storage`, `logs pci`, `disk identify`,
  `disk read-test`, `gpt scan` et selection du disque actif via
  `storage select <n>`
- journal noyau en memoire avec `dmesg`, lecture des journaux via `logs` et
  rapport compact `report`; apres installation, le boot log est conserve dans
  `/logs/boot.log`
- timer noyau PIT 100 Hz, uptime reel, boucle idle `hlt` pour eviter le CPU a 100%
- debut de table processus/scheduler visible avec `ps`
- timer LAPIC/APIC sur machines UEFI modernes, avec fallback PIT puis polling
  de diagnostic si aucune IRQ timer ne parvient au shell

Ce qui est volontairement absent du profil actif:

- gestionnaire de fichiers integre
- bureau de demonstration
- jeux et applications integrees non essentielles
- flux de mise a jour amont non-Orizon

## Installation Disque

L'installation sur disque se lance depuis la console:

```text
install
```

L'assistant demande la langue, le clavier, le mode disque, le hostname et le
choix optionnel du bureau, puis
peut preparer Orizon OS sur le disque cible. Le flux affiche les disques
detectes (`disk0`, `disk1`, etc.) avec type, taille et modele.

Le bureau reste absent par defaut. Si tu le choisis pendant l'installation, ou
si tu l'ajoutes plus tard avec `pkg install orizon-desktop-hypr`, Orizon genere
et installe le paquet local `orizon-desktop-hypr`, puis active un profil inspire
de Hyprland. `desktop doctor`, `desktop logs` et `desktop shortcuts` servent a
le diagnostiquer/configurer. `desktop settings` expose les reglages systeme
persistants dans `/system/desktop-settings.conf`; `desktop settings set <key>
<value>` les modifie, et `desktop settings repair` restaure les valeurs
propres. `desktop settings presets`, `desktop settings preset <name>` et
`desktop settings doctor` ajoutent des profils systeme et un diagnostic dedie
pour ce fichier. `desktop settings paths` montre le hub de settings, `desktop
settings export` regenere `/home/orizon/.config/hypr/orizon-hypr.conf` depuis
`/system`, et `desktop settings sync` exporte puis rafraichit les hints runtime.
`desktop config doctor` analyse la config Hyprland-style dans
`/home/orizon/.config/hypr/orizon-hypr.conf`; `source =
~/.config/hypr/orizon-local.conf` cree/charge maintenant un fichier local
d'overrides VM-safe sous `/home/orizon/.config/hypr/orizon-local.conf`, avec
diagnostics `source-resolve`. `desktop config apply` importe le sous-ensemble
supporte vers la session, les settings, et les fichiers
runtime inspectables `/system/desktop-binds.conf`,
`/system/desktop-autostart.conf`, `/system/desktop-rules.conf`,
`/system/desktop-monitors.conf`, `/system/desktop-layers.conf`,
`/system/desktop-runtime.conf`, `/system/desktop-architecture.conf`,
`/system/desktop-backend.conf`, `/system/desktop-protocol.conf` et
`/system/desktop-state.conf`.
`desktop config trace` explique ligne par ligne ce qui est applique, prepare
comme hint, ignore ou malforme, et indique aussi si chaque `source` est charge,
manquant ou ignore, sans modifier la session.
`desktop start|stop|restart|reload|recover|rescue` gere la session Hyprland-style avec etat
persistant et log `/logs/desktop-session.log`; `desktop state`, `desktop
hyprctl -j session [status|start|stop|restart|reload|recover|rescue]`, `desktop
session`, `desktop theme`, `desktop wallpaper`, `desktop preset`, `desktop
focus`, `desktop bar` et `desktop launcher` reglent la session persistante.
`desktop rescue` affiche une checklist non destructive avec health,
desired/runtime/policy, boot live/installe et les fichiers a reparer avant
`desktop recover`.
Depuis `0.88.0`, `desktop state`, `desktop rescue` et
`desktop hyprctl -j session [status|start|stop|restart|reload|recover|rescue]`
exposent aussi un audit de session VM-ready: fichiers presents/taille,
`recommendedAction`, `rescueRecommended`, policy, runtime et limites explicites
sans validation materiel.
Depuis `0.89.0`, `system init` recharge aussi la session desktop via une action
`desktop-restore` idempotente, journalisee dans `/logs/init.log` et
`/logs/service.log`, puis le compositor reapplique la policy desktop au boot VM.
Depuis `0.90.0`, cette restauration tente aussi `desktop recover` si le reload
revient en WARN et expose `fallback-action`/`fallback-result` avant de
recommander `desktop rescue`.
`desktop input` centralise le layout clavier, le profil pointeur et
focus-follows-mouse: `desktop input layout fr|us` synchronise
`/system/desktop-settings.conf`, `/system/keyboard` et
`/workspace/.orizon/keyboard`, `desktop input pointer <profile>` garde le profil
dans les diagnostics VM, et `desktop input focus <on|off|toggle>` controle le
focus souris sans activer de deplacement libre des fenetres.
`desktop pointer` affiche la position du curseur et les diagnostics PS/2,
USB HID souris/tablette et I2C-HID, utile en VM quand QEMU/libvirt expose une
`usb-tablet`. `desktop keymap` montre les raccourcis actifs, le dernier
evenement clavier, les submaps F9 resize/F10 move/F11 launch/F12 reset et le
compteur de focus par souris quand `desktop focus on` active focus-follows-mouse.
`desktop binds` lit maintenant le runtime de binds genere et
`desktop rules`, `desktop monitors`, `desktop runtime`, `desktop layers`,
`desktop version`, `desktop devices`, `desktop keymap`, `desktop systeminfo`,
`desktop backend`, `desktop protocol`, `desktop architecture`, `desktop truth`, `desktop layouts`, `desktop layout-state`, `desktop layout-tree`,
`desktop animations`, `desktop decorations`, `desktop render`, `desktop descriptions`, `desktop
instances`, `desktop modules`, `desktop shortcuts`, `desktop submap`, `desktop configerrors`, `desktop rollinglog`,
`desktop focus-history`, `desktop workspace-stack`, `desktop client-model`, `desktop rule-matches`,
`desktop keyword <key> <value>` et
`desktop hyprctl [-j] version|systeminfo|backend|protocol|architecture|truth|clients|clientmodel|rulematches|workspaces|activeworkspace|activewindow|focushistory|workspacestack|layouts|layoutstate|layouttree|animations|decorations|render|descriptions|instances|modules|shortcuts|autostart|apps|app|launch|submap|devices|keymap|cursorpos|splash|session|configerrors|configtrace|rollinglog|getoption|keyword|dispatch|reload|binds|layers` exposent/modifient le
sous-ensemble Hyprland-style supporte ou conserve comme hint runtime.
Le parser conserve maintenant aussi les familles `input`, `device`,
`decoration`, `cursor`, `render`, `debug`, `dwindle`, `master`, `group`,
`binds:*`, `plugin` et `permission` comme hints runtime inspectables quand
Orizon ne sait pas encore les appliquer comme un vrai backend Wayland. Les
variantes `bindl`/`bindr`/`binde`/`bindm` sont classees dans les diagnostics;
`bindm` reste un hint prepare et n'active pas de drag souris.
Depuis `0.86.0`, `desktop configerrors`, `desktop hyprctl -j configerrors`,
`desktop hyprctl -j binds` et `desktop hyprctl -j shortcuts` exposent aussi les
compteurs `plain`/`keyboard`/`mouse`/`compositeFlags` et
`manualDragFromBindm=false` pour distinguer clairement la compatibilite
Hyprland-style de l'absence de deplacement manuel libre.
Depuis `0.87.0`, les diagnostics `source` listent aussi chaque fichier resolu
avec statut `LOADED`/`MISSING`/`SKIP_*`/`*_LIMIT` dans `desktop configerrors`
et `desktop hyprctl -j configerrors`.
Depuis `0.93.0`, `desktop render`, `desktop decorations`, `desktop animations`
et les sorties JSON equivalentes detaillent aussi la surface framebuffer, les
zones reservees, la zone tiled, la scale-policy, le frame-budget, les clients
rendus, les gaps/borders/rounding, le focus ring, les shadows et l'etat des
animations client/workspace. Cela reste une facade VM Hyprland-style sur
framebuffer logiciel, sans Wayland/wlroots reel. Les reglages persistants
passent par `desktop settings set focus-ring <yes|no>`,
`desktop settings set render-profile <balanced|performance|cozy>`,
`desktop keyword decoration:shadow:range <0-32>` et
`desktop keyword animations:tick_budget <4-60>`.
`desktop modules` affiche la carte de packaging modulaire: le paquet actuel
reste `orizon-desktop-hypr`, tandis que `orizon-desktop-core`,
`orizon-terminal`, `orizon-settings` et `orizon-launcher` ont maintenant des
samples `.opkg` separes via `pkg sample <module>` et des installs nommes sur VM
installee. Les modules app auto-preparent `orizon-desktop-core`; `orizon-waybar`
est seulement annonce comme paquet separe ulterieur, pas genere ni installe
maintenant.
Depuis `0.94.0`, `desktop modules`, `desktop hyprctl -j modules`, `pkg info`
et les `.opkg` de modules exposent aussi `split-plan`, `dependency-graph`,
frontieres de modules, roles d'activation et `split-version 2`, afin de rendre
la separation `orizon-desktop-core`/apps plus verifiable en VM.
Depuis `0.96.0`, `desktop truth`, `desktop hyprctl truth` et
`desktop hyprctl -j truth` exposent la taxonomie runtime implemente/VM-ready/
simulated-facade/prepared/not-implemented/not-hardware-proven, afin de verifier
en VM que le bureau reste une facade Orizon Hyprland-style et non upstream
Hyprland/Wayland/wlroots/Waybar/materiel reel.
Depuis `0.95.0`, `desktop architecture`, `desktop backend`, `desktop protocol`
et les sorties JSON `desktop hyprctl -j architecture|backend|protocol` exposent
aussi les contrats `single-framebuffer-surface-v0`,
`software-raster-present-v0`, `internal-tiled-client-v0`, les capacites/limites
du backend courant et le contrat du futur backend `wayland-wlroots`. Cela reste
une preparation d'architecture: aucun serveur Wayland/wlroots, xdg-shell,
layer-shell reel, XWayland ou client Wayland externe n'est actif.
`desktop architecture`, `desktop backend` et `desktop protocol` documentent le
split d'architecture: API `orizon-compositor-api-v0`, API backend interne
`compositor-backend-v0` (`kernel/include/compositor_backend.h` et
`kernel/gui/compositor_backend.c`), backend actuel `framebuffer-vm`, protocole
interne `orizon-desktop-ipc-v0` avec trace runtime `desktop-protocol-v0`
(`kernel/include/desktop_protocol.h` et `kernel/system/desktop_protocol.c`),
future cible `wayland-wlroots` preparee, mais pas encore Wayland/wlroots,
xdg-shell, layer-shell reel, XWayland ni clients Wayland externes.
`desktop layout-tree` et `desktop hyprctl layouttree` exposent l'arbre actif du
workspace: roles `dwindle`/`master`/`monocle`, rectangles, focus,
`focusHistoryID`, et la limite explicite `manual-drag=no`.
`desktop layout-state` et `desktop hyprctl layoutstate` exposent maintenant
l'etat tiling par workspace: layout actif, split mode, split ratio et master
ratio et `nmaster`. Depuis `0.85.0`, `layout-state`, `layout-tree` et
`desktop hyprctl -j dispatch` ajoutent aussi `lastDispatch`: dernier
dispatcher, arguments, statut, code `error`, `hint`, resultat lisible,
workspace, layout, ratios, submap et client focus pour diagnostiquer les
erreurs sans activer floating, drag manuel ni socket Hyprland reel.
`desktop dispatch layoutmsg layout
<dwindle|master|monocle>` modifie le workspace courant sans activer de
fenetres flottantes ni de drag manuel; en monocle, seul le client focus est
rendu et les autres restent visibles dans les diagnostics comme
`monocle-deck`/`rendered=no`.
`desktop dispatch layoutmsg reset`, `splitratio reset`, `masterratio reset`,
`nmaster reset`, et `preselect <l|r|u|d|reset>` ajoutent des controles de
recuperation/script VM-safe pour retrouver rapidement un tiling propre sans
floating, drag manuel, barre Windows ni Waybar.
`desktop workspace-stack` et `desktop hyprctl workspacestack` exposent le stack
par workspace: client master, clients stack/dwindle, scope local/pinned,
rang de focus et geometrie, toujours avec `manual-drag=no`.
`desktop client-model` et `desktop hyprctl clientmodel` agregent workspaces,
clients, focus history, etats fullscreen/pseudo/pinned/urgent, regles et backend dans
un graphe de diagnostic read-only pour comprendre l'etat tiling courant.
Les cibles workspace acceptent maintenant les prefixes Hyprland-style
`r+/-n`, `r~n`, `m+/-n`, `e+/-n`, `m~n` et `e~n`; `r` inclut les slots vides
et `m/e` parcourent les workspaces ouverts dans la facade VM mono-moniteur.
`desktop hyprctl -j version|systeminfo|backend|protocol|architecture|truth|clients|workspaces|activeworkspace|activewindow|focushistory|workspacestack|clientmodel|rulematches|layoutstate|layouttree|monitors|devices|keymap|cursorpos|animations|decorations|render|layouts|descriptions|instances|modules|shortcuts|autostart|apps|app|launch|submap|splash|session|rollinglog|configerrors|configtrace|getoption|keyword|dispatch|reload|binds|layers`
ajoute une sortie JSON compacte pour les futurs paquets/outils de status, avec
des champs Hyprland-style (`address`, `workspace`, `fullscreenClient`, `tags`,
`windows`, `lastwindow`, `focusHistoryID`, `scope`, `role`, `pinnedAware`,
`summary`, `safeAction`, `nodes`, `rect`, `parserSummary`, `trace`, `result`,
`dispatcher`, `args`, `runtimeFile`, `singleFramebuffer`, `libinput`, `activeSubmap`,
`desiredState`, `runtimeState`, `sessionLogTail`,
`currentBackend`, `futureBackend`, `protocol`, `renderer`, `focusRing`,
`transition`, `renderProfile`, `manualWindowDrag`, `mouseBindsPreparedOnly`,
`waybarActive`) et le marqueur explicite
`hyprlandStyleFacade=true`.
`desktop rule-matches` et `desktop hyprctl rulematches` lisent
`/system/desktop-rules.conf` et indiquent quelles regles `windowrulev2`
correspondent aux clients actuels par class/title/app/tag, initialClass,
initialTitle, workspace, focus, pin et fullscreen avec un matching simplifie.
Les actions sures `tile`, `fullscreen`, `pseudo`, `pin`, `tag` et
`workspace N` sont appliquees au spawn des clients tiles; les actions
floating/free-drag restent ignorees et visibles dans le diagnostic. Ce n'est
pas encore le moteur regex/Wayland d'Hyprland upstream.
`desktop apps` expose le catalogue des clients desktop, `desktop app <id>`
detaille classe/module/surface, source de donnees, runbook et limites, et
`desktop launch terminal|settings|logs|packages|update|launcher` ouvre les
premieres apps natives comme clients tiles geres par le compositor. Les panels
framebuffer settings/logs/packages/update affichent maintenant leurs sources et
commandes utiles dans le tile actif. Depuis `0.91.0`, ces apps exposent aussi
des diagnostics runtime read-only par app: clients ouverts/mappes/caches,
workspace, focus, adresse du client focalise et visibilite overlay du launcher.
Depuis `0.92.0`, `desktop submap`, `desktop keymap`, `desktop shortcuts` et les
sorties JSON liees exposent aussi role/actions de submap active, sticky reset,
sortie Esc/F12, layout clavier, profil pointeur et compteurs focus-follows-mouse.
Le launcher est seulement un overlay de dispatch: il n'ajoute ni barre type
Windows, ni menu demarrer permanent, ni fenetres flottantes.
`desktop hyprctl -j apps|app <id>|launch <app>` expose les memes apps en JSON
VM-ready avec resultats de lancement, objet `runtime`, `manualDrag=false`,
`taskbar=false`, `startMenu=false` et `waybarActive=false`.
`desktop hyprctl -j autostart [terminal on|off|toggle]` expose et pilote
l'autostart terminal persistant avec runtime `exec-once` VM-ready, sans ajouter
de barre, menu demarrer, Waybar actif ou drag manuel.
`desktop dispatch exec|killactive|workspace|focusworkspaceoncurrentmonitor|focusmonitor|movecurrentworkspacetomonitor|moveworkspacetomonitor|togglespecialworkspace|renameworkspace|movetoworkspace|movetoworkspacesilent|movefocus|focusmwindow|focuswindow|focuscurrentorlast|focusurgentorlast|markurgent|tagwindow|swapwindow|swapmwindow|movewindow|fullscreen|fullscreenstate|pseudo|pseudotile|pin|cyclenext|swapnext|focusmaster|swapwithmaster|togglesplit|layoutmsg|resizeactive|submap`
installent un modele facon Hyprland: workspaces, clients tiles, focus, etats
client fullscreen/pseudo/pinned/urgent, workspaces relatifs/dynamiques `next`/`empty`,
workspaces nommes via `renameworkspace` puis `workspace name:<nom>`, scratchpad
special via `togglespecialworkspace [nom]` et `movetoworkspace special[:nom]`,
dispatchers monitor VM-safe qui rapportent `single-framebuffer=yes` sans
pretendre a un routage multi-ecran Wayland,
restauration du focus par workspace, `movetoworkspace` qui suit le client et
deplacement silencieux qui reste sur place, focus/swap/movewindow directionnels `l/r/u/d`,
focus/swap par rang `focusmwindow`/`swapmwindow <next|prev|master|rank:n|index:n>`,
ciblage
direct de client par `id`, adresse `0x...`, `class:`, `title:` ou `tag:`, layouts
`dwindle/master/monocle`, reset/preselect de layout, split/master ratios, `nmaster`, orientations explicites
`orientationleft/right/top/bottom`, sans deplacement manuel de fenetres a la souris.
`movewindow <l|r|u|d|next|prev|master>` deplace seulement l'ordre tiling du
client actif, sans mode flottant et sans drag pixel libre.
Les dispatchers de deplacement acceptent aussi un selecteur de client facon
Hyprland (`desktop dispatch movetoworkspacesilent 2,class:orizon-settings` ou
`desktop dispatch movetoworkspace active,activewindow`) pour deplacer une
fenetre tile ciblee sans drag manuel ni bureau flottant.
Le scratchpad special reste tiling: `desktop dispatch movetoworkspacesilent
special:magic,activewindow` cache le client dans l'overlay special, puis
`desktop dispatch togglespecialworkspace magic` l'affiche/masque sur le
workspace courant sans activer de flottant ni de deplacement manuel.
`desktop dispatch tagwindow +settings class:orizon-settings` ajoute un tag
diagnostic VM-safe, ensuite reutilisable avec `focuswindow tag:settings` ou
`movetoworkspace 2,tag:settings`.
Les etats du client actif acceptent aussi des valeurs idempotentes:
`desktop dispatch fullscreen|pseudo|pseudotile|pin on|off|toggle|1|0` et
`desktop dispatch fullscreenstate <internal 0-3|-1> <client 0-3|-1>`, avec
compatibilite `on|off|toggle|1|0`, ce qui evite les bascules ambigues dans les
scripts et tests VM. `fullscreenClient` est expose en diagnostic, prepare pour
de futurs vrais clients, mais le backend reste framebuffer VM.
`markurgent` est un diagnostic VM pour exercer `focusurgentorlast`; ce n'est
pas encore un signal d'urgence Wayland/wlroots venant d'une application reelle.
`desktop windows`, `desktop clients`, `desktop activewindow`, `desktop
focus-history`, `desktop workspace-stack`, `desktop client-model` et `desktop rule-matches` exposent les clients tiles, adresses
stables, geometries, tags, graphe workspaces/focus, etats fullscreen/pseudo/pinned/urgent,
regles appliquees au spawn, backend et workspace courant/precedent avec
`focusHistoryID`. `desktop
profiles` liste les profils symboliques, `desktop preset <name>` applique une
session complete, et `desktop autostart terminal on|off|toggle` controle le
terminal au demarrage. F1 lance un terminal, F2 ferme le client actif, F3
affiche le lanceur, F4 bascule fullscreen, F5 pseudo, F6 cycle le focus, et
F7/F8 changent de workspace. F9 active la submap resize, F10 la submap move,
F11 la submap launch et F12/Esc revient a `default`, toujours sans drag manuel.

Le mode recommande pour une machine qui contient deja Windows/Linux est
`dual-boot-data`: il detecte la GPT existante, trouve l'ESP FAT32, affiche les
partitions avec `partitions`, puis te demande quelle partition vide/prete peut
devenir `Orizon Data`. La confirmation est `DUALDATA disk0 partN`. Orizon ecrit
les fichiers de boot dans `/EFI/Orizon`, marque uniquement la partition choisie
comme data Orizon, sauvegarde `/workspace`, `/home`, `/system`, `/packages` et
`/logs`, puis autorise `update`.

Le mode `dual-boot-esp` reste disponible pour tester sans installation data:
il ecrit uniquement `/EFI/Orizon/BOOTX64.EFI`, `/EFI/Orizon/kernel.elf`,
`/EFI/Orizon/limine.conf` et `/EFI/Orizon/INSTALL.TXT`. La confirmation est
`DUALBOOT disk0`. Aucune partition existante n'est reformatee et Orizon ne se
marque pas comme installe complet, donc `update` et `pkg install/remove`
restent bloques tant qu'il n'y a pas de vraie partition data Orizon.

Le mode `guided-full-disk` reste le mode complet et destructif. Il demande une
confirmation sous la forme `ERASE disk0`, ecrit une GPT, formate une ESP FAT32,
copie `BOOTX64.EFI`, `kernel.elf` et `limine.conf`, puis conserve une partition
data Orizon pour `/workspace` et les racines `/home`, `/system`, `/packages`
et `/logs`.
Avant l'ecriture disque, `/workspace` est synchronise pour garder les dossiers
et fichiers crees pendant le live boot.

Apres une installation reussie, Orizon OS marque le disque comme installe,
affiche une consigne de retrait/ejection de l'ISO ou de la cle USB, puis lance
un shutdown. Au boot suivant, la commande `install` est bloquee pour proteger
le disque et les donnees.

Au premier boot installe, commence par:

```text
system status
system health
system firstboot
system services
system logs
system snapshot
system backup
firstboot done
```

`system status` distingue clairement `boot-mode: live` et
`boot-mode: installed`, affiche le hostname, l'etat first-boot, les racines
persistantes, les fichiers initiaux et les commandes sures suivantes. Si un
fichier systeme de base manque dans `/system`, `/home`, `/packages` ou
`/logs`, `system health` donne un resume PASS/WARN, `system logs` regroupe
`/system/boot-state`, `/system/service-state`, `/logs/init.log` et
`/logs/service.log`, `system snapshot` ecrit
`/workspace/.orizon/system-snapshot.txt`, `system backup` exporte uniquement
la configuration non sensible vers `/workspace/.orizon/admin-backup.txt`, et
`system repair` recree uniquement les defaults manquants avant d'ecrire un
rapport dans `/workspace/.orizon/rescue-report.txt`; il ne partitionne pas et
n'installe pas l'OS.

L'installateur verifie maintenant le boot installe avant de marquer le disque
comme pret: MBR protecteur, GPT, ESP FAT32, label, `EFI/BOOT/BOOTX64.EFI`,
`boot/kernel.elf` et les configurations Limine. Pour refaire ce diagnostic:

```text
boot-check
```

Pour verifier les fichiers side-by-side du mode dual boot:

```text
dualboot-check
```

Apres une mise a jour qui remplace le kernel, la commande suivante montre si
le nouveau boot a ete valide ou si un rollback reste en attente:

```text
bootguard
bootguard confirm
rollback-status
```

Si le disque a deja une installation Orizon mais que l'ESP est abimee, la
commande suivante reecrit uniquement les fichiers de boot et preserve la
partition data:

```text
repair-boot
```

## Reseau Bridge Et Proxmox

Orizon OS peut utiliser une carte reseau VM en NAT ou en bridge. Pour Proxmox,
la configuration recommandee est:

```text
Bridge: vmbr0
Model: VirtIO (paravirtualized)
DHCP: active sur le LAN
```

Le bridge ne remplace pas DHCP: il branche simplement la VM sur le meme reseau
que l'hote. Si le reseau local ne distribue pas d'adresse, Orizon peut utiliser
une IP statique persistante dans `/system/network.conf`.
Dans Orizon, `net` affiche le pilote detecte, `net dhcp` teste l'obtention
d'une adresse IPv4 sans lancer une mise a jour, et `net auto` tente DHCP puis
la configuration statique si DHCP echoue. `net check` donne un diagnostic
quotidien PASS/WARN/FAIL pour lien, IPv4, route, passerelle et DNS; `net daily`
ajoute la politique de retry et les conseils NAT/bridge; `net tcp
raw.githubusercontent.com 443` fait maintenant plusieurs essais par defaut et
separe rapidement DNS/TCP/firewall des problemes TLS. `net tls` lance le probe
HTTPS GitHub/root-trust quand `update` ou `pkg` echoue. `net diag` chaine
daily + check + TCP + TLS pour un rapport VM quotidien plus complet.

Exemple IP statique:

```text
net config ip 192.168.1.50 gateway 192.168.1.1 dns 192.168.1.1
net auto
net check
net daily
net tcp raw.githubusercontent.com 443
net tcp raw.githubusercontent.com 443 attempts 2
net tls
net diag
ping 8.8.8.8
dns raw.githubusercontent.com
route
logs network
```

La configuration est sauvegardee dans `/system/network.conf` et les journaux
reseau/USB dans `/logs/network.log` et `/logs/usb.log`, donc une machine Proxmox en bridge sans NAT
peut rester connectee a GitHub si son LAN autorise la passerelle et le DNS.
En cas de machine Proxmox configuree en VirtIO moderne-only, choisir le modele
reseau `Intel E1000` reste un fallback compatible.

Details: [docs/orizon/NETWORK.md](docs/orizon/NETWORK.md).

Pour lancer la matrice reseau VM depuis le labo ZimaOS:

```powershell
python scripts/orizon/build_x86_64_on_zimaos.py
python scripts/orizon/test_vm_matrix.py --cases nat-e1000e,nat-virtio,nat-rtl8139
python scripts/orizon/test_vm_matrix.py --cases nat-e1000e --include-lifecycle
python scripts/orizon/test_vm_matrix.py --cases nat-e1000e --disk-bus virtio --include-lifecycle
python scripts/orizon/test_update_rollback_vm.py
```

Le build ZimaOS direct rapatrie maintenant aussi `Orizon-OS.iso` a la racine
apres compilation reussie, sauf avec `--no-publish-root-iso`. La matrice
provisionne des VMs dediees, demarre Orizon, lance DHCP puis SSH, et teste
`system status`, `rescue`, `hostname`, `net status`, `timer`, `ping`, `dns`,
`net check`, `net daily`, `net tcp raw.githubusercontent.com 443`,
`net tcp raw.githubusercontent.com 443 attempts 2`, `pkg status`, `update status`,
`selftest`, les logs, `report save`, `cat /workspace/hardware-report.txt` et
`hostkey`. Le mode `--include-lifecycle` capture une screenshot framebuffer,
declenche `reboot`, reverifie SSH apres redemarrage, puis teste `shutdown`
propre en VM. Le smoke `test_update_rollback_vm.py` utilise une VM jetable
installee pour verifier `update`, manifeste/signature, bootguard et `rollback`.

## Acces SSH Orizon

Le service SSH se demarre explicitement depuis la console:

```text
net dhcp
ssh password <mot-de-passe>
ssh start
ssh status
ssh audit
ssh auth
ssh auth max <essais>
ssh auth lockout <secondes>
ssh auth default
ssh hostkey
security
security policy
security audit
security keys
security doctor
security rotate ssh-hostkey
ssh hostkey reload
ssh hostkey reset
ssh algorithms
ssh reload
ssh lockout clear
logs ssh
logs security
```

La commande configure IPv4 si besoin, ouvre TCP/22, ecrit la configuration
dans `/system/ssh.conf`, envoie la banniere `SSH-2.0-OrizonSSH_0.1`, negocie
`curve25519-sha256` avec `rsa-sha2-256`, signe `ECDH_REPLY`, derive les cles
AES-128-CTR/HMAC-SHA256, echange `NEWKEYS`, puis repond au premier
`SERVICE_REQUEST` chiffre par `SERVICE_ACCEPT`. L'authentification password est
desactivee tant que `ssh password <mot-de-passe>` n'a pas ete lance depuis la
console; ensuite OpenSSH peut se connecter avec `ssh orizon@<ip-orizon>`.
Le canal `session` accepte deja `pty-req`, `shell` et `exec` avec un mini-shell
de diagnostic (`help`, `ls`, `cd`, `cat`, `head`, `tail`, `touch`, `mkdir`, `rm`,
`write`, `append`, `system status`, `system repair`, `rescue`, `hostname`,
`logs`, `net`, `route`, `dns`, `ping`, `usb`, `wifi`, `ps`, `security`,
`security policy`, `security audit`, `security keys`, `security doctor`,
`pkg`, `update`, `update status`, `storage`, `disk identify`,
`disk read-test`, `gpt scan`, `selftest`, `hw next`, `report save`,
`report next`, `free`, `timer`,
`audit`, `ssh sessions`, `persist status`, `persist slots`, `persist save`, `sync`, `reboot`, `shutdown`, `status`, `auth`, `hostkey`,
`whoami`, `uname`, `pwd`, `uptime`, `exit`). Les commandes admin `ssh auth`,
`ssh lockout`, `ssh password`, `ssh hostkey reload/reset` et
`security rotate ssh-hostkey` fonctionnent aussi en
commande distante directe. Le service remet l'ecoute TCP en etat apres une
session fermee, garde une protection anti-bruteforce dans `/system/ssh.conf`,
et expose `audit` / `ssh audit` pour verifier sessions, auth, commandes,
derniers evenements et fermetures de canal. `report save` ecrit
`/workspace/hardware-report.txt` depuis SSH pour exporter un diagnostic complet.
Les longues sorties SSH sont segmentees en plusieurs paquets pour eviter les
coupures sur `report show`, `cat /workspace/hardware-report.txt` ou `logs`;
un meme transport SSH peut aussi rouvrir un canal `exec` apres une commande.
Les commandes `tail`, `logs ssh`, `logs security` et `logs boot` montrent la fin des fichiers ou
journaux quand ils deviennent longs. Les commandes `exec` inconnues renvoient
maintenant un `exit-status` non nul pour mieux fonctionner avec les scripts.
`ssh hostkey` affiche l'identite hote RSA generee pour
l'installation et stockee dans `/system/ssh_host_rsa.key`.
`security` resume la politique active: auth/lockout SSH, host key persistante,
politique VFS v2, manifest signe obligatoire, index paquet epingle,
compteurs de refus et garde-fous du shell SSH. Il rafraichit aussi
`/system/security-policy`, `/system/security-state` et le rapport doctor
`/workspace/.orizon/security-doctor.txt` sans exposer de secret.
`security policy` detaille les regles appliquees, `security audit` ajoute le
miroir persistant `/logs/security.log`, les fichiers d'etat et les compteurs de
refus, `security keys` montre la posture de rotation sans exposer de secret, et
`security doctor` donne un bilan PASS/WARN non destructif. `security rotate
ssh-hostkey` regenere l'identite SSH locale pour les futures sessions; le
client devra accepter le nouveau known_hosts. Les racines update/package restent
en rotation `release-required`, donc elles changent via release signee.
Les commandes generiques `cat/head/tail/write/append/touch/mkdir/rm` ne peuvent
plus lire ou modifier `/system/ssh.conf`, `/system/ssh_host_rsa.key` ni les
noms sensibles (`.env`, `.key`, `.pem`, `.ssh`, private, secret, token,
credential, id_rsa, id_ed25519). Les ecritures generiques SSH sont limitees a
`/workspace`, `/home`, `/logs` et `/packages`, avec `/workspace/.orizon`
reserve comme etat interne, et `rm` ne peut pas supprimer ces racines distantes.
`logs security` lit le miroir persistant
`/logs/security.log` sans exposer les mots de passe; l'audit masque aussi les
commandes `ssh password`, `write`, `append` et les identifiants Wi-Fi.

Details: [docs/orizon/SSH.md](docs/orizon/SSH.md).
Securite: [docs/orizon/SECURITY.md](docs/orizon/SECURITY.md).

La premiere version cible le cas le plus utile pour le labo et les machines
UEFI simples: un disque AHCI/SATA ou NVMe 512-byte LBA. Le mode dual boot
installe fonctionne maintenant si une partition vide/prete existe deja: Orizon
utilise l'ESP existante en side-by-side et reserve seulement la partition
selectionnee pour ses donnees et ses mises a jour. Il faudra encore ajouter une
entree UEFI NVRAM/BCD automatique et un outil de creation/redimensionnement de
partition depuis Orizon.

Pour revoir le plan:

```text
install-plan
install-status
```

Pour verifier le layout clavier actif:

```text
keyboard
```

Pour inspecter ou changer le disque actif avant diagnostic/reparation:

```text
disks
partitions
storage detail
storage select 1
```

Details: [docs/orizon/INSTALL.md](docs/orizon/INSTALL.md).

## Boot Sur Vrai Materiel

Si le splash `Orizon OS` apparait sur un PC portable mais que la console ne
s'ouvre jamais, le kernel est bien lance. Orizon tente maintenant le timer
LAPIC/APIC via ACPI MADT, puis retombe sur PIT, puis sur polling de diagnostic
si aucune IRQ timer n'arrive au shell.

Apres boot, verifier:

```text
sysinfo
report
report save
selftest
hw
pci
pci bars
logs storage
logs pci
storage diag
storage vmcheck
disk identify
disk read-test
disk read-test last
gpt scan
input
wifi
```

Le premier portable cible documente est le Lenovo 500w Yoga Gen 4. Son clavier
interne passe par PS/2, son SSD par NVMe, et son pave tactile ELAN/Wacom passe
par I2C-HID. Orizon contient maintenant une premiere sonde Intel LPSS/I2C-HID
pour ce chemin, avant le parseur multitouch complet. Le Wi-Fi Intel CNVi est
detecte par `wifi status`; `wifi firmware`, `wifi apm`, `wifi boot arm`,
`wifi alive`, `wifi queues arm`, `wifi context arm`, `wifi scheduler arm`,
`wifi rx poll`, `wifi command arm`, `wifi nvm arm`, `wifi nvm-info arm`
et `wifi bringup`
couvrent maintenant la chaine de
diagnostic
firmware: presence du blob Intel, reveil APM du controleur, release CPU
firmware, transfert FH DMA garde, attente du signal firmware alive, puis
preparation des rings commande/RX/TX cote hote, du context-info firmware,
des anneaux message MTR/MCR, de la premiere trame de commande scheduler, du
polling RX de reponse firmware, du doorbell commande explicite et d'une
premiere lecture NVM cache/capacites radio firmware. `wifi bringup` lance
la sequence complete et indique la premiere etape bloquante. `wifi command`
affiche aussi les snapshots avant/apres, les mots bruts RX/completion et les
valeurs TFD/byte-count pour diagnostiquer un blocage sur vrai materiel.
`wifi scan` prepare maintenant un plan de scan passif et `wifi scan arm` tente
la premiere requete UMAC scan minimale. `wifi scan poll` lit ensuite les
notifications firmware UMAC de debut, iteration et fin de scan pour verifier
que la carte parcourt bien les canaux, avec un premier tableau `result[...]`
indiquant canal, bande, statut probe et duree d'ecoute par canal. Le poll RX
surveille aussi les premieres trames beacon/probe-response pour remplir une
table `ap[...]` avec SSID, BSSID, canal, securite detectee et source de
detection. Si aucun AP ne sort encore, `wifi scan poll` affiche maintenant un
bloc `mpdu-debug` avec les octets bruts et offsets candidats pour corriger le
parseur sur vrai materiel.
`wifi connect <ssid> [password]` peut ensuite selectionner un AP scanne et
preparer les trames 802.11 open-system authentication + association request,
avec template RSN WPA2-PSK si un mot de passe est fourni. Pour WPA2, Orizon
derive aussi la PMK par PBKDF2-HMAC-SHA1 sans afficher la cle; `wifi crypto`
verifie les vecteurs SHA-1/PBKDF2, AES key unwrap, AES-CCM et un aller-retour
logiciel de RX CCMP protege AP->STA. Les passphrases 8-63 caracteres et les
PSK hexadecimales 64 caracteres sont acceptees. Le chemin RX reconnait deja
les reponses authentication/association correspondant au plan de connexion et
stocke leurs status codes. `wifi tx [auth|assoc|m2|m4|data|all]`
prepare maintenant les trames de gestion dans les buffers DMA TX et affiche le
doorbell prevu sans l'ecrire. Le chemin RX detecte aussi les trames
EAPOL-Key WPA2, capture l'ANonce, derive un PTK de diagnostic, prepare une
reponse M2 inspectable avec `wifi wpa`, puis prepare aussi M3/GTK/M4 quand les
trames EAPOL suivantes arrivent. `wifi tx m2` et `wifi tx m4` peuvent placer ces
reponses WPA en DMA. `wifi txcmd [auth|assoc|m2|m4|data]` construit aussi une
enveloppe Intel `TX_CMD` v10 de diagnostic dans un buffer separe, puis peut
l'envoyer avec `arm` si le contexte, RX et le binding STA sont prets.
`wifi bind` prepare maintenant les enveloppes diagnostiques `MAC_CONFIG`,
`LINK_CONFIG` et `STA_CONFIG` avec un `sta-id` AP local; apres cela,
`wifi bind arm` peut les envoyer une par une dans la queue commande avec ACK
firmware strict avant de passer a la suivante. Quand les trois ACKs sont vus,
`wifi txcmd` indique `bound=acked` dans son rapport, et `wifi txcmd <cible> arm`
peut envoyer le `TX_CMD` avec le meme garde-fou: contexte arme, RX pret,
binding ACKe, puis reponse firmware strictement associee a la sequence TX.
L'etat `wifi status` et le rapport `wifi rx poll` separent maintenant l'ACK
firmware de la reponse AP: Orizon ne marque une association comme confirmee que
si authentication TX, association TX, binding STA, authentication response et
association response sont tous acceptes. `wifi join <ssid> [password]` enchaine
maintenant automatiquement bringup, scan, connexion, binding, auth/assoc, puis
la sequence WPA2 M1/M2/M3/M4 avec progression courte. Pour WPA2, le chemin data
reste volontairement bloque tant que les cles de chiffrement ne sont pas
installees. `wifi key pairwise [arm]`
prepare la commande Intel `SEC_KEY_CMD` du groupe data-path pour installer la
cle paire CCMP derivee du PTK; elle ne s'envoie qu'apres association confirmee,
binding STA ACKe et `wifi txcmd m2 arm` ACKe. Apres M3, Orizon dechiffre le key
data avec AES key unwrap, extrait la GTK, puis `wifi key gtk [arm]` prepare et
peut envoyer la cle groupe CCMP. Le chemin WPA attend ensuite `wifi txcmd m4 arm`
avant de marquer la data path comme prete. `wifi data` construit alors une
premiere trame data protegee CCMP de diagnostic, et `wifi tx data` /
`wifi txcmd data arm` peuvent la faire passer par le meme chemin TX garde. La
pile IPv4 sait maintenant choisir ce lien Wi-Fi quand WPA2 est guarded-ready:
`net status` affiche `link=wifi`, puis `net dhcp`, ARP, IPv4 et les essais
GitHub passent par des trames Ethernet encapsulees en CCMP. `wifi online
<ssid> [password]` enchaine maintenant `wifi join`, DHCP via CCMP, DNS vers
`raw.githubusercontent.com` et un probe TLS GitHub; si ce probe passe,
`update` peut utiliser le lien Wi-Fi deja configure. `wifi validate <ssid>
[password]` fait la meme validation sans lancer de mise a jour, et `wifi
update <ssid> [password]` valide puis lance directement l'updater sur un
systeme installe. Ces commandes ecrivent une preuve PASS/FAIL multi-lignes dans
`/logs/wifi.log` et `/workspace/.orizon/wifi-validation`, avec etat WPA/CCMP,
DHCP, route, DNS et indice de prochaine action, consultable avec `logs wifi`.
Il reste a valider ce chemin sur le Lenovo avec un vrai AP et a durcir les
traces de diagnostic quand un AP refuse ou chiffre differemment une trame
protegee.

Pour importer localement le firmware Intel depuis le Linux du Lenovo sans le
committer dans Git:

```powershell
python scripts/orizon/import_intel_wifi_firmware.py
python scripts/orizon/orizon_update.py --mode zimaos-vm
```

Details:
[docs/orizon/HARDWARE_BOOT.md](docs/orizon/HARDWARE_BOOT.md) et
[docs/orizon/LAPTOP_HARDWARE.md](docs/orizon/LAPTOP_HARDWARE.md).

## Paquets Orizon

Orizon OS contient maintenant une premiere base de gestionnaire de paquets.
Le format est volontairement simple: un fichier texte `.opkg` contient `name`,
`version`, un `sha256` du payload, des blocs `file` a installer, puis un
bloc `post-install` minimal. La couche v5 ajoute recherche, verification de
l'index distant signe en cache, `pkg upgrade plan`, `pkg doctor`, audit/cache,
simulation dry-run, scripts `pre-remove`/`post-remove`, historique
transactionnel, et rollback local apres suppression.

Commandes disponibles:

```text
pkg list
pkg status
pkg audit
pkg doctor
pkg cache
pkg search orizon
pkg remote
pkg remote verify
pkg upgrade plan
pkg update
pkg upgrade
pkg info orizon-hello
pkg history
pkg sample
pkg hash /workspace/packages/orizon-hello.opkg
pkg verify /workspace/packages/orizon-hello.opkg
pkg simulate /workspace/packages/orizon-hello.opkg
pkg install /workspace/packages/orizon-hello.opkg
pkg remove orizon-hello
pkg rollback orizon-hello
```

`pkg update`, `pkg upgrade`, `pkg install`, `pkg remove` et `pkg rollback` sont
reserves a un OS installe sur disque. `pkg audit`, `pkg doctor`, `pkg cache`,
`pkg simulate <file>` et `pkg upgrade plan` restent non destructifs et peuvent
tourner depuis le live ISO. Les paquets installes sont stockes dans
`/workspace/.orizon/pkgdb`, puis rejoues au boot pour restaurer les fichiers
systeme en RAM comme `/system/share/...`. `pkg verify` controle le hash payload
et les dependances simples `depends`; `pkg install` restaure l'ancien paquet si
l'installation echoue avant la fin. `pkg remove` conserve un snapshot dans
`/workspace/.orizon/pkgdb/removed`, que `pkg rollback <name>` peut restaurer.
`pkg remote verify` ecrit aussi `/workspace/.orizon/pkgdb/cache/remote.status`
et `/workspace/.orizon/pkgdb/cache/remote.sig.status`. `pkg upgrade plan`
met en cache le dernier plan dans `/workspace/.orizon/pkgdb/upgrade.plan`, et
les operations d'ecriture exposent `/workspace/.orizon/pkgdb/transaction.state`.
`pkg info <name>` affiche les metadonnees, dependances, scripts et fichiers
possedes par un paquet. La signature detachee du depot packages est preparee
via `/workspace/.orizon/package-index.sig`; si ce sidecar n'est pas present,
Orizon l'indique en WARN et retombe honnetement sur le manifest update signe,
le commit du depot packages et le SHA-256 epingle de l'index.

Le depot officiel de paquets est:

```text
https://github.com/Orizon-cmd/Orizon-Packages
```

Details: [docs/orizon/PACKAGES.md](docs/orizon/PACKAGES.md).

## Update Dans Orizon OS

La commande `update` est volontairement reservee a un Orizon OS installe sur
disque. En live-boot, elle n'apparait pas dans `help` et refuse de demarrer si
quelqu'un la tape quand meme, parce qu'un ISO demarre en lecture seule ne peut
pas se modifier lui-meme proprement.

Dans la console Orizon OS:

```text
update
```

La commande lance une transaction interne, facon `apt full-upgrade`, sans
programme externe: preparation de la base packages, probe Ethernet Intel
`e1000/e1000e`, `RTL8139` ou `VirtIO-net`, configuration IPv4 DHCP avec fallback
IP statique, DNS, TCP, TLS vers GitHub avec verification SAN, chaine RSA et
racine ISRG Root X1 embarquee, telechargement du manifeste public,
verification de `updates/x86_64/manifest.sig` avec la cle update embarquee,
telechargement des artefacts par requetes HTTP `Range`, verification SHA-256,
reprise des artefacts partiels caches dans `/workspace/.orizon`, puis
reecriture de l'ESP installee avec le nouveau `kernel.elf`, `BOOTX64.EFI` et
`limine.conf`, puis lecture de l'index public `Orizon-Packages` epingle par
commit et SHA-256 dans le manifeste signe pour installer ou mettre a jour les
paquets `.opkg`. La
partition data Orizon et `/workspace` sont preserves.

Pendant l'operation, la console affiche les etapes en continu: etat courant,
manifest recu, progression par pourcentage sur chaque artefact, verification
SHA-256 et ecriture de l'ESP. L'ecran ne reste donc plus silencieux jusqu'a la
fin de la transaction. Les timings par etape sont aussi sauvegardes dans
`/workspace/.orizon/update.log`. Les telechargements du manifeste et de l'index
paquets sont retentes; les artefacts de boot reprennent depuis leur cache
partiel si une tentative precedente a echoue.

Avant de remplacer le payload principal, Orizon garde le kernel et le loader
actuellement demarres dans un slot rollback sur l'ESP:

```text
/boot/KROLLBK.ELF
/EFI/BOOT/BOOTX64.ROL
```

Le menu Limine contient ensuite une entree `Orizon OS Rollback`. Au premier
boot du kernel mis a jour, Orizon arme automatiquement cette entree comme
default de secours jusqu'a ce que le shell soit atteint; si le shell est pret,
le default normal est restaure et le boot est valide. Si une mise a jour boote
mal apres l'entree dans Orizon mais avant le shell, le boot suivant choisit le
rollback par defaut. Le bootguard journalise aussi le compteur de tentatives,
la strategie `limine-boot-count-shell-validation`, les configs Limine
normal/fallback, le type de firmware et, en UEFI, l'adresse EFI system table
exposee par Limine. `update status` affiche aussi `nvram-bootnext:
prepared=no` et `ab-slots: prepared=no`: la vraie ecriture NVRAM `BootNext`
reste volontairement non active tant que les Runtime Services ne sont pas
cables. `update status` expose aussi `bootguard-recover` et
`pseudo-ab-slots: prepared=yes scope=single-esp-main-plus-rollback-entry`: le
rollback actuel est un fallback Limine/pseudo-A-B, pas un vrai A/B firmware.
Pour forcer le prochain boot vers l'entree rollback, utilise:

```text
bootguard recover
```

Une fois dans ce slot, la commande suivante restaure le payload demarre comme
slot principal:

```text
rollback
```

Pour consulter les metadonnees du rollback:

```text
rollback-status
```

Le depot public GitHub est la source officielle:

```text
https://github.com/Orizon-cmd/Orizon-OS
```

Le manifeste lu par le noyau se trouve ici:

```text
updates/x86_64/manifest.txt
updates/x86_64/manifest.sig
```

`manifest.sig` est une signature RSA PKCS#1/SHA-256 detachee. L'outil de
publication la regenere avec la cle locale ignoree
`config/keys/update-signing.private.pem`; le noyau ne contient que la cle
publique update `orizon-update-root-2026-05`. Une branche publique sans
`manifest.sig` valide est refusee avant toute installation de payload. Le
manifeste signe epingle aussi le commit du depot `Orizon-Packages` et le
SHA-256 de `packages/x86_64/index.txt`, puis chaque entree de l'index epingle
le SHA-256 du `.opkg`.

La transaction ecrit ses etats et journaux ici:

```text
/workspace/.orizon/update.log
/workspace/.orizon/update-state
/workspace/.orizon/update-manifest
/workspace/.orizon/package-index
/workspace/.orizon/last-update
/workspace/.orizon/pkgdb
/system/installed
```

Apres une mise a jour reussie, il suffit de redemarrer pour booter sur le
payload installe rafraichi.

## Noyau Et Performance

La VM ne doit plus tourner en boucle active permanente. Le noyau utilise un
timer PIT a 100 Hz pour l'uptime et rentre en idle avec `hlt` entre les ticks
et les evenements clavier/souris. Les commandes utiles pour verifier:

```text
uptime
free
ps
neofetch
```

## Mise A Jour Par Internet

ZimaOS est seulement le labo VM actuel. Orizon OS doit rester portable vers
d'autres machines `x86_64` UEFI.

Le depot public GitHub est la source officielle:

```text
https://github.com/Orizon-cmd/Orizon-OS
```

Pour recuperer la derniere ISO publiee depuis Internet, sans compiler:

```powershell
python scripts/orizon/orizon_update.py --mode github-iso
```

Sur une nouvelle machine, le demarrage le plus simple est:

```powershell
git clone https://github.com/Orizon-cmd/Orizon-OS.git
cd Orizon-OS
python scripts/orizon/orizon_update.py --mode github-iso
```

Pour reconstruire depuis le dernier code GitHub sur la machine courante:

```powershell
python scripts/orizon/orizon_update.py --from-github --mode local-iso
```

Ces commandes rafraichissent `Orizon-OS.iso` a la racine. Le mode `github-iso`
est le plus simple pour une machine qui veut juste recevoir une mise a jour; le
mode `--from-github --mode local-iso` sert aux machines qui ont la toolchain et
  doivent reconstruire.
Les modes de build rafraichissent aussi `updates/x86_64/` pour que la commande
`update` dans Orizon recoive le meme kernel que l'ISO publiee. Le manifeste
signe contient maintenant la taille et le SHA-256 de `Orizon-OS.iso`, et
`updates/x86_64/release.txt` resume les hashes ISO/payload/manifest/signature
pour eviter d'oublier un artefact release dans le commit. La validation release
refuse aussi un manifeste ou un `release.txt` dont les tailles/SHA-256 de
`kernel.elf`, `BOOTX64.EFI`, `limine.conf`, `manifest.txt`, `manifest.sig` ou
`Orizon-OS.iso` ne correspondent pas aux artefacts courants.
Le guide complet de publication est dans
[docs/orizon/RELEASE.md](docs/orizon/RELEASE.md); le guide de diagnostic
VM/ZimaOS est dans
[docs/orizon/TROUBLESHOOTING.md](docs/orizon/TROUBLESHOOTING.md).
Pour verifier les artefacts sans rebuild, utilisez:

```powershell
python scripts/orizon/orizon_update.py --mode validate-release
```

Le garde-fou rapide local/CI regroupe `git diff --check`, syntaxe Python,
syntaxe PowerShell quand disponible et validation release:

```powershell
python scripts/orizon/quick_check.py
```

Le point d'entree utilise par GitHub Actions ajoute les release notes et un
resume d'artefacts lisible. Il verifie aussi qu'un changement de source runtime
est accompagne des artefacts release attendus:

```powershell
python scripts/orizon/ci_release_guard.py --output-dir artifacts
```

Les rapports `artifacts/source-artifact-sync.md` et
`artifacts/source-artifact-sync.json` expliquent les changements source et les
artefacts vus par la CI.

Backends disponibles:

- `github-iso`: telecharge l'ISO publique depuis GitHub
- `local-iso`: build local portable, pour toute machine avec clang/lld/xorriso
- `zimaos-iso`: build via Docker sur le serveur ZimaOS, puis recupere l'ISO
- `zimaos-vm`: build via ZimaOS, deploie sur la VM `orizon-dev`, puis recupere l'ISO
- `validate-release`: verifie `Orizon-OS.iso`, le manifeste signe et
  `updates/x86_64/release.txt` sans compiler

## Boucle De Travail VM

Le cycle le plus rapide aujourd'hui passe encore par le serveur ZimaOS:

```powershell
python scripts/orizon/orizon_update.py --from-github --mode zimaos-vm
powershell -File scripts/orizon/open_orizon_vnc.ps1
```

La premiere commande reconstruit `orizon-os-x86_64`, deploie le resultat sur
`orizon-dev`, preserve la partition donnees Orizon, puis met a jour
`Orizon-OS.iso`. La seconde ouvre la console VNC avec TigerVNC.

## Arborescence Utile

- `orizon-os-x86_64/` : noyau et image de demarrage `x86_64`
- `CHANGELOG.md` : historique court des blocs VM/ZimaOS termines et limites
  importantes
- `docs/orizon/START_HERE.md` : page de reprise avec etat courant, limites et
  prochaine validation
- `docs/orizon/STATUS.md` : tableau court des fonctions implementees, preparees
  et non validees materiellement
- `docs/orizon/COMMANDS.md` : aide-memoire des commandes terminal utiles
- `docs/orizon/RELEASE.md` : checklist release, artefacts, CI et erreurs
- `docs/orizon/SECURITY.md` : posture SSH/VFS/update/package et limites de
  securite
- `docs/orizon/DESKTOP.md` : profil bureau optionnel inspire de Hyprland,
  commandes `desktop`, paquet et limites actuelles
- `docs/orizon/PACKAGES.md` : format `.opkg`, commandes `pkg`, signatures et
  rollback paquet
- `docs/orizon/INSTALL.md` : installateur, preflight VM et limites dual boot
- `docs/orizon/TROUBLESHOOTING.md` : diagnostic VM/ZimaOS par symptome
- `docs/orizon/` : notes de projet et labo ZimaOS
- `scripts/orizon/` : update portable, build, deploiement VM, acces VNC et SSH
- `config/hosts/*.local.*` : secrets locaux ignores par Git

## Base Active `x86_64`

Le socle actif vise un noyau propre, stable et facile a faire evoluer. Il
privilegie la lisibilite, le controle du boot, et une surface reduite pour
reintroduire les fonctionnalites plus tard, uniquement si elles servent la
vision d'Orizon OS.

Pour les details de build locaux, voir
[orizon-os-x86_64/README.md](orizon-os-x86_64/README.md).
