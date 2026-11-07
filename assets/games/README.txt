PicoCalc games folder
=====================
Each game keeps its data in /PIM/games/<game>/ and reads it only when needed.

game.cfg      settings, one key=value per line (lines starting with ; are ignored)
scores.txt    the five best scores, written by the game
dungeon/floorN.txt   hand-made dungeon floors, 16 columns x 12 rows:
                     # wall, . floor, @ start, > stairs, $ gold, ! potion, M monster
                     A missing or invalid floor file falls back to a random floor.

Edit the files with the PicoCalc text editor (Home > Text editor > games).
Delete a file to restore the built-in copy on the next start.
