# Colin McRae Rally 2.0 – mod split screen a 3 e 4 giocatori (OpenCMR2)

Questa mod porta il gioco da 2 a 4 giocatori contemporanei sullo stesso
schermo, in tutte le modalità, ciascuno con il proprio profilo, la propria
auto e il proprio controller. È costruita sopra OpenCMR2, il port moderno
del gioco (github.com/pablocpas/OpenCMR2), che già offre risoluzioni
widescreen, frame rate alto e supporto ai pad moderni.

## Contenuto dello zip

| File | Cosa è |
| --- | --- |
| `opencmr2.exe` | il gioco con la mod |
| `SDL3.dll`, `msvcp140.dll`, `vcruntime140*.dll` | librerie necessarie all'exe |
| `LEGGIMI-mod-4-giocatori.md` | questo documento |
| `README-mod-4-players.md` | lo stesso in inglese |
| `OpenCMR2-4giocatori-NOTE.md` | note tecniche brevi |
| `OpenCMR2-4giocatori-src/split-screen-4-giocatori.patch` | tutte le modifiche al sorgente di OpenCMR2 |

## Installazione

1. Serve un'installazione di Colin McRae Rally 2.0 per PC (le cartelle
   `Game`, `FrontEnd`, `Sounds`, `CountrySpecific`, `SetupRep`).
2. Scompattare lo zip nella cartella del gioco, sovrascrivendo `opencmr2.exe`
   e `SDL3.dll` se c'era già OpenCMR2.
3. Avviare `opencmr2.exe`. Al primo avvio chiede la cartella del gioco se
   non la trova da solo. Impostazioni, salvataggi e log stanno in
   `%APPDATA%\OpenCMR2\` (`opencmr2.ini`, `opencmr2.log`).

Per tornare all'originale basta rimettere l'`opencmr2.exe` e la `SDL3.dll`
della release ufficiale di OpenCMR2.

## Prima di giocare: assegnare i controller

**Options → Controllers** elenca "Player 1 Controller" ... "Player 4
Controller". Assegnate a ogni giocatore il suo pad: con l'assegnazione
predefinita il primo pad risulta del giocatore 3, quindi fatelo prima di
iniziare. L'assegnazione viene salvata in `Configuration\Controller.rcf`.

- Giocatore 1: tastiera (frecce) oppure il suo pad.
- Giocatore 2: tastierino numerico oppure il suo pad.
- Giocatori 3 e 4: solo pad (la scelta scorre fra i joystick disponibili).

Ridefinizione dei comandi: nelle pagine dei comandi (lista, dispositivo,
ridefinizione, calibrazione) si naviga anche con il pad del giocatore 1 e
con il pad che si sta configurando. Per cambiare un comando si seleziona la
voce con il pad e si preme il tasto o si muove l'asse nuovo. Durante la
calibrazione di un asse i pulsanti del pad confermano o annullano e le
direzioni spostano i marcatori, salvo quando l'asse in calibrazione è lo
stick stesso: lì si usa la tastiera.

## Le modalità e come funzionano

### Arcade → Single Race (gara singola)

La pagina **Number of Players** offre 1, 2, 3 o 4 giocatori. Con 2 o più
giocatori la pagina delle impostazioni avanzate della gara (tipo di gara,
giri, ...) ha la voce **Number Of CPU Drivers**: da 0 fino a 6 − giocatori
(le tabelle dell'IA tengono sei auto in tutto), e sotto **Catch Up** come
nell'originale a 2 giocatori. Tutti partono insieme dalla griglia, si vedono
e si urtano.

### Arcade → Championship (campionato arcade)

Dopo la difficoltà compare la pagina **Number of Players** (1-4). Nel gioco
originale questa pagina esisteva ma non era collegata e si andava dritti al
giocatore singolo. Con 2-4 giocatori corrono anche 6 − giocatori piloti CPU,
che contano per i punti del campionato. Le classifiche e le schermate dei
punteggi tengono conto di tutti i giocatori umani.

### Rally → Single Rally, Championship, Time Trial

Scegliendo 3 o 4 giocatori compare la pagina **Split Screen Mode /
Alternate Mode**, che prima compariva solo con 2. Ha tre voci:

- **Split Screen Mode** ("fantasma", come nell'originale a 2): tutti sullo
  schermo diviso, via contemporaneo, partenza dallo stesso punto; i
  giocatori non si vedono e non si toccano.
- **Alternate Mode**: a turni, un giocatore alla volta, come sempre.
- **Split Screen - Contact** (nuova): i giocatori si vedono e si urtano, e
  partono su una griglia 2+2: giocatori 1 e 2 affiancati sulla linea, 3 e 4
  nove metri dietro. Via contemporaneo.

Ogni giocatore sceglie profilo (o ospite), auto e cambio come sempre; i tempi
di ciascuno finiscono nelle classifiche del rally o del campionato come
nella modalità a 2 giocatori. La scelta Contatto/fantasma non viene salvata
nel campionato: ricaricandolo torna la modalità fantasma. Un campionato
salvato a 3-4 giocatori si ricarica con lo stesso numero di giocatori.

### Schermo diviso in quattro

Con 3 o 4 giocatori lo schermo è diviso in quattro quadranti (con 3 giocatori
il quarto resta vuoto): 1 in alto a sinistra, 2 in alto a destra, 3 in basso a
sinistra, 4 in basso a destra. In ogni quadrante l'HUD è disposto come su uno
schermo intero (tempi e posizioni a sinistra e a destra in alto, contamarce,
tachimetro, note del copilota) a metà grandezza; si regola in percentuale
con `hud_scale_quad` nella sezione `[video]` di `opencmr2.ini` (100 = metà
grandezza, 120 = un po' più grande).

### Menu per giocatore

Le pagine di menu di ogni giocatore rispondono solo al suo dispositivo:
scelta del profilo, dell'auto, del cambio, setup dell'auto prima della prova
e menu di pausa in gara. Il giocatore 1 non può più comandare le scelte
degli altri. Se il pad di un giocatore non è collegato, la sua pagina
risponde alla tastiera. Per far comandare anche dalla tastiera le pagine dei
giocatori 2-4: `menu_keyboard_shared = 1` nella sezione `[input]` di
`opencmr2.ini`.

### Tutto sbloccato in multigiocatore

Con 2 o più giocatori ogni auto (anche le bonus), ogni paese e percorso, la
classe Expert e tutte le difficoltà sono disponibili in tutte le modalità,
come con i trucchi "all cars" e "all tracks", ma senza i loro effetti sui
record. A giocatore singolo nulla cambia. Si disattiva con
`multiplayer_unlock_all = 0` nella sezione `[game]` di `opencmr2.ini`.

## Opzioni aggiunte in `opencmr2.ini`

| Sezione | Opzione | Predefinito | Effetto |
| --- | --- | --- | --- |
| `[video]` | `hud_scale_quad` | 100 | grandezza dell'HUD nei quadranti, in percento della metà |
| `[input]` | `menu_keyboard_shared` | 0 | 1: la tastiera comanda anche i menu dei giocatori 2-4 |
| `[game]` | `multiplayer_unlock_all` | 1 | 0: in multigiocatore valgono gli sblocchi normali |

## Il lavoro fatto (per chi vuole guardare la patch)

Il gioco originale era scritto per al massimo 2 giocatori a schermo: molte
tabelle avevano due posti e molti controlli dicevano "== 2". La patch
(`split-screen-4-giocatori.patch`, ogni modifica marcata `// PORT:`) ha:

- **Menu**: pagina Number of Players a 1-4 in arcade; pagina Number of
  Players collegata nel campionato arcade; pagina Split/Alternate anche con
  3-4 giocatori e terza voce "Split Screen - Contact"; voce "Number Of CPU
  Drivers" nelle impostazioni avanzate della gara arcade; quattro voci in
  Options → Controllers.
- **Viste**: quattro viste di gioco invece di due (tabelle dei livelli di
  dettaglio, maschere di visibilità dei nodi, telecamere a bordo pista,
  stato delle telecamere per auto). Prima i giocatori 3 e 4 non vedevano le
  auto e il cambio inquadratura o il traguardo facevano crashare il gioco.
- **Partenze**: griglia 2+2 lungo la direzione del percorso nella modalità
  Contatto; stesso punto nella modalità fantasma.
- **HUD**: in quattro quadranti ogni vista è impaginata come uno schermo
  intero scalato a un quarto, con i caratteri rimpiccioliti; contamarce e
  tachimetro funzionano anche per i giocatori 3 e 4; tabelle degli
  intertempi e dei record portate a 4 auto.
- **Input**: slot controller 3 e 4 salvati in `Controller.rcf` (file più
  lungo di 4 byte, ancora leggibile dall'originale); ogni giocatore comanda i
  propri menu dal proprio dispositivo; ridefinizione dei comandi dal pad;
  force feedback e marcatori del cambio per 4 slot.
- **Logica di gara**: contatori di fine prova, messaggi di record, reset
  delle chiamate del copilota e del "senso contrario" per 4 auto; fade di
  fine gara per tutte le viste; piloti CPU scelti in arcade.
- **Sblocchi**: con 2+ giocatori i flag "all cars" e "all tracks" risultano
  attivi senza toccare i record.
- **Prestazioni**: lo sgravio che l'originale applica allo schermo diviso in
  2 (nasconde cespugli, pali e dettagli secondari del percorso) vale anche
  con 3 e 4 viste; senza, a inizio gara c'erano cali di fotogrammi.

Tutte le modifiche sono state provate con la sonda headless del progetto
(15 gare da 1 a 4 giocatori in tutte le modalità) e a schermo con quattro
pad.

## Limiti noti

- La visuale interna (abitacolo) è disponibile solo per i giocatori 1 e 2.
- Tasti di tastiera assegnati nella configurazione del pad dei giocatori 3-4
  non vengono letti in gara: per loro usare pulsanti e assi del pad.
- Con 3 giocatori il quarto quadrante resta nero.
- Se i fotogrammi calano comunque, abbassare `car_detail` nella sezione
  `[video]` di `opencmr2.ini`.
