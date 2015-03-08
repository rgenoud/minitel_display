# minitel_display
Transforme votre minitel en cadre photo

À quoi ça sert ?
=====
Pour épater la galerie avec du vintage.

Quel est le matériel nécessaire ?
=====
 * Un minitel 1B avec une touche fnct, en niveau de gris.
 * Une machine linux avec port série en niveaux TTL.

Personnellement, j'ai utilisé une cubieboard pour le piloter (les sorties série de la cubieboard sont TTL), mais on peut aussi utiliser un pc standard avec un cable USB/TTL.

Compilation/installation
=====
```bash
make
sudo make install
```
Lancement du programme
====
```bash
mkdir -p /repertoire_contenant_les_photos
cp examples/* /repertoire_contenant_les_photos
minitel_slideshow.sh /dev/ttyxxxx /repertoire_contenant_les_photos fast
```
Le repertoire_contenant_les_photos peut être un dossier FTP par exemple, ce qui permet une mise à jour à distance.

Le rendu est super moche !!! On ne voit rien !
====
 * Il n'y a que 8 niveaux de gris en 80x72, donc déjà, on ne peut pas faire de miracle.
 * Il faut se placer à 4 ou 5 mètres pour mieux voir une photo sur un minitel.
 * Et surtout, le choix de la photo est important.

Conseils pour bien choisir sa photo:
 * Découper uniquement le visage, genre le menton frôle le bas et les cheveux dépassent presque. (on a que 80x72 pixels, il ne faut pas les gâcher !)
 * Éviter les photos trop ensoleillées, autrement dit, les photos avec trop de dynamique. (il faut peu d'écart entre les zones les plus sombres et les zones les plus claires)

Comment ça marche ?
====
Déjà, il faut comprendre le videotexte. cf https://fr.wikipedia.org/wiki/Vid%C3%A9otex#Tables_de_codage.

Ce qui est important pour nous, c'est le fait que les "pixels" du minitel ne sont pas directement adressables et ne peuvent pas non plus avoir n'importe quelle couleur.

Ces contraintes sont exprimées par la table de code mosaïque.

Cette table donne le code sur 7 bits correspondant à l'affichage d'un pavé de 2x3 "pixels".

Ce pavé de 2x3 fait la taille d'un caractère alphanumérique. (et on a bien 40x24 caractère sur l'écran qui donnent 80x72 "pixels").

 * 1er indice: il faut découper notre photo par blocs de 2x3 pixels pour les afficher sur le minitel.
 * 2ème indice: sur chaque bloc de 2x3 pixel, on dirait bien qu'il n'y a que 2 couleurs possibles...

Effectivement, pour chaque bloc de 2x3 pixels, il faudra choisir uniquement 2 couleurs (enfin, 2 niveaux de gris), qui correspondent en fait à la couleur de fond et à la couleur du caractère.

Ce qui a été fait dans le code, c'est de trouver les 2 niveaux de gris les plus représentés dans les 6 pixels, de prendre le plus sombre pour le fond, et d'allumer les pixels correspondant au niveaux les plus clairs.

Le passage de 256 niveaux de gris (8 bits) à 8 niveaux de gris (3 bits) se fait simplement en décalant de 5 bits vers la droite. C'est un peu simpliste et brutal, et je pense que l'on pourrait améliorer ce point (mais les résultats sont déjà assez convenable en l'état).

Le reste, c'est de l'encodage videotexte (changement de couleur de fond, saut de caractères etc.) et de l'optimisation (pour envoyer le minimum de caractères possible, vu que c'est **LENT**)
