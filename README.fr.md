# Eternal Siege — version PS Vita

*[English version](README.md)*

Version PS Vita native (homebrew) d'**Eternal Siege** : un château médiéval à défendre, à la première personne, contre des vagues d'assaillants qui traversent les époques, du Moyen Âge jusqu'aux essaims de drones.

La Vita ne peut pas offrir le serveur persistant à 32 joueurs du jeu PC. Cette version privilégie donc un **monde vivant et destructible** plutôt que des graphismes lourds.

## Ce qu'il y a dans le jeu

- **Château en blocs de pierre** : il se fissure, perd des pierres et s'effondre par pans entiers. Les éboulis forment des talus que les ennemis peuvent escalader. Tu peux réparer en maintenant ○.
- **Terrain déformable** : cratères d'obus et de boulets, terre retournée.
- **Sang persistant** : éclaboussures, flaques qui s'étendent sous les corps, membres arrachés par les explosions. La pluie lave lentement le champ de bataille.
- **Feu** : flèches enflammées (bande ton arc près d'un brasero), huile bouillante au-dessus de la porte, forêts qui brûlent, portes en bois qui se consument, tonneaux de poudre qui explosent en chaîne.
- **Objets physiques** : caisses, tonneaux, foin, charrettes, rochers. On peut les prendre (□) et les lancer (R). Ils cassent, roulent, flottent et dérivent dans la rivière.
- **Arbres** : ils bougent au vent, s'abattent à l'épée ou aux explosions, écrasent ce qui est dessous et brûlent.
- **Météo et jour/nuit** : pluie, orages avec éclairs et tonnerre, brouillard, nuits éclairées par les incendies.
- **Vie** : corbeaux qui tournent au-dessus du champ de bataille et s'envolent aux explosions, herbe qui ondule, cris, musique qui s'emballe pendant les assauts.
- **9 ères** : médiévale, Moyen Âge tardif, Renaissance, baroque, industrielle, Grande Guerre, guerre froide, moderne, contemporaine. À chaque ère ses armes, ses ennemis, ses uniformes et ses engins (béliers, catapultes, trébuchets, canons, artillerie, chars, avions, hélicoptères, drones, missiles).
- **Défenseurs alliés** sur le chemin de ronde, **échelles** que tu peux repousser, **canons** et **mitrailleuses** du château.
- **Persistance** : sauvegarde automatique. Le temps s'arrête quand la Vita est en veille. Si le donjon tombe, une nouvelle ère commence et l'ancienne entre dans les *Chroniques*.
- **Accélération du temps** (SELECT) : x1 à x64 sur les années, les répits et le cycle jour/nuit.

## Commandes

| Touche | Action |
| --- | --- |
| Stick gauche / droit | Se déplacer / regarder |
| R | Tirer, frapper, lancer l'objet tenu |
| L | Viser |
| ✕ | Sauter |
| △ | Changer d'arme |
| □ | Prendre un objet, utiliser un canon ou le chaudron, repousser une échelle |
| ○ (maintenir) | Réparer les murs |
| Haut | Grenade (ères modernes) |
| SELECT | Vitesse du temps |
| START | Menu (sauvegarde, chroniques, commandes) |

## Installation

1. Il faut une Vita hackée (HENkaku/Enso) avec **VitaShell**.
2. Le jeu utilise vitaGL, qui a besoin du compilateur de shaders `ur0:data/libshacccg.suprx`. S'il est absent, extrais-le avec **ShaRKBR33D** (disponible dans Vita Homebrew Browser).
3. Télécharge `EternalSiege.vpk` dans les *Releases* du dépôt et copie-le sur la Vita (FTP ou USB avec VitaShell).
4. Installe le VPK depuis VitaShell, puis lance « Eternal Siege » depuis l'écran d'accueil.

Les sauvegardes et le journal (`log.txt`) se trouvent dans `ux0:data/EternalSiege/`. En cas de problème, ce journal indique jusqu'où le démarrage est allé.

## Compilation

Rien à installer sur ton PC : à chaque envoi sur GitHub, **GitHub Actions** compile le jeu (VitaSDK + vitaGL), télécharge des textures photo CC0 sur [Poly Haven](https://polyhaven.com) et publie le `.vpk`.

Pour compiler en local (facultatif) :

```bash
export VITASDK=/usr/local/vitasdk
python3 tools/fetch_textures.py build_tex   # facultatif : textures photo
cmake -S . -B build && cmake --build build
```

## Organisation du code

| Fichier | Rôle |
| --- | --- |
| `src/gfx.cpp`, `textures.cpp` | Rendu vitaGL, lots de géométrie, lumières, textures, police |
| `src/terrain.cpp` | Relief, rivière, herbe, cratères |
| `src/castle.cpp` | Château en blocs : maillage, dégâts, effondrements, réparation, incendies |
| `src/nature.cpp`, `sky.cpp` | Arbres, oiseaux, ciel, météo, pluie |
| `src/bodies.cpp` | Physique des objets (caisses, tonneaux, débris, échelles…) |
| `src/characters.cpp` | Soldats articulés, animations, IA, champ de flux |
| `src/combat.cpp` | Projectiles, tirs, engins de siège, véhicules, postes fixes |
| `src/player.cpp` | Joueur à la première personne et arme en main |
| `src/fx.cpp` | Particules, sang, brûlures, explosions |
| `src/audio.cpp` | Sons synthétisés, mixage 3D, musique procédurale |
| `src/game.cpp` | Ères, vagues, sauvegarde, interface |

## Licence

Code : MIT. Textures téléchargées : CC0 (Poly Haven). Sons, musique, modèles et police : générés par le code du jeu.
