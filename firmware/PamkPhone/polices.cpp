// Seul fichier qui inclut les données de polices (elles ne sont copiées qu'une fois).
#include "polices.h"
#include "parametres.h"
#include "src/polices/BarlowMoyen22.h"
#include "src/polices/BarlowTexte28.h"
#include "src/polices/BarlowGras28.h"
#include "src/polices/BarlowTexte36.h"
#include "src/polices/BarlowGras36.h"
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

// Taille « grande » : Barlow 36 px (majuscules de 26 px au lieu de 19-20, interligne 43
// au lieu de 34). Même jeu de caractères que F_TEXTE / F_GRAS.
const GFXfont* policeTexte() { return Param.tailleTexte ? &BarlowTexte36 : &BarlowTexte28; }
const GFXfont* policeGras() { return Param.tailleTexte ? &BarlowGras36 : &BarlowGras28; }
