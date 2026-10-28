// Console série (115200 bauds, USB) en français : tape « aide ».
#pragma once
#include "config.h"

namespace Console {

void demarrer();
void boucle();
bool active();   // une commande a été tapée il y a moins de 2 minutes (pas de veille légère)

}  // namespace Console
