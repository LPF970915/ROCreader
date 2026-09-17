#include "key_guide_panel.h"

#include "app_language.h"
#include "key_calibration_runtime.h"
#include "screen_profile.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace {
int ScalePx(float scale, int value) {
  return std::max(1, static_cast<int>(std::round(static_cast<float>(value) * std::max(0.1f, scale))));
}

struct KeyGuideLine {
  const char *button_label;
  AppTextId action_text;
};

struct StaticKeyGuideLine {
  const char *button_label;
  const char *action_text;
};

constexpr std::array<KeyGuideLine, 9> kKeyGuideLines = {{
    {"D-Pad", AppTextId::KeyGuideActionDpad},
    {"A", AppTextId::KeyGuideActionA},
    {"B", AppTextId::KeyGuideActionB},
    {"X", AppTextId::KeyGuideActionX},
    {"Y", AppTextId::KeyGuideActionY},
    {"L1 / R1", AppTextId::KeyGuideActionShoulders},
    {"L2 / R2", AppTextId::KeyGuideActionTriggers},
    {"Menu / Start / Select", AppTextId::KeyGuideActionMenu},
    {"Vol+ / Vol-", AppTextId::KeyGuideActionVolume},
}};

const char *GkdKeyGuideTitle(int language_index) {
  static constexpr std::array<const char *, 12> kTitles = {{
      u8"GKD350HUltra \u4e13\u5c5e\u6620\u5c04",
      u8"GKD350HUltra \u5c08\u5c6c\u6620\u5c04",
      "GKD350HUltra Mapping",
      "Mapa GKD350HUltra",
      "Mappage GKD350HUltra",
      "GKD350HUltra-Belegung",
      u8"GKD350HUltra \u5c02\u7528\u30de\u30c3\u30d7",
      u8"GKD350HUltra \uc804\uc6a9 \ub9e4\ud551",
      u8"\u062a\u062e\u0637\u064a\u0637 GKD350HUltra",
      u8"\u0420\u0430\u0441\u043a\u043b\u0430\u0434\u043a\u0430 GKD350HUltra",
      "Mapa GKD350HUltra",
      u8"S\u01a1 \u0111\u1ed3 GKD350HUltra",
  }};
  return kTitles[static_cast<size_t>(ClampSystemLanguageIndex(language_index))];
}

struct StaticKeyGuide {
  const char *title;
  std::array<StaticKeyGuideLine, 10> lines;
};

constexpr std::array<StaticKeyGuide, 12> kRgdsKeyGuides = {{
    {u8"RGDS \u53cc\u5c4f\u4e13\u7528\u6620\u5c04",
     {{
         {"D-Pad", u8"\u4e66\u67b6\u79fb\u52a8\u9009\u62e9\uff1b\u4e0b\u5c4f\u83dc\u5355\u79fb\u52a8\uff1b\u9605\u8bfb\u65f6\u7ffb\u9875\u6216\u6eda\u52a8"},
         {"A", u8"\u786e\u8ba4\uff0c\u6253\u5f00\u4e66\u7c4d\u6216\u6267\u884c\u83dc\u5355\u9879"},
         {"B", u8"\u8fd4\u56de\uff1b\u5173\u95ed\u4e0b\u5c4f\u83dc\u5355\uff1b\u9605\u8bfb\u65f6\u9000\u56de\u4e66\u67b6"},
         {"X", u8"\u9605\u8bfb\u65f6\u663e\u793a\u9605\u8bfb\u8fdb\u5ea6"},
         {"Y", u8"\u9605\u8bfb\u65f6\u6253\u5f00\u7ae0\u8282\u76ee\u5f55"},
         {"L1 / R1", u8"\u4e66\u67b6\u5207\u6362\u5206\u7c7b\uff1b\u9605\u8bfb\u65f6\u4ec5\u56fe\u7247\u6a21\u5f0f\u4fdd\u7559\u7f29\u653e\uff0c\u7eaf\u6587\u672c\u548c\u56fe\u6587\u6df7\u5408\u4e0d\u7f29\u653e"},
         {"L2 / R2", u8"\u4e66\u67b6\u8f85\u52a9\u7ffb\u9875\uff1b\u56fe\u7247\u9605\u8bfb\u53ef\u65cb\u8f6c\uff0c90/270 \u8fdb\u5165\u6a2a\u5411\u53cc\u9875\uff1b\u7eaf\u6587\u672c\u548c\u56fe\u6587\u6df7\u5408\u4e0d\u65cb\u8f6c"},
         {"Select", u8"\u5728\u4e0a\u5c4f\u4e66\u67b6/\u9605\u8bfb\u548c\u4e0b\u5c4f\u83dc\u5355\u95f4\u5207\u6362\u7126\u70b9"},
         {"Menu", u8"\u4e66\u67b6\u65f6\u805a\u7126\u4e0b\u5c4f\u83dc\u5355\uff1b\u9605\u8bfb\u65f6\u5728\u4e0b\u5c4f\u6253\u5f00/\u5173\u95ed\u83dc\u5355"},
         {"RG", u8"\u53ef\u5728\u6309\u952e\u6821\u51c6\u4e2d\u6620\u5c04\uff1bVol+ / Vol- \u53ea\u8c03\u6574\u97f3\u91cf"},
     }}},
    {u8"RGDS \u96d9\u87a2\u5e55\u5c08\u7528\u6620\u5c04",
     {{
         {"D-Pad", u8"\u66f8\u67b6\u79fb\u52d5\u9078\u64c7\uff1b\u4e0b\u5c4f\u9078\u55ae\u79fb\u52d5\uff1b\u95b1\u8b80\u6642\u7ffb\u9801\u6216\u6372\u52d5"},
         {"A", u8"\u78ba\u8a8d\uff0c\u958b\u555f\u66f8\u7c4d\u6216\u57f7\u884c\u9078\u55ae\u9805"},
         {"B", u8"\u8fd4\u56de\uff1b\u95dc\u9589\u4e0b\u5c4f\u9078\u55ae\uff1b\u95b1\u8b80\u6642\u56de\u5230\u66f8\u67b6"},
         {"X", u8"\u95b1\u8b80\u6642\u986f\u793a\u95b1\u8b80\u9032\u5ea6"},
         {"Y", u8"\u95b1\u8b80\u6642\u958b\u555f\u7ae0\u7bc0\u76ee\u9304"},
         {"L1 / R1", u8"\u66f8\u67b6\u5207\u63db\u5206\u985e\uff1b\u95b1\u8b80\u6642\u50c5\u5716\u7247\u6a21\u5f0f\u4fdd\u7559\u7e2e\u653e\uff0c\u7d14\u6587\u672c\u548c\u5716\u6587\u6df7\u5408\u4e0d\u7e2e\u653e"},
         {"L2 / R2", u8"\u66f8\u67b6\u8f14\u52a9\u7ffb\u9801\uff1b\u5716\u7247\u95b1\u8b80\u53ef\u65cb\u8f49\uff0c90/270 \u9032\u5165\u6a6b\u5411\u96d9\u9801\uff1b\u7d14\u6587\u672c\u548c\u5716\u6587\u6df7\u5408\u4e0d\u65cb\u8f49"},
         {"Select", u8"\u5728\u4e0a\u5c4f\u66f8\u67b6/\u95b1\u8b80\u548c\u4e0b\u5c4f\u9078\u55ae\u9593\u5207\u63db\u7126\u9ede"},
         {"Menu", u8"\u66f8\u67b6\u6642\u805a\u7126\u4e0b\u5c4f\u9078\u55ae\uff1b\u95b1\u8b80\u6642\u5728\u4e0b\u5c4f\u958b\u555f/\u95dc\u9589\u9078\u55ae"},
         {"RG", u8"\u53ef\u5728\u6309\u9375\u6821\u6e96\u4e2d\u6620\u5c04\uff1bVol+ / Vol- \u53ea\u8abf\u6574\u97f3\u91cf"},
     }}},
    {"RGDS Dual-Screen Mapping",
     {{
         {"D-Pad", "Move on the shelf; move in the lower menu; turn pages or scroll while reading"},
         {"A", "Confirm, open a book, or run the selected menu item"},
         {"B", "Go back; close the lower menu; return from reader to shelf"},
         {"X", "Show reading progress while reading"},
         {"Y", "Open the chapter list while reading"},
         {"L1 / R1", "Shelf categories; zoom only in image readers"},
         {"L2 / R2", "Rotate image readers; 90/270 switch to spread pages"},
         {"Select", "Switch focus between the upper shelf/reader and the lower menu"},
         {"Menu", "Focus the lower menu on shelf; open/close the lower menu while reading"},
         {"RG", "Available for key calibration; Vol+ / Vol- adjust volume"},
     }}},
    {"Asignacion RGDS de doble pantalla",
     {{
         {"D-Pad", "Mover en la estanteria; mover en el menu inferior; pasar pagina o desplazar al leer"},
         {"A", "Confirmar, abrir un libro o ejecutar la opcion seleccionada"},
         {"B", "Volver; cerrar el menu inferior; volver del lector a la estanteria"},
         {"X", "Mostrar el progreso de lectura"},
         {"Y", "Abrir la lista de capitulos"},
         {"L1 / R1", "Categorias; zoom solo en lectores de imagen"},
         {"L2 / R2", "Rotar imagenes; 90/270 doble pagina"},
         {"Select", "Cambiar foco entre la pantalla superior y el menu inferior"},
         {"Menu", "En estanteria enfoca el menu inferior; en lectura abre/cierra ese menu"},
         {"RG", "Disponible para calibracion; Vol+ / Vol- ajustan volumen"},
     }}},
    {"Mappage RGDS double ecran",
     {{
         {"D-Pad", "Se deplacer dans la bibliotheque; menu inferieur; tourner ou defiler en lecture"},
         {"A", "Confirmer, ouvrir un livre ou lancer l'element de menu choisi"},
         {"B", "Retour; fermer le menu inferieur; revenir du lecteur a la bibliotheque"},
         {"X", "Afficher la progression de lecture"},
         {"Y", "Ouvrir la liste des chapitres"},
         {"L1 / R1", "Categories; zoom seulement pour images"},
         {"L2 / R2", "Rotation images; 90/270 double page"},
         {"Select", "Basculer le focus entre l'ecran superieur et le menu inferieur"},
         {"Menu", "Focus sur le menu inferieur en bibliotheque; ouvrir/fermer ce menu en lecture"},
         {"RG", "Disponible pour calibrage; Vol+ / Vol- volume"},
     }}},
    {"RGDS Dualscreen-Belegung",
     {{
         {"D-Pad", "Im Regal bewegen; im unteren Menue bewegen; beim Lesen blattern oder scrollen"},
         {"A", "Bestatigen, Buch offnen oder gewahlten Menuepunkt ausfuehren"},
         {"B", "Zurueck; unteres Menue schliessen; vom Leser zum Regal zurueck"},
         {"X", "Lesefortschritt anzeigen"},
         {"Y", "Kapitelliste oeffnen"},
         {"L1 / R1", "Kategorien; Zoom nur bei Bildern"},
         {"L2 / R2", "Bilder drehen; 90/270 Doppelseite"},
         {"Select", "Fokus zwischen oberem Bildschirm und unterem Menue wechseln"},
         {"Menu", "Im Regal unteres Menue fokussieren; beim Lesen unteres Menue oeffnen/schliessen"},
         {"RG", "Fuer Kalibrierung verfuegbar; Vol+ / Vol- Lautstaerke"},
     }}},
    {u8"RGDS \u30c7\u30e5\u30a2\u30eb\u753b\u9762\u30de\u30c3\u30d4\u30f3\u30b0",
     {{
         {"D-Pad", u8"\u672c\u68da\u306e\u79fb\u52d5\uff1b\u4e0b\u753b\u9762\u30e1\u30cb\u30e5\u30fc\u306e\u79fb\u52d5\uff1b\u8aad\u66f8\u4e2d\u306e\u30da\u30fc\u30b8\u79fb\u52d5/\u30b9\u30af\u30ed\u30fc\u30eb"},
         {"A", u8"\u6c7a\u5b9a\u3001\u672c\u3092\u958b\u304f\u3001\u307e\u305f\u306f\u9078\u629e\u4e2d\u306e\u30e1\u30cb\u30e5\u30fc\u3092\u5b9f\u884c"},
         {"B", u8"\u623b\u308b\uff1b\u4e0b\u753b\u9762\u30e1\u30cb\u30e5\u30fc\u3092\u9589\u3058\u308b\uff1b\u30ea\u30fc\u30c0\u30fc\u304b\u3089\u672c\u68da\u3078"},
         {"X", u8"\u8aad\u66f8\u4e2d\u306b\u9032\u6357\u3092\u8868\u793a"},
         {"Y", u8"\u8aad\u66f8\u4e2d\u306b\u7ae0\u30ea\u30b9\u30c8\u3092\u958b\u304f"},
         {"L1 / R1", u8"\u5206\u985e\u5207\u66ff\uff1b\u753b\u50cf\u306e\u307f\u30ba\u30fc\u30e0"},
         {"L2 / R2", u8"\u753b\u50cf\u3092\u56de\u8ee2\uff1b90/270\u3067\u898b\u958b\u304d"},
         {"Select", u8"\u4e0a\u753b\u9762\u306e\u672c\u68da/\u8aad\u66f8\u3068\u4e0b\u753b\u9762\u30e1\u30cb\u30e5\u30fc\u306e\u30d5\u30a9\u30fc\u30ab\u30b9\u5207\u66ff"},
         {"Menu", u8"\u672c\u68da\u3067\u4e0b\u30e1\u30cb\u30e5\u30fc\u306b\u30d5\u30a9\u30fc\u30ab\u30b9\uff1b\u8aad\u66f8\u4e2d\u306f\u4e0b\u30e1\u30cb\u30e5\u30fc\u3092\u958b\u9589"},
         {"RG", u8"\u30ad\u30fc\u8f03\u6b63\u3067\u5272\u308a\u5f53\u3066\u53ef\u80fd\uff1bVol+ / Vol- \u97f3\u91cf"},
     }}},
    {u8"RGDS \ub4c0\uc5bc \uc2a4\ud06c\ub9b0 \ub9e4\ud551",
     {{
         {"D-Pad", u8"\ucc45\uc7a5 \uc774\ub3d9; \ud558\ub2e8 \uba54\ub274 \uc774\ub3d9; \uc77d\uae30 \uc911 \ud398\uc774\uc9c0 \uc804\ud658/\uc2a4\ud06c\ub864"},
         {"A", u8"\ud655\uc778, \ucc45 \uc5f4\uae30 \ub610\ub294 \uc120\ud0dd\ud55c \uba54\ub274 \uc2e4\ud589"},
         {"B", u8"\ub4a4\ub85c; \ud558\ub2e8 \uba54\ub274 \ub2eb\uae30; \ub9ac\ub354\uc5d0\uc11c \ucc45\uc7a5\uc73c\ub85c"},
         {"X", u8"\uc77d\uae30 \uc911 \uc9c4\ud589\ub960 \ud45c\uc2dc"},
         {"Y", u8"\uc77d\uae30 \uc911 \uc7a5 \ubaa9\ub85d \uc5f4\uae30"},
         {"L1 / R1", u8"\uce74\ud14c\uace0\ub9ac; \uc774\ubbf8\uc9c0\ub9cc \uc90c"},
         {"L2 / R2", u8"\uc774\ubbf8\uc9c0 \ud68c\uc804; 90/270 \uc591\ucabd \ud398\uc774\uc9c0"},
         {"Select", u8"\uc0c1\ub2e8 \ucc45\uc7a5/\ub9ac\ub354\uc640 \ud558\ub2e8 \uba54\ub274 \uc0ac\uc774\uc758 \ud3ec\ucee4\uc2a4 \uc804\ud658"},
         {"Menu", u8"\ucc45\uc7a5\uc5d0\uc11c \ud558\ub2e8 \uba54\ub274 \ud3ec\ucee4\uc2a4; \uc77d\uae30 \uc911 \ud558\ub2e8 \uba54\ub274 \uc5f4\uae30/\ub2eb\uae30"},
         {"RG", u8"\ud0a4 \ubcf4\uc815\uc5d0\uc11c \ub9e4\ud551 \uac00\ub2a5; Vol+ / Vol- \uc74c\ub7c9"},
     }}},
    {u8"\u062a\u062e\u0637\u064a\u0637 RGDS \u0644\u0644\u0634\u0627\u0634\u062a\u064a\u0646",
     {{
         {"D-Pad", u8"\u062a\u062d\u0631\u064a\u0643 \u0627\u0644\u0631\u0641\u061b \u062a\u062d\u0631\u064a\u0643 \u0627\u0644\u0642\u0627\u0626\u0645\u0629 \u0627\u0644\u0633\u0641\u0644\u064a\u0629\u061b \u062a\u0642\u0644\u064a\u0628 \u0623\u0648 \u062a\u0645\u0631\u064a\u0631 \u0623\u062b\u0646\u0627\u0621 \u0627\u0644\u0642\u0631\u0627\u0621\u0629"},
         {"A", u8"\u062a\u0623\u0643\u064a\u062f\u060c \u0641\u062a\u062d \u0643\u062a\u0627\u0628\u060c \u0623\u0648 \u062a\u0646\u0641\u064a\u0630 \u0639\u0646\u0635\u0631 \u0627\u0644\u0642\u0627\u0626\u0645\u0629"},
         {"B", u8"\u0631\u062c\u0648\u0639\u061b \u0625\u063a\u0644\u0627\u0642 \u0627\u0644\u0642\u0627\u0626\u0645\u0629 \u0627\u0644\u0633\u0641\u0644\u064a\u0629\u061b \u0627\u0644\u0639\u0648\u062f\u0629 \u0645\u0646 \u0627\u0644\u0642\u0627\u0631\u0626 \u0625\u0644\u0649 \u0627\u0644\u0631\u0641"},
         {"X", u8"\u0639\u0631\u0636 \u062a\u0642\u062f\u0645 \u0627\u0644\u0642\u0631\u0627\u0621\u0629"},
         {"Y", u8"\u0641\u062a\u062d \u0642\u0627\u0626\u0645\u0629 \u0627\u0644\u0641\u0635\u0648\u0644"},
         {"L1 / R1", u8"\u0641\u0626\u0627\u062a\u061b \u062a\u0643\u0628\u064a\u0631 \u0644\u0644\u0635\u0648\u0631 \u0641\u0642\u0637"},
         {"L2 / R2", u8"\u062a\u062f\u0648\u064a\u0631 \u0627\u0644\u0635\u0648\u0631\u061b 90/270 \u0635\u0641\u062d\u062a\u0627\u0646"},
         {"Select", u8"\u062a\u0628\u062f\u064a\u0644 \u0627\u0644\u062a\u0631\u0643\u064a\u0632 \u0628\u064a\u0646 \u0627\u0644\u0634\u0627\u0634\u0629 \u0627\u0644\u0639\u0644\u064a\u0627 \u0648\u0627\u0644\u0642\u0627\u0626\u0645\u0629 \u0627\u0644\u0633\u0641\u0644\u064a\u0629"},
         {"Menu", u8"\u062a\u0631\u0643\u064a\u0632 \u0627\u0644\u0642\u0627\u0626\u0645\u0629 \u0627\u0644\u0633\u0641\u0644\u064a\u0629 \u0641\u064a \u0627\u0644\u0631\u0641\u061b \u0641\u062a\u062d/\u0625\u063a\u0644\u0627\u0642\u0647\u0627 \u0623\u062b\u0646\u0627\u0621 \u0627\u0644\u0642\u0631\u0627\u0621\u0629"},
         {"RG", u8"\u0645\u062a\u0627\u062d \u0644\u0645\u0639\u0627\u064a\u0631\u0629 \u0627\u0644\u0623\u0632\u0631\u0627\u0631\u061b Vol+ / Vol- \u0635\u0648\u062a"},
     }}},
    {u8"\u0420\u0430\u0441\u043a\u043b\u0430\u0434\u043a\u0430 RGDS \u0434\u043b\u044f \u0434\u0432\u0443\u0445 \u044d\u043a\u0440\u0430\u043d\u043e\u0432",
     {{
         {"D-Pad", u8"\u041d\u0430\u0432\u0438\u0433\u0430\u0446\u0438\u044f \u043f\u043e \u043f\u043e\u043b\u043a\u0435; \u043d\u0438\u0436\u043d\u0435\u0435 \u043c\u0435\u043d\u044e; \u043b\u0438\u0441\u0442\u0430\u043d\u0438\u0435 \u0438\u043b\u0438 \u043f\u0440\u043e\u043a\u0440\u0443\u0442\u043a\u0430 \u043f\u0440\u0438 \u0447\u0442\u0435\u043d\u0438\u0438"},
         {"A", u8"\u041f\u043e\u0434\u0442\u0432\u0435\u0440\u0434\u0438\u0442\u044c, \u043e\u0442\u043a\u0440\u044b\u0442\u044c \u043a\u043d\u0438\u0433\u0443 \u0438\u043b\u0438 \u0432\u044b\u043f\u043e\u043b\u043d\u0438\u0442\u044c \u043f\u0443\u043d\u043a\u0442 \u043c\u0435\u043d\u044e"},
         {"B", u8"\u041d\u0430\u0437\u0430\u0434; \u0437\u0430\u043a\u0440\u044b\u0442\u044c \u043d\u0438\u0436\u043d\u0435\u0435 \u043c\u0435\u043d\u044e; \u0432\u0435\u0440\u043d\u0443\u0442\u044c\u0441\u044f \u0438\u0437 \u0447\u0442\u0435\u043d\u0438\u044f \u043a \u043f\u043e\u043b\u043a\u0435"},
         {"X", u8"\u041f\u043e\u043a\u0430\u0437\u0430\u0442\u044c \u043f\u0440\u043e\u0433\u0440\u0435\u0441\u0441 \u0447\u0442\u0435\u043d\u0438\u044f"},
         {"Y", u8"\u041e\u0442\u043a\u0440\u044b\u0442\u044c \u0441\u043f\u0438\u0441\u043e\u043a \u0433\u043b\u0430\u0432"},
         {"L1 / R1", u8"\u041a\u0430\u0442\u0435\u0433\u043e\u0440\u0438\u0438; \u0437\u0443\u043c \u0442\u043e\u043b\u044c\u043a\u043e \u0434\u043b\u044f \u043a\u0430\u0440\u0442\u0438\u043d\u043e\u043a"},
         {"L2 / R2", u8"\u041f\u043e\u0432\u043e\u0440\u043e\u0442 \u0438\u0437\u043e\u0431\u0440.; 90/270 \u0440\u0430\u0437\u0432\u043e\u0440\u043e\u0442"},
         {"Select", u8"\u0424\u043e\u043a\u0443\u0441 \u043c\u0435\u0436\u0434\u0443 \u0432\u0435\u0440\u0445\u043d\u0438\u043c \u044d\u043a\u0440\u0430\u043d\u043e\u043c \u0438 \u043d\u0438\u0436\u043d\u0438\u043c \u043c\u0435\u043d\u044e"},
         {"Menu", u8"\u041d\u0430 \u043f\u043e\u043b\u043a\u0435 \u0444\u043e\u043a\u0443\u0441 \u043d\u0430 \u043d\u0438\u0436\u043d\u0435\u0435 \u043c\u0435\u043d\u044e; \u0432 \u0447\u0442\u0435\u043d\u0438\u0438 \u043e\u0442\u043a\u0440\u044b\u0442\u044c/\u0437\u0430\u043a\u0440\u044b\u0442\u044c"},
         {"RG", u8"\u041c\u043e\u0436\u043d\u043e \u043d\u0430\u0437\u043d\u0430\u0447\u0438\u0442\u044c \u0432 \u043a\u0430\u043b\u0438\u0431\u0440\u043e\u0432\u043a\u0435; Vol+ / Vol- \u0433\u0440\u043e\u043c\u043a."},
     }}},
    {"Mapeamento RGDS de tela dupla",
     {{
         {"D-Pad", "Mover na estante; mover no menu inferior; virar pagina ou rolar na leitura"},
         {"A", "Confirmar, abrir livro ou executar o item selecionado"},
         {"B", "Voltar; fechar o menu inferior; retornar do leitor para a estante"},
         {"X", "Mostrar progresso de leitura"},
         {"Y", "Abrir lista de capitulos"},
         {"L1 / R1", "Categorias; zoom so em imagens"},
         {"L2 / R2", "Girar imagens; 90/270 pagina dupla"},
         {"Select", "Alternar foco entre tela superior/leitor e menu inferior"},
         {"Menu", "Na estante foca o menu inferior; na leitura abre/fecha esse menu"},
         {"RG", "Disponivel na calibracao; Vol+ / Vol- volume"},
     }}},
    {u8"S\u01a1 \u0111\u1ed3 ph\u00edm RGDS hai m\u00e0n h\u00ecnh",
     {{
         {"D-Pad", u8"Di chuy\u1ec3n tr\u00ean gi\u00e1 s\u00e1ch; di chuy\u1ec3n menu d\u01b0\u1edbi; l\u1eadt trang ho\u1eb7c cu\u1ed9n khi \u0111\u1ecdc"},
         {"A", u8"X\u00e1c nh\u1eadn, m\u1edf s\u00e1ch ho\u1eb7c ch\u1ea1y m\u1ee5c menu \u0111ang ch\u1ecdn"},
         {"B", u8"Quay l\u1ea1i; \u0111\u00f3ng menu d\u01b0\u1edbi; t\u1eeb tr\u00ecnh \u0111\u1ecdc v\u1ec1 gi\u00e1 s\u00e1ch"},
         {"X", u8"Hi\u1ec3n th\u1ecb ti\u1ebfn \u0111\u1ed9 \u0111\u1ecdc"},
         {"Y", u8"M\u1edf danh s\u00e1ch ch\u01b0\u01a1ng"},
         {"L1 / R1", u8"Danh m\u1ee5c; ch\u1ec9 zoom \u1ea3nh"},
         {"L2 / R2", u8"Xoay \u1ea3nh; 90/270 hai trang"},
         {"Select", u8"Chuy\u1ec3n ti\u00eau \u0111i\u1ec3m gi\u1eefa m\u00e0n tr\u00ean/tr\u00ecnh \u0111\u1ecdc v\u00e0 menu d\u01b0\u1edbi"},
         {"Menu", u8"\u1ede gi\u00e1 s\u00e1ch chuy\u1ec3n focus v\u00e0o menu d\u01b0\u1edbi; khi \u0111\u1ecdc m\u1edf/\u0111\u00f3ng menu n\u00e0y"},
         {"RG", u8"Co the gan khi hieu chinh phim; Vol+ / Vol- am luong"},
     }}},
}};

size_t Utf8CharSize(unsigned char lead) {
  if ((lead & 0x80u) == 0) return 1;
  if ((lead & 0xE0u) == 0xC0u) return 2;
  if ((lead & 0xF0u) == 0xE0u) return 3;
  if ((lead & 0xF8u) == 0xF0u) return 4;
  return 1;
}

std::string TrimAsciiSpaces(std::string text) {
  while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
    text.erase(text.begin());
  }
  while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
    text.pop_back();
  }
  return text;
}

std::vector<std::string> WrapTextByWidth(
    const std::string &text, int max_width,
    const std::function<TextCacheEntry *(const std::string &, SDL_Color, bool)> &get_text,
    SDL_Color color, bool emphasis = false) {
  std::vector<std::string> lines;
  if (text.empty() || max_width <= 0) {
    if (!text.empty()) lines.push_back(text);
    return lines;
  }

  auto measure = [&](const std::string &value) -> int {
    if (TextCacheEntry *entry = get_text(value, color, emphasis); entry) return entry->w;
    return 0;
  };

  std::string current;
  size_t last_break = std::string::npos;
  for (size_t pos = 0; pos < text.size();) {
    const size_t char_size = std::min(Utf8CharSize(static_cast<unsigned char>(text[pos])), text.size() - pos);
    const std::string glyph = text.substr(pos, char_size);
    const std::string candidate = current + glyph;
    if (!current.empty() && measure(candidate) > max_width) {
      if (last_break != std::string::npos) {
        std::string line = TrimAsciiSpaces(current.substr(0, last_break));
        std::string remain = TrimAsciiSpaces(current.substr(last_break));
        if (!line.empty()) lines.push_back(line);
        current = remain + glyph;
      } else {
        lines.push_back(TrimAsciiSpaces(current));
        current = glyph;
      }
      last_break = std::string::npos;
      for (size_t i = 0; i < current.size(); ++i) {
        if (current[i] == ' ' || current[i] == '/' || current[i] == ';') last_break = i + 1;
      }
    } else {
      current = candidate;
      if (glyph == " " || glyph == "/" || glyph == ";") last_break = current.size();
    }
    pos += char_size;
  }

  current = TrimAsciiSpaces(current);
  if (!current.empty()) lines.push_back(current);
  return lines;
}

}  // namespace

void DrawKeyGuidePanel(SettingsRuntimeRenderDeps &deps, SDL_Rect preview_rect,
                       int language_index, int first_row_y) {
  if (!deps.renderer) return;

  const SDL_Color title_color{240, 246, 255, 255};
  const SDL_Color key_color{191, 221, 247, 255};
  const SDL_Color divider_color{66, 95, 124, 255};

  auto get_text = [&](const std::string &text, SDL_Color color, bool emphasis = false) -> TextCacheEntry * {
    if (emphasis && deps.services.get_title_text_texture) return deps.services.get_title_text_texture(text, color);
    if (deps.services.get_text_texture) return deps.services.get_text_texture(text, color);
    return nullptr;
  };

  const bool rgds_profile = deps.input_profile == InputProfile::RGDS;
  const bool gkd_profile = deps.input_profile == InputProfile::GKD350HUltra;
  const float scale = deps.layout.ui_scale;
  const int left = preview_rect.x + ScalePx(scale, rgds_profile ? 12 : (gkd_profile ? 32 : 22));
  const int right = preview_rect.x + preview_rect.w - ScalePx(scale, rgds_profile ? 12 : (gkd_profile ? 32 : 20));
  const int divider_y = first_row_y - ScalePx(scale, gkd_profile ? 18 : 12);
  const StaticKeyGuide *rgds_guide = nullptr;
  if (rgds_profile) {
    rgds_guide = &kRgdsKeyGuides[static_cast<size_t>(std::clamp(language_index, 0,
                                                                static_cast<int>(kRgdsKeyGuides.size()) - 1))];
  }
  AppTextId profile_text_id = AppTextId::KeyGuideProfileOtherH700;
  if (!rgds_profile) {
    if (!deps.has_calibrated_keymap && deps.input_profile == InputProfile::TrimuiBrick) {
      profile_text_id = AppTextId::KeyGuideProfileTrimuiBrick;
    }
  }
  std::string profile_title =
      rgds_guide ? rgds_guide->title
                 : (gkd_profile ? GkdKeyGuideTitle(language_index)
                    : deps.has_calibrated_keymap ? CalibratedKeyGuideTitle(language_index)
                                                : LocalizedAppText(language_index, profile_text_id));
  if (rgds_guide && DetectDeviceModelToken() == "rgds-plus") {
    const size_t model_pos = profile_title.find("RGDS");
    if (model_pos != std::string::npos) profile_title.replace(model_pos, 4, "RGDSplus");
    if (language_index == 0) {
      profile_title = u8"RGDSplus\u53cc\u5c4f\u4e13\u7528\u6620\u5c04";
    }
  }
  const int max_text_w = std::max(0, right - left);
  if (TextCacheEntry *profile = get_text(profile_title, title_color, true); profile && profile->texture) {
    SDL_Rect dst{left, divider_y - profile->h - ScalePx(scale, gkd_profile ? 12 : 8), profile->w, profile->h};
    SDL_RenderCopy(deps.renderer, profile->texture, nullptr, &dst);
  }
  deps.services.draw_rect(preview_rect.x + ScalePx(scale, gkd_profile ? 18 : 10),
                 divider_y,
                 std::max(0, preview_rect.w - ScalePx(scale, gkd_profile ? 36 : 20)),
                 ScalePx(scale, 1),
                 divider_color,
                 true);

  const int line_gap = ScalePx(scale, gkd_profile ? 8 : 4);
  const int row_gap = ScalePx(scale, gkd_profile ? 16 : 10);
  const int start_y = divider_y + ScalePx(scale, gkd_profile ? 24 : 16);
  int cursor_y = start_y;
  const size_t line_count = rgds_guide ? rgds_guide->lines.size() : kKeyGuideLines.size();
  for (size_t i = 0; i < line_count; ++i) {
    std::string line_text;
    if (rgds_guide) {
      const StaticKeyGuideLine &line = rgds_guide->lines[i];
      line_text = std::string(line.button_label) + ": " + line.action_text;
    } else {
      const KeyGuideLine &line = kKeyGuideLines[i];
      line_text = std::string(line.button_label) + ": " + LocalizedAppText(language_index, line.action_text);
    }
    const std::vector<std::string> wrapped_lines = WrapTextByWidth(line_text, max_text_w, get_text, key_color);
    for (size_t line_index = 0; line_index < wrapped_lines.size(); ++line_index) {
      if (TextCacheEntry *line_entry = get_text(wrapped_lines[line_index], key_color);
          line_entry && line_entry->texture) {
        SDL_Rect dst{left, cursor_y, line_entry->w, line_entry->h};
        SDL_RenderCopy(deps.renderer, line_entry->texture, nullptr, &dst);
        cursor_y += line_entry->h + line_gap;
      }
    }
    cursor_y += row_gap;
  }
}
