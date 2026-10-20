// SPDX-License-Identifier: GPL-2.0-or-later
// bbport: the in-game menu (bbport_overlay.cpp) in the languages other than English and
// Russian, which MenuText takes at its call sites. Each table maps the English text, exactly as
// passed to MenuText (the effect labels: bbport_settings.h Effects), to its translation; text
// missing here is shown in English. Translations keep the printf specifiers of the English text in
// the same order and the ImGui ID after "###"; FSR, DLSS, TAA, RCAS, SSAO, key and button names,
// preset names and paths stay as they are. The menu font (DejaVu Sans) has every character used.
#include "bbport_menu_i18n.h"

#include <array>
#include <cstddef>
#include <string_view>
#include <unordered_map>

namespace BbSettings {

namespace {

struct Entry {
    const char* english;
    const char* text;
};

// Spanish (es)
constexpr Entry kSpanish[] = {
    {"Hide mouse cursor",
     "Ocultar el cursor del ratón"},
    {"Picture",
     "Imagen"},
    {"Picture tuning",
     "Ajuste de imagen"},
    {"UI brightness",
     "Brillo de la interfaz"},
    {"UI colour",
     "Color de la interfaz"},
    {"Brightness",
     "Brillo"},
    {"Contrast",
     "Contraste"},
    {"Saturation",
     "Saturación"},
    {"Hue",
     "Tono"},
    {"Fog",
     "Niebla"},
    {"Vignette",
     "Viñeta"},
    {"Ambient occlusion strength",
     "Intensidad de la oclusión ambiental"},
    {"Shadow distance",
     "Distancia de sombras"},
    {"Game's own picture",
     "Imagen original del juego"},
    {"Bloodborne - Graphics  (Insert / L3+R3)###bbport_settings",
     "Bloodborne - Gráficos  (Insert / L3+R3)###bbport_settings"},
    {"%.0f FPS  (%.1f ms)",
     "%.0f FPS  (%.1f ms)"},
    {"Temporal upscaler",
     "Escalado temporal"},
    {"Off",
     "Desactivado"},
    {"TAA (native anti-aliasing)",
     "TAA (antialiasing nativo)"},
    {"Upscaler",
     "Escalador"},
    {"— not supported by this GPU",
     "— no compatible con esta GPU"},
    {"— in development",
     "— en desarrollo"},
    {"FSR 4.1.1 in INT8 mode: the model from AMD's 4.1.1 DLL, reproduced in Vulkan (output "
     "matches the DLL). One model for Native through Performance and another for Ultra "
     "Performance. Assets: tools/fsr4cap/build_assets.sh (requires the DLL and Proton).",
     "FSR 4.1.1 en modo INT8: el modelo de la DLL 4.1.1 de AMD, reproducido en Vulkan (el "
     "resultado coincide con la DLL). Un modelo de Native a Performance y otro para Ultra "
     "Performance. Recursos: tools/fsr4cap/build_assets.sh (requiere la DLL y Proton)."},
    {"FSR 4 in INT8 mode (v07 model from AMD FidelityFX SDK sources). Higher quality than "
     "FSR 3.1, but the pass is more demanding. Changing the preset rebuilds the model (a "
     "brief pause). Assets: tools/fetch_fsr4_assets.sh.",
     "FSR 4 en modo INT8 (modelo v07 del código fuente de AMD FidelityFX SDK). Mayor calidad "
     "que FSR 3.1, pero el pase es más exigente. Cambiar el preajuste reconstruye el modelo "
     "(una breve pausa). Recursos: tools/fetch_fsr4_assets.sh."},
    {"FSR 4: auto exposure",
     "FSR 4: exposición automática"},
    {"FSR 4: invert jitter sign",
     "FSR 4: invertir signo del jitter"},
    {"For diagnosing ghosting: the FSR 4 network normalizes color by exposure and uses it to "
     "decide when to discard previous frames. Changes apply immediately, without a restart.",
     "Para diagnosticar ghosting: la red de FSR 4 normaliza el color según la exposición y "
     "la usa para decidir cuándo descartar fotogramas anteriores. Los cambios se aplican al "
     "instante, sin reiniciar."},
    {"Preset",
     "Preajuste"},
    {"%s (x%.1f, render %dx%d)",
     "%s (x%.1f, renderizado %dx%d)"},
    {"Character motion vectors",
     "Vectores de movimiento de personajes"},
    {"Accurate vectors for animated objects: clothing and weapons break up less during "
     "movement. The static scene does not get an additional pass. Changes apply after "
     "restarting the game.",
     "Vectores precisos para objetos animados: la ropa y las armas se deshacen menos en "
     "movimiento. La escena estática no recibe un pase adicional. Se aplica tras reiniciar "
     "el juego."},
    {"Show motion vectors (debug)",
     "Mostrar vectores de movimiento (depuración)"},
    {"Red/green: horizontal/vertical motion (8 pixels = full brightness). Blue: the pixel "
     "received an accurate object vector, not just camera motion. The upscaler treats a "
     "moving object with no blue or red/green as stationary, causing trails.",
     "Rojo/verde: movimiento horizontal/vertical (8 píxeles = brillo máximo). Azul: el píxel "
     "recibió un vector de objeto preciso, no solo el movimiento de la cámara. El escalador "
     "trata un objeto en movimiento sin azul ni rojo/verde como inmóvil, lo que provoca "
     "estelas."},
    {"Output resolution",
     "Resolución de salida"},
    {"Size of the final frame and UI. The preset sets the scene size relative to the output: "
     "4K Performance = 1920x1080. Applies after restarting the game.",
     "Tamaño del fotograma final y de la interfaz. El preajuste fija el tamaño de la escena "
     "respecto a la salida: 4K Performance = 1920x1080. Se aplica tras reiniciar el juego."},
    {"The final frame and UI size changes at the next frame boundary. The preset sets the "
     "scene size relative to the output: 4K Performance = 1920x1080. Changing the size "
     "resets FSR history and may cause a brief pause.",
     "El tamaño del fotograma final y de la interfaz cambia en el siguiente fotograma. El "
     "preajuste fija el tamaño de la escena respecto a la salida: 4K Performance = "
     "1920x1080. Cambiar el tamaño reinicia el historial de FSR y puede causar una breve "
     "pausa."},
    {"Auto (based on GPU)",
     "Automático (según la GPU)"},
    {"Off (faster)",
     "Desactivado (más rápido)"},
    {"On",
     "Activado"},
    {"Live resolution changes",
     "Cambio de resolución en vivo"},
    {"On: output resolution and preset change without restarting, but game post-processing "
     "stays at 1080p, which is noticeably slower on Steam Deck and older GPUs. Off: "
     "everything renders at the preset resolution, and changes require a restart. Auto "
     "enables this on powerful discrete GPUs. Applies after restarting the game.",
     "Activado: la resolución de salida y el preajuste cambian sin reiniciar, pero el "
     "posprocesado del juego se queda en 1080p, lo que es notablemente más lento en Steam "
     "Deck y GPU antiguas. Desactivado: todo se renderiza a la resolución del preajuste y "
     "los cambios requieren reiniciar. Automático lo activa en GPU dedicadas potentes. Se "
     "aplica tras reiniciar el juego."},
    {"Game effects (restart required)",
     "Efectos del juego (requiere reinicio)"},
    {"Highest (-2)",
     "Máximo (-2)"},
    {"Game default",
     "Predeterminado del juego"},
    {"Lower (1)",
     "Bajo (1)"},
    {"Lowest (2)",
     "Mínimo (2)"},
    {"Model detail",
     "Detalle de modelos"},
    {"Effects are enabled and disabled by game patches at startup (patches/Bloodborne.xml). "
     "Motion blur and shadows from dynamic lights place a significant load on the GPU.",
     "Los efectos se activan y desactivan mediante parches del juego al iniciar "
     "(patches/Bloodborne.xml). El desenfoque de movimiento y las sombras de luces dinámicas "
     "cargan notablemente la GPU."},
    {"Free camera: hold Cross and press L3 (keyboard: Space + Z). Debug menu: left touchpad "
     "/ Tab. Requires DbgFont14h.ccm and DbgFont14h.tpf in dvdroot_ps4/font from Nexus mod "
     "#253. Right touchpad: Backspace.",
     "Cámara libre: mantén Cross y pulsa L3 (teclado: Space + Z). Menú de depuración: "
     "touchpad izquierdo / Tab. Requiere DbgFont14h.ccm y DbgFont14h.tpf en dvdroot_ps4/font "
     "del mod #253 de Nexus. Touchpad derecho: Backspace."},
    {"Changes apply after restarting the game",
     "Los cambios se aplican tras reiniciar el juego"},
    {"Apply and restart game",
     "Aplicar y reiniciar el juego"},
    {"Other",
     "Otros"},
    {"FPS counter in corner",
     "Contador de FPS en la esquina"},
    {"Frame rate",
     "Tasa de fotogramas"},
    {"Frame time",
     "Tiempo de fotograma"},
    {"Close",
     "Cerrar"},
    {"Settings are saved to bbport.ini",
     "Los ajustes se guardan en bbport.ini"},
    {"Chromatic aberration",
     "Aberración cromática"},
    {"Depth of field (DoF)",
     "Profundidad de campo (DoF)"},
    {"Motion blur",
     "Desenfoque de movimiento"},
    {"Ambient occlusion (SSAO)",
     "Oclusión ambiental (SSAO)"},
    {"Game's own anti-aliasing",
     "Antialiasing propio del juego"},
    {"Shadows from dynamic lights",
     "Sombras de luces dinámicas"},
    {"Screen-space reflections (not in original game)",
     "Reflejos en espacio de pantalla (no estaban en el juego)"},
    {"Skip startup intros",
     "Omitir intros al iniciar"},
    {"Free camera (Cross + L3)",
     "Cámara libre (Cross + L3)"},
    {"Debug menu (requires font files)",
     "Menú de depuración (requiere archivos de fuente)"},
};

// Portuguese, Brazil (pt_BR)
constexpr Entry kPortugueseBrazil[] = {
    {"Hide mouse cursor",
     "Ocultar o cursor do mouse"},
    {"Picture",
     "Imagem"},
    {"Picture tuning",
     "Ajuste de imagem"},
    {"UI brightness",
     "Brilho da interface"},
    {"UI colour",
     "Cor da interface"},
    {"Brightness",
     "Brilho"},
    {"Contrast",
     "Contraste"},
    {"Saturation",
     "Saturação"},
    {"Hue",
     "Matiz"},
    {"Fog",
     "Névoa"},
    {"Vignette",
     "Vinheta"},
    {"Ambient occlusion strength",
     "Intensidade da oclusão de ambiente"},
    {"Shadow distance",
     "Distância das sombras"},
    {"Game's own picture",
     "Imagem original do jogo"},
    {"Bloodborne - Graphics  (Insert / L3+R3)###bbport_settings",
     "Bloodborne - Gráficos  (Insert / L3+R3)###bbport_settings"},
    {"%.0f FPS  (%.1f ms)",
     "%.0f FPS  (%.1f ms)"},
    {"Temporal upscaler",
     "Upscaler temporal"},
    {"Off",
     "Desligado"},
    {"TAA (native anti-aliasing)",
     "TAA (anti-aliasing nativo)"},
    {"Upscaler",
     "Upscaler"},
    {"— not supported by this GPU",
     "— não suportado por esta GPU"},
    {"— in development",
     "— em desenvolvimento"},
    {"FSR 4.1.1 in INT8 mode: the model from AMD's 4.1.1 DLL, reproduced in Vulkan (output "
     "matches the DLL). One model for Native through Performance and another for Ultra "
     "Performance. Assets: tools/fsr4cap/build_assets.sh (requires the DLL and Proton).",
     "FSR 4.1.1 no modo INT8: o modelo da DLL 4.1.1 da AMD, reproduzido em Vulkan (o "
     "resultado é igual ao da DLL). Um modelo de Native a Performance e outro para Ultra "
     "Performance. Assets: tools/fsr4cap/build_assets.sh (requer a DLL e o Proton)."},
    {"FSR 4 in INT8 mode (v07 model from AMD FidelityFX SDK sources). Higher quality than "
     "FSR 3.1, but the pass is more demanding. Changing the preset rebuilds the model (a "
     "brief pause). Assets: tools/fetch_fsr4_assets.sh.",
     "FSR 4 no modo INT8 (modelo v07 do código-fonte do AMD FidelityFX SDK). Qualidade maior "
     "que a do FSR 3.1, mas a passada é mais pesada. Trocar a predefinição reconstrói o "
     "modelo (uma breve pausa). Assets: tools/fetch_fsr4_assets.sh."},
    {"FSR 4: auto exposure",
     "FSR 4: exposição automática"},
    {"FSR 4: invert jitter sign",
     "FSR 4: inverter sinal do jitter"},
    {"For diagnosing ghosting: the FSR 4 network normalizes color by exposure and uses it to "
     "decide when to discard previous frames. Changes apply immediately, without a restart.",
     "Para diagnosticar ghosting: a rede do FSR 4 normaliza a cor pela exposição e a usa "
     "para decidir quando descartar quadros anteriores. As alterações valem na hora, sem "
     "reiniciar."},
    {"Preset",
     "Predefinição"},
    {"%s (x%.1f, render %dx%d)",
     "%s (x%.1f, renderização %dx%d)"},
    {"Character motion vectors",
     "Vetores de movimento dos personagens"},
    {"Accurate vectors for animated objects: clothing and weapons break up less during "
     "movement. The static scene does not get an additional pass. Changes apply after "
     "restarting the game.",
     "Vetores precisos para objetos animados: roupas e armas se desfazem menos durante o "
     "movimento. A cena estática não recebe uma passada extra. Aplica-se após reiniciar o "
     "jogo."},
    {"Show motion vectors (debug)",
     "Mostrar vetores de movimento (depuração)"},
    {"Red/green: horizontal/vertical motion (8 pixels = full brightness). Blue: the pixel "
     "received an accurate object vector, not just camera motion. The upscaler treats a "
     "moving object with no blue or red/green as stationary, causing trails.",
     "Vermelho/verde: movimento horizontal/vertical (8 pixels = brilho máximo). Azul: o "
     "pixel recebeu um vetor de objeto preciso, não apenas o movimento da câmera. O upscaler "
     "trata um objeto em movimento sem azul nem vermelho/verde como parado, o que causa "
     "rastros."},
    {"Output resolution",
     "Resolução de saída"},
    {"Size of the final frame and UI. The preset sets the scene size relative to the output: "
     "4K Performance = 1920x1080. Applies after restarting the game.",
     "Tamanho do quadro final e da interface. A predefinição define o tamanho da cena em "
     "relação à saída: 4K Performance = 1920x1080. Aplica-se após reiniciar o jogo."},
    {"The final frame and UI size changes at the next frame boundary. The preset sets the "
     "scene size relative to the output: 4K Performance = 1920x1080. Changing the size "
     "resets FSR history and may cause a brief pause.",
     "O tamanho do quadro final e da interface muda no próximo quadro. A predefinição define "
     "o tamanho da cena em relação à saída: 4K Performance = 1920x1080. Mudar o tamanho "
     "redefine o histórico do FSR e pode causar uma breve pausa."},
    {"Auto (based on GPU)",
     "Automático (conforme a GPU)"},
    {"Off (faster)",
     "Desligada (mais rápido)"},
    {"On",
     "Ligada"},
    {"Live resolution changes",
     "Mudança de resolução em tempo real"},
    {"On: output resolution and preset change without restarting, but game post-processing "
     "stays at 1080p, which is noticeably slower on Steam Deck and older GPUs. Off: "
     "everything renders at the preset resolution, and changes require a restart. Auto "
     "enables this on powerful discrete GPUs. Applies after restarting the game.",
     "Ligada: a resolução de saída e a predefinição mudam sem reiniciar, mas o "
     "pós-processamento do jogo fica em 1080p, o que é bem mais lento no Steam Deck e em "
     "GPUs antigas. Desligada: tudo é renderizado na resolução da predefinição, e as "
     "mudanças exigem reinício. Automático a liga em GPUs dedicadas potentes. Aplica-se após "
     "reiniciar o jogo."},
    {"Game effects (restart required)",
     "Efeitos do jogo (requer reinício)"},
    {"Highest (-2)",
     "Máximo (-2)"},
    {"Game default",
     "Padrão do jogo"},
    {"Lower (1)",
     "Baixo (1)"},
    {"Lowest (2)",
     "Mínimo (2)"},
    {"Model detail",
     "Detalhe dos modelos"},
    {"Effects are enabled and disabled by game patches at startup (patches/Bloodborne.xml). "
     "Motion blur and shadows from dynamic lights place a significant load on the GPU.",
     "Os efeitos são ligados e desligados por patches do jogo na inicialização "
     "(patches/Bloodborne.xml). Desfoque de movimento e sombras de luzes dinâmicas pesam "
     "bastante na GPU."},
    {"Free camera: hold Cross and press L3 (keyboard: Space + Z). Debug menu: left touchpad "
     "/ Tab. Requires DbgFont14h.ccm and DbgFont14h.tpf in dvdroot_ps4/font from Nexus mod "
     "#253. Right touchpad: Backspace.",
     "Câmera livre: segure Cross e pressione L3 (teclado: Space + Z). Menu de depuração: "
     "touchpad esquerdo / Tab. Requer DbgFont14h.ccm e DbgFont14h.tpf em dvdroot_ps4/font, "
     "do mod #253 do Nexus. Touchpad direito: Backspace."},
    {"Changes apply after restarting the game",
     "As alterações valem após reiniciar o jogo"},
    {"Apply and restart game",
     "Aplicar e reiniciar o jogo"},
    {"Other",
     "Outros"},
    {"FPS counter in corner",
     "Contador de FPS no canto"},
    {"Frame rate",
     "Taxa de quadros"},
    {"Frame time",
     "Tempo de quadro"},
    {"Close",
     "Fechar"},
    {"Settings are saved to bbport.ini",
     "As configurações são salvas em bbport.ini"},
    {"Chromatic aberration",
     "Aberração cromática"},
    {"Depth of field (DoF)",
     "Profundidade de campo (DoF)"},
    {"Motion blur",
     "Desfoque de movimento"},
    {"Ambient occlusion (SSAO)",
     "Oclusão de ambiente (SSAO)"},
    {"Game's own anti-aliasing",
     "Anti-aliasing próprio do jogo"},
    {"Shadows from dynamic lights",
     "Sombras de luzes dinâmicas"},
    {"Screen-space reflections (not in original game)",
     "Reflexos em espaço de tela (não existiam no jogo)"},
    {"Skip startup intros",
     "Pular vídeos de abertura"},
    {"Free camera (Cross + L3)",
     "Câmera livre (Cross + L3)"},
    {"Debug menu (requires font files)",
     "Menu de depuração (requer arquivos de fonte)"},
};

// French (fr)
constexpr Entry kFrench[] = {
    {"Hide mouse cursor",
     "Masquer le curseur de la souris"},
    {"Picture",
     "Image"},
    {"Picture tuning",
     "Réglage de l'image"},
    {"UI brightness",
     "Luminosité de l'interface"},
    {"UI colour",
     "Couleurs de l'interface"},
    {"Brightness",
     "Luminosité"},
    {"Contrast",
     "Contraste"},
    {"Saturation",
     "Saturation"},
    {"Hue",
     "Teinte"},
    {"Fog",
     "Brouillard"},
    {"Vignette",
     "Vignettage"},
    {"Ambient occlusion strength",
     "Intensité de l'occlusion ambiante"},
    {"Shadow distance",
     "Distance des ombres"},
    {"Game's own picture",
     "Image d'origine du jeu"},
    {"Bloodborne - Graphics  (Insert / L3+R3)###bbport_settings",
     "Bloodborne - Graphismes  (Insert / L3+R3)###bbport_settings"},
    {"%.0f FPS  (%.1f ms)",
     "%.0f FPS  (%.1f ms)"},
    {"Temporal upscaler",
     "Mise à l'échelle temporelle"},
    {"Off",
     "Désactivé"},
    {"TAA (native anti-aliasing)",
     "TAA (anticrénelage natif)"},
    {"Upscaler",
     "Upscaler"},
    {"— not supported by this GPU",
     "— non pris en charge par ce GPU"},
    {"— in development",
     "— en développement"},
    {"FSR 4.1.1 in INT8 mode: the model from AMD's 4.1.1 DLL, reproduced in Vulkan (output "
     "matches the DLL). One model for Native through Performance and another for Ultra "
     "Performance. Assets: tools/fsr4cap/build_assets.sh (requires the DLL and Proton).",
     "FSR 4.1.1 en mode INT8 : le modèle de la DLL 4.1.1 d'AMD, reproduit en Vulkan "
     "(résultat identique à la DLL). Un modèle de Native à Performance et un autre pour "
     "Ultra Performance. Ressources : tools/fsr4cap/build_assets.sh (nécessite la DLL et "
     "Proton)."},
    {"FSR 4 in INT8 mode (v07 model from AMD FidelityFX SDK sources). Higher quality than "
     "FSR 3.1, but the pass is more demanding. Changing the preset rebuilds the model (a "
     "brief pause). Assets: tools/fetch_fsr4_assets.sh.",
     "FSR 4 en mode INT8 (modèle v07 issu des sources du AMD FidelityFX SDK). Meilleure "
     "qualité que FSR 3.1, mais la passe est plus exigeante. Changer de préréglage "
     "reconstruit le modèle (brève pause). Ressources : tools/fetch_fsr4_assets.sh."},
    {"FSR 4: auto exposure",
     "FSR 4 : exposition automatique"},
    {"FSR 4: invert jitter sign",
     "FSR 4 : inverser le signe du jitter"},
    {"For diagnosing ghosting: the FSR 4 network normalizes color by exposure and uses it to "
     "decide when to discard previous frames. Changes apply immediately, without a restart.",
     "Pour diagnostiquer le ghosting : le réseau FSR 4 normalise la couleur selon "
     "l'exposition et s'en sert pour décider quand rejeter les images précédentes. Les "
     "changements s'appliquent immédiatement, sans redémarrage."},
    {"Preset",
     "Préréglage"},
    {"%s (x%.1f, render %dx%d)",
     "%s (x%.1f, rendu %dx%d)"},
    {"Character motion vectors",
     "Vecteurs de mouvement des personnages"},
    {"Accurate vectors for animated objects: clothing and weapons break up less during "
     "movement. The static scene does not get an additional pass. Changes apply after "
     "restarting the game.",
     "Vecteurs précis pour les objets animés : les vêtements et les armes se désagrègent "
     "moins en mouvement. La scène statique ne reçoit pas de passe supplémentaire. "
     "S'applique après le redémarrage du jeu."},
    {"Show motion vectors (debug)",
     "Afficher les vecteurs de mouvement (débogage)"},
    {"Red/green: horizontal/vertical motion (8 pixels = full brightness). Blue: the pixel "
     "received an accurate object vector, not just camera motion. The upscaler treats a "
     "moving object with no blue or red/green as stationary, causing trails.",
     "Rouge/vert : mouvement horizontal/vertical (8 pixels = luminosité maximale). Bleu : le "
     "pixel a reçu un vecteur d'objet précis, pas seulement le mouvement de la caméra. "
     "L'upscaler considère un objet en mouvement sans bleu ni rouge/vert comme immobile, "
     "d'où des traînées."},
    {"Output resolution",
     "Résolution de sortie"},
    {"Size of the final frame and UI. The preset sets the scene size relative to the output: "
     "4K Performance = 1920x1080. Applies after restarting the game.",
     "Taille de l'image finale et de l'interface. Le préréglage fixe la taille de la scène "
     "par rapport à la sortie : 4K Performance = 1920x1080. S'applique après le redémarrage "
     "du jeu."},
    {"The final frame and UI size changes at the next frame boundary. The preset sets the "
     "scene size relative to the output: 4K Performance = 1920x1080. Changing the size "
     "resets FSR history and may cause a brief pause.",
     "La taille de l'image finale et de l'interface change à l'image suivante. Le préréglage "
     "fixe la taille de la scène par rapport à la sortie : 4K Performance = 1920x1080. "
     "Changer de taille réinitialise l'historique FSR et peut provoquer une brève pause."},
    {"Auto (based on GPU)",
     "Auto (selon le GPU)"},
    {"Off (faster)",
     "Désactivé (plus rapide)"},
    {"On",
     "Activé"},
    {"Live resolution changes",
     "Changement de résolution à la volée"},
    {"On: output resolution and preset change without restarting, but game post-processing "
     "stays at 1080p, which is noticeably slower on Steam Deck and older GPUs. Off: "
     "everything renders at the preset resolution, and changes require a restart. Auto "
     "enables this on powerful discrete GPUs. Applies after restarting the game.",
     "Activé : la résolution de sortie et le préréglage changent sans redémarrage, mais le "
     "post-traitement du jeu reste en 1080p, ce qui est nettement plus lent sur Steam Deck "
     "et les GPU anciens. Désactivé : tout est rendu à la résolution du préréglage, et les "
     "changements nécessitent un redémarrage. Auto l'active sur les GPU dédiés puissants. "
     "S'applique après le redémarrage du jeu."},
    {"Game effects (restart required)",
     "Effets du jeu (redémarrage requis)"},
    {"Highest (-2)",
     "Maximal (-2)"},
    {"Game default",
     "Valeur par défaut du jeu"},
    {"Lower (1)",
     "Réduit (1)"},
    {"Lowest (2)",
     "Minimal (2)"},
    {"Model detail",
     "Détail des modèles"},
    {"Effects are enabled and disabled by game patches at startup (patches/Bloodborne.xml). "
     "Motion blur and shadows from dynamic lights place a significant load on the GPU.",
     "Les effets sont activés et désactivés par des patchs du jeu au démarrage "
     "(patches/Bloodborne.xml). Le flou de mouvement et les ombres des lumières dynamiques "
     "sollicitent fortement le GPU."},
    {"Free camera: hold Cross and press L3 (keyboard: Space + Z). Debug menu: left touchpad "
     "/ Tab. Requires DbgFont14h.ccm and DbgFont14h.tpf in dvdroot_ps4/font from Nexus mod "
     "#253. Right touchpad: Backspace.",
     "Caméra libre : maintenez Cross et appuyez sur L3 (clavier : Space + Z). Menu de "
     "débogage : pavé tactile gauche / Tab. Nécessite DbgFont14h.ccm et DbgFont14h.tpf dans "
     "dvdroot_ps4/font, issus du mod Nexus #253. Pavé tactile droit : Backspace."},
    {"Changes apply after restarting the game",
     "Les changements s'appliquent après le redémarrage du jeu"},
    {"Apply and restart game",
     "Appliquer et redémarrer le jeu"},
    {"Other",
     "Divers"},
    {"FPS counter in corner",
     "Compteur FPS dans le coin"},
    {"Frame rate",
     "Fréquence d'images"},
    {"Frame time",
     "Temps par image"},
    {"Close",
     "Fermer"},
    {"Settings are saved to bbport.ini",
     "Les réglages sont enregistrés dans bbport.ini"},
    {"Chromatic aberration",
     "Aberration chromatique"},
    {"Depth of field (DoF)",
     "Profondeur de champ (DoF)"},
    {"Motion blur",
     "Flou de mouvement"},
    {"Ambient occlusion (SSAO)",
     "Occlusion ambiante (SSAO)"},
    {"Game's own anti-aliasing",
     "Anticrénelage propre au jeu"},
    {"Shadows from dynamic lights",
     "Ombres des lumières dynamiques"},
    {"Screen-space reflections (not in original game)",
     "Réflexions en espace écran (absentes du jeu d'origine)"},
    {"Skip startup intros",
     "Passer les intros au démarrage"},
    {"Free camera (Cross + L3)",
     "Caméra libre (Cross + L3)"},
    {"Debug menu (requires font files)",
     "Menu de débogage (fichiers de police requis)"},
};

// German (de)
constexpr Entry kGerman[] = {
    {"Hide mouse cursor",
     "Mauszeiger ausblenden"},
    {"Picture",
     "Bild"},
    {"Picture tuning",
     "Bildanpassung"},
    {"UI brightness",
     "Helligkeit der Oberfläche"},
    {"UI colour",
     "Farben der Oberfläche"},
    {"Brightness",
     "Helligkeit"},
    {"Contrast",
     "Kontrast"},
    {"Saturation",
     "Sättigung"},
    {"Hue",
     "Farbton"},
    {"Fog",
     "Nebel"},
    {"Vignette",
     "Vignette"},
    {"Ambient occlusion strength",
     "Stärke der Umgebungsverdeckung"},
    {"Shadow distance",
     "Schattenweite"},
    {"Game's own picture",
     "Originalbild des Spiels"},
    {"Bloodborne - Graphics  (Insert / L3+R3)###bbport_settings",
     "Bloodborne - Grafik  (Insert / L3+R3)###bbport_settings"},
    {"%.0f FPS  (%.1f ms)",
     "%.0f FPS  (%.1f ms)"},
    {"Temporal upscaler",
     "Temporales Upscaling"},
    {"Off",
     "Aus"},
    {"TAA (native anti-aliasing)",
     "TAA (native Kantenglättung)"},
    {"Upscaler",
     "Upscaler"},
    {"— not supported by this GPU",
     "— von dieser GPU nicht unterstützt"},
    {"— in development",
     "— in Entwicklung"},
    {"FSR 4.1.1 in INT8 mode: the model from AMD's 4.1.1 DLL, reproduced in Vulkan (output "
     "matches the DLL). One model for Native through Performance and another for Ultra "
     "Performance. Assets: tools/fsr4cap/build_assets.sh (requires the DLL and Proton).",
     "FSR 4.1.1 im INT8-Modus: das Modell aus AMDs 4.1.1-DLL, in Vulkan nachgebildet "
     "(Ausgabe identisch mit der DLL). Ein Modell für Native bis Performance und ein "
     "weiteres für Ultra Performance. Assets: tools/fsr4cap/build_assets.sh (benötigt die "
     "DLL und Proton)."},
    {"FSR 4 in INT8 mode (v07 model from AMD FidelityFX SDK sources). Higher quality than "
     "FSR 3.1, but the pass is more demanding. Changing the preset rebuilds the model (a "
     "brief pause). Assets: tools/fetch_fsr4_assets.sh.",
     "FSR 4 im INT8-Modus (v07-Modell aus den Quellen des AMD FidelityFX SDK). Höhere "
     "Qualität als FSR 3.1, aber der Pass ist aufwendiger. Ein Wechsel der Voreinstellung "
     "baut das Modell neu auf (kurze Pause). Assets: tools/fetch_fsr4_assets.sh."},
    {"FSR 4: auto exposure",
     "FSR 4: Automatische Belichtung"},
    {"FSR 4: invert jitter sign",
     "FSR 4: Jitter-Vorzeichen umkehren"},
    {"For diagnosing ghosting: the FSR 4 network normalizes color by exposure and uses it to "
     "decide when to discard previous frames. Changes apply immediately, without a restart.",
     "Zur Diagnose von Ghosting: Das FSR-4-Netz normalisiert die Farbe anhand der Belichtung "
     "und entscheidet damit, wann frühere Frames verworfen werden. Änderungen gelten sofort, "
     "ohne Neustart."},
    {"Preset",
     "Voreinstellung"},
    {"%s (x%.1f, render %dx%d)",
     "%s (x%.1f, Rendering %dx%d)"},
    {"Character motion vectors",
     "Bewegungsvektoren für Figuren"},
    {"Accurate vectors for animated objects: clothing and weapons break up less during "
     "movement. The static scene does not get an additional pass. Changes apply after "
     "restarting the game.",
     "Genaue Vektoren für animierte Objekte: Kleidung und Waffen zerfallen bei Bewegung "
     "weniger. Die statische Szene erhält keinen zusätzlichen Pass. Gilt nach einem Neustart "
     "des Spiels."},
    {"Show motion vectors (debug)",
     "Bewegungsvektoren anzeigen (Debug)"},
    {"Red/green: horizontal/vertical motion (8 pixels = full brightness). Blue: the pixel "
     "received an accurate object vector, not just camera motion. The upscaler treats a "
     "moving object with no blue or red/green as stationary, causing trails.",
     "Rot/Grün: horizontale/vertikale Bewegung (8 Pixel = volle Helligkeit). Blau: Das Pixel "
     "hat einen genauen Objektvektor erhalten, nicht nur die Kamerabewegung. Ein bewegtes "
     "Objekt ohne Blau oder Rot/Grün hält der Upscaler für unbewegt, daher Schlieren."},
    {"Output resolution",
     "Ausgabeauflösung"},
    {"Size of the final frame and UI. The preset sets the scene size relative to the output: "
     "4K Performance = 1920x1080. Applies after restarting the game.",
     "Größe des fertigen Bildes und der Oberfläche. Die Voreinstellung legt die Szenengröße "
     "relativ zur Ausgabe fest: 4K Performance = 1920x1080. Gilt nach einem Neustart des "
     "Spiels."},
    {"The final frame and UI size changes at the next frame boundary. The preset sets the "
     "scene size relative to the output: 4K Performance = 1920x1080. Changing the size "
     "resets FSR history and may cause a brief pause.",
     "Die Größe des fertigen Bildes und der Oberfläche ändert sich ab dem nächsten Frame. "
     "Die Voreinstellung legt die Szenengröße relativ zur Ausgabe fest: 4K Performance = "
     "1920x1080. Eine Größenänderung setzt den FSR-Verlauf zurück und kann eine kurze Pause "
     "verursachen."},
    {"Auto (based on GPU)",
     "Auto (je nach GPU)"},
    {"Off (faster)",
     "Aus (schneller)"},
    {"On",
     "An"},
    {"Live resolution changes",
     "Auflösung live ändern"},
    {"On: output resolution and preset change without restarting, but game post-processing "
     "stays at 1080p, which is noticeably slower on Steam Deck and older GPUs. Off: "
     "everything renders at the preset resolution, and changes require a restart. Auto "
     "enables this on powerful discrete GPUs. Applies after restarting the game.",
     "An: Ausgabeauflösung und Voreinstellung ändern sich ohne Neustart, aber die "
     "Nachbearbeitung des Spiels bleibt bei 1080p, was auf dem Steam Deck und älteren GPUs "
     "spürbar langsamer ist. Aus: Alles wird in der Auflösung der Voreinstellung gerendert, "
     "Änderungen erfordern einen Neustart. Auto aktiviert dies auf leistungsstarken "
     "dedizierten GPUs. Gilt nach einem Neustart des Spiels."},
    {"Game effects (restart required)",
     "Spieleffekte (Neustart erforderlich)"},
    {"Highest (-2)",
     "Maximal (-2)"},
    {"Game default",
     "Spielstandard"},
    {"Lower (1)",
     "Niedriger (1)"},
    {"Lowest (2)",
     "Minimal (2)"},
    {"Model detail",
     "Modelldetails"},
    {"Effects are enabled and disabled by game patches at startup (patches/Bloodborne.xml). "
     "Motion blur and shadows from dynamic lights place a significant load on the GPU.",
     "Effekte werden beim Start durch Spiel-Patches ein- und ausgeschaltet "
     "(patches/Bloodborne.xml). Bewegungsunschärfe und Schatten dynamischer Lichter belasten "
     "die GPU spürbar."},
    {"Free camera: hold Cross and press L3 (keyboard: Space + Z). Debug menu: left touchpad "
     "/ Tab. Requires DbgFont14h.ccm and DbgFont14h.tpf in dvdroot_ps4/font from Nexus mod "
     "#253. Right touchpad: Backspace.",
     "Freie Kamera: Cross halten und L3 drücken (Tastatur: Space + Z). Debug-Menü: linkes "
     "Touchpad / Tab. Benötigt DbgFont14h.ccm und DbgFont14h.tpf in dvdroot_ps4/font aus "
     "Nexus-Mod #253. Rechtes Touchpad: Backspace."},
    {"Changes apply after restarting the game",
     "Änderungen gelten nach einem Neustart des Spiels"},
    {"Apply and restart game",
     "Übernehmen und Spiel neu starten"},
    {"Other",
     "Sonstiges"},
    {"FPS counter in corner",
     "FPS-Anzeige in der Ecke"},
    {"Frame rate",
     "Bildrate"},
    {"Frame time",
     "Bildzeit"},
    {"Close",
     "Schließen"},
    {"Settings are saved to bbport.ini",
     "Einstellungen werden in bbport.ini gespeichert"},
    {"Chromatic aberration",
     "Chromatische Aberration"},
    {"Depth of field (DoF)",
     "Tiefenschärfe (DoF)"},
    {"Motion blur",
     "Bewegungsunschärfe"},
    {"Ambient occlusion (SSAO)",
     "Umgebungsverdeckung (SSAO)"},
    {"Game's own anti-aliasing",
     "Spieleigene Kantenglättung"},
    {"Shadows from dynamic lights",
     "Schatten dynamischer Lichter"},
    {"Screen-space reflections (not in original game)",
     "Bildschirmraum-Reflexionen (nicht im Originalspiel)"},
    {"Skip startup intros",
     "Intros beim Start überspringen"},
    {"Free camera (Cross + L3)",
     "Freie Kamera (Cross + L3)"},
    {"Debug menu (requires font files)",
     "Debug-Menü (benötigt Schriftdateien)"},
};

// Italian (it)
constexpr Entry kItalian[] = {
    {"Hide mouse cursor",
     "Nascondi il cursore del mouse"},
    {"Picture",
     "Immagine"},
    {"Picture tuning",
     "Regolazione immagine"},
    {"UI brightness",
     "Luminosità dell'interfaccia"},
    {"UI colour",
     "Colori dell'interfaccia"},
    {"Brightness",
     "Luminosità"},
    {"Contrast",
     "Contrasto"},
    {"Saturation",
     "Saturazione"},
    {"Hue",
     "Tonalità"},
    {"Fog",
     "Nebbia"},
    {"Vignette",
     "Vignettatura"},
    {"Ambient occlusion strength",
     "Intensità dell'occlusione ambientale"},
    {"Shadow distance",
     "Distanza delle ombre"},
    {"Game's own picture",
     "Immagine originale del gioco"},
    {"Bloodborne - Graphics  (Insert / L3+R3)###bbport_settings",
     "Bloodborne - Grafica  (Insert / L3+R3)###bbport_settings"},
    {"%.0f FPS  (%.1f ms)",
     "%.0f FPS  (%.1f ms)"},
    {"Temporal upscaler",
     "Upscaler temporale"},
    {"Off",
     "Disattivato"},
    {"TAA (native anti-aliasing)",
     "TAA (anti-aliasing nativo)"},
    {"Upscaler",
     "Upscaler"},
    {"— not supported by this GPU",
     "— non supportato da questa GPU"},
    {"— in development",
     "— in sviluppo"},
    {"FSR 4.1.1 in INT8 mode: the model from AMD's 4.1.1 DLL, reproduced in Vulkan (output "
     "matches the DLL). One model for Native through Performance and another for Ultra "
     "Performance. Assets: tools/fsr4cap/build_assets.sh (requires the DLL and Proton).",
     "FSR 4.1.1 in modalità INT8: il modello della DLL 4.1.1 di AMD, riprodotto in Vulkan "
     "(il risultato coincide con la DLL). Un modello da Native a Performance e un altro per "
     "Ultra Performance. Risorse: tools/fsr4cap/build_assets.sh (richiede la DLL e Proton)."},
    {"FSR 4 in INT8 mode (v07 model from AMD FidelityFX SDK sources). Higher quality than "
     "FSR 3.1, but the pass is more demanding. Changing the preset rebuilds the model (a "
     "brief pause). Assets: tools/fetch_fsr4_assets.sh.",
     "FSR 4 in modalità INT8 (modello v07 dai sorgenti di AMD FidelityFX SDK). Qualità "
     "superiore a FSR 3.1, ma il passaggio è più pesante. Cambiare preset ricostruisce il "
     "modello (breve pausa). Risorse: tools/fetch_fsr4_assets.sh."},
    {"FSR 4: auto exposure",
     "FSR 4: esposizione automatica"},
    {"FSR 4: invert jitter sign",
     "FSR 4: inverti segno del jitter"},
    {"For diagnosing ghosting: the FSR 4 network normalizes color by exposure and uses it to "
     "decide when to discard previous frames. Changes apply immediately, without a restart.",
     "Per diagnosticare il ghosting: la rete di FSR 4 normalizza il colore in base "
     "all'esposizione e la usa per decidere quando scartare i fotogrammi precedenti. Le "
     "modifiche si applicano subito, senza riavvio."},
    {"Preset",
     "Preset"},
    {"%s (x%.1f, render %dx%d)",
     "%s (x%.1f, rendering %dx%d)"},
    {"Character motion vectors",
     "Vettori di movimento dei personaggi"},
    {"Accurate vectors for animated objects: clothing and weapons break up less during "
     "movement. The static scene does not get an additional pass. Changes apply after "
     "restarting the game.",
     "Vettori precisi per gli oggetti animati: vestiti e armi si sfaldano meno durante il "
     "movimento. La scena statica non riceve un passaggio aggiuntivo. Si applica dopo il "
     "riavvio del gioco."},
    {"Show motion vectors (debug)",
     "Mostra vettori di movimento (debug)"},
    {"Red/green: horizontal/vertical motion (8 pixels = full brightness). Blue: the pixel "
     "received an accurate object vector, not just camera motion. The upscaler treats a "
     "moving object with no blue or red/green as stationary, causing trails.",
     "Rosso/verde: movimento orizzontale/verticale (8 pixel = luminosità massima). Blu: il "
     "pixel ha ricevuto un vettore oggetto preciso, non solo il movimento della telecamera. "
     "L'upscaler considera fermo un oggetto in movimento senza blu né rosso/verde, causando "
     "scie."},
    {"Output resolution",
     "Risoluzione di uscita"},
    {"Size of the final frame and UI. The preset sets the scene size relative to the output: "
     "4K Performance = 1920x1080. Applies after restarting the game.",
     "Dimensione del fotogramma finale e dell'interfaccia. Il preset imposta la dimensione "
     "della scena rispetto all'uscita: 4K Performance = 1920x1080. Si applica dopo il "
     "riavvio del gioco."},
    {"The final frame and UI size changes at the next frame boundary. The preset sets the "
     "scene size relative to the output: 4K Performance = 1920x1080. Changing the size "
     "resets FSR history and may cause a brief pause.",
     "La dimensione del fotogramma finale e dell'interfaccia cambia dal fotogramma "
     "successivo. Il preset imposta la dimensione della scena rispetto all'uscita: 4K "
     "Performance = 1920x1080. Cambiare dimensione azzera la cronologia di FSR e può causare "
     "una breve pausa."},
    {"Auto (based on GPU)",
     "Auto (in base alla GPU)"},
    {"Off (faster)",
     "Disattivato (più veloce)"},
    {"On",
     "Attivato"},
    {"Live resolution changes",
     "Cambio di risoluzione al volo"},
    {"On: output resolution and preset change without restarting, but game post-processing "
     "stays at 1080p, which is noticeably slower on Steam Deck and older GPUs. Off: "
     "everything renders at the preset resolution, and changes require a restart. Auto "
     "enables this on powerful discrete GPUs. Applies after restarting the game.",
     "Attivato: risoluzione di uscita e preset cambiano senza riavvio, ma la "
     "post-elaborazione del gioco resta a 1080p, cosa nettamente più lenta su Steam Deck e "
     "GPU meno recenti. Disattivato: tutto viene renderizzato alla risoluzione del preset e "
     "le modifiche richiedono un riavvio. Auto lo attiva sulle GPU dedicate potenti. Si "
     "applica dopo il riavvio del gioco."},
    {"Game effects (restart required)",
     "Effetti di gioco (richiede riavvio)"},
    {"Highest (-2)",
     "Massimo (-2)"},
    {"Game default",
     "Predefinito del gioco"},
    {"Lower (1)",
     "Basso (1)"},
    {"Lowest (2)",
     "Minimo (2)"},
    {"Model detail",
     "Dettaglio modelli"},
    {"Effects are enabled and disabled by game patches at startup (patches/Bloodborne.xml). "
     "Motion blur and shadows from dynamic lights place a significant load on the GPU.",
     "Gli effetti vengono attivati e disattivati da patch del gioco all'avvio "
     "(patches/Bloodborne.xml). Sfocatura di movimento e ombre delle luci dinamiche pesano "
     "molto sulla GPU."},
    {"Free camera: hold Cross and press L3 (keyboard: Space + Z). Debug menu: left touchpad "
     "/ Tab. Requires DbgFont14h.ccm and DbgFont14h.tpf in dvdroot_ps4/font from Nexus mod "
     "#253. Right touchpad: Backspace.",
     "Telecamera libera: tieni premuto Cross e premi L3 (tastiera: Space + Z). Menu di "
     "debug: touchpad sinistro / Tab. Richiede DbgFont14h.ccm e DbgFont14h.tpf in "
     "dvdroot_ps4/font dalla mod Nexus #253. Touchpad destro: Backspace."},
    {"Changes apply after restarting the game",
     "Le modifiche si applicano dopo il riavvio del gioco"},
    {"Apply and restart game",
     "Applica e riavvia il gioco"},
    {"Other",
     "Altro"},
    {"FPS counter in corner",
     "Contatore FPS nell'angolo"},
    {"Frame rate",
     "Frequenza fotogrammi"},
    {"Frame time",
     "Tempo per fotogramma"},
    {"Close",
     "Chiudi"},
    {"Settings are saved to bbport.ini",
     "Le impostazioni sono salvate in bbport.ini"},
    {"Chromatic aberration",
     "Aberrazione cromatica"},
    {"Depth of field (DoF)",
     "Profondità di campo (DoF)"},
    {"Motion blur",
     "Sfocatura di movimento"},
    {"Ambient occlusion (SSAO)",
     "Occlusione ambientale (SSAO)"},
    {"Game's own anti-aliasing",
     "Anti-aliasing del gioco"},
    {"Shadows from dynamic lights",
     "Ombre delle luci dinamiche"},
    {"Screen-space reflections (not in original game)",
     "Riflessi in screen space (assenti nel gioco originale)"},
    {"Skip startup intros",
     "Salta i filmati iniziali"},
    {"Free camera (Cross + L3)",
     "Telecamera libera (Cross + L3)"},
    {"Debug menu (requires font files)",
     "Menu di debug (richiede file dei font)"},
};

// Polish (pl)
constexpr Entry kPolish[] = {
    {"Hide mouse cursor",
     "Ukryj kursor myszy"},
    {"Picture",
     "Obraz"},
    {"Picture tuning",
     "Strojenie obrazu"},
    {"UI brightness",
     "Jasność interfejsu"},
    {"UI colour",
     "Kolory interfejsu"},
    {"Brightness",
     "Jasność"},
    {"Contrast",
     "Kontrast"},
    {"Saturation",
     "Nasycenie"},
    {"Hue",
     "Odcień"},
    {"Fog",
     "Mgła"},
    {"Vignette",
     "Winieta"},
    {"Ambient occlusion strength",
     "Siła okluzji otoczenia"},
    {"Shadow distance",
     "Zasięg cieni"},
    {"Game's own picture",
     "Oryginalny obraz gry"},
    {"Bloodborne - Graphics  (Insert / L3+R3)###bbport_settings",
     "Bloodborne - Grafika  (Insert / L3+R3)###bbport_settings"},
    {"%.0f FPS  (%.1f ms)",
     "%.0f FPS  (%.1f ms)"},
    {"Temporal upscaler",
     "Skalowanie czasowe"},
    {"Off",
     "Wyłączone"},
    {"TAA (native anti-aliasing)",
     "TAA (natywny antyaliasing)"},
    {"Upscaler",
     "Upscaler"},
    {"— not supported by this GPU",
     "— nieobsługiwane przez ten GPU"},
    {"— in development",
     "— w przygotowaniu"},
    {"FSR 4.1.1 in INT8 mode: the model from AMD's 4.1.1 DLL, reproduced in Vulkan (output "
     "matches the DLL). One model for Native through Performance and another for Ultra "
     "Performance. Assets: tools/fsr4cap/build_assets.sh (requires the DLL and Proton).",
     "FSR 4.1.1 w trybie INT8: model z biblioteki DLL AMD 4.1.1 odtworzony w Vulkanie (wynik "
     "zgodny z DLL). Jeden model od Native do Performance i osobny dla Ultra Performance. "
     "Zasoby: tools/fsr4cap/build_assets.sh (wymaga DLL i Protona)."},
    {"FSR 4 in INT8 mode (v07 model from AMD FidelityFX SDK sources). Higher quality than "
     "FSR 3.1, but the pass is more demanding. Changing the preset rebuilds the model (a "
     "brief pause). Assets: tools/fetch_fsr4_assets.sh.",
     "FSR 4 w trybie INT8 (model v07 ze źródeł AMD FidelityFX SDK). Wyższa jakość niż FSR "
     "3.1, ale przebieg jest bardziej wymagający. Zmiana profilu przebudowuje model (krótka "
     "pauza). Zasoby: tools/fetch_fsr4_assets.sh."},
    {"FSR 4: auto exposure",
     "FSR 4: automatyczna ekspozycja"},
    {"FSR 4: invert jitter sign",
     "FSR 4: odwróć znak jittera"},
    {"For diagnosing ghosting: the FSR 4 network normalizes color by exposure and uses it to "
     "decide when to discard previous frames. Changes apply immediately, without a restart.",
     "Do diagnozowania smużenia (ghosting): sieć FSR 4 normalizuje kolor według ekspozycji i "
     "na tej podstawie decyduje, kiedy odrzucić poprzednie klatki. Zmiany działają od razu, "
     "bez restartu."},
    {"Preset",
     "Profil"},
    {"%s (x%.1f, render %dx%d)",
     "%s (x%.1f, renderowanie %dx%d)"},
    {"Character motion vectors",
     "Wektory ruchu postaci"},
    {"Accurate vectors for animated objects: clothing and weapons break up less during "
     "movement. The static scene does not get an additional pass. Changes apply after "
     "restarting the game.",
     "Dokładne wektory dla animowanych obiektów: ubrania i broń mniej się rozpadają w ruchu. "
     "Statyczna scena nie dostaje dodatkowego przebiegu. Działa po ponownym uruchomieniu gry."},
    {"Show motion vectors (debug)",
     "Pokaż wektory ruchu (debug)"},
    {"Red/green: horizontal/vertical motion (8 pixels = full brightness). Blue: the pixel "
     "received an accurate object vector, not just camera motion. The upscaler treats a "
     "moving object with no blue or red/green as stationary, causing trails.",
     "Czerwony/zielony: ruch w poziomie/pionie (8 pikseli = pełna jasność). Niebieski: "
     "piksel otrzymał dokładny wektor obiektu, a nie tylko ruch kamery. Poruszający się "
     "obiekt bez niebieskiego ani czerwonego/zielonego upscaler uznaje za nieruchomy, stąd "
     "smugi."},
    {"Output resolution",
     "Rozdzielczość wyjściowa"},
    {"Size of the final frame and UI. The preset sets the scene size relative to the output: "
     "4K Performance = 1920x1080. Applies after restarting the game.",
     "Rozmiar gotowej klatki i interfejsu. Profil ustala rozmiar sceny względem wyjścia: 4K "
     "Performance = 1920x1080. Działa po ponownym uruchomieniu gry."},
    {"The final frame and UI size changes at the next frame boundary. The preset sets the "
     "scene size relative to the output: 4K Performance = 1920x1080. Changing the size "
     "resets FSR history and may cause a brief pause.",
     "Rozmiar gotowej klatki i interfejsu zmienia się od następnej klatki. Profil ustala "
     "rozmiar sceny względem wyjścia: 4K Performance = 1920x1080. Zmiana rozmiaru resetuje "
     "historię FSR i może spowodować krótką pauzę."},
    {"Auto (based on GPU)",
     "Auto (zależnie od GPU)"},
    {"Off (faster)",
     "Wyłączona (szybciej)"},
    {"On",
     "Włączona"},
    {"Live resolution changes",
     "Zmiana rozdzielczości na żywo"},
    {"On: output resolution and preset change without restarting, but game post-processing "
     "stays at 1080p, which is noticeably slower on Steam Deck and older GPUs. Off: "
     "everything renders at the preset resolution, and changes require a restart. Auto "
     "enables this on powerful discrete GPUs. Applies after restarting the game.",
     "Włączona: rozdzielczość wyjściowa i profil zmieniają się bez restartu, ale "
     "postprocessing gry zostaje w 1080p, co na Steam Decku i starszych GPU jest wyraźnie "
     "wolniejsze. Wyłączona: wszystko jest renderowane w rozdzielczości profilu, a zmiany "
     "wymagają restartu. Auto włącza ją na mocnych dedykowanych GPU. Działa po ponownym "
     "uruchomieniu gry."},
    {"Game effects (restart required)",
     "Efekty gry (wymagany restart)"},
    {"Highest (-2)",
     "Najwyższa (-2)"},
    {"Game default",
     "Jak w grze"},
    {"Lower (1)",
     "Niższa (1)"},
    {"Lowest (2)",
     "Najniższa (2)"},
    {"Model detail",
     "Szczegółowość modeli"},
    {"Effects are enabled and disabled by game patches at startup (patches/Bloodborne.xml). "
     "Motion blur and shadows from dynamic lights place a significant load on the GPU.",
     "Efekty są włączane i wyłączane przez łatki gry przy uruchomieniu "
     "(patches/Bloodborne.xml). Rozmycie ruchu i cienie od dynamicznych świateł wyraźnie "
     "obciążają GPU."},
    {"Free camera: hold Cross and press L3 (keyboard: Space + Z). Debug menu: left touchpad "
     "/ Tab. Requires DbgFont14h.ccm and DbgFont14h.tpf in dvdroot_ps4/font from Nexus mod "
     "#253. Right touchpad: Backspace.",
     "Wolna kamera: przytrzymaj Cross i naciśnij L3 (klawiatura: Space + Z). Menu "
     "debugowania: lewy touchpad / Tab. Wymaga DbgFont14h.ccm i DbgFont14h.tpf w "
     "dvdroot_ps4/font z moda Nexus #253. Prawy touchpad: Backspace."},
    {"Changes apply after restarting the game",
     "Zmiany zadziałają po ponownym uruchomieniu gry"},
    {"Apply and restart game",
     "Zastosuj i uruchom grę ponownie"},
    {"Other",
     "Inne"},
    {"FPS counter in corner",
     "Licznik FPS w rogu"},
    {"Frame rate",
     "Liczba klatek na sekundę"},
    {"Frame time",
     "Czas klatki"},
    {"Close",
     "Zamknij"},
    {"Settings are saved to bbport.ini",
     "Ustawienia są zapisywane w bbport.ini"},
    {"Chromatic aberration",
     "Aberracja chromatyczna"},
    {"Depth of field (DoF)",
     "Głębia ostrości (DoF)"},
    {"Motion blur",
     "Rozmycie ruchu"},
    {"Ambient occlusion (SSAO)",
     "Okluzja otoczenia (SSAO)"},
    {"Game's own anti-aliasing",
     "Własny antyaliasing gry"},
    {"Shadows from dynamic lights",
     "Cienie od dynamicznych świateł"},
    {"Screen-space reflections (not in original game)",
     "Odbicia w przestrzeni ekranu (nie było ich w grze)"},
    {"Skip startup intros",
     "Pomiń intro przy uruchomieniu"},
    {"Free camera (Cross + L3)",
     "Wolna kamera (Cross + L3)"},
    {"Debug menu (requires font files)",
     "Menu debugowania (wymaga plików czcionek)"},
};

// Turkish (tr)
constexpr Entry kTurkish[] = {
    {"Hide mouse cursor",
     "Fare imlecini gizle"},
    {"Picture",
     "Görüntü"},
    {"Picture tuning",
     "Görüntü ayarı"},
    {"UI brightness",
     "Arayüz parlaklığı"},
    {"UI colour",
     "Arayüz renkleri"},
    {"Brightness",
     "Parlaklık"},
    {"Contrast",
     "Kontrast"},
    {"Saturation",
     "Doygunluk"},
    {"Hue",
     "Renk tonu"},
    {"Fog",
     "Sis"},
    {"Vignette",
     "Vinyet"},
    {"Ambient occlusion strength",
     "Ortam kapatma gücü"},
    {"Shadow distance",
     "Gölge mesafesi"},
    {"Game's own picture",
     "Oyunun kendi görüntüsü"},
    {"Bloodborne - Graphics  (Insert / L3+R3)###bbport_settings",
     "Bloodborne - Grafik  (Insert / L3+R3)###bbport_settings"},
    {"%.0f FPS  (%.1f ms)",
     "%.0f FPS  (%.1f ms)"},
    {"Temporal upscaler",
     "Zamansal ölçekleyici"},
    {"Off",
     "Kapalı"},
    {"TAA (native anti-aliasing)",
     "TAA (yerel kenar yumuşatma)"},
    {"Upscaler",
     "Ölçekleyici"},
    {"— not supported by this GPU",
     "— bu GPU tarafından desteklenmiyor"},
    {"— in development",
     "— geliştiriliyor"},
    {"FSR 4.1.1 in INT8 mode: the model from AMD's 4.1.1 DLL, reproduced in Vulkan (output "
     "matches the DLL). One model for Native through Performance and another for Ultra "
     "Performance. Assets: tools/fsr4cap/build_assets.sh (requires the DLL and Proton).",
     "INT8 modunda FSR 4.1.1: AMD'nin 4.1.1 DLL'indeki model, Vulkan'da yeniden üretildi "
     "(çıktı DLL ile aynı). Native'den Performance'a kadar bir model, Ultra Performance için "
     "ayrı bir model. Dosyalar: tools/fsr4cap/build_assets.sh (DLL ve Proton gerekir)."},
    {"FSR 4 in INT8 mode (v07 model from AMD FidelityFX SDK sources). Higher quality than "
     "FSR 3.1, but the pass is more demanding. Changing the preset rebuilds the model (a "
     "brief pause). Assets: tools/fetch_fsr4_assets.sh.",
     "INT8 modunda FSR 4 (AMD FidelityFX SDK kaynaklarından v07 modeli). FSR 3.1'den daha "
     "yüksek kalite, ancak geçiş daha ağır. Ön ayarı değiştirmek modeli yeniden oluşturur "
     "(kısa bir duraklama). Dosyalar: tools/fetch_fsr4_assets.sh."},
    {"FSR 4: auto exposure",
     "FSR 4: otomatik pozlama"},
    {"FSR 4: invert jitter sign",
     "FSR 4: jitter işaretini ters çevir"},
    {"For diagnosing ghosting: the FSR 4 network normalizes color by exposure and uses it to "
     "decide when to discard previous frames. Changes apply immediately, without a restart.",
     "Hayalet görüntü (ghosting) teşhisi için: FSR 4 ağı rengi pozlamaya göre normalleştirir "
     "ve önceki kareleri ne zaman atacağına buna göre karar verir. Değişiklikler yeniden "
     "başlatmadan hemen uygulanır."},
    {"Preset",
     "Ön ayar"},
    {"%s (x%.1f, render %dx%d)",
     "%s (x%.1f, işleme %dx%d)"},
    {"Character motion vectors",
     "Karakter hareket vektörleri"},
    {"Accurate vectors for animated objects: clothing and weapons break up less during "
     "movement. The static scene does not get an additional pass. Changes apply after "
     "restarting the game.",
     "Animasyonlu nesneler için hassas vektörler: kıyafetler ve silahlar hareket sırasında "
     "daha az dağılır. Statik sahne ek bir geçiş almaz. Oyun yeniden başlatıldıktan sonra "
     "uygulanır."},
    {"Show motion vectors (debug)",
     "Hareket vektörlerini göster (hata ayıklama)"},
    {"Red/green: horizontal/vertical motion (8 pixels = full brightness). Blue: the pixel "
     "received an accurate object vector, not just camera motion. The upscaler treats a "
     "moving object with no blue or red/green as stationary, causing trails.",
     "Kırmızı/yeşil: yatay/dikey hareket (8 piksel = tam parlaklık). Mavi: piksel yalnızca "
     "kamera hareketini değil, hassas bir nesne vektörünü aldı. Ölçekleyici, mavisi ya da "
     "kırmızısı/yeşili olmayan hareketli bir nesneyi sabit sayar; bu da iz bırakmaya yol "
     "açar."},
    {"Output resolution",
     "Çıkış çözünürlüğü"},
    {"Size of the final frame and UI. The preset sets the scene size relative to the output: "
     "4K Performance = 1920x1080. Applies after restarting the game.",
     "Son karenin ve arayüzün boyutu. Ön ayar, sahne boyutunu çıkışa göre belirler: 4K "
     "Performance = 1920x1080. Oyun yeniden başlatıldıktan sonra uygulanır."},
    {"The final frame and UI size changes at the next frame boundary. The preset sets the "
     "scene size relative to the output: 4K Performance = 1920x1080. Changing the size "
     "resets FSR history and may cause a brief pause.",
     "Son karenin ve arayüzün boyutu bir sonraki karede değişir. Ön ayar, sahne boyutunu "
     "çıkışa göre belirler: 4K Performance = 1920x1080. Boyutu değiştirmek FSR geçmişini "
     "sıfırlar ve kısa bir duraklamaya neden olabilir."},
    {"Auto (based on GPU)",
     "Otomatik (GPU'ya göre)"},
    {"Off (faster)",
     "Kapalı (daha hızlı)"},
    {"On",
     "Açık"},
    {"Live resolution changes",
     "Anlık çözünürlük değişimi"},
    {"On: output resolution and preset change without restarting, but game post-processing "
     "stays at 1080p, which is noticeably slower on Steam Deck and older GPUs. Off: "
     "everything renders at the preset resolution, and changes require a restart. Auto "
     "enables this on powerful discrete GPUs. Applies after restarting the game.",
     "Açık: çıkış çözünürlüğü ve ön ayar yeniden başlatmadan değişir, ancak oyunun son "
     "işlemesi 1080p'de kalır; bu, Steam Deck'te ve eski GPU'larda belirgin şekilde daha "
     "yavaştır. Kapalı: her şey ön ayarın çözünürlüğünde işlenir ve değişiklikler yeniden "
     "başlatma gerektirir. Otomatik, bunu güçlü harici GPU'larda açar. Oyun yeniden "
     "başlatıldıktan sonra uygulanır."},
    {"Game effects (restart required)",
     "Oyun efektleri (yeniden başlatma gerekir)"},
    {"Highest (-2)",
     "En yüksek (-2)"},
    {"Game default",
     "Oyun varsayılanı"},
    {"Lower (1)",
     "Düşük (1)"},
    {"Lowest (2)",
     "En düşük (2)"},
    {"Model detail",
     "Model detayı"},
    {"Effects are enabled and disabled by game patches at startup (patches/Bloodborne.xml). "
     "Motion blur and shadows from dynamic lights place a significant load on the GPU.",
     "Efektler, başlangıçta oyun yamalarıyla açılıp kapatılır (patches/Bloodborne.xml). "
     "Hareket bulanıklığı ve dinamik ışıkların gölgeleri GPU'ya belirgin bir yük bindirir."},
    {"Free camera: hold Cross and press L3 (keyboard: Space + Z). Debug menu: left touchpad "
     "/ Tab. Requires DbgFont14h.ccm and DbgFont14h.tpf in dvdroot_ps4/font from Nexus mod "
     "#253. Right touchpad: Backspace.",
     "Serbest kamera: Cross'u basılı tutup L3'e basın (klavye: Space + Z). Hata ayıklama "
     "menüsü: sol touchpad / Tab. Nexus mod #253'ten dvdroot_ps4/font içine DbgFont14h.ccm "
     "ve DbgFont14h.tpf gerekir. Sağ touchpad: Backspace."},
    {"Changes apply after restarting the game",
     "Değişiklikler oyun yeniden başlatıldıktan sonra uygulanır"},
    {"Apply and restart game",
     "Uygula ve oyunu yeniden başlat"},
    {"Other",
     "Diğer"},
    {"FPS counter in corner",
     "Köşede FPS sayacı"},
    {"Frame rate",
     "Kare hızı"},
    {"Frame time",
     "Kare süresi"},
    {"Close",
     "Kapat"},
    {"Settings are saved to bbport.ini",
     "Ayarlar bbport.ini dosyasına kaydedilir"},
    {"Chromatic aberration",
     "Renk sapması"},
    {"Depth of field (DoF)",
     "Alan derinliği (DoF)"},
    {"Motion blur",
     "Hareket bulanıklığı"},
    {"Ambient occlusion (SSAO)",
     "Ortam perdelemesi (SSAO)"},
    {"Game's own anti-aliasing",
     "Oyunun kendi kenar yumuşatması"},
    {"Shadows from dynamic lights",
     "Dinamik ışıkların gölgeleri"},
    {"Screen-space reflections (not in original game)",
     "Ekran alanı yansımaları (orijinal oyunda yoktu)"},
    {"Skip startup intros",
     "Açılış videolarını atla"},
    {"Free camera (Cross + L3)",
     "Serbest kamera (Cross + L3)"},
    {"Debug menu (requires font files)",
     "Hata ayıklama menüsü (yazı tipi dosyaları gerekir)"},
};

using Table = std::unordered_map<std::string_view, const char*>;

template <std::size_t N>
Table Build(const Entry (&entries)[N]) {
    Table table;
    table.reserve(N);
    for (const Entry& entry : entries) {
        table.emplace(entry.english, entry.text);
    }
    return table;
}

} // namespace

const char* MenuTranslation(MenuLanguage language, const char* english) {
    // Built once (thread-safe static initialization); looked up for every string every frame.
    static const std::array<Table, 7> tables = {
        Build(kSpanish), Build(kPortugueseBrazil), Build(kFrench), Build(kGerman),
        Build(kItalian), Build(kPolish), Build(kTurkish),
    };
    std::size_t index = 0;
    switch (language) {
    case MenuLanguage::Spanish: index = 0; break;
    case MenuLanguage::PortugueseBrazil: index = 1; break;
    case MenuLanguage::French: index = 2; break;
    case MenuLanguage::German: index = 3; break;
    case MenuLanguage::Italian: index = 4; break;
    case MenuLanguage::Polish: index = 5; break;
    case MenuLanguage::Turkish: index = 6; break;
    default: return english;
    }
    const Table& table = tables[index];
    const auto it = table.find(english);
    return it != table.end() ? it->second : english;
}

} // namespace BbSettings
