// Seul fichier qui inclut les données de polices (elles ne sont copiées qu'une fois).
#include "polices.h"
#include "src/polices/BarlowMoyen22.h"
#include "src/polices/BarlowTexte28.h"
#include "src/polices/BarlowGras28.h"
#include "src/polices/Pixel50.h"
#include "src/polices/Pixel75.h"
#include "src/polices/Pixel125.h"
#include "src/polices/Pixel200.h"

const GFXfont* const F_PETITE  = &BarlowMoyen22;
const GFXfont* const F_TEXTE   = &BarlowTexte28;
const GFXfont* const F_GRAS    = &BarlowGras28;
const GFXfont* const F_PIXEL   = &Pixel50;
const GFXfont* const F_TITRE   = &Pixel75;
const GFXfont* const F_GRANDE  = &Pixel125;
const GFXfont* const F_HORLOGE = &Pixel200;
